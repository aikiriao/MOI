#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>

#include "moi.h"
#include "wav.h"
#include "command_line_parser.h"

/* コマンドライン仕様 */
static struct CommandLineParserSpecification command_line_spec[] = {
    { 'e', "encode", "Encode mode (PCM wav -> IMA-ADPCM wav)",
        COMMAND_LINE_PARSER_FALSE, NULL, COMMAND_LINE_PARSER_FALSE },
    { 'd', "decode", "Decode mode (IMA-ADPCM wav -> PCM wav)",
        COMMAND_LINE_PARSER_FALSE, NULL, COMMAND_LINE_PARSER_FALSE },
    { 'c', "calculate-stats", "Calculate statistics mode",
        COMMAND_LINE_PARSER_FALSE, NULL, COMMAND_LINE_PARSER_FALSE },
    { 'B', "block-size", "Specify encode block size (default:1024)",
        COMMAND_LINE_PARSER_TRUE, "1024", COMMAND_LINE_PARSER_FALSE },
    { 'W', "search-beam-width", "Specify search beam width in encoding (default:4)",
        COMMAND_LINE_PARSER_TRUE, "4", COMMAND_LINE_PARSER_FALSE },
    { 'D', "search-depth", "Specify search depth in encoding (default:2)",
        COMMAND_LINE_PARSER_TRUE, "2", COMMAND_LINE_PARSER_FALSE },
    { 'h', "help", "Show command help message",
        COMMAND_LINE_PARSER_FALSE, NULL, COMMAND_LINE_PARSER_FALSE },
    { 'v', "version", "Show version information",
        COMMAND_LINE_PARSER_FALSE, NULL, COMMAND_LINE_PARSER_FALSE },
    { 0, NULL,  }
};

/* デコード処理 */
static int do_decode(const char *adpcm_filename, const char *decoded_filename)
{
    FILE *fp;
    struct stat fstat;
    uint8_t *buffer;
    uint32_t buffer_size;
    struct MOIDecoder *decoder;
    struct IMAADPCMWAVHeader header;
    struct WAVFile *wav;
    struct WAVFileFormat wavformat;
    int16_t *output[MOI_MAX_NUM_CHANNELS];
    uint32_t ch, smpl;
    MOIApiResult ret;

    /* ファイルオープン */
    fp = fopen(adpcm_filename, "rb");
    if (fp == NULL) {
        fprintf(stderr, "Failed to open %s. \n", adpcm_filename);
        return 1;
    }

    /* 入力ファイルのサイズ取得 / バッファ領域割り当て */
    stat(adpcm_filename, &fstat);
    buffer_size = (uint32_t)fstat.st_size;
    buffer = (uint8_t *)malloc(buffer_size);
    /* バッファ領域にデータをロード */
    fread(buffer, sizeof(uint8_t), buffer_size, fp);
    fclose(fp);

    /* デコーダ作成 */
    decoder = MOIDecoder_Create(NULL, 0);

    /* ヘッダ読み取り */
    if ((ret = MOIDecoder_DecodeHeader(buffer, buffer_size, &header))
            != MOI_APIRESULT_OK) {
        fprintf(stderr, "Failed to read header. API result: %d \n", ret);
        return 1;
    }

    /* 出力バッファ領域確保 */
    for (ch = 0; ch < header.num_channels; ch++) {
        output[ch] = malloc(sizeof(int16_t) * header.num_samples);
    }

    /* 全データをデコード */
    if ((ret = MOIDecoder_DecodeWhole(decoder, 
                    buffer, buffer_size, output, 
                    header.num_channels, header.num_samples)) != MOI_APIRESULT_OK) {
        fprintf(stderr, "Failed to decode. API result: %d \n", ret);
        return 1;
    }

    /* 出力ファイルを作成 */
    wavformat.data_format = WAV_DATA_FORMAT_PCM;
    wavformat.num_channels = header.num_channels;
    wavformat.sampling_rate = header.sampling_rate;
    wavformat.bits_per_sample = 16;
    wavformat.num_samples = header.num_samples;
    wav = WAV_Create(&wavformat);

    /* PCM書き出し */
    for (ch = 0; ch < header.num_channels; ch++) {
        for (smpl = 0; smpl < header.num_samples; smpl++) {
            WAVFile_PCM(wav, smpl, ch) = (output[ch][smpl] << 16);
        }
    }

    WAV_WriteToFile(decoded_filename, wav);

    MOIDecoder_Destroy(decoder);
    for (ch = 0; ch < header.num_channels; ch++) {
        free(output[ch]);
    }
    WAV_Destroy(wav);
    free(buffer);

    return 0;
}

/* エンコード処理 */
static int do_encode(
        const char *wav_file, const char *encoded_filename,
        uint16_t block_size, uint32_t search_beam_width, uint32_t search_depth)
{
    FILE *fp;
    struct WAVFile *wavfile;
    struct stat fstat;
    int16_t *input[MOI_MAX_NUM_CHANNELS];
    uint32_t ch, smpl, buffer_size, output_size;
    uint32_t num_channels, num_samples;
    uint8_t *buffer;
    struct MOIEncodeParameter enc_param;
    struct MOIEncoder *encoder;
    struct MOIEncoderConfig config;
    MOIApiResult api_result;

    /* 入力wav取得 */
    wavfile = WAV_CreateFromFile(wav_file);
    if (wavfile == NULL) {
        fprintf(stderr, "Failed to open %s. \n", wav_file);
        return 1;
    }

    num_channels = wavfile->format.num_channels;
    num_samples = wavfile->format.num_samples;

    /* 出力データの領域割当て */
    for (ch = 0; ch < num_channels; ch++) {
        input[ch] = malloc(sizeof(int16_t) * num_samples);
    }
    /* 入力wavと同じサイズの出力領域を確保（増えることはないと期待） */
    stat(wav_file, &fstat);
    buffer_size = (uint32_t)fstat.st_size;
    buffer = malloc(buffer_size);

    /* 16bit幅でデータ取得 */
    for (ch = 0; ch < num_channels; ch++) {
        for (smpl = 0; smpl < num_samples; smpl++) {
            input[ch][smpl] = (int16_t)(WAVFile_PCM(wavfile, smpl, ch) >> 16);
        }
    }

    /* ハンドル作成 */
    config.max_block_size = block_size;
    encoder = MOIEncoder_Create(&config, NULL, 0);

    /* エンコードパラメータをセット */
    enc_param.num_channels = (uint16_t)num_channels;
    enc_param.sampling_rate = wavfile->format.sampling_rate;
    enc_param.bits_per_sample = MOI_BITS_PER_SAMPLE;
    enc_param.block_size = block_size;
    enc_param.search_beam_width = search_beam_width;
    enc_param.search_depth = search_depth;
    if ((api_result = MOIEncoder_SetEncodeParameter(encoder, &enc_param))
            != MOI_APIRESULT_OK) {
        fprintf(stderr, "Failed to set encode parameter. API result:%d \n", api_result);
        return 1;
    }

    /* エンコード */
    if ((api_result = MOIEncoder_EncodeWhole(
                    encoder, (const int16_t *const *)input, num_samples,
                    buffer, buffer_size, &output_size)) != MOI_APIRESULT_OK) {
        fprintf(stderr, "Failed to encode. API result:%d \n", api_result);
        return 1;
    }

    /* ファイル書き出し */
    fp = fopen(encoded_filename, "wb");
    if (fp == NULL) {
        fprintf(stderr, "Failed to open output file %s \n", encoded_filename);
        return 1;
    }
    if (fwrite(buffer, sizeof(uint8_t), output_size, fp) < output_size) {
        fprintf(stderr, "Warning: failed to write encoded data \n");
        return 1;
    }
    fclose(fp);

    /* 領域開放 */
    MOIEncoder_Destroy(encoder);
    free(buffer);
    for (ch = 0; ch < num_channels; ch++) {
        free(input[ch]);
    }
    WAV_Destroy(wavfile);

    return 0;
}

/* 再構成処理 */
static int do_reconstruction_core(
        const char *wav_file, int16_t **decoded, const struct MOIEncodeParameter *parameter)
{
    struct WAVFile *wavfile;
    struct stat fstat;
    int16_t *pcmdata[MOI_MAX_NUM_CHANNELS];
    uint32_t ch, smpl, buffer_size, output_size;
    uint32_t num_channels, num_samples;
    uint8_t *buffer;
    struct MOIEncoder *encoder;
    struct MOIDecoder *decoder;
    struct MOIEncoderConfig enc_config;
    MOIApiResult api_result;

    /* 入力wav取得 */
    wavfile = WAV_CreateFromFile(wav_file);
    if (wavfile == NULL) {
        fprintf(stderr, "Failed to open %s. \n", wav_file);
        return 1;
    }

    num_channels = wavfile->format.num_channels;
    num_samples = wavfile->format.num_samples;

    /* 出力データの領域割当て */
    for (ch = 0; ch < num_channels; ch++) {
        pcmdata[ch] = malloc(sizeof(int16_t) * num_samples);
    }
    /* 入力wavと同じサイズの出力領域を確保（増えることはないと期待） */
    stat(wav_file, &fstat);
    buffer_size = (uint32_t)fstat.st_size;
    buffer = malloc(buffer_size);

    /* 16bit幅でデータ取得 */
    for (ch = 0; ch < num_channels; ch++) {
        for (smpl = 0; smpl < num_samples; smpl++) {
            pcmdata[ch][smpl] = (int16_t)(WAVFile_PCM(wavfile, smpl, ch) >> 16);
        }
    }

    /* ハンドル作成 */
    enc_config.max_block_size = parameter->block_size;
    encoder = MOIEncoder_Create(&enc_config, NULL, 0);
    decoder = MOIDecoder_Create(NULL, 0);

    /* エンコードパラメータをセット */
    if ((api_result = MOIEncoder_SetEncodeParameter(encoder, parameter))
            != MOI_APIRESULT_OK) {
        fprintf(stderr, "Failed to set encode parameter. API result:%d \n", api_result);
        return 1;
    }

    /* エンコード */
    if ((api_result = MOIEncoder_EncodeWhole(
                    encoder, (const int16_t *const *)pcmdata, num_samples,
                    buffer, buffer_size, &output_size)) != MOI_APIRESULT_OK) {
        fprintf(stderr, "Failed to encode. API result:%d \n", api_result);
        return 1;
    }

    /* そのままデコード */
    if ((api_result = MOIDecoder_DecodeWhole(decoder, 
                    buffer, output_size, decoded, num_channels, num_samples)) != MOI_APIRESULT_OK) {
        fprintf(stderr, "Failed to decode. API result: %d \n", api_result);
        return 1;
    }

    /* 領域開放 */
    MOIEncoder_Destroy(encoder);
    MOIDecoder_Destroy(decoder);
    free(buffer);
    for (ch = 0; ch < num_channels; ch++) {
        free(pcmdata[ch]);
    }
    WAV_Destroy(wavfile);

    return 0;
}

/* 統計計算処理 */
static int do_calculate_statistics(
        const char *wav_file,
        uint16_t block_size, uint32_t search_beam_width, uint32_t search_depth)
{
    struct WAVFile *wavfile;
    struct stat fstat;
    int16_t *pcmdata[MOI_MAX_NUM_CHANNELS];
    uint32_t ch, smpl, buffer_size;
    uint32_t num_channels, num_samples;
    uint8_t *buffer;
    struct MOIEncodeParameter enc_param;
    double rms_error;

    /* 入力wav取得 */
    wavfile = WAV_CreateFromFile(wav_file);
    if (wavfile == NULL) {
        fprintf(stderr, "Failed to open %s. \n", wav_file);
        return 1;
    }

    num_channels = wavfile->format.num_channels;
    num_samples = wavfile->format.num_samples;

    /* 出力データの領域割当て */
    for (ch = 0; ch < num_channels; ch++) {
        pcmdata[ch] = malloc(sizeof(int16_t) * num_samples);
    }
    /* 入力wavと同じサイズの出力領域を確保（増えることはないと期待） */
    stat(wav_file, &fstat);
    buffer_size = (uint32_t)fstat.st_size;
    buffer = malloc(buffer_size);

    /* エンコードパラメータをセット */
    enc_param.num_channels = (uint16_t)num_channels;
    enc_param.sampling_rate = wavfile->format.sampling_rate;
    enc_param.bits_per_sample = MOI_BITS_PER_SAMPLE;
    enc_param.block_size = block_size;
    enc_param.search_beam_width = search_beam_width;
    enc_param.search_depth = search_depth;

    /* 再構成処理 */
    if (do_reconstruction_core(wav_file, pcmdata, &enc_param) != 0) {
        return 1;
    }

    /* 残差（量子化誤差）計算 */
    rms_error = 0.0;
    for (ch = 0; ch < num_channels; ch++) {
        for (smpl = 0; smpl < num_samples; smpl++) {
            double pcm1, pcm2, abs_error;
            pcm1 = (double)WAVFile_PCM(wavfile, smpl, ch) / INT32_MAX;
            pcm2 = (double)pcmdata[ch][smpl] / INT16_MAX;
            abs_error = fabs(pcm1 - pcm2);
            rms_error += abs_error * abs_error;
        }
    }

    printf("RMSE:%f \n", sqrt(rms_error / (num_samples * num_channels)));

    /* 領域開放 */
    free(buffer);
    for (ch = 0; ch < num_channels; ch++) {
        free(pcmdata[ch]);
    }
    WAV_Destroy(wavfile);

    return 0;
}

/* 使用法の表示 */
static void print_usage(char** argv)
{
    printf("Usage: %s [options] INPUT_FILE_NAME OUTPUT_FILE_NAME \n", argv[0]);
}

/* バージョン情報の表示 */
static void print_version_info(void)
{
    printf("MOI -- My Optimized IMA-ADPCM encoder Version.%d \n", MOI_VERSION);
}

/* 数値オプションを取得 */
static int32_t check_get_numerical_option(char **argv, const char *option, uint32_t *result)
{
    char *e;
    uint32_t tmp;
    const char *lstr = CommandLineParser_GetArgumentString(command_line_spec, option);

    tmp = (uint32_t)strtol(lstr, &e, 10);
    if (*e != '\0') {
        fprintf(stderr, "%s: invalid %s. (irregular character found in %s at %s)\n", argv[0], option, lstr, e);
        return 1;
    }

    (*result) = tmp;
    return 0;
}

/* メインエントリ */
int main(int argc, char **argv)
{
    const char *filename_ptr[2] = { NULL, NULL };
    const char *input_file;
    const char *output_file;
    uint32_t search_beam_width, search_depth, block_size;

    /* 引数が足らない */
    if (argc == 1) {
        print_usage(argv);
        /* 初めて使った人が詰まらないようにヘルプの表示を促す */
        printf("Type `%s -h` to display command helps. \n", argv[0]);
        return 1;
    }

    /* コマンドライン解析 */
    if (CommandLineParser_ParseArguments(command_line_spec,
                argc, (const char* const *)argv, filename_ptr, sizeof(filename_ptr) / sizeof(filename_ptr[0]))
            != COMMAND_LINE_PARSER_RESULT_OK) {
        return 1;
    }

    /* ヘルプやバージョン情報の表示判定 */
    if (CommandLineParser_GetOptionAcquired(command_line_spec, "help") == COMMAND_LINE_PARSER_TRUE) {
        print_usage(argv);
        printf("options: \n");
        CommandLineParser_PrintDescription(command_line_spec);
        return 0;
    } else if (CommandLineParser_GetOptionAcquired(command_line_spec, "version") == COMMAND_LINE_PARSER_TRUE) {
        print_version_info();
        return 0;
    }

    /* 入力ファイル名の取得 */
    if ((input_file = filename_ptr[0]) == NULL) {
        fprintf(stderr, "%s: input file must be specified. \n", argv[0]);
        return 1;
    }

    /* 出力ファイル名の取得 */
    if ((CommandLineParser_GetOptionAcquired(command_line_spec, "decode") == COMMAND_LINE_PARSER_TRUE)
            || (CommandLineParser_GetOptionAcquired(command_line_spec, "encode") == COMMAND_LINE_PARSER_TRUE)) {
        /* 入力ファイル名の取得 */
        if ((output_file = filename_ptr[1]) == NULL) {
            fprintf(stderr, "%s: output file must be specified. \n", argv[0]);
            return 1;
        }
    }

    /* エンコードとデコードは同時に指定できない */
    if ((CommandLineParser_GetOptionAcquired(command_line_spec, "decode") == COMMAND_LINE_PARSER_TRUE)
            && (CommandLineParser_GetOptionAcquired(command_line_spec, "encode") == COMMAND_LINE_PARSER_TRUE)) {
        fprintf(stderr, "%s: encode and decode mode cannot specify simultaneously. \n", argv[0]);
        return 1;
    }

    /* ブロックサイズを取得 */
    if (check_get_numerical_option(argv, "block-size", &block_size) != 0) {
        return 1;
    }
    if ((block_size == 0) || (block_size > UINT16_MAX)) {
        fprintf(stderr, "%s: block size(=%d) is out of range (%d,%d]. \n",
                argv[0], block_size, 0, UINT16_MAX);
        return 1;
    }

    /* 探索ビーム幅を取得 */
    if (check_get_numerical_option(argv, "search-beam-width", &search_beam_width) != 0) {
        return 1;
    }
    if ((search_beam_width == 0) || (search_beam_width > MOI_MAX_SEARCH_BEAM_WIDTH)) {
        fprintf(stderr, "%s: search beam width(=%d) is out of range (%d,%d]. \n",
                argv[0], search_beam_width, 0, MOI_MAX_SEARCH_BEAM_WIDTH);
        return 1;
    }

    /* 探索深さを取得 */
    if (check_get_numerical_option(argv, "search-depth", &search_depth) != 0) {
        return 1;
    }
    if ((search_depth == 0) || (search_depth > MOI_MAX_SEARCH_DEPTH)) {
        fprintf(stderr, "%s: search depth(=%d) is out of range (%d,%d]. \n",
                argv[0], search_depth, 0, MOI_MAX_SEARCH_DEPTH);
        return 1;
    }

    if (CommandLineParser_GetOptionAcquired(command_line_spec, "decode") == COMMAND_LINE_PARSER_TRUE) {
        /* 一括デコード実行 */
        if (do_decode(input_file, output_file) != 0) {
            fprintf(stderr, "%s: failed to decode %s. \n", argv[0], input_file);
            return 1;
        }
    } else if (CommandLineParser_GetOptionAcquired(command_line_spec, "encode") == COMMAND_LINE_PARSER_TRUE) {
        /* 一括エンコード実行 */
        if (do_encode(input_file, output_file, (uint16_t)block_size, search_beam_width, search_depth) != 0) {
            fprintf(stderr, "%s: failed to encode %s. \n", argv[0], input_file);
            return 1;
        }
    } else if (CommandLineParser_GetOptionAcquired(command_line_spec, "calculate-stats") == COMMAND_LINE_PARSER_TRUE) {
        /* 統計出力処理実行 */
        if (do_calculate_statistics(input_file, (uint16_t)block_size, search_beam_width, search_depth) != 0) {
            fprintf(stderr, "%s: failed to calculate statistics %s. \n", argv[0], input_file);
            return 1;
        }
    } else {
        fprintf(stderr, "%s: mode option must be specified. \n", argv[0]);
        return 1;
    }

    return 0;
}
