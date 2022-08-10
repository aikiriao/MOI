#ifndef MOIINTERNAL_H_INCLUDED
#define MOIINTERNAL_H_INCLUDED

/* アラインメント */
#define MOI_ALIGNMENT 16

/* nの倍数への切り上げ */
#define MOI_ROUND_UP(val, n) ((((val) + ((n) - 1)) / (n)) * (n))

/* 最大値を選択 */
#define MOI_MAX_VAL(a, b) (((a) > (b)) ? (a) : (b))

/* 最小値を選択 */
#define MOI_MIN_VAL(a, b) (((a) < (b)) ? (a) : (b))

/* min以上max未満に制限 */
#define MOI_INNER_VAL(val, min, max) \
    MOI_MAX_VAL(min, MOI_MIN_VAL(max, val))

/* 静的アサート */
#define MOI_STATIC_ASSERT(expr) { void static_assertion_failed(char dummy[(expr) ? 1 : -1]); }

/* アサートマクロ */
#ifdef DEBUG
#include <assert.h>
#define MOI_ASSERT(condition) assert(condition)
#else
#define MOI_ASSERT(condition) (void)(condition)
#endif

/* 内部エラー型 */
typedef enum {
    MOI_ERROR_OK = 0,              /* OK */
    MOI_ERROR_NG,                  /* 分類不能な失敗 */
    MOI_ERROR_INVALID_ARGUMENT,    /* 不正な引数 */
    MOI_ERROR_INVALID_FORMAT,      /* 不正なフォーマット       */
    MOI_ERROR_INSUFFICIENT_BUFFER, /* バッファサイズが足りない */
    MOI_ERROR_INSUFFICIENT_DATA    /* データサイズが足りない   */
} MOIError;

/* インデックス変動テーブル */
static const int8_t MOI_index_table[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
};

/* ステップサイズ量子化テーブル */
static const uint16_t MOI_stepsize_table[89] = {
    7,     8,     9,    10,    11,    12,    13,    14,
    16,    17,    19,    21,    23,    25,    28,    31,
    34,    37,    41,    45,    50,    55,    60,    66,
    73,    80,    88,    97,   107,   118,   130,   143,
    157,   173,   190,   209,   230,   253,   279,   307,
    337,   371,   408,   449,   494,   544,   598,   658,
    724,   796,   876,   963,  1060,  1166,  1282,  1411,
    1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,
    3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,
    7132,  7845,  8630,  9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767
};

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* MOIINTERNAL_H_INCLUDED */
