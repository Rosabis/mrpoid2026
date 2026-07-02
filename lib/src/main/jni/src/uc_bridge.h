#ifndef __UC_BRIDGE_H__
#define __UC_BRIDGE_H__

#include <stdint.h>
#include <stdbool.h>
#include <unicorn/unicorn.h>

#include "rbtree.h"

typedef struct BridgeMap BridgeMap;
typedef void (*BridgeCB)(struct BridgeMap *o, uc_engine *uc);
typedef void (*BridgeInit)(struct BridgeMap *o, uc_engine *uc, uint32_t addr);

typedef enum BridgeMapType {
    MAP_DATA,
    MAP_FUNC,
} BridgeMapType;

typedef struct BridgeMap {
    uint32_t pos;
    BridgeMapType type;
    char *name;
    BridgeInit initFn;
    BridgeCB fn;
    uint32_t extraData;
} BridgeMap;

#define BRIDGE_FUNC_MAP(offset, mapType, field, init, func, extra) \
    { offset, mapType, #field, init, func, extra }

#ifndef countof
#define countof(x) (sizeof(x) / sizeof((x)[0]))
#endif

typedef int32_t int32;
typedef uint32_t uint32;
typedef int16_t int16;
typedef uint16_t uint16;
typedef uint8_t uint8;

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} uc_mr_datetime;

enum {
    DSM_INIT = -100,
    UC_MR_START_DSM,
    UC_MR_PAUSEAPP,
    UC_MR_RESUMEAPP,
    UC_MR_TIMER,
    UC_MR_EVENT
};

typedef enum {
    FLAG_USE_UTF8_FS = 1 << 0,
    FLAG_USE_UTF8_EDIT = 1 << 1
} dsm_flag;

#define VMRP_VER 20210701

typedef struct {
    int32_t code;
    int32_t p0;
    int32_t p1;
} uc_event_t;

typedef struct {
    uint32_t filename;
    uint32_t ext;
    uint32_t entry;
} uc_start_t;

typedef struct {
    uint32_t start_of_ER_RW;
    uint32_t ER_RW_Length;
    int32_t ext_type;
    uint32_t mrc_extChunk;
    int32_t stack;
} uc_mr_c_function_P_t;

uc_err uc_bridge_init(uc_engine *uc);
uc_err uc_bridge_ext_init(uc_engine *uc);

int32_t uc_bridge_dsm_init(uc_engine *uc, int screenW, int screenH);
int32_t uc_bridge_dsm_mr_start_dsm(uc_engine *uc, char *filename, char *ext, char *entry);
int32_t uc_bridge_dsm_mr_pauseApp(uc_engine *uc);
int32_t uc_bridge_dsm_mr_resumeApp(uc_engine *uc);
int32_t uc_bridge_dsm_mr_timer(uc_engine *uc);
int32_t uc_bridge_dsm_mr_event(uc_engine *uc, int32_t code, int32_t p0, int32_t p1);
int32_t uc_bridge_dsm_network_cb(uc_engine *uc, uint32_t addr, int32_t p0, uint32_t p1);

#endif
