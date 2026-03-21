/**
 * mr_types.h - MRP VM Type Definitions
 * 
 * Updated to use portable fixed-width types from mr_stdint.h
 * This ensures consistent type sizes across 32-bit and 64-bit platforms.
 */

#ifndef _MR_TYPES_H
#define _MR_TYPES_H

/* Include portable type definitions first */
#include "mr_stdint.h"

/*============================================================================
 * Additional type definitions not in mr_stdint.h
 *============================================================================*/

typedef char *          PSTR;
typedef const char *    PCSTR;

typedef unsigned int    UINT;
typedef unsigned long   DWORD;
typedef unsigned char   BYTE;
typedef DWORD *         DWORD_PTR;

typedef int BOOL;

/*============================================================================
 * Platform-specific 64-bit types (for compatibility)
 *============================================================================*/

#ifdef _WIN32
    typedef unsigned __int64   MR_UINT64;
    typedef __int64            MR_INT64;
#else
    typedef unsigned long long MR_UINT64;
    typedef long long          MR_INT64;
#endif

/*============================================================================
 * Helper macros
 *============================================================================*/

#define LOBYTE(w) ((BYTE)((DWORD_PTR)(w) & 0xff))

/*============================================================================
 * MRP Return codes
 *============================================================================*/

#define MR_SUCCESS  0    // 成功
#define MR_FAILED   -1   // 失败
#define MR_IGNORE   1    // 不关心
#define MR_WAITING  2    // 异步(非阻塞)模式

#endif /* _MR_TYPES_H */
