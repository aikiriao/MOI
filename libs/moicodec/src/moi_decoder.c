#include "moi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "moi_internal.h"
#include "byte_array.h"

/* FourCCの一致確認 */
#define MOI_CHECK_FOURCC(u32lebuf, c1, c2, c3, c4) \
    ((u32lebuf) == ((c1 << 0) | (c2 << 8) | (c3 << 16) | (c4 << 24)))

/* コア処理デコーダ */
struct MOICoreDecoder {
    int16_t sample_val; /* サンプル値 */
    int8_t  stepsize_index; /* ステップサイズテーブルの参照インデックス */
};

/* デコーダ */
struct MOIDecoder {
    struct IMAADPCMWAVHeader header;
    struct MOICoreDecoder core_decoder[MOI_MAX_NUM_CHANNELS];
    void *work;
};

/* ワークサイズ計算 */
int32_t MOIDecoder_CalculateWorkSize(void)
{
    return MOI_ALIGNMENT + sizeof(struct MOIDecoder);
}

/* デコードハンドル作成 */
struct MOIDecoder *MOIDecoder_Create(void *work, int32_t work_size)
{
    struct MOIDecoder *decoder;
    uint8_t *work_ptr;
    uint32_t alloced_by_malloc = 0;

    /* 領域自前確保の場合 */
    if ((work == NULL) && (work_size == 0)) {
        work_size = MOIDecoder_CalculateWorkSize();
        work = malloc((uint32_t)work_size);
        alloced_by_malloc = 1;
    }

    /* 引数チェック */
    if ((work == NULL) || (work_size < MOIDecoder_CalculateWorkSize())) {
        return NULL;
    }

    work_ptr = (uint8_t *)work;

    /* アラインメントを揃えてから構造体を配置 */
    work_ptr = (uint8_t *)MOI_ROUND_UP((uintptr_t)work_ptr, MOI_ALIGNMENT);
    decoder = (struct MOIDecoder *)work_ptr;

    /* ハンドルの中身を0初期化 */
    memset(decoder, 0, sizeof(struct MOIDecoder));

    /* 自前確保の場合はメモリを記憶しておく */
    decoder->work = alloced_by_malloc ? work : NULL;

    return decoder;
}

/* デコードハンドル破棄 */
void MOIDecoder_Destroy(struct MOIDecoder *decoder)
{
    if (decoder != NULL) {
        /* 自分で領域確保していたら破棄 */
        if (decoder->work != NULL) {
            free(decoder->work);
        }
    }
}

/* ヘッダデコード */
MOIApiResult MOIDecoder_DecodeHeader(
        const uint8_t *data, uint32_t data_size, struct IMAADPCMWAVHeader *header)
{
    const uint8_t *data_pos;
    uint32_t u32buf;
    uint16_t u16buf;
    uint32_t find_fact_chunk;
    struct IMAADPCMWAVHeader tmp_header;

    /* 引数チェック */
    if ((data == NULL) || (header == NULL)) {
        return MOI_APIRESULT_INVALID_ARGUMENT;
    }

    /* 読み出し用ポインタ設定 */
    data_pos = data;

    /* RIFFチャンクID */
    ByteArray_GetUint32LE(data_pos, &u32buf);
    if (!MOI_CHECK_FOURCC(u32buf, 'R', 'I', 'F', 'F')) {
        fprintf(stderr, "Invalid RIFF chunk id. \n");
        return MOI_APIRESULT_INVALID_FORMAT;
    }
    /* RIFFチャンクサイズ（読み飛ばし） */
    ByteArray_GetUint32LE(data_pos, &u32buf);

    /* WAVEチャンクID */
    ByteArray_GetUint32LE(data_pos, &u32buf);
    if (!MOI_CHECK_FOURCC(u32buf, 'W', 'A', 'V', 'E')) {
        fprintf(stderr, "Invalid WAVE chunk id. \n");
        return MOI_APIRESULT_INVALID_FORMAT;
    }

    /* FMTチャンクID */
    ByteArray_GetUint32LE(data_pos, &u32buf);
    if (!MOI_CHECK_FOURCC(u32buf, 'f', 'm', 't', ' ')) {
        fprintf(stderr, "Invalid fmt  chunk id. \n");
        return MOI_APIRESULT_INVALID_FORMAT;
    }
    /* fmtチャンクサイズ */
    ByteArray_GetUint32LE(data_pos, &u32buf);
    if (data_size <= u32buf) {
        fprintf(stderr, "Data size too small. fmt chunk size:%d data size:%d \n", u32buf, data_size);
        return MOI_APIRESULT_INSUFFICIENT_DATA;
    }
    /* WAVEフォーマットタイプ: IMA-ADPCM以外は受け付けない */
    ByteArray_GetUint16LE(data_pos, &u16buf);
    if (u16buf != 17) {
        fprintf(stderr, "Unsupported format: %d \n", u16buf);
        return MOI_APIRESULT_INVALID_FORMAT;
    }
    /* チャンネル数 */
    ByteArray_GetUint16LE(data_pos, &u16buf);
    if (u16buf > MOI_MAX_NUM_CHANNELS) {
        fprintf(stderr, "Unsupported channels: %d \n", u16buf);
        return MOI_APIRESULT_INVALID_FORMAT;
    }
    tmp_header.num_channels = u16buf;
    /* サンプリングレート */
    ByteArray_GetUint32LE(data_pos, &u32buf);
    tmp_header.sampling_rate = u32buf;
    /* データ速度[byte/sec] */
    ByteArray_GetUint32LE(data_pos, &u32buf);
    tmp_header.bytes_per_sec = u32buf;
    /* ブロックサイズ */
    ByteArray_GetUint16LE(data_pos, &u16buf);
    tmp_header.block_size = u16buf;
    /* サンプルあたりビット数 */
    ByteArray_GetUint16LE(data_pos, &u16buf);
    tmp_header.bits_per_sample = u16buf;
    /* fmtチャンクのエキストラサイズ: 2以外は想定していない */
    ByteArray_GetUint16LE(data_pos, &u16buf);
    if (u16buf != 2) {
        fprintf(stderr, "Unsupported fmt chunk extra size: %d \n", u16buf);
        return MOI_APIRESULT_INVALID_FORMAT;
    }
    /* ブロックあたりサンプル数 */
    ByteArray_GetUint16LE(data_pos, &u16buf);
    tmp_header.num_samples_per_block = u16buf;

    /* dataチャンクまで読み飛ばし */
    find_fact_chunk = 0;
    while (1) {
        uint32_t chunkid;
        /* サイズ超過 */
        if (data_size < (uint32_t)(data_pos - data)) {
            return MOI_APIRESULT_INSUFFICIENT_DATA;
        }
        /* チャンクID取得 */
        ByteArray_GetUint32LE(data_pos, &chunkid);
        if (MOI_CHECK_FOURCC(chunkid, 'd', 'a', 't', 'a')) {
            /* データチャンクを見つけたら終わり */
            break;
        } else if (MOI_CHECK_FOURCC(chunkid, 'f', 'a', 'c', 't')) {
            /* FACTチャンク（オプショナル） */
            ByteArray_GetUint32LE(data_pos, &u32buf);
            /* FACTチャンクサイズ: 4以外は想定していない */
            if (u32buf != 4) {
                fprintf(stderr, "Unsupported fact chunk size: %d \n", u16buf);
                return MOI_APIRESULT_INVALID_FORMAT;
            }
            /* サンプル数 */
            ByteArray_GetUint32LE(data_pos, &u32buf);
            tmp_header.num_samples = u32buf;
            /* factチャンクを見つけたことをマーク */
            MOI_ASSERT(find_fact_chunk == 0);
            find_fact_chunk = 1;
        } else {
            uint32_t size;
            /* 他のチャンクはサイズだけ取得してシークにより読み飛ばす */
            ByteArray_GetUint32LE(data_pos, &size);
            /* printf("chunk:%8X size:%d \n", chunkid, (int32_t)size); */
            data_pos += size;
        }
    }

    /* データチャンクサイズ（読み飛ばし） */
    ByteArray_GetUint32LE(data_pos, &u32buf);

    /* factチャンクがない場合は、サンプル数をブロックサイズから計算 */
    if (find_fact_chunk == 0) {
        uint32_t data_chunk_size = u32buf;
        /* 末尾のブロック分も含めるため+1 */
        uint32_t num_blocks = data_chunk_size / tmp_header.block_size + 1;
        tmp_header.num_samples = tmp_header.num_samples_per_block * num_blocks;
    }

    /* データ領域先頭までのオフセット */
    tmp_header.header_size = (uint32_t)(data_pos - data);

    /* 成功終了 */
    (*header) = tmp_header;
    return MOI_APIRESULT_OK;
}

/* 1サンプルデコード */
static int16_t MOICoreDecoder_DecodeSample(
        struct MOICoreDecoder *decoder, uint8_t nibble)
{
    int8_t  idx;
    int32_t predict, qdiff, delta, stepsize;

    MOI_ASSERT(decoder != NULL);

    /* 頻繁に参照する変数をオート変数に受ける */
    predict = decoder->sample_val;
    idx = decoder->stepsize_index;

    /* ステップサイズの取得 */
    stepsize = MOI_stepsize_table[idx];

    /* インデックス更新 */
    idx = (int8_t)(idx + MOI_index_table[nibble]);
    idx = MOI_INNER_VAL(idx, 0, 88);

    /* 差分算出 */
    /* diff = stepsize * (delta * 2 + 1) / 8 */
    /* memo:ffmpegを参考に、よくある分岐多用の実装はしない。
    * 分岐多用の実装は近似で結果がおかしいし、分岐ミスの方が負荷が大きいと判断 */
    delta = nibble & 7;
    qdiff = (stepsize * ((delta << 1) + 1)) >> 3;

    /* 差分を加える 符号ビットで加算/減算を切り替え */
    if (nibble & 8) {
        predict -= qdiff;
    } else {
        predict += qdiff;
    }

    /* 16bit幅にクリップ */
    predict = MOI_INNER_VAL(predict, -32768, 32767);

    /* 計算結果の反映 */
    decoder->sample_val = (int16_t)predict;
    decoder->stepsize_index = idx;

    return decoder->sample_val;
}

/* モノラルブロックのデコード */
static MOIError MOIDecoder_DecodeBlockMono(
        struct MOICoreDecoder *core_decoder,
        const uint8_t *read_pos, uint32_t data_size,
        int16_t **buffer, uint32_t buffer_num_samples,
        uint32_t *num_decode_samples)
{
    uint8_t u8buf;
    uint8_t nibble[2];
    uint32_t smp, smpl, tmp_num_decode_samples;
    const uint8_t *read_head = read_pos;

    /* 引数チェック */
    if ((core_decoder == NULL) || (read_pos == NULL)
            || (buffer == NULL) || (buffer[0] == NULL)) {
        return MOI_ERROR_INVALID_ARGUMENT;
    }

    /* デコード可能なサンプル数を計算 *2は1バイトに2サンプル, +1はヘッダ分 */
    tmp_num_decode_samples = (data_size - 4) * 2;
    tmp_num_decode_samples += 1;
    /* バッファサイズで切り捨て */
    tmp_num_decode_samples = MOI_MIN_VAL(tmp_num_decode_samples, buffer_num_samples);

    /* ブロックヘッダデコード */
    ByteArray_GetUint16LE(read_pos, (uint16_t *)&(core_decoder->sample_val));
    ByteArray_GetUint8(read_pos, (uint8_t *)&(core_decoder->stepsize_index));
    ByteArray_GetUint8(read_pos, &u8buf); /* reserved */
    if (u8buf != 0) {
        return MOI_ERROR_INVALID_FORMAT;
    }

    /* 先頭サンプルはヘッダに入っている */
    buffer[0][0] = core_decoder->sample_val;

    /* ブロックデータデコード */
    for (smpl = 1; smpl < tmp_num_decode_samples - 2; smpl += 2) {
        MOI_ASSERT((uint32_t)(read_pos - read_head) < data_size);
        ByteArray_GetUint8(read_pos, &u8buf);
        nibble[0] = (u8buf >> 0) & 0xF;
        nibble[1] = (u8buf >> 4) & 0xF;
        buffer[0][smpl + 0] = MOICoreDecoder_DecodeSample(core_decoder, nibble[0]);
        buffer[0][smpl + 1] = MOICoreDecoder_DecodeSample(core_decoder, nibble[1]);
    }

    /* 末尾サンプル */
    ByteArray_GetUint8(read_pos, &u8buf);
    nibble[0] = (u8buf >> 0) & 0xF;
    nibble[1] = (u8buf >> 4) & 0xF;
    for (smp = 0; (smp < 2) && ((smpl + smp) < tmp_num_decode_samples); smp++) {
        buffer[0][smpl + smp] = MOICoreDecoder_DecodeSample(core_decoder, nibble[smp]);
    }

    /* デコードしたサンプル数をセット */
    (*num_decode_samples) = tmp_num_decode_samples;
    return MOI_ERROR_OK;
}

/* ステレオブロックのデコード */
static MOIError MOIDecoder_DecodeBlockStereo(
        struct MOICoreDecoder *core_decoder,
        const uint8_t *read_pos, uint32_t data_size,
        int16_t **buffer, uint32_t buffer_num_samples,
        uint32_t *num_decode_samples)
{
    uint32_t u32buf;
    uint8_t nibble[8];
    uint32_t ch, smpl, tmp_num_decode_samples;
    const uint8_t *read_head = read_pos;

    /* 引数チェック */
    if ((core_decoder == NULL) || (read_pos == NULL)
            || (buffer == NULL) || (buffer[0] == NULL) || (buffer[1] == NULL)) {
        return MOI_ERROR_INVALID_ARGUMENT;
    }

    /* デコード可能なサンプル数を計算 +1はヘッダ分 */
    tmp_num_decode_samples = data_size - 8;
    tmp_num_decode_samples += 1;
    /* バッファサイズで切り捨て */
    tmp_num_decode_samples = MOI_MIN_VAL(tmp_num_decode_samples, buffer_num_samples);

    /* ブロックヘッダデコード */
    for (ch = 0; ch < 2; ch++) {
        uint8_t reserved;
        ByteArray_GetUint16LE(read_pos, (uint16_t *)&(core_decoder[ch].sample_val));
        ByteArray_GetUint8(read_pos, (uint8_t *)&(core_decoder[ch].stepsize_index));
        ByteArray_GetUint8(read_pos, &reserved);
        if (reserved != 0) {
            return MOI_ERROR_INVALID_FORMAT;
        }
    }

    /* 最初のサンプルの取得 */
    for (ch = 0; ch < 2; ch++) {
        buffer[ch][0] = core_decoder[ch].sample_val;
    }

    /* ブロックデータデコード */
    for (smpl = 1; smpl < tmp_num_decode_samples; smpl += 8) {
        uint32_t smp;
        int16_t  buf[8];
        for (ch = 0; ch < 2; ch++) {
            MOI_ASSERT((uint32_t)(read_pos - read_head) < data_size);
            ByteArray_GetUint32LE(read_pos, &u32buf);
            nibble[0] = (uint8_t)((u32buf >>  0) & 0xF);
            nibble[1] = (uint8_t)((u32buf >>  4) & 0xF);
            nibble[2] = (uint8_t)((u32buf >>  8) & 0xF);
            nibble[3] = (uint8_t)((u32buf >> 12) & 0xF);
            nibble[4] = (uint8_t)((u32buf >> 16) & 0xF);
            nibble[5] = (uint8_t)((u32buf >> 20) & 0xF);
            nibble[6] = (uint8_t)((u32buf >> 24) & 0xF);
            nibble[7] = (uint8_t)((u32buf >> 28) & 0xF);

            /* サンプル数が 1 + (8の倍数) でない場合があるため、一旦バッファに受ける */
            buf[0] = MOICoreDecoder_DecodeSample(&(core_decoder[ch]), nibble[0]);
            buf[1] = MOICoreDecoder_DecodeSample(&(core_decoder[ch]), nibble[1]);
            buf[2] = MOICoreDecoder_DecodeSample(&(core_decoder[ch]), nibble[2]);
            buf[3] = MOICoreDecoder_DecodeSample(&(core_decoder[ch]), nibble[3]);
            buf[4] = MOICoreDecoder_DecodeSample(&(core_decoder[ch]), nibble[4]);
            buf[5] = MOICoreDecoder_DecodeSample(&(core_decoder[ch]), nibble[5]);
            buf[6] = MOICoreDecoder_DecodeSample(&(core_decoder[ch]), nibble[6]);
            buf[7] = MOICoreDecoder_DecodeSample(&(core_decoder[ch]), nibble[7]);
            for (smp = 0; (smp < 8) && ((smpl + smp) < tmp_num_decode_samples); smp++) {
                buffer[ch][smpl + smp] = buf[smp];
            }
        }
    }

    /* デコードしたサンプル数をセット */
    (*num_decode_samples) = tmp_num_decode_samples;
    return MOI_ERROR_OK;
}

/* 単一データブロックデコード */
static MOIApiResult MOIDecoder_DecodeBlock(
        struct MOIDecoder *decoder,
        const uint8_t *data, uint32_t data_size,
        int16_t **buffer, uint32_t buffer_num_channels, uint32_t buffer_num_samples,
        uint32_t *num_decode_samples)
{
    MOIError err;
    const struct IMAADPCMWAVHeader *header;

    /* 引数チェック */
    if ((decoder == NULL) || (data == NULL)
            || (buffer == NULL) || (num_decode_samples == NULL)) {
        return MOI_APIRESULT_INVALID_ARGUMENT;
    }

    header = &(decoder->header);

    /* バッファサイズチェック */
    if (buffer_num_channels < header->num_channels) {
        return MOI_APIRESULT_INSUFFICIENT_BUFFER;
    }

    /* ブロックデコード */
    switch (header->num_channels) {
    case 1:
        err = MOIDecoder_DecodeBlockMono(decoder->core_decoder,
                data, data_size, buffer, buffer_num_samples, num_decode_samples);
        break;
    case 2:
        err = MOIDecoder_DecodeBlockStereo(decoder->core_decoder,
                data, data_size, buffer, buffer_num_samples, num_decode_samples);
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

/* ヘッダ含めファイル全体をデコード */
MOIApiResult MOIDecoder_DecodeWhole(
        struct MOIDecoder *decoder, const uint8_t *data, uint32_t data_size,
        int16_t **buffer, uint32_t buffer_num_channels, uint32_t buffer_num_samples)
{
    MOIApiResult ret;
    uint32_t progress, ch, read_offset, read_block_size, num_decode_samples;
    const uint8_t *read_pos;
    int16_t *buffer_ptr[MOI_MAX_NUM_CHANNELS];
    const struct IMAADPCMWAVHeader *header;

    /* 引数チェック */
    if ((decoder == NULL) || (data == NULL) || (buffer == NULL)) {
        return MOI_APIRESULT_INVALID_ARGUMENT;
    }

    /* ヘッダデコード */
    if ((ret = MOIDecoder_DecodeHeader(data, data_size, &(decoder->header)))
            != MOI_APIRESULT_OK) {
        return ret;
    }
    header = &(decoder->header);

    /* バッファサイズチェック */
    if ((buffer_num_channels < header->num_channels)
            || (buffer_num_samples < header->num_samples)) {
        return MOI_APIRESULT_INSUFFICIENT_BUFFER;
    }

    progress = 0;
    read_offset = header->header_size;
    read_pos = data + header->header_size;
    while ((progress < header->num_samples) && (read_offset < data_size)) {
        /* 読み出しサイズの確定 */
        read_block_size = MOI_MIN_VAL(data_size - read_offset, header->block_size);
        /* サンプル書き出し位置のセット */
        for (ch = 0; ch < header->num_channels; ch++) {
            buffer_ptr[ch] = &buffer[ch][progress];
        }

        /* ブロックデコード */
        if ((ret = MOIDecoder_DecodeBlock(decoder,
                        read_pos, read_block_size,
                        buffer_ptr, buffer_num_channels, buffer_num_samples - progress,
                        &num_decode_samples)) != MOI_APIRESULT_OK) {
            return ret;
        }

        /* 進捗更新 */
        read_pos    += read_block_size;
        read_offset += read_block_size;
        progress    += num_decode_samples;
    }

    /* 成功終了 */
    return MOI_APIRESULT_OK;
}

