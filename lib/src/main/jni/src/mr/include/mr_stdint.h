/**
 * mr_stdint.h - Portable type definitions for MRP VM
 * 
 * This header provides fixed-width integer types that are consistent
 * across both 32-bit and 64-bit platforms.
 * 
 * Key design decisions:
 * - uint32/int32 are ALWAYS 32-bit (use uint32_t/int32_t)
 * - uintptr/intptr are pointer-sized (use uintptr_t/intptr_t)
 * - All types use C99 stdint.h when available
 */

#ifndef MR_STDINT_H
#define MR_STDINT_H

/* Use C99 standard integer types */
#include <stdint.h>
#include <stddef.h>

/*============================================================================
 * Fixed-width integer types (guaranteed size on all platforms)
 *============================================================================*/

/* 8-bit types */
typedef uint8_t     mr_uint8;
typedef int8_t      mr_int8;

/* 16-bit types */
typedef uint16_t    mr_uint16;
typedef int16_t     mr_int16;

/* 32-bit types - ALWAYS 32 bits, even on 64-bit platforms */
typedef uint32_t    mr_uint32;
typedef int32_t     mr_int32;

/* 64-bit types */
typedef uint64_t    mr_uint64;
typedef int64_t     mr_int64;

/*============================================================================
 * Legacy type aliases for backward compatibility
 * These are the types used throughout the MRP codebase
 *============================================================================*/

/* Primary integer types */
#ifndef MR_C_NUMBER_TYPE
#define MR_C_NUMBER_TYPE

/* Override the problematic type definitions */
typedef mr_uint8    uint8;
typedef mr_int8     int8;
typedef mr_uint16   uint16;
typedef mr_int16    int16;
typedef mr_uint32   uint32;
typedef mr_int32    int32;
typedef mr_uint64   uint64;
typedef mr_int64    int64;

#endif /* MR_C_NUMBER_TYPE */

/*============================================================================
 * Pointer-sized types for safe pointer arithmetic
 *============================================================================*/

/* Types that can safely hold a pointer value */
typedef uintptr_t   mr_uintptr;
typedef intptr_t    mr_intptr;

/*============================================================================
 * Utility macros for pointer operations
 *============================================================================*/

/* Safe pointer alignment macro - works on both 32-bit and 64-bit */
#define MR_ALIGN_UP(ptr, alignment) \
    ((void*)(((mr_uintptr)(ptr) + ((alignment) - 1)) & ~((mr_uintptr)(alignment) - 1)))

#define MR_ALIGN_DOWN(ptr, alignment) \
    ((void*)((mr_uintptr)(ptr) & ~((mr_uintptr)(alignment) - 1)))

/* Safe pointer to integer conversion for hashing */
#define MR_PTR_TO_INT(p)    ((mr_uintptr)(p))

/*============================================================================
 * Platform detection
 *============================================================================*/

#if defined(__LP64__) || defined(_LP64) || defined(__x86_64__) || \
    defined(__aarch64__) || defined(_WIN64)
    #define MR_64BIT_PLATFORM 1
#else
    #define MR_64BIT_PLATFORM 0
#endif

/*============================================================================
 * Compatibility macros
 *============================================================================*/

/* Ensure NULL is properly defined */
#ifndef NULL
    #ifdef __cplusplus
        #define NULL 0
    #else
        #define NULL ((void*)0)
    #endif
#endif

/* Boolean type */
#ifndef TRUE
    #define TRUE 1
#endif

#ifndef FALSE
    #define FALSE 0
#endif

#endif /* MR_STDINT_H */
