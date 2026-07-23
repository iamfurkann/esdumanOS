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

/**
 * @brief Null pointer definition
 */
#define NULL ((void *)0)

#endif // TYPES_H