#ifndef TYPES_H
#define TYPES_H

/**
 * @brief Architecture-independent size type
 */
typedef __SIZE_TYPE__ size_t;

/**
 * @brief Unsigned 8-bit integer
 */
typedef unsigned char      uint8_t;

/**
 * @brief Unsigned 16-bit integer
 */
typedef unsigned short     uint16_t;

/**
 * @brief Unsigned 32-bit integer
 */
typedef unsigned int       uint32_t;

/**
 * @brief Unsigned 64-bit integer
 */
typedef unsigned long long uint64_t;

/**
 * @brief Signed 8-bit integer
 */
typedef signed char        int8_t;

/**
 * @brief Signed 16-bit integer
 */
typedef short              int16_t;

/**
 * @brief Signed 32-bit integer
 */
typedef int                int32_t;

/**
 * @brief Signed 64-bit integer
 */
typedef long long          int64_t;

/*
 * Boolean type.
 *
 * C23 promoted bool, true and false to keywords, so defining them is no longer
 * merely redundant - "typedef _Bool bool" is a syntax error there, and GCC 15
 * defaults to C23. The kernel build pins no -std, so it inherits whatever the
 * host compiler defaults to and this file decides whether the tree still
 * compiles at all.
 *
 * Defined only for the standards that lack them.
 */
#if !defined(__cplusplus) && (!defined(__STDC_VERSION__) || __STDC_VERSION__ < 202311L)

/**
 * @brief Boolean type
 */
typedef _Bool bool;

/**
 * @brief Boolean true value
 */
#define true  1

/**
 * @brief Boolean false value
 */
#define false 0

#endif

/**
 * @brief Null pointer definition
 */
#define NULL ((void *)0)

#endif // TYPES_H