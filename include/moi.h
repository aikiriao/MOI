#ifndef MOI_H_INCLUDED
#define MOI_H_INCLUDED

#include <stdint.h>

/* ライブラリバージョン */
#define MOI_VERSION 1

/* 処理可能な最大チャンネル数 */
#define MOI_MAX_NUM_CHANNELS 2

/* サンプルあたりビット数は4で固定 */
#define MOI_BITS_PER_SAMPLE 4

/* ビームサーチの最大幅 */
#define MOI_MAX_SEARCH_BEAM_WIDTH 16

/* 最大先読みサンプル数 */
#define MOI_MAX_SEARCH_DEPTH 8

/* API結果型 */
typedef enum {
    MOI_APIRESULT_OK = 0,              /* 成功                         */
    MOI_APIRESULT_INVALID_ARGUMENT,    /* 無効な引数                   */
    MOI_APIRESULT_INVALID_FORMAT,      /* 不正なフォーマット           */
    MOI_APIRESULT_INSUFFICIENT_BUFFER, /* バッファサイズが足りない     */
    MOI_APIRESULT_INSUFFICIENT_DATA,   /* データが足りない             */
    MOI_APIRESULT_PARAMETER_NOT_SET,   /* パラメータがセットされてない */
    MOI_APIRESULT_NG                   /* 分類不能な失敗               */
} MOIApiResult;

/* IMA-ADPCM形式のwavファイルのヘッダ情報 */
struct IMAADPCMWAVHeader {
    uint16_t num_channels;          /* チャンネル数                                 */
    uint32_t sampling_rate;         /* サンプリングレート                           */
    uint32_t bytes_per_sec;         /* データ速度[byte/sec]                         */
    uint16_t block_size;            /* ブロックサイズ                               */
    uint16_t bits_per_sample;       /* サンプルあたりビット数                       */
    uint16_t num_samples_per_block; /* ブロックあたりサンプル数                     */
    uint32_t num_samples;           /* 1チャンネルあたり総サンプル数                */
    uint32_t header_size;           /* ファイル先頭からdata領域先頭までのオフセット */
};

/* エンコーダ生成コンフィグ */
struct MOIEncoderConfig {
    uint16_t max_block_size;        /* 最大ブロックサイズ                           */
};

/* エンコードパラメータ */
struct MOIEncodeParameter {
    uint16_t num_channels;          /* チャンネル数                                 */
    uint32_t sampling_rate;         /* サンプリングレート                           */
    uint16_t bits_per_sample;       /* サンプルあたりビット数（今の所4で固定）      */
    uint16_t block_size;            /* ブロックサイズ[byte]                         */
    uint32_t search_beam_width;     /* 探索ビーム幅                                 */
    uint32_t search_depth;          /* 探索深さ                                     */
};

/* デコーダハンドル */
struct MOIDecoder;

/* エンコーダハンドル */
struct MOIEncoder;

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* ヘッダデコード */
MOIApiResult MOIDecoder_DecodeHeader(
        const uint8_t *data, uint32_t data_size, struct IMAADPCMWAVHeader *header);

/* ヘッダエンコード */
MOIApiResult MOIEncoder_EncodeHeader(
        const struct IMAADPCMWAVHeader *header, uint8_t *data, uint32_t data_size);

/* デコーダワークサイズ計算 */
int32_t MOIDecoder_CalculateWorkSize(void);

/* デコーダハンドル作成 */
struct MOIDecoder *MOIDecoder_Create(void *work, int32_t work_size);

/* デコーダハンドル破棄 */
void MOIDecoder_Destroy(struct MOIDecoder *decoder);

/* ヘッダ含めファイル全体をデコード */
MOIApiResult MOIDecoder_DecodeWhole(
        struct MOIDecoder *decoder,
        const uint8_t *data, uint32_t data_size,
        int16_t **buffer, uint32_t buffer_num_channels, uint32_t buffer_num_samples);

/* エンコーダワークサイズ計算 */
int32_t MOIEncoder_CalculateWorkSize(const struct MOIEncoderConfig *config);

/* エンコーダハンドル作成 */
struct MOIEncoder *MOIEncoder_Create(
        const struct MOIEncoderConfig *config, void *work, int32_t work_size);

/* エンコーダハンドル破棄 */
void MOIEncoder_Destroy(struct MOIEncoder *encoder);

/* エンコードパラメータの設定 */
MOIApiResult MOIEncoder_SetEncodeParameter(
        struct MOIEncoder *encoder, const struct MOIEncodeParameter *parameter);

/* ヘッダ含めファイル全体をエンコード */
MOIApiResult MOIEncoder_EncodeWhole(
        struct MOIEncoder *encoder,
        const int16_t *const *input, uint32_t num_samples,
        uint8_t *data, uint32_t data_size, uint32_t *output_size);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* MOI_H_INCLUDED */
