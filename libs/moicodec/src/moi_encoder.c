#include "moi.h"

#include <stdlib.h>
#include <string.h>
#include <float.h>

#include "moi_internal.h"
#include "byte_array.h"

/* エンコード時に書き出すヘッダサイズ（データブロック直前までのファイルサイズ） */
#define MOIENCODER_HEADER_SIZE 60

/* 指定サンプル数が占めるデータサイズ[byte]を計算 */
#define MOI_CALCULATE_DATASIZE_BYTE(num_samples, bits_per_sample) \
    (MOI_ROUND_UP((num_samples) * (bits_per_sample), 8) / 8)

/* 符号語の個数 */
#define MOIENCODER_NUM_CODES (1 << MOI_BITS_PER_SAMPLE)

/* 量子化誤差の計算 */
#define MOICoreEncoder_CalculateQuantizedDiff(encoder, nibble) MOI_qdiff_table[(encoder)->stepsize_index][(nibble)]

/* コア処理エンコーダ */
struct MOICoreEncoder {
    int16_t prev_sample; /* サンプル値 */
    int8_t stepsize_index; /* ステップサイズテーブルの参照インデックス */
    double total_cost; /* これまでのコスト */
};

/* エンコーダ候補 */
struct MOICoreEncoderCandidate {
    int8_t init_stepsize_index;
    struct MOICoreEncoder encoder;
    uint8_t *code;
};

/* エンコーダ */
struct MOIEncoder {
    struct MOIEncodeParameter encode_parameter;
    uint16_t max_block_size;
    uint8_t set_parameter;
    uint8_t *best_code[MOI_MAX_NUM_CHANNELS];
    int8_t best_init_stepsize_index[MOI_MAX_NUM_CHANNELS];
    struct MOICoreEncoderCandidate candidate[MOI_MAX_SEARCH_BEAM_WIDTH];
    struct MOICoreEncoderCandidate backup[MOI_MAX_SEARCH_BEAM_WIDTH];
    struct MOICoreEncoderCandidate default_candidate;
    void *work;
};

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
    MOI_ASSERT(header->num_samples_per_block != 0);
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
int32_t MOIEncoder_CalculateWorkSize(const struct MOIEncoderConfig *config)
{
    int32_t work_size;

    /* 引数チェック */
    if (config == NULL) {
        return -1;
    }

    /* コンフィグチェック */
    if (config->max_block_size == 0) {
        return -1;
    }

    /* ハンドルサイズ */
    work_size = MOI_ALIGNMENT + sizeof(struct MOIEncoder);

    /* 符号領域 チャンネル数 + 候補 + 候補バックアップ + デフォルト候補分 */
    /* 1バイトあたり2サンプル入りうるので2倍確保 */
    work_size += (MOI_MAX_NUM_CHANNELS + (2 * MOI_MAX_SEARCH_BEAM_WIDTH) + 1) * (MOI_ALIGNMENT + (2 * config->max_block_size));

    return work_size;
}

/* エンコーダハンドル作成 */
struct MOIEncoder *MOIEncoder_Create(
        const struct MOIEncoderConfig *config, void *work, int32_t work_size)
{
    struct MOIEncoder *encoder;
    uint8_t *work_ptr;
    uint32_t i, alloced_by_malloc = 0;

    /* 領域自前確保の場合 */
    if ((work == NULL) && (work_size == 0)) {
        if ((work_size = MOIEncoder_CalculateWorkSize(config)) < 0) {
            return NULL;
        }
        work = malloc((size_t)work_size);
        alloced_by_malloc = 1;
    }

    /* 引数チェック */
    if ((config == NULL) || (work == NULL)
            || (work_size < MOIEncoder_CalculateWorkSize(config))) {
        return NULL;
    }

    /* コンフィグチェック */
    if (config->max_block_size == 0) {
        return NULL;
    }

    work_ptr = (uint8_t *)work;

    /* アラインメントを揃えてから構造体を配置 */
    work_ptr = (uint8_t *)MOI_ROUND_UP((uintptr_t)work_ptr, MOI_ALIGNMENT);
    encoder = (struct MOIEncoder *)work_ptr;
    work_ptr += sizeof(struct MOIEncoder);

    /* ハンドルの中身を0初期化 */
    memset(encoder, 0, sizeof(struct MOIEncoder));

    /* 符号領域の割当て */
    for (i = 0; i < MOI_MAX_SEARCH_BEAM_WIDTH; i++) {
        work_ptr = (uint8_t *)MOI_ROUND_UP((uintptr_t)work_ptr, MOI_ALIGNMENT);
        encoder->candidate[i].code = (uint8_t *)work_ptr;
        work_ptr += 2 * config->max_block_size;
        work_ptr = (uint8_t *)MOI_ROUND_UP((uintptr_t)work_ptr, MOI_ALIGNMENT);
        encoder->backup[i].code = (uint8_t *)work_ptr;
        work_ptr += 2 * config->max_block_size;
    }
    work_ptr = (uint8_t*)MOI_ROUND_UP((uintptr_t)work_ptr, MOI_ALIGNMENT);
    encoder->default_candidate.code = (uint8_t *)work_ptr;
    work_ptr += 2 * config->max_block_size;
    for (i = 0; i < MOI_MAX_NUM_CHANNELS; i++) {
        work_ptr = (uint8_t *)MOI_ROUND_UP((uintptr_t)work_ptr, MOI_ALIGNMENT);
        encoder->best_code[i] = (uint8_t *)work_ptr;
        work_ptr += 2 * config->max_block_size;
    }

    /* 最大ブロックサイズの設定 */
    encoder->max_block_size = config->max_block_size;

    /* パラメータは未セット状態に */
    encoder->set_parameter = 0;

    /* 自前確保の場合はメモリを記憶しておく */
    encoder->work = alloced_by_malloc ? work : NULL;

    /* バッファオーバーランチェック */
    MOI_ASSERT((int32_t)(work_ptr - (uint8_t *)work) <= work_size);

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

/* 符号語のコストを計算 */
static double MOICoreEncoder_CalculateCost(
        const struct MOICoreEncoder *encoder, const int32_t sample, const uint8_t nibble)
{
    double err;

    MOI_ASSERT(encoder != NULL);
    MOI_ASSERT(nibble < MOIENCODER_NUM_CODES);

    /* 量子化した差分を計算 */
    err = MOICoreEncoder_CalculateQuantizedDiff(encoder, nibble);

    /* 量子化した差分により次の値を予測し、真のサンプルとの差をとる */
    err += encoder->prev_sample - sample;

    /* 差の2乗をコストとする */
    return err * err;
}

/* エンコーダの状態を更新 */
static void MOICoreEncoder_Update(
        struct MOICoreEncoder *encoder, const int16_t sample, const uint8_t nibble)
{
    int32_t qdiff;

    MOI_ASSERT(encoder != NULL);
    MOI_ASSERT(nibble < MOIENCODER_NUM_CODES);

    /* 量子化した差分を計算 */
    qdiff = MOICoreEncoder_CalculateQuantizedDiff(encoder, nibble);

    /* 合計コストの更新 */
    encoder->total_cost += MOICoreEncoder_CalculateCost(encoder, sample, nibble);

    /* 直前サンプルの更新 */
    encoder->prev_sample
        = (int16_t)MOI_INNER_VAL(encoder->prev_sample + qdiff, INT16_MIN, INT16_MAX);

    /* テーブルインデックスの更新 */
    encoder->stepsize_index
        = MOI_INNER_VAL(encoder->stepsize_index + IMAADPCM_index_table[nibble], 0, (int8_t)MOI_IMAADPCM_STEPSIZE_TABLE_SIZE - 1);
}

/* IMA-ADPCMの符号計算 */
static uint8_t MOICoreEncoder_CalculateIMAADPCMNibble(
        const struct MOICoreEncoder *encoder, const int16_t sample)
{
    uint8_t nibble;
    int32_t diff, diffabs, sign;

    /* 差分 */
    diff = sample - encoder->prev_sample;
    sign = diff < 0;
    diffabs = sign ? -diff : diff;

#if 0
    /* 差分を符号表現に変換 */
    /* nibble = sign(diff) * round(|diff| * 4 / stepsize) */
    nibble = (uint8_t)MOI_MIN_VAL((diffabs << 2) / IMAADPCM_stepsize_table[encoder->stepsize_index], 7);

    /* 符号ビットを付加 */
    return sign ? (nibble | 0x8) : nibble;
#else
    /* IMA-ADPCMリファレンス実装 */
    nibble = sign ? 8 : 0;
    {
        uint32_t i;
        uint8_t mask = 4;
        uint16_t stepsize = IMAADPCM_stepsize_table[encoder->stepsize_index];

        for (i = 0; i < 3; i++) {
            if (diffabs >= stepsize) {
                nibble |= mask;
                diffabs -= stepsize;
            }
            stepsize >>= 1;
            mask >>= 1;
        }
    }
    return nibble;
#endif
}

/* 深さdepthでの最小スコア探索 */
static double MOICoreEncoder_SearchMinScore(
        const struct MOICoreEncoder *encoder, const int16_t *sample, uint32_t depth, double min)
{
    uint8_t abs, killer_nibble;
    double score, killer_cost;
    struct MOICoreEncoder next;

    MOI_ASSERT(encoder != NULL);
    MOI_ASSERT(sample != NULL);
    MOI_ASSERT(depth <= MOI_MAX_SEARCH_DEPTH);

    /* 先読みの打ち切り */
    if (depth == 0) {
        return encoder->total_cost;
    }

    /* 先にIMA-ADPCMの符号とコストを計算
    * 最も良い可能性が高いため、これ以降の枝刈り増加を期待 */
    killer_nibble = MOICoreEncoder_CalculateIMAADPCMNibble(encoder, sample[0]);
    killer_cost = encoder->total_cost + MOICoreEncoder_CalculateCost(encoder, sample[0], killer_nibble);

    /* 深さ1の場合はIMA-ADPCMの符号が最善 */
    if (depth == 1) {
        return killer_cost;
    }

    /* 更新した時点のコストがこれまでの最小を越えていたら探索しない（コストはdepthに関して単調に増加するため） */
    if (killer_cost < min) {
        next = (*encoder);
        MOICoreEncoder_Update(&next, sample[0], killer_nibble);
        score = MOICoreEncoder_SearchMinScore(&next, sample + 1, depth - 1, min);
        min = MOI_MIN_VAL(score, min);
    }

    /* 符号候補で探索 */
    for (abs = 0; abs <= 0x7; abs++) {
        const uint8_t nibble = (abs | (killer_nibble & 0x8));
        if (nibble != killer_nibble) {
            if ((encoder->total_cost + MOICoreEncoder_CalculateCost(encoder, sample[0], nibble)) < min) {
                next = (*encoder);
                MOICoreEncoder_Update(&next, sample[0], nibble);
                score = MOICoreEncoder_SearchMinScore(&next, sample + 1, depth - 1, min);
                min = MOI_MIN_VAL(score, min);
            }
        }
    }

    return min;
}

/* nibbleの評価値を計算 */
static double MOICoreEncoder_EvaluateScore(
        const struct MOICoreEncoder *encoder, const int16_t *sample, uint32_t depth, uint8_t nibble)
{
    struct MOICoreEncoder next = (*encoder);

    /* 評価対象のnibbleで更新 */
    MOICoreEncoder_Update(&next, sample[0], nibble);

    /* スコア評価 */
    return MOICoreEncoder_SearchMinScore(&next, sample + 1, depth - 1, FLT_MAX);
}

/* 小さい方から数えてk番目の要素を取得（配列は破壊される） */
static double MOICoreEncoder_SelectTopK(double *data, uint32_t n, uint32_t k)
{
    uint32_t i, j, left, right;
    double x, t;

    left = 0; right = n - 1;
    while (left < right) {
        x = data[k];  i = left;  j = right;
        for ( ; ; ) {
            while (data[i] < x) { i++; }
            while (x < data[j]) { j--; }
            if (i >= j) { break; }
            t = data[i]; data[i] = data[j];  data[j] = t;
            i++;  j--;
        }
        if (i <= k) { left = j + 1; }
        if (k <= j) { right = i - 1; }
    }

    return data[k];
}

/* モノラルブロックのエンコード */
static MOIError MOIEncoder_EncodeSamples(
    struct MOIEncoder *encoder, const int16_t *input, uint32_t num_samples,
    uint8_t *code_seq, int8_t *best_init_stepsize_index)
{
#define HALF_NUM_CODES (MOIENCODER_NUM_CODES / 2)
#define SCORE_SIZE MOI_MAX_VAL(MOI_MAX_SEARCH_BEAM_WIDTH * HALF_NUM_CODES, MOI_IMAADPCM_STEPSIZE_TABLE_SIZE)
    uint32_t i, smpl, beam_width, depth;
    double threshold;
    double score[SCORE_SIZE];
    double score_work[SCORE_SIZE];
    struct MOICoreEncoderCandidate *candidate, *backup, *defalut_enc;

    /* 引数チェック */
    if ((encoder == NULL) || (input == NULL) || (code_seq == NULL) || (num_samples == 0)) {
        return MOI_ERROR_INVALID_ARGUMENT;
    }

    /* オート変数に受ける */
    candidate = encoder->candidate;
    backup = encoder->backup;
    beam_width = encoder->encode_parameter.search_beam_width;
    depth = encoder->encode_parameter.search_depth;
    defalut_enc = &(encoder->default_candidate);

    MOI_ASSERT((beam_width > 0) && (beam_width <= MOI_MAX_SEARCH_BEAM_WIDTH));
    MOI_ASSERT((depth > 0) && (depth <= MOI_MAX_SEARCH_DEPTH));

    /* 初期ステップサイズインデックスの選択 */
    {
        struct MOICoreEncoder init;

        /* 各ステップサイズでスコア計算 */
        init.prev_sample = input[0]; init.total_cost = 0.0;
        for (i = 0; i < MOI_IMAADPCM_STEPSIZE_TABLE_SIZE; i++) {
            init.stepsize_index = (int8_t)i;
            score[i] = MOICoreEncoder_SearchMinScore(&init,
                    input + 1, MOI_MIN_VAL(depth, num_samples - 1), FLT_MAX);
        }

        /* 上位選択の閾値 */
        memcpy(score_work, score, sizeof(double) * MOI_IMAADPCM_STEPSIZE_TABLE_SIZE);
        threshold = MOICoreEncoder_SelectTopK(score_work, MOI_IMAADPCM_STEPSIZE_TABLE_SIZE, beam_width);

        /* 上位選択 */
        {
            uint32_t n = 0, argmin = beam_width;
            double min = FLT_MAX;
            for (i = 0; i < MOI_IMAADPCM_STEPSIZE_TABLE_SIZE; i++) {
                if (score[i] <= threshold) {
                    candidate[n].encoder.prev_sample = input[0];
                    candidate[n].encoder.total_cost = 0.0;
                    candidate[n].encoder.stepsize_index = (int8_t)i;
                    candidate[n].init_stepsize_index = (int8_t)i;
                    if (min > score[i]) {
                        min = score[i];
                        argmin = n;
                    }
                    n++;
                    if (n == beam_width) {
                        break;
                    }
                }
            }
            MOI_ASSERT(n == beam_width);
            MOI_ASSERT(argmin < beam_width);

            /* デフォルト候補の初期化 */
            defalut_enc->encoder = candidate[argmin].encoder;
            defalut_enc->init_stepsize_index = candidate[argmin].init_stepsize_index;
        }
    }

    /* ブロックデータエンコード */
    for (smpl = 1; smpl < num_samples; smpl++) {
        /* コスト計算 */
        for (i = 0; i < beam_width; i++) {
            const struct MOICoreEncoder *core = &(candidate[i].encoder);
            const uint32_t init_depth = MOI_MIN_VAL(depth, num_samples - smpl);
            const uint8_t sign = ((input[smpl] - core->prev_sample) < 0) ? 8 : 0;
            uint8_t abs;
            for (abs = 0; abs < HALF_NUM_CODES; abs++) {
                /* 同一符号の中でコスト計算 */
                score[i * HALF_NUM_CODES + abs]
                    = MOICoreEncoder_EvaluateScore(core, &input[smpl], init_depth, abs | sign);
            }
        }

        /* 上位選択の閾値 */
        memcpy(score_work, score, sizeof(double) * beam_width * HALF_NUM_CODES);
        threshold = MOICoreEncoder_SelectTopK(score_work, beam_width * HALF_NUM_CODES, beam_width);
        /* 最大値が小さい場合の対策 */
        if (threshold < FLT_MIN) {
            threshold = FLT_MIN;
        }

        /* 上位選択 */
        /* 符号列（状態遷移記録）と候補エンコーダのバックアップ */
        for (i = 0; i < beam_width; i++) {
            memcpy(backup[i].code, candidate[i].code, sizeof(uint8_t) * smpl);
            backup[i].encoder = candidate[i].encoder;
            backup[i].init_stepsize_index = candidate[i].init_stepsize_index;
        }
        /* 閾値未満のコストを持つエンコーダを次の候補に選択 */
        {
            uint32_t n = 0;
            uint8_t abs;
            for (i = 0; i < beam_width; i++) {
                for (abs = 0; abs < HALF_NUM_CODES; abs++) {
                    if (score[i * HALF_NUM_CODES + abs] <= threshold) {
                        struct MOICoreEncoder entry = backup[i].encoder;
                        const uint8_t nibble = ((input[smpl] - entry.prev_sample) < 0) ? (abs | 0x8) : abs;
                        MOICoreEncoder_Update(&entry, input[smpl], nibble);
                        candidate[n].encoder = entry;
                        candidate[n].init_stepsize_index = backup[i].init_stepsize_index;
                        memcpy(candidate[n].code, backup[i].code, sizeof(uint8_t) * smpl);
                        candidate[n].code[smpl] = nibble;
                        n++;
                        if (n == beam_width) {
                            goto SELECT_END;
                        }
                    }
                }
            }
SELECT_END:
            MOI_ASSERT(n == beam_width);
        }

        /* デフォルト候補の符号作成 */
        {
            const uint8_t nibble = MOICoreEncoder_CalculateIMAADPCMNibble(&(defalut_enc->encoder), input[smpl]);
            MOICoreEncoder_Update(&(defalut_enc->encoder), input[smpl], nibble);
            defalut_enc->code[smpl] = nibble;
        }
    }

    {
        /* 最小コストのインデックス探索 */
        double min = FLT_MAX;
        uint32_t best_index = beam_width;
        for (i = 0; i < beam_width; i++) {
            if (min > candidate[i].encoder.total_cost) {
                min = candidate[i].encoder.total_cost;
                best_index = i;
            }
        }
        MOI_ASSERT(best_index < beam_width);

        /* デフォルト候補の方がコストが小さければそちらを使う */
        if (defalut_enc->encoder.total_cost < candidate[best_index].encoder.total_cost) {
            memcpy(code_seq, defalut_enc->code, sizeof(uint8_t) * num_samples);
            (*best_init_stepsize_index) = defalut_enc->init_stepsize_index;
        } else {
            memcpy(code_seq, candidate[best_index].code, sizeof(uint8_t) * num_samples);
            (*best_init_stepsize_index) = candidate[best_index].init_stepsize_index;
        }
    }

    return MOI_ERROR_OK;
#undef SCORE_SIZE
#undef HALF_NUM_CODES
}

/* 単一データブロックエンコード */
MOIApiResult MOIEncoder_EncodeBlock(
        struct MOIEncoder *encoder,
        const int16_t *const *input, uint32_t num_samples,
        uint8_t *data, uint32_t data_size, uint32_t *output_size)
{
    MOIError err;
    uint32_t ch, smpl;
    uint8_t *data_pos;
    const struct MOIEncodeParameter *parameter;

    /* 引数チェック */
    if ((encoder == NULL) || (data == NULL)
            || (input == NULL) || (output_size == NULL)) {
        return MOI_APIRESULT_INVALID_ARGUMENT;
    }
    parameter = &(encoder->encode_parameter);

    /* 十分なデータサイズがあるか確認 */
    if (data_size < ((num_samples * parameter->num_channels) / 2 + 4)) {
        return MOI_APIRESULT_INSUFFICIENT_DATA;
    }
    data_pos = data;

    /* 最前符号列の探索 */
    for (ch = 0; ch < parameter->num_channels; ch++) {
        if ((err = MOIEncoder_EncodeSamples(encoder, input[ch], num_samples,
                encoder->best_code[ch], &(encoder->best_init_stepsize_index[ch]))) != MOI_ERROR_OK) {
            /* エラーハンドル */
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
    }

    /* ブロックヘッダエンコード */
    for (ch = 0; ch < parameter->num_channels; ch++) {
        ByteArray_PutUint16LE(data_pos, input[ch][0]);
        ByteArray_PutUint8(data_pos, encoder->best_init_stepsize_index[ch]);
        ByteArray_PutUint8(data_pos, 0); /* reserved */
    }

    /* ブロックデータエンコード */
    switch (parameter->num_channels) {
    case 1:
        for (smpl = 1; smpl < num_samples; smpl += 2) {
            uint8_t nibble[2];
            uint8_t u8buf;
            nibble[0] = encoder->best_code[0][smpl + 0];
            nibble[1] = encoder->best_code[0][smpl + 1];
            MOI_ASSERT((uint32_t)(data_pos - data) < data_size);
            MOI_ASSERT((nibble[0] <= 0xF) && (nibble[1] <= 0xF));
            u8buf = (uint8_t)((nibble[0] << 0) | (nibble[1] << 4));
            ByteArray_PutUint8(data_pos, u8buf);
        }
        break;
    case 2:
        for (smpl = 1; smpl < num_samples; smpl += 8) {
            for (ch = 0; ch < 2; ch++) {
                uint8_t nibble[8];
                uint32_t u32buf;
                MOI_ASSERT((uint32_t)(data_pos - data) < data_size);
                nibble[0] = encoder->best_code[ch][smpl + 0];
                nibble[1] = encoder->best_code[ch][smpl + 1];
                nibble[2] = encoder->best_code[ch][smpl + 2];
                nibble[3] = encoder->best_code[ch][smpl + 3];
                nibble[4] = encoder->best_code[ch][smpl + 4];
                nibble[5] = encoder->best_code[ch][smpl + 5];
                nibble[6] = encoder->best_code[ch][smpl + 6];
                nibble[7] = encoder->best_code[ch][smpl + 7];
                MOI_ASSERT((nibble[0] <= 0xF) && (nibble[1] <= 0xF) && (nibble[2] <= 0xF) && (nibble[3] <= 0xF)
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
        break;
    default:
        MOI_ASSERT(0);
    }

    /* 書き出しサイズをセット */
    (*output_size) = (uint32_t)(data_pos - data);

    return MOI_APIRESULT_OK;
}

/* エンコードパラメータをヘッダに変換 */
static MOIError MOIEncoder_ConvertParameterToHeader(
        const struct MOIEncodeParameter *parameter, uint32_t num_samples,
        struct IMAADPCMWAVHeader *header)
{
    uint32_t block_data_size;
    struct IMAADPCMWAVHeader tmp_header = { 0, };

    /* 引数チェック */
    if ((parameter == NULL) || (header == NULL)) {
        return MOI_ERROR_INVALID_ARGUMENT;
    }

    /* サンプルあたりビット数は4固定 */
    if (parameter->bits_per_sample != MOI_BITS_PER_SAMPLE) {
        return MOI_ERROR_INVALID_FORMAT;
    }

    /* ヘッダサイズは決め打ち */
    tmp_header.header_size = MOIENCODER_HEADER_SIZE;
    /* 総サンプル数 */
    tmp_header.num_samples = num_samples;

    /* そのままヘッダに入れられるメンバ */
    tmp_header.num_channels = parameter->num_channels;
    tmp_header.sampling_rate = parameter->sampling_rate;
    tmp_header.bits_per_sample = parameter->bits_per_sample;
    tmp_header.block_size = parameter->block_size;

    /* 計算が必要なメンバ */
    if (parameter->block_size <= parameter->num_channels * 4) {
        /* データを入れる領域がない */
        return MOI_ERROR_INVALID_FORMAT;
    }
    /* 4はチャンネルあたりのヘッダ領域サイズ */
    MOI_ASSERT(parameter->block_size >= (parameter->num_channels * 4));
    block_data_size = (uint32_t)(parameter->block_size - (parameter->num_channels * 4));
    MOI_ASSERT((block_data_size * 8) % (uint32_t)(parameter->bits_per_sample * parameter->num_channels) == 0);
    MOI_ASSERT((parameter->bits_per_sample * parameter->num_channels) != 0);
    tmp_header.num_samples_per_block = (uint16_t)((block_data_size * 8) / (uint32_t)(parameter->bits_per_sample * parameter->num_channels));
    /* ヘッダに入っている分+1 */
    tmp_header.num_samples_per_block++;
    MOI_ASSERT(tmp_header.num_samples_per_block != 0);
    tmp_header.bytes_per_sec = (parameter->block_size * parameter->sampling_rate) / tmp_header.num_samples_per_block;

    /* 成功終了 */
    (*header) = tmp_header;

    return MOI_ERROR_OK;
}

/* エンコードパラメータの設定 */
MOIApiResult MOIEncoder_SetEncodeParameter(
        struct MOIEncoder *encoder, const struct MOIEncodeParameter *parameter)
{
    struct IMAADPCMWAVHeader tmp_header = { 0, };

    /* 引数チェック */
    if ((encoder == NULL) || (parameter == NULL)) {
        return MOI_APIRESULT_INVALID_ARGUMENT;
    }

    /* ブロックサイズが大きすぎる */
    if (encoder->max_block_size < parameter->block_size) {
        return MOI_APIRESULT_INVALID_FORMAT;
    }

    /* パラメータ設定がおかしくないか、ヘッダへの変換を通じて確認 */
    /* 総サンプル数はダミー値を入れる */
    if (MOIEncoder_ConvertParameterToHeader(parameter, 0, &tmp_header) != MOI_ERROR_OK) {
        return MOI_APIRESULT_INVALID_FORMAT;
    }

    /* パラメータ設定 */
    encoder->encode_parameter = (*parameter);

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
    if (MOIEncoder_ConvertParameterToHeader(&(encoder->encode_parameter), num_samples, &header) != MOI_ERROR_OK) {
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
        num_encode_samples = MOI_MIN_VAL(header.num_samples_per_block, num_samples - progress);
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
        data_pos += write_size;
        write_offset += write_size;
        progress += num_encode_samples;
        MOI_ASSERT(write_size <= header.block_size);
        MOI_ASSERT(write_offset <= data_size);
    }

    /* 成功終了 */
    (*output_size) = write_offset;
    return MOI_APIRESULT_OK;
}
