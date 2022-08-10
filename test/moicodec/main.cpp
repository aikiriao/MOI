#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <gtest/gtest.h>

#include "wav.h"

/* テスト対象のモジュール */
extern "C" {
#include "../../libs/moicodec/src/moi_encoder.c"
#include "../../libs/moicodec/src/moi_decoder.c"
}

/* 有効なエンコーダコンフィグをセット */
#define MOI_SetValidEncoderConfig(p_config) {\
    struct MOIEncoderConfig *p__config = p_config;\
    p__config->max_block_size = 256;\
}

/* 有効なヘッダをセット */
#define MOI_SetValidHeader(p_header) {\
    struct IMAADPCMWAVHeader *header__p = p_header;\
    header__p->num_channels = 1;\
    header__p->sampling_rate = 44100;\
    header__p->bytes_per_sec = 89422;\
    header__p->block_size = 256;\
    header__p->bits_per_sample = MOI_BITS_PER_SAMPLE;\
    header__p->num_samples_per_block = 505;\
    header__p->num_samples = 1024;\
    header__p->header_size = MOIENCODER_HEADER_SIZE;\
}

/* 有効なパラメータをセット */
#define MOI_SetValidParameter(p_param) {\
    struct MOIEncodeParameter *p__param = p_param;\
    p__param->num_channels = 1;\
    p__param->sampling_rate = 8000;\
    p__param->bits_per_sample = MOI_BITS_PER_SAMPLE;\
    p__param->block_size = 256;\
    p__param->search_beam_width = 2;\
    p__param->search_depth = 2;\
}

/* ヘッダエンコードデコードテスト */
TEST(IMAADPCMTest, HeaderEncodeDecodeTest)
{
    /* 成功例 */
    {
        uint8_t data[MOIENCODER_HEADER_SIZE] = { 0, };
        struct IMAADPCMWAVHeader header = { 0, }, tmp_header = { 0, };

        MOI_SetValidHeader(&header);

        /* エンコード->デコード */
        EXPECT_EQ(MOI_APIRESULT_OK, MOIEncoder_EncodeHeader(&header, data, sizeof(data)));
        EXPECT_EQ(MOI_APIRESULT_OK, MOIDecoder_DecodeHeader(data, sizeof(data), &tmp_header));

        /* デコードしたヘッダの一致確認 */
        EXPECT_EQ(header.num_channels, tmp_header.num_channels);
        EXPECT_EQ(header.sampling_rate, tmp_header.sampling_rate);
        EXPECT_EQ(header.bytes_per_sec, tmp_header.bytes_per_sec);
        EXPECT_EQ(header.block_size, tmp_header.block_size);
        EXPECT_EQ(header.bits_per_sample, tmp_header.bits_per_sample);
        EXPECT_EQ(header.num_samples_per_block, tmp_header.num_samples_per_block);
        EXPECT_EQ(header.num_samples, tmp_header.num_samples);
        EXPECT_EQ(header.header_size, tmp_header.header_size);
    }

    /* ヘッダエンコード失敗ケース */
    {
        struct IMAADPCMWAVHeader header;
        uint8_t data[MOIENCODER_HEADER_SIZE] = { 0, };

        /* 引数が不正 */
        MOI_SetValidHeader(&header);
        EXPECT_EQ(MOI_APIRESULT_INVALID_ARGUMENT, MOIEncoder_EncodeHeader(NULL, data, sizeof(data)));
        EXPECT_EQ(MOI_APIRESULT_INVALID_ARGUMENT, MOIEncoder_EncodeHeader(&header, NULL, sizeof(data)));

        /* データサイズ不足 */
        MOI_SetValidHeader(&header);
        EXPECT_EQ(MOI_APIRESULT_INSUFFICIENT_DATA, MOIEncoder_EncodeHeader(&header, data, sizeof(data) - 1));
        EXPECT_EQ(MOI_APIRESULT_INSUFFICIENT_DATA, MOIEncoder_EncodeHeader(&header, data, MOIENCODER_HEADER_SIZE - 1));

        /* チャンネル数異常 */
        MOI_SetValidHeader(&header);
        header.num_channels = 3;
        EXPECT_EQ(MOI_APIRESULT_INVALID_FORMAT, MOIEncoder_EncodeHeader(&header, data, sizeof(data)));

        /* ビット深度異常 */
        MOI_SetValidHeader(&header);
        header.bits_per_sample = 2;
        EXPECT_EQ(MOI_APIRESULT_INVALID_FORMAT, MOIEncoder_EncodeHeader(&header, data, sizeof(data)));
    }

    /* ヘッダデコード失敗ケース */
    {
        struct IMAADPCMWAVHeader header, getheader;
        uint8_t valid_data[MOIENCODER_HEADER_SIZE] = { 0, };
        uint8_t data[MOIENCODER_HEADER_SIZE];

        /* 有効な内容を作っておく */
        MOI_SetValidHeader(&header);
        MOIEncoder_EncodeHeader(&header, valid_data, sizeof(valid_data));

        /* チャンクIDが不正 */
        /* RIFFの破壊 */
        memcpy(data, valid_data, sizeof(valid_data));
        data[0] = 'a';
        EXPECT_EQ(MOI_APIRESULT_INVALID_FORMAT, MOIDecoder_DecodeHeader(data, sizeof(data), &getheader));
        /* WAVEの破壊 */
        memcpy(data, valid_data, sizeof(valid_data));
        data[8] = 'a';
        EXPECT_EQ(MOI_APIRESULT_INVALID_FORMAT, MOIDecoder_DecodeHeader(data, sizeof(data), &getheader));
        /* FMTの破壊 */
        memcpy(data, valid_data, sizeof(valid_data));
        data[12] = 'a';
        EXPECT_EQ(MOI_APIRESULT_INVALID_FORMAT, MOIDecoder_DecodeHeader(data, sizeof(data), &getheader));
        /* factの破壊はスキップ: factチャンクはオプショナルだから
        memcpy(data, valid_data, sizeof(valid_data));
        data[40] = 'a';
        EXPECT_EQ(MOI_APIRESULT_INVALID_FORMAT, MOIDecoder_DecodeHeader(data, sizeof(data), &getheader));
        */
        /* dataの破壊: dataチャンクが見つけられない */
        memcpy(data, valid_data, sizeof(valid_data));
        data[52] = 'a';
        EXPECT_EQ(MOI_APIRESULT_INSUFFICIENT_DATA, MOIDecoder_DecodeHeader(data, sizeof(data), &getheader));

        /* クソデカfmtチャンクサイズ */
        memcpy(data, valid_data, sizeof(valid_data));
        ByteArray_WriteUint32LE(&data[16], sizeof(data));
        EXPECT_EQ(MOI_APIRESULT_INSUFFICIENT_DATA, MOIDecoder_DecodeHeader(data, sizeof(data), &getheader));
        /* 異常なWAVEフォーマットタイプ */
        memcpy(data, valid_data, sizeof(valid_data));
        ByteArray_WriteUint32LE(&data[20], 0);
        EXPECT_EQ(MOI_APIRESULT_INVALID_FORMAT, MOIDecoder_DecodeHeader(data, sizeof(data), &getheader));
        /* 異常なチャンネル数 */
        memcpy(data, valid_data, sizeof(valid_data));
        ByteArray_WriteUint32LE(&data[22], MOI_MAX_NUM_CHANNELS + 1);
        EXPECT_EQ(MOI_APIRESULT_INVALID_FORMAT, MOIDecoder_DecodeHeader(data, sizeof(data), &getheader));
        /* 異常なfmtチャンクのエキストラサイズ */
        memcpy(data, valid_data, sizeof(valid_data));
        ByteArray_WriteUint32LE(&data[36], 0);
        EXPECT_EQ(MOI_APIRESULT_INVALID_FORMAT, MOIDecoder_DecodeHeader(data, sizeof(data), &getheader));
        /* 異常なFACTチャンクサイズ */
        memcpy(data, valid_data, sizeof(valid_data));
        ByteArray_WriteUint32LE(&data[44], 0);
        EXPECT_EQ(MOI_APIRESULT_INVALID_FORMAT, MOIDecoder_DecodeHeader(data, sizeof(data), &getheader));
    }
}

/* デコードハンドル作成破棄テスト */
TEST(MOIDecoder, CreateDestroyTest)
{
    /* ワークサイズ計算テスト */
    {
        int32_t work_size;

        work_size = MOIDecoder_CalculateWorkSize();
        EXPECT_TRUE(work_size >= (int32_t)sizeof(struct MOIDecoder));
    }

    /* ワーク領域渡しによるハンドル作成（成功例） */
    {
        void *work;
        int32_t work_size;
        struct MOIDecoder *decoder;

        work_size = MOIDecoder_CalculateWorkSize();
        work = malloc(work_size);

        decoder = MOIDecoder_Create(work, work_size);
        EXPECT_TRUE(decoder != NULL);
        EXPECT_TRUE(decoder->work == NULL);

        MOIDecoder_Destroy(decoder);
        free(work);
    }

    /* 自前確保によるハンドル作成（成功例） */
    {
        struct MOIDecoder *decoder;

        decoder = MOIDecoder_Create(NULL, 0);
        EXPECT_TRUE(decoder != NULL);
        EXPECT_TRUE(decoder->work != NULL);

        MOIDecoder_Destroy(decoder);
    }

    /* ワーク領域渡しによるハンドル作成（失敗ケース） */
    {
        void *work;
        int32_t work_size;
        struct MOIDecoder *decoder;

        work_size = MOIDecoder_CalculateWorkSize();
        work = malloc(work_size);

        /* 引数が不正 */
        decoder = MOIDecoder_Create(NULL, work_size);
        EXPECT_TRUE(decoder == NULL);
        decoder = MOIDecoder_Create(work, 0);
        EXPECT_TRUE(decoder == NULL);

        /* ワークサイズ不足 */
        decoder = MOIDecoder_Create(work, work_size - 1);
        EXPECT_TRUE(decoder == NULL);

        free(work);
    }
}

/* デコード結果が一致するか確認するサブルーチン 一致していたら1, していなければ0を返す */
static uint8_t MOIDecoderTest_CheckDecodeResult(const char *adpcm_filename, const char *decodedwav_filename)
{
    FILE        *fp;
    uint8_t     *data;
    struct stat fstat;
    uint32_t    data_size;
    uint32_t    ch, smpl, is_ok;
    int16_t     *output[MOI_MAX_NUM_CHANNELS];
    struct MOIDecoder *decoder;
    struct IMAADPCMWAVHeader header;
    struct WAVFile *wavfile;

    /* データロード */
    fp = fopen(adpcm_filename, "rb");
    assert(fp != NULL);
    stat(adpcm_filename, &fstat);
    data_size = (uint32_t)fstat.st_size;
    data = (uint8_t *)malloc(data_size);
    fread(data, sizeof(uint8_t), data_size, fp);
    fclose(fp);

    /* ヘッダデコード */
    if (MOIDecoder_DecodeHeader(data, data_size, &header) != MOI_APIRESULT_OK) {
        free(data);
        return 0;
    }

    /* リソース確保 */
    wavfile = WAV_CreateFromFile(decodedwav_filename);
    assert(wavfile != NULL);
    for (ch = 0; ch < header.num_channels; ch++) {
        output[ch] = (int16_t *)malloc(sizeof(int16_t) * header.num_samples);
    }
    decoder = MOIDecoder_Create(NULL, 0);

    /* デコード実行 */
    if (MOIDecoder_DecodeWhole(decoder,
                data, data_size, output, header.num_channels, header.num_samples) != MOI_APIRESULT_OK) {
        is_ok = 0;
        goto CHECK_END;
    }

    /* ffmpegでデコードしたものと一致するか？ */
    is_ok = 1;
    for (ch = 0; ch < header.num_channels; ch++) {
        for (smpl = 0; smpl < header.num_samples; smpl++) {
            if (WAVFile_PCM(wavfile, smpl, ch) != (output[ch][smpl] << 16)) {
                is_ok = 0;
                goto CHECK_END;
            }
        }
    }

CHECK_END:
    /* 確保した領域の開放 */
    WAV_Destroy(wavfile);
    MOIDecoder_Destroy(decoder);
    for (ch = 0; ch < header.num_channels; ch++) {
        free(output[ch]);
    }
    free(data);

    return is_ok;
}

/* デコードテスト */
TEST(MOIDecoder, DecodeTest)
{
    /* 実データのヘッダデコード */
    {
        const char  test_filename[] = "sin300Hz_mono_adpcm_ffmpeg.wav";
        FILE        *fp;
        uint8_t     *data;
        struct stat fstat;
        uint32_t    data_size;
        struct IMAADPCMWAVHeader header;

        /* データロード */
        fp = fopen(test_filename, "rb");
        assert(fp != NULL);
        stat(test_filename, &fstat);
        data_size = (uint32_t)fstat.st_size;
        data = (uint8_t *)malloc(data_size);
        fread(data, sizeof(uint8_t), data_size, fp);
        fclose(fp);

        /* ヘッダデコード */
        EXPECT_EQ(MOI_APIRESULT_OK, MOIDecoder_DecodeHeader(data, data_size, &header));

        /* 想定した内容になっているか */
        EXPECT_EQ(1, header.num_channels);
        EXPECT_EQ(48000, header.sampling_rate);
        EXPECT_EQ(16000, header.bytes_per_sec);
        EXPECT_EQ(1024, header.block_size);
        EXPECT_EQ(2041, header.num_samples_per_block);
        EXPECT_EQ(24492, header.num_samples);
        EXPECT_EQ(94, header.header_size);

        free(data);
    }

    /* 実データのヘッダデコード（factチャンクがないwav） */
    {
        const char  test_filename[] = "bunny1im.wav";
        FILE        *fp;
        uint8_t     *data;
        struct stat fstat;
        uint32_t    data_size;
        struct IMAADPCMWAVHeader header;

        /* データロード */
        fp = fopen(test_filename, "rb");
        assert(fp != NULL);
        stat(test_filename, &fstat);
        data_size = (uint32_t)fstat.st_size;
        data = (uint8_t *)malloc(data_size);
        fread(data, sizeof(uint8_t), data_size, fp);
        fclose(fp);

        /* ヘッダデコード */
        EXPECT_EQ(MOI_APIRESULT_OK, MOIDecoder_DecodeHeader(data, data_size, &header));

        /* 想定した内容になっているか */
        EXPECT_EQ(1, header.num_channels);
        EXPECT_EQ(8000, header.sampling_rate);
        EXPECT_EQ(4064, header.bytes_per_sec);
        EXPECT_EQ(256, header.block_size);
        EXPECT_EQ(505, header.num_samples_per_block);
        EXPECT_EQ(187860, header.num_samples);
        EXPECT_EQ(48, header.header_size);

        free(data);
    }

    /* 実データのデータデコード一致確認 */
    {
        EXPECT_EQ(1,
                MOIDecoderTest_CheckDecodeResult(
                    "sin300Hz_mono_adpcm_ffmpeg.wav", "sin300Hz_mono_adpcm_ffmpeg_decoded.wav"));
        EXPECT_EQ(1,
                MOIDecoderTest_CheckDecodeResult(
                    "sin300Hz_adpcm_ffmpeg.wav", "sin300Hz_adpcm_ffmpeg_decoded.wav"));
        EXPECT_EQ(1,
                MOIDecoderTest_CheckDecodeResult(
                    "unit_impulse_mono_adpcm_ffmpeg.wav", "unit_impulse_mono_adpcm_ffmpeg_decoded.wav"));
        EXPECT_EQ(1,
                MOIDecoderTest_CheckDecodeResult(
                    "unit_impulse_adpcm_ffmpeg.wav", "unit_impulse_adpcm_ffmpeg_decoded.wav"));
    }
}

/* エンコードハンドル作成破棄テスト */
TEST(MOIEncoder, CreateDestroyTest)
{
    /* ワークサイズ計算テスト */
    {
        int32_t work_size;
        struct MOIEncoderConfig config;

        MOI_SetValidEncoderConfig(&config);
        work_size = MOIEncoder_CalculateWorkSize(&config);
        EXPECT_TRUE(work_size >= (int32_t)sizeof(struct MOIEncoder));

        MOI_SetValidEncoderConfig(&config);
        config.max_block_size = 0;
        work_size = MOIEncoder_CalculateWorkSize(&config);
        EXPECT_TRUE(work_size < 0);
    }

    /* ワーク領域渡しによるハンドル作成（成功例） */
    {
        void *work;
        int32_t work_size;
        struct MOIEncoder *encoder;
        struct MOIEncoderConfig config;

        MOI_SetValidEncoderConfig(&config);
        work_size = MOIEncoder_CalculateWorkSize(&config);
        work = malloc(work_size);

        encoder = MOIEncoder_Create(&config, work, work_size);
        EXPECT_TRUE(encoder != NULL);
        EXPECT_TRUE(encoder->work == NULL);
        EXPECT_EQ(0, encoder->set_parameter);
        EXPECT_EQ(config.max_block_size, encoder->max_block_size);

        MOIEncoder_Destroy(encoder);
        free(work);
    }

    /* 自前確保によるハンドル作成（成功例） */
    {
        struct MOIEncoder *encoder;
        struct MOIEncoderConfig config;

        MOI_SetValidEncoderConfig(&config);
        encoder = MOIEncoder_Create(&config, NULL, 0);
        EXPECT_TRUE(encoder != NULL);
        EXPECT_TRUE(encoder->work != NULL);
        EXPECT_EQ(0, encoder->set_parameter);
        EXPECT_EQ(config.max_block_size, encoder->max_block_size);

        MOIEncoder_Destroy(encoder);
    }

    /* ワーク領域渡しによるハンドル作成（失敗ケース） */
    {
        void *work;
        int32_t work_size;
        struct MOIEncoder *encoder;
        struct MOIEncoderConfig config;

        MOI_SetValidEncoderConfig(&config);
        work_size = MOIEncoder_CalculateWorkSize(&config);
        work = malloc(work_size);

        /* 引数が不正 */
        encoder = MOIEncoder_Create(NULL, work, work_size);
        EXPECT_TRUE(encoder == NULL);
        encoder = MOIEncoder_Create(&config, NULL, work_size);
        EXPECT_TRUE(encoder == NULL);
        encoder = MOIEncoder_Create(&config, work, 0);
        EXPECT_TRUE(encoder == NULL);

        /* ワークサイズ不足 */
        encoder = MOIEncoder_Create(&config, work, work_size - 1);
        EXPECT_TRUE(encoder == NULL);

        /* コンフィグ不正 */
        MOI_SetValidEncoderConfig(&config);
        config.max_block_size = 0;
        encoder = MOIEncoder_Create(&config, work, work_size);
        EXPECT_TRUE(encoder == NULL);

        free(work);
    }

    /* 自前確保によるハンドル作成（失敗ケース） */
    {
        struct MOIEncoder *encoder;
        struct MOIEncoderConfig config;

        MOI_SetValidEncoderConfig(&config);
        config.max_block_size = 0;
        encoder = MOIEncoder_Create(&config, NULL, 0);
        EXPECT_TRUE(encoder == NULL);

        MOIEncoder_Destroy(encoder);
    }
}

/* エンコードパラメータ設定テスト */
TEST(MOIEncoder, SetEncodeParameterTest)
{
    /* 成功例 */
    {
        struct MOIEncoder *encoder;
        struct MOIEncoderConfig config;
        struct MOIEncodeParameter param;

        MOI_SetValidEncoderConfig(&config);
        encoder = MOIEncoder_Create(&config, NULL, 0);

        MOI_SetValidParameter(&param);
        EXPECT_EQ(MOI_APIRESULT_OK, MOIEncoder_SetEncodeParameter(encoder, &param));
        EXPECT_EQ(1, encoder->set_parameter);
        EXPECT_EQ(0, memcmp(&(encoder->encode_parameter), &param, sizeof(struct MOIEncodeParameter)));

        MOIEncoder_Destroy(encoder);
    }

    /* 失敗ケース */
    {
        struct MOIEncoder *encoder;
        struct MOIEncoderConfig config;
        struct MOIEncodeParameter param;

        MOI_SetValidEncoderConfig(&config);
        encoder = MOIEncoder_Create(&config, NULL, 0);

        /* 引数が不正 */
        MOI_SetValidParameter(&param);
        EXPECT_EQ(MOI_APIRESULT_INVALID_ARGUMENT, MOIEncoder_SetEncodeParameter(NULL,  &param));
        EXPECT_EQ(MOI_APIRESULT_INVALID_ARGUMENT, MOIEncoder_SetEncodeParameter(encoder, NULL));

        /* サンプルあたりビット数が異常 */
        MOI_SetValidParameter(&param);
        param.bits_per_sample = 0;
        EXPECT_EQ(MOI_APIRESULT_INVALID_FORMAT, MOIEncoder_SetEncodeParameter(encoder, &param));

        /* ブロックサイズが小さすぎる */
        MOI_SetValidParameter(&param);
        param.block_size = 0;
        EXPECT_EQ(MOI_APIRESULT_INVALID_FORMAT, MOIEncoder_SetEncodeParameter(encoder, &param));
        MOI_SetValidParameter(&param);
        param.block_size = param.num_channels * 4;
        EXPECT_EQ(MOI_APIRESULT_INVALID_FORMAT, MOIEncoder_SetEncodeParameter(encoder, &param));

        MOIEncoder_Destroy(encoder);
    }
}

/* エンコード→デコードテスト 成功時は1, 失敗時は0を返す */
static uint8_t MOIEncoderTest_EncodeDecodeTest(
        const char *wav_filename, uint16_t bits_per_sample, uint16_t block_size, double rms_epsilon)
{
    struct WAVFile *wavfile;
    struct stat fstat;
    int16_t *input[MOI_MAX_NUM_CHANNELS];
    int16_t *decoded[MOI_MAX_NUM_CHANNELS];
    uint8_t is_ok;
    uint32_t ch, smpl, buffer_size, output_size;
    uint32_t num_channels, num_samples;
    uint8_t *buffer;
    double rms_error;
    struct MOIEncodeParameter enc_param;
    struct MOIEncoderConfig enc_config;
    struct MOIEncoder *encoder;
    struct MOIDecoder *decoder;

    assert((wav_filename != NULL) && (rms_epsilon >= 0.0f));

    /* 入力wav取得 */
    wavfile = WAV_CreateFromFile(wav_filename);
    assert(wavfile != NULL);
    num_channels = wavfile->format.num_channels;
    num_samples = wavfile->format.num_samples;

    /* 出力データの領域割当て */
    for (ch = 0; ch < num_channels; ch++) {
        input[ch]   = (int16_t *)malloc(sizeof(int16_t) * num_samples);
        decoded[ch] = (int16_t *)malloc(sizeof(int16_t) * num_samples);
    }
    /* 入力wavと同じサイズの出力領域を確保（増えることはないと期待） */
    stat(wav_filename, &fstat);
    buffer_size = fstat.st_size;
    buffer = (uint8_t *)malloc(buffer_size);

    /* 16bit幅でデータ取得 */
    for (ch = 0; ch < num_channels; ch++) {
        for (smpl = 0; smpl < num_samples; smpl++) {
            input[ch][smpl] = WAVFile_PCM(wavfile, smpl, ch) >> 16;
        }
    }

    /* ハンドル作成 */
    MOI_SetValidEncoderConfig(&enc_config);
    enc_config.max_block_size = block_size;
    encoder = MOIEncoder_Create(&enc_config, NULL, 0);
    decoder = MOIDecoder_Create(NULL, 0);

    /* エンコードパラメータをセット */
    MOI_SetValidParameter(&enc_param);
    enc_param.num_channels = num_channels;
    enc_param.sampling_rate = wavfile->format.sampling_rate;
    enc_param.bits_per_sample = bits_per_sample;
    enc_param.block_size = block_size;
    if (MOIEncoder_SetEncodeParameter(encoder, &enc_param) != MOI_APIRESULT_OK) {
        is_ok = 0;
        goto CHECK_END;
    }

    /* エンコード */
    if (MOIEncoder_EncodeWhole(
                encoder, (const int16_t *const *)input, num_samples,
                buffer, buffer_size, &output_size) != MOI_APIRESULT_OK) {
        is_ok = 0;
        goto CHECK_END;
    }
    /* 半分以下にはなるはず */
    if (output_size >= (buffer_size / 2)) {
        is_ok = 0;
        goto CHECK_END;
    }

    /* デコード */
    if (MOIDecoder_DecodeWhole(
                decoder, buffer, output_size,
                decoded, num_channels, num_samples) != MOI_APIRESULT_OK) {
        is_ok = 0;
        goto CHECK_END;
    }

    /* ロスがあるのでRMSE基準でチェック */
    rms_error = 0.0;
    for (ch = 0; ch < num_channels; ch++) {
        for (smpl = 0; smpl < num_samples; smpl++) {
            double pcm1, pcm2, abs_error;
            pcm1 = (double)input[ch][smpl] / INT16_MAX;
            pcm2 = (double)decoded[ch][smpl] / INT16_MAX;
            abs_error = fabs(pcm1 - pcm2);
            rms_error += abs_error * abs_error;
        }
    }
    rms_error = sqrt(rms_error / (num_samples * num_channels));

    /* マージンチェック */
    if (rms_error < rms_epsilon) {
        is_ok = 1;
    } else {
        is_ok = 0;
    }

CHECK_END:
    /* 領域開放 */
    MOIEncoder_Destroy(encoder);
    MOIDecoder_Destroy(decoder);
    free(buffer);
    for (ch = 0; ch < num_channels; ch++) {
        free(input[ch]);
        free(decoded[ch]);
    }

    return is_ok;
}

/* エンコードテスト */
TEST(MOIEncoder, EncodeTest)
{
    /* 簡単なデータをエンコード→デコードしてみる */
    {
#define NUM_CHANNELS  1
#define NUM_SAMPLES   1024
        int16_t *input[MOI_MAX_NUM_CHANNELS];
        int16_t *decoded[MOI_MAX_NUM_CHANNELS];
        uint32_t ch, smpl, buffer_size, output_size;
        uint8_t *buffer;
        double rms_error;
        struct MOIEncodeParameter enc_param;
        struct MOIEncoderConfig enc_config;
        struct MOIEncoder *encoder;
        struct MOIDecoder *decoder;

        /* 出力データの領域割当て */
        for (ch = 0; ch < NUM_CHANNELS; ch++) {
            input[ch] = (int16_t *)malloc(sizeof(int16_t) * NUM_SAMPLES);
            decoded[ch] = (int16_t *)malloc(sizeof(int16_t) * NUM_SAMPLES);
        }
        /* 入力wavと同じサイズの出力領域を確保（増えることはないと期待） */
        buffer_size = NUM_CHANNELS * NUM_SAMPLES * sizeof(int16_t);
        buffer = (uint8_t *)malloc(buffer_size);

        /* データ作成: 正弦波 */
        for (ch = 0; ch < NUM_CHANNELS; ch++) {
            for (smpl = 0; smpl < NUM_SAMPLES; smpl++) {
                input[ch][smpl] = (int16_t)(INT16_MAX * sin((2.0 * 3.1415 * 440.0 * smpl) / 48000.0));
            }
        }

        /* ハンドル作成 */
        MOI_SetValidEncoderConfig(&enc_config);
        encoder = MOIEncoder_Create(&enc_config, NULL, 0);
        decoder = MOIDecoder_Create(NULL, 0);

        /* エンコードパラメータをセット */
        MOI_SetValidParameter(&enc_param);
        enc_param.num_channels = NUM_CHANNELS;
        enc_param.sampling_rate = 8000;
        enc_param.bits_per_sample = MOI_BITS_PER_SAMPLE;
        enc_param.block_size = 256;
        EXPECT_EQ(MOI_APIRESULT_OK, MOIEncoder_SetEncodeParameter(encoder, &enc_param));

        /* エンコード */
        EXPECT_EQ(
                MOI_APIRESULT_OK,
                MOIEncoder_EncodeWhole(
                    encoder, (const int16_t *const *)input, NUM_SAMPLES,
                    buffer, buffer_size, &output_size));
        /* 半分以下にはなるはず */
        EXPECT_TRUE(output_size < (buffer_size / 2));

        /* デコード */
        EXPECT_EQ(
                MOI_APIRESULT_OK,
                MOIDecoder_DecodeWhole(
                    decoder, buffer, output_size,
                    decoded, NUM_CHANNELS, NUM_SAMPLES));

        /* ロスがあるのでRMSE基準でチェック */
        rms_error = 0.0;
        for (ch = 0; ch < NUM_CHANNELS; ch++) {
            for (smpl = 0; smpl < NUM_SAMPLES; smpl++) {
                double pcm1, pcm2, abs_error;
                pcm1 = (double)input[ch][smpl] / INT16_MAX;
                pcm2 = (double)decoded[ch][smpl] / INT16_MAX;
                abs_error = fabs(pcm1 - pcm2);
                rms_error += abs_error * abs_error;
            }
        }
        rms_error = sqrt(rms_error / (NUM_SAMPLES * NUM_CHANNELS));

        /* 経験的に0.05 */
        EXPECT_TRUE(rms_error < 5.0e-2);

        /* 領域開放 */
        MOIEncoder_Destroy(encoder);
        MOIDecoder_Destroy(decoder);
        free(buffer);
        for (ch = 0; ch < NUM_CHANNELS; ch++) {
            free(input[ch]);
            free(decoded[ch]);
        }
#undef NUM_CHANNELS
#undef NUM_SAMPLES
    }

    /* エンコードデコードテスト */
    {
        EXPECT_EQ(1, MOIEncoderTest_EncodeDecodeTest("unit_impulse_mono.wav", 4,  128, 5.0e-2));
        EXPECT_EQ(1, MOIEncoderTest_EncodeDecodeTest("unit_impulse_mono.wav", 4,  256, 5.0e-2));
        EXPECT_EQ(1, MOIEncoderTest_EncodeDecodeTest("unit_impulse_mono.wav", 4,  512, 5.0e-2));
        EXPECT_EQ(1, MOIEncoderTest_EncodeDecodeTest("unit_impulse_mono.wav", 4, 1024, 5.0e-2));
        EXPECT_EQ(1, MOIEncoderTest_EncodeDecodeTest("unit_impulse.wav",      4,  128, 5.0e-2));
        EXPECT_EQ(1, MOIEncoderTest_EncodeDecodeTest("unit_impulse.wav",      4,  256, 5.0e-2));
        EXPECT_EQ(1, MOIEncoderTest_EncodeDecodeTest("unit_impulse.wav",      4,  512, 5.0e-2));
        EXPECT_EQ(1, MOIEncoderTest_EncodeDecodeTest("unit_impulse.wav",      4, 1024, 5.0e-2));
        EXPECT_EQ(1, MOIEncoderTest_EncodeDecodeTest("sin300Hz_mono.wav",     4,  128, 5.0e-2));
        EXPECT_EQ(1, MOIEncoderTest_EncodeDecodeTest("sin300Hz_mono.wav",     4,  256, 5.0e-2));
        EXPECT_EQ(1, MOIEncoderTest_EncodeDecodeTest("sin300Hz_mono.wav",     4,  512, 5.0e-2));
        EXPECT_EQ(1, MOIEncoderTest_EncodeDecodeTest("sin300Hz_mono.wav",     4, 1024, 5.0e-2));
        EXPECT_EQ(1, MOIEncoderTest_EncodeDecodeTest("sin300Hz.wav",          4,  128, 5.0e-2));
        EXPECT_EQ(1, MOIEncoderTest_EncodeDecodeTest("sin300Hz.wav",          4,  256, 5.0e-2));
        EXPECT_EQ(1, MOIEncoderTest_EncodeDecodeTest("sin300Hz.wav",          4,  512, 5.0e-2));
        EXPECT_EQ(1, MOIEncoderTest_EncodeDecodeTest("sin300Hz.wav",          4, 1024, 5.0e-2));
    }

}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
