#include "moi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "moi_internal.h"
#include "byte_array.h"

/* エンコード時に書き出すヘッダサイズ（データブロック直前までのファイルサイズ） */
#define MOIENCODER_HEADER_SIZE 60

/* 指定サンプル数が占めるデータサイズ[byte]を計算 */
#define MOI_CALCULATE_DATASIZE_BYTE(num_samples, bits_per_sample) \
  (MOI_ROUND_UP((num_samples) * (bits_per_sample), 8) / 8)

/* コア処理エンコーダ */
struct MOICoreEncoder {
    int16_t prev_sample; /* サンプル値 */
    int8_t stepsize_index; /* ステップサイズテーブルの参照インデックス */
};

/* エンコーダ */
struct MOIEncoder {
    struct MOIEncodeParameter encode_paramemter;
    uint8_t set_parameter;
    struct MOICoreEncoder core_encoder[MOI_MAX_NUM_CHANNELS];
    void *work;
};

/* 単一データブロックエンコード */
/* デコードとは違いstaticに縛る: エンコーダが内部的に状態を持ち、連続でEncodeBlockを呼ぶ必要があるから */
static MOIApiResult MOIEncoder_EncodeBlock(
        struct MOIEncoder *encoder,
        const int16_t *const *input, uint32_t num_samples, 
        uint8_t *data, uint32_t data_size, uint32_t *output_size);

/* モノラルブロックのエンコード */
static MOIError MOIEncoder_EncodeBlockMono(
        struct MOICoreEncoder *core_encoder,
        const int16_t *const *input, uint32_t num_samples,
        uint8_t *data, uint32_t data_size, uint32_t *output_size);

/* ステレオブロックのエンコード */
static MOIError MOIEncoder_EncodeBlockStereo(
        struct MOICoreEncoder *core_encoder,
        const int16_t *const *input, uint32_t num_samples,
        uint8_t *data, uint32_t data_size, uint32_t *output_size);

/* ヘッダエンコード */
MOIApiResult MOIEncoder_EncodeHeader(
        const struct IMAADPCMWAVHeader *header, uint8_t *data, uint32_t data_size)
{
    uint8_t *data_pos;
    uint32_t num_blocks, data_chunk_size;
    uint32_t tail_block_num_samples, tail_block_size;

    /* 引数チェック */
    if ((header == NULL) || (data == NULL)) {
        return MOI_APIRESULT_INVALID_ARGUMENT;
    }

    /* ヘッダサイズと入力データサイズの比較 */
    if (data_size < MOIENCODER_HEADER_SIZE) {
        return MOI_APIRESULT_INSUFFICIENT_DATA;
    }

    /* ヘッダの簡易チェック: ブロックサイズはサンプルデータを全て入れられるはず */
    if (MOI_CALCULATE_DATASIZE_BYTE(header->num_samples_per_block, header->bits_per_sample) > header->block_size) {
        return MOI_APIRESULT_INVALID_FORMAT;
    }

    /* データサイズ計算 */
    assert(header->num_samples_per_block != 0);
    num_blocks = (header->num_samples / header->num_samples_per_block) + 1;
    data_chunk_size = header->block_size * num_blocks;
    /* 末尾のブロックの剰余サンプルサイズだけ減じる */
    tail_block_num_samples = header->num_samples % header->num_samples_per_block;
    tail_block_size = MOI_CALCULATE_DATASIZE_BYTE(header->num_samples_per_block - tail_block_num_samples, header->bits_per_sample);
    data_chunk_size -= tail_block_size;

    /* 書き出し用ポインタ設定 */
    data_pos = data;

    /* RIFFチャンクID */
    ByteArray_PutUint8(data_pos, 'R');
    ByteArray_PutUint8(data_pos, 'I');
    ByteArray_PutUint8(data_pos, 'F');
    ByteArray_PutUint8(data_pos, 'F');
    /* RIFFチャンクサイズ */
    ByteArray_PutUint32LE(data_pos, MOIENCODER_HEADER_SIZE + data_chunk_size - 8);
    /* WAVEチャンクID */
    ByteArray_PutUint8(data_pos, 'W');
    ByteArray_PutUint8(data_pos, 'A');
    ByteArray_PutUint8(data_pos, 'V');
    ByteArray_PutUint8(data_pos, 'E');
    /* FMTチャンクID */
    ByteArray_PutUint8(data_pos, 'f');
    ByteArray_PutUint8(data_pos, 'm');
    ByteArray_PutUint8(data_pos, 't');
    ByteArray_PutUint8(data_pos, ' ');
    /* FMTチャンクサイズは20で決め打ち */
    ByteArray_PutUint32LE(data_pos, 20);
    /* WAVEフォーマットタイプ: IMA-ADPCM(17)で決め打ち */
    ByteArray_PutUint16LE(data_pos, 17);
    /* チャンネル数 */
    if (header->num_channels > MOI_MAX_NUM_CHANNELS) {
        return MOI_APIRESULT_INVALID_FORMAT;
    }
    ByteArray_PutUint16LE(data_pos, header->num_channels);
    /* サンプリングレート */
    ByteArray_PutUint32LE(data_pos, header->sampling_rate);
    /* データ速度[byte/sec] */
    ByteArray_PutUint32LE(data_pos, header->bytes_per_sec);
    /* ブロックサイズ */
    ByteArray_PutUint16LE(data_pos, header->block_size);
    /* サンプルあたりビット数: 4で決め打ち */
    if (header->bits_per_sample != MOI_BITS_PER_SAMPLE) {
        return MOI_APIRESULT_INVALID_FORMAT;
    }
    ByteArray_PutUint16LE(data_pos, header->bits_per_sample);
    /* fmtチャンクのエキストラサイズ: 2で決め打ち */
    ByteArray_PutUint16LE(data_pos, 2);
    /* ブロックあたりサンプル数 */
    ByteArray_PutUint16LE(data_pos, header->num_samples_per_block);

    /* FACTチャンクID */
    ByteArray_PutUint8(data_pos, 'f');
    ByteArray_PutUint8(data_pos, 'a');
    ByteArray_PutUint8(data_pos, 'c');
    ByteArray_PutUint8(data_pos, 't');
    /* FACTチャンクのエキストラサイズ: 4で決め打ち */
    ByteArray_PutUint32LE(data_pos, 4);
    /* サンプル数 */
    ByteArray_PutUint32LE(data_pos, header->num_samples);

    /* その他のチャンクは書き出さず、すぐにdataチャンクへ */

    /* dataチャンクID */
    ByteArray_PutUint8(data_pos, 'd');
    ByteArray_PutUint8(data_pos, 'a');
    ByteArray_PutUint8(data_pos, 't');
    ByteArray_PutUint8(data_pos, 'a');
    /* データチャンクサイズ */
    ByteArray_PutUint32LE(data_pos, data_chunk_size);

    /* 成功終了 */
    return MOI_APIRESULT_OK;
}

/* エンコーダワークサイズ計算 */
int32_t MOIEncoder_CalculateWorkSize(void)
{
    return MOI_ALIGNMENT + sizeof(struct MOIEncoder);
}

/* エンコーダハンドル作成 */
struct MOIEncoder *MOIEncoder_Create(void *work, int32_t work_size)
{
    struct MOIEncoder *encoder;
    uint8_t *work_ptr;
    uint32_t alloced_by_malloc = 0;

    /* 領域自前確保の場合 */
    if ((work == NULL) && (work_size == 0)) {
        work_size = MOIEncoder_CalculateWorkSize();
        work = malloc((uint32_t)work_size);
        alloced_by_malloc = 1;
    }

    /* 引数チェック */
    if ((work == NULL) || (work_size < MOIEncoder_CalculateWorkSize())) {
        return NULL;
    }

    work_ptr = (uint8_t *)work;

    /* アラインメントを揃えてから構造体を配置 */
    work_ptr = (uint8_t *)MOI_ROUND_UP((uintptr_t)work_ptr, MOI_ALIGNMENT);
    encoder = (struct MOIEncoder *)work_ptr;

    /* ハンドルの中身を0初期化 */
    memset(encoder, 0, sizeof(struct MOIEncoder));

    /* パラメータは未セット状態に */
    encoder->set_parameter = 0;

    /* 自前確保の場合はメモリを記憶しておく */
    encoder->work = alloced_by_malloc ? work : NULL;

    return encoder;
}

/* エンコーダハンドル破棄 */
void MOIEncoder_Destroy(struct MOIEncoder *encoder)
{
    if (encoder != NULL) {
        /* 自分で領域確保していたら破棄 */
        if (encoder->work != NULL) {
            free(encoder->work);
        }
    }
}

/* 1サンプルエンコード */
static uint8_t MOICoreEncoder_EncodeSample(
        struct MOICoreEncoder *encoder, int16_t sample)
{
    uint8_t nibble;
    int8_t idx;
    int32_t prev, diff, qdiff, delta, stepsize, diffabs, sign;

    assert(encoder != NULL);

    /* 頻繁に参照する変数をオート変数に受ける */
    prev = encoder->prev_sample;
    idx = encoder->stepsize_index;

    /* ステップサイズの取得 */
    stepsize = MOI_stepsize_table[idx];

    /* 差分 */
    diff = sample - prev;
    sign = diff < 0;
    diffabs = sign ? -diff : diff;

    /* 差分を符号表現に変換 */
    /* nibble = sign(diff) * round(|diff| * 4 / stepsize) */
    nibble = (uint8_t)MOI_MIN_VAL((diffabs << 2) / stepsize, 7);
    /* nibbleの最上位ビットは符号ビット */
    if (sign) {
        nibble |= 0x8;
    }

    /* 量子化した差分を計算 */
    delta = nibble & 7;
    qdiff = (stepsize * ((delta << 1) + 1)) >> 3;

    /* ここで量子化誤差が出る */
    /* printf("%d \n", sign ? (-qdiff - diff) : (qdiff - diff)); */

    /* 量子化した差分を加える */
    if (sign) {
        prev -= qdiff;
    } else {
        prev += qdiff;
    }
    prev = MOI_INNER_VAL(prev, -32768, 32767);

    /* インデックス更新 */
    idx = (int8_t)(idx + MOI_index_table[nibble]);
    idx = MOI_INNER_VAL(idx, 0, 88);

    /* 計算結果の反映 */
    encoder->prev_sample = (int16_t)prev;
    encoder->stepsize_index = idx;

    return nibble;
}

/* モノラルブロックのエンコード */
static MOIError MOIEncoder_EncodeBlockMono(
        struct MOICoreEncoder *core_encoder,
        const int16_t *const *input, uint32_t num_samples,
        uint8_t *data, uint32_t data_size, uint32_t *output_size)
{
    uint8_t u8buf;
    uint8_t nibble[2];
    uint32_t smpl;
    uint8_t *data_pos = data;

    /* 引数チェック */
    if ((core_encoder == NULL) || (input == NULL)
            || (data == NULL) || (output_size == NULL)) {
        return MOI_ERROR_INVALID_ARGUMENT;
    }

    /* 十分なデータサイズがあるか確認 */
    if (data_size < (num_samples / 2 + 4)) {
        return MOI_ERROR_INSUFFICIENT_DATA;
    }

    /* 先頭サンプルをエンコーダにセット */
    core_encoder->prev_sample = input[0][0];

    /* ブロックヘッダエンコード */
    ByteArray_PutUint16LE(data_pos, core_encoder->prev_sample);
    ByteArray_PutUint8(data_pos, core_encoder->stepsize_index);
    ByteArray_PutUint8(data_pos, 0); /* reserved */

    /* ブロックデータエンコード */
    for (smpl = 1; smpl < num_samples; smpl += 2) {
        assert((uint32_t)(data_pos - data) < data_size);
        nibble[0] = MOICoreEncoder_EncodeSample(core_encoder, input[0][smpl + 0]);
        nibble[1] = MOICoreEncoder_EncodeSample(core_encoder, input[0][smpl + 1]);
        assert((nibble[0] <= 0xF) && (nibble[1] <= 0xF));
        u8buf = (uint8_t)((nibble[0] << 0) | (nibble[1] << 4));
        ByteArray_PutUint8(data_pos, u8buf);
    }

    /* 書き出しサイズをセット */
    (*output_size) = (uint32_t)(data_pos - data);
    return MOI_ERROR_OK;
}

/* ステレオブロックのエンコード */
static MOIError MOIEncoder_EncodeBlockStereo(
        struct MOICoreEncoder *core_encoder,
        const int16_t *const *input, uint32_t num_samples,
        uint8_t *data, uint32_t data_size, uint32_t *output_size)
{
    uint32_t u32buf;
    uint8_t nibble[8];
    uint32_t ch, smpl;
    uint8_t *data_pos = data;

    /* 引数チェック */
    if ((core_encoder == NULL) || (input == NULL)
            || (data == NULL) || (output_size == NULL)) {
        return MOI_ERROR_INVALID_ARGUMENT;
    }

    /* 十分なデータサイズがあるか確認 */
    if (data_size < (num_samples + 4)) {
        return MOI_ERROR_INSUFFICIENT_DATA;
    }

    /* 先頭サンプルをエンコーダにセット */
    for (ch = 0; ch < 2; ch++) {
        core_encoder[ch].prev_sample = input[ch][0];
    }

    /* ブロックヘッダエンコード */
    for (ch = 0; ch < 2; ch++) {
        ByteArray_PutUint16LE(data_pos, core_encoder[ch].prev_sample);
        ByteArray_PutUint8(data_pos, core_encoder[ch].stepsize_index);
        ByteArray_PutUint8(data_pos, 0); /* reserved */
    }

    /* ブロックデータエンコード */
    for (smpl = 1; smpl < num_samples; smpl += 8) {
        for (ch = 0; ch < 2; ch++) {
            assert((uint32_t)(data_pos - data) < data_size);
            nibble[0] = MOICoreEncoder_EncodeSample(&(core_encoder[ch]), input[ch][smpl + 0]);
            nibble[1] = MOICoreEncoder_EncodeSample(&(core_encoder[ch]), input[ch][smpl + 1]);
            nibble[2] = MOICoreEncoder_EncodeSample(&(core_encoder[ch]), input[ch][smpl + 2]);
            nibble[3] = MOICoreEncoder_EncodeSample(&(core_encoder[ch]), input[ch][smpl + 3]);
            nibble[4] = MOICoreEncoder_EncodeSample(&(core_encoder[ch]), input[ch][smpl + 4]);
            nibble[5] = MOICoreEncoder_EncodeSample(&(core_encoder[ch]), input[ch][smpl + 5]);
            nibble[6] = MOICoreEncoder_EncodeSample(&(core_encoder[ch]), input[ch][smpl + 6]);
            nibble[7] = MOICoreEncoder_EncodeSample(&(core_encoder[ch]), input[ch][smpl + 7]);
            assert((nibble[0] <= 0xF) && (nibble[1] <= 0xF) && (nibble[2] <= 0xF) && (nibble[3] <= 0xF)
                    && (nibble[4] <= 0xF) && (nibble[5] <= 0xF) && (nibble[6] <= 0xF) && (nibble[7] <= 0xF));
            u32buf  = (uint32_t)(nibble[0] <<  0);
            u32buf |= (uint32_t)(nibble[1] <<  4);
            u32buf |= (uint32_t)(nibble[2] <<  8);
            u32buf |= (uint32_t)(nibble[3] << 12);
            u32buf |= (uint32_t)(nibble[4] << 16);
            u32buf |= (uint32_t)(nibble[5] << 20);
            u32buf |= (uint32_t)(nibble[6] << 24);
            u32buf |= (uint32_t)(nibble[7] << 28);
            ByteArray_PutUint32LE(data_pos, u32buf);
        }
    }

    /* 書き出しサイズをセット */
    (*output_size) = (uint32_t)(data_pos - data);
    return MOI_ERROR_OK;
}

/* 単一データブロックエンコード */
static MOIApiResult MOIEncoder_EncodeBlock(
        struct MOIEncoder *encoder,
        const int16_t *const *input, uint32_t num_samples, 
        uint8_t *data, uint32_t data_size, uint32_t *output_size)
{
    MOIError err;
    const struct MOIEncodeParameter *enc_param;

    /* 引数チェック */
    if ((encoder == NULL) || (data == NULL)
            || (input == NULL) || (output_size == NULL)) {
        return MOI_APIRESULT_INVALID_ARGUMENT;
    }
    enc_param = &(encoder->encode_paramemter);

    /* ブロックデコード */
    switch (enc_param->num_channels) {
    case 1:
        err = MOIEncoder_EncodeBlockMono(encoder->core_encoder, 
                input, num_samples, data, data_size, output_size);
        break;
    case 2:
        err = MOIEncoder_EncodeBlockStereo(encoder->core_encoder, 
                input, num_samples, data, data_size, output_size);
        break;
    default:
        return MOI_APIRESULT_INVALID_FORMAT;
    }

    /* デコード時のエラーハンドル */
    if (err != MOI_ERROR_OK) {
        switch (err) {
        case MOI_ERROR_INVALID_ARGUMENT:
            return MOI_APIRESULT_INVALID_ARGUMENT;
        case MOI_ERROR_INVALID_FORMAT:
            return MOI_APIRESULT_INVALID_FORMAT;
        case MOI_ERROR_INSUFFICIENT_BUFFER:
            return MOI_APIRESULT_INSUFFICIENT_BUFFER;
        default:
            return MOI_APIRESULT_NG;
        }
    }

    return MOI_APIRESULT_OK;
}

/* エンコードパラメータをヘッダに変換 */
static MOIError MOIEncoder_ConvertParameterToHeader(
        const struct MOIEncodeParameter *enc_param, uint32_t num_samples,
        struct IMAADPCMWAVHeader *header)
{
    uint32_t block_data_size;
    struct IMAADPCMWAVHeader tmp_header = {0, };

    /* 引数チェック */
    if ((enc_param == NULL) || (header == NULL)) {
        return MOI_ERROR_INVALID_ARGUMENT;
    }

    /* サンプルあたりビット数は4固定 */
    if (enc_param->bits_per_sample != MOI_BITS_PER_SAMPLE) {
        return MOI_ERROR_INVALID_FORMAT;
    }

    /* ヘッダサイズは決め打ち */
    tmp_header.header_size = MOIENCODER_HEADER_SIZE;
    /* 総サンプル数 */
    tmp_header.num_samples = num_samples;

    /* そのままヘッダに入れられるメンバ */
    tmp_header.num_channels = enc_param->num_channels;
    tmp_header.sampling_rate = enc_param->sampling_rate;
    tmp_header.bits_per_sample = enc_param->bits_per_sample;
    tmp_header.block_size = enc_param->block_size;

    /* 計算が必要なメンバ */
    if (enc_param->block_size <= enc_param->num_channels * 4) {
        /* データを入れる領域がない */
        return MOI_ERROR_INVALID_FORMAT;
    }
    /* 4はチャンネルあたりのヘッダ領域サイズ */
    assert(enc_param->block_size >= (enc_param->num_channels * 4));
    block_data_size = (uint32_t)(enc_param->block_size - (enc_param->num_channels * 4));
    assert((block_data_size * 8) % (uint32_t)(enc_param->bits_per_sample * enc_param->num_channels) == 0);
    assert((enc_param->bits_per_sample * enc_param->num_channels) != 0);
    tmp_header.num_samples_per_block = (uint16_t)((block_data_size * 8) / (uint32_t)(enc_param->bits_per_sample * enc_param->num_channels));
    /* ヘッダに入っている分+1 */
    tmp_header.num_samples_per_block++;
    assert(tmp_header.num_samples_per_block != 0);
    tmp_header.bytes_per_sec = (enc_param->block_size * enc_param->sampling_rate) / tmp_header.num_samples_per_block;

    /* 成功終了 */
    (*header) = tmp_header;

    return MOI_ERROR_OK;
}

/* エンコードパラメータの設定 */
MOIApiResult MOIEncoder_SetEncodeParameter(
        struct MOIEncoder *encoder, const struct MOIEncodeParameter *parameter)
{
    struct IMAADPCMWAVHeader tmp_header = {0, };

    /* 引数チェック */
    if ((encoder == NULL) || (parameter == NULL)) {
        return MOI_APIRESULT_INVALID_ARGUMENT;
    }

    /* パラメータ設定がおかしくないか、ヘッダへの変換を通じて確認 */
    /* 総サンプル数はダミー値を入れる */
    if (MOIEncoder_ConvertParameterToHeader(parameter, 0, &tmp_header) != MOI_ERROR_OK) {
        return MOI_APIRESULT_INVALID_FORMAT;
    }

    /* パラメータ設定 */
    encoder->encode_paramemter = (*parameter);

    /* パラメータ設定済みフラグを立てる */
    encoder->set_parameter = 1;

    return MOI_APIRESULT_OK;
}

/* ヘッダ含めファイル全体をエンコード */
MOIApiResult MOIEncoder_EncodeWhole(
        struct MOIEncoder *encoder,
        const int16_t *const *input, uint32_t num_samples,
        uint8_t *data, uint32_t data_size, uint32_t *output_size)
{
    MOIApiResult ret;
    uint32_t progress, ch, write_size, write_offset, num_encode_samples;
    uint8_t *data_pos;
    const int16_t *input_ptr[MOI_MAX_NUM_CHANNELS];
    struct IMAADPCMWAVHeader header = { 0, };

    /* 引数チェック */
    if ((encoder == NULL) || (input == NULL)
            || (data == NULL) || (output_size == NULL)) {
        return MOI_APIRESULT_INVALID_ARGUMENT;
    }

    /* パラメータ未セットではエンコードできない */
    if (encoder->set_parameter == 0) {
        return MOI_APIRESULT_PARAMETER_NOT_SET;
    }

    /* 書き出し位置を取得 */
    data_pos = data;

    /* エンコードパラメータをヘッダに変換 */
    if (MOIEncoder_ConvertParameterToHeader(&(encoder->encode_paramemter), num_samples, &header) != MOI_ERROR_OK) {
        return MOI_APIRESULT_INVALID_FORMAT;
    }

    /* ヘッダエンコード */
    if ((ret = MOIEncoder_EncodeHeader(&header, data_pos, data_size)) != MOI_APIRESULT_OK) {
        return ret;
    }

    progress = 0;
    write_offset = MOIENCODER_HEADER_SIZE;
    data_pos = data + MOIENCODER_HEADER_SIZE;
    while (progress < num_samples) {
        /* エンコードサンプル数の確定 */
        num_encode_samples 
            = MOI_MIN_VAL(header.num_samples_per_block, num_samples - progress);
        /* サンプル参照位置のセット */
        for (ch = 0; ch < header.num_channels; ch++) {
            input_ptr[ch] = &input[ch][progress];
        }

        /* ブロックエンコード */
        if ((ret = MOIEncoder_EncodeBlock(encoder,
                        input_ptr, num_encode_samples,
                        data_pos, data_size - write_offset, &write_size)) != MOI_APIRESULT_OK) {
            return ret;
        }

        /* 進捗更新 */
        data_pos      += write_size;
        write_offset  += write_size;
        progress      += num_encode_samples;
        assert(write_size <= header.block_size);
        assert(write_offset <= data_size);
    }

    /* 成功終了 */
    (*output_size) = write_offset;
    return MOI_APIRESULT_OK;
}
