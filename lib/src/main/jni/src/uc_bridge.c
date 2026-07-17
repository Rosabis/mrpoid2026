#include "uc_bridge.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>
#include <android/log.h>

#include "uc_vm.h"
#include "uc_memory.h"
#include "uc_fileLib.h"
#include "emulator.h"
#include "dsm.h"

#undef LOG_TAG
#undef LOGI
#undef LOGE
#undef LOGW
#define LOG_TAG "uc_bridge"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

#define SET_RET_V(ret)                        \
    {                                         \
        uint32_t _v = (uint32_t)(ret);        \
        uc_reg_write(uc, UC_ARM_REG_R0, &_v); \
    }

/* DSM_REQUIRE_FUNCS layout - must match vmrp's dsm.h exactly */
typedef struct {
    uint32_t test;            /* 0x00 */
    uint32_t log;             /* 0x04 */
    uint32_t exit_func;       /* 0x08 */
    uint32_t srand_func;      /* 0x0c */
    uint32_t rand_func;       /* 0x10 */
    uint32_t mem_get;         /* 0x14 */
    uint32_t mem_free;        /* 0x18 */
    uint32_t timerStart;      /* 0x1c */
    uint32_t timerStop;       /* 0x20 */
    uint32_t get_uptime_ms;   /* 0x24 */
    uint32_t getDatetime;     /* 0x28 */
    uint32_t sleep_func;      /* 0x2c */
    uint32_t open;            /* 0x30 */
    uint32_t close_func;      /* 0x34 */
    uint32_t read_func;       /* 0x38 */
    uint32_t write_func;      /* 0x3c */
    uint32_t seek;            /* 0x40 */
    uint32_t info;            /* 0x44 */
    uint32_t remove_func;     /* 0x48 */
    uint32_t rename_func;     /* 0x4c */
    uint32_t mkDir;           /* 0x50 */
    uint32_t rmDir;           /* 0x54 */
    uint32_t opendir;         /* 0x58 */
    uint32_t readdir_func;    /* 0x5c */
    uint32_t closedir_func;   /* 0x60 */
    uint32_t getLen;          /* 0x64 */
    uint32_t drawBitmap;      /* 0x68 */
    uint32_t getHostByName;   /* 0x6c */
    uint32_t initNetwork;     /* 0x70 */
    uint32_t mr_closeNetwork; /* 0x74 */
    uint32_t mr_socket;       /* 0x78 */
    uint32_t mr_connect;      /* 0x7c */
    uint32_t mr_getSocketState; /* 0x80 */
    uint32_t mr_closeSocket;  /* 0x84 */
    uint32_t mr_recv;         /* 0x88 */
    uint32_t mr_send;         /* 0x8c */
    uint32_t mr_recvfrom;     /* 0x90 */
    uint32_t mr_sendto;       /* 0x94 */
    uint32_t mr_startShake;   /* 0x98 */
    uint32_t mr_stopShake;    /* 0x9c */
    uint32_t mr_playSound;    /* 0xa0 */
    uint32_t mr_stopSound;    /* 0xa4 */
    uint32_t mr_dialogCreate; /* 0xa8 */
    uint32_t mr_dialogRelease; /* 0xac */
    uint32_t mr_dialogRefresh; /* 0xb0 */
    uint32_t mr_textCreate;   /* 0xb4 */
    uint32_t mr_textRelease;  /* 0xb8 */
    uint32_t mr_textRefresh;  /* 0xbc */
    uint32_t mr_editCreate;   /* 0xc0 */
    uint32_t mr_editRelease;  /* 0xc4 */
    uint32_t mr_editGetText;  /* 0xc8 */
    int32_t flags;            /* 0xcc */
} UC_DSM_REQUIRE_FUNCS;

static void *uc_mr_table_ptr;
static uc_mr_c_function_P_t *mr_c_function_P;
static void *dsm_require_funcs;
static uc_event_t *mr_c_event;
static uc_event_t *dsm_event;
static uc_start_t *mr_start_dsm_param;
static uint32_t mr_extHelper_addr;

static int g_screenW = 240;
static int g_screenH = 320;

static struct timeval g_startTime;

/* 不可在每次 uc_bridge_init 里重复 pthread_mutex_init（未 destroy 时行为未定义） */
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static struct rb_root root = RB_ROOT;

static void uc_runCode(uc_engine *uc, uint32_t startAddr, uint32_t stopAddr, bool isThumb);

/*
 * Unicorn 2：宿主直接改 guest 内存不会走 QEMU SMC，必须手动失效 TB。
 * 否则 pause→dump0→resume（短信/支付插件）会执行旧翻译块，表现为黑屏/卡「请稍等」。
 * mrpoid 自带 unicorn.h 里 uc_ctl_flush_tlb 实际是 UC_CTL_TB_FLUSH。
 */
#if UC_API_MAJOR >= 2
#ifndef uc_ctl_flush_tb
#define uc_ctl_flush_tb uc_ctl_flush_tlb
#endif

static void unicorn2_refresh_pc(uc_engine *uc) {
    uint32_t pc = 0, cpsr = 0;
    uc_reg_read(uc, UC_ARM_REG_PC, &pc);
    uc_reg_read(uc, UC_ARM_REG_CPSR, &cpsr);
    if (cpsr & (1u << 5)) {
        pc |= 1u;
    } else {
        pc &= ~1u;
    }
    uc_reg_write(uc, UC_ARM_REG_PC, &pc);
}

static void unicorn2_invalidate_range(uc_engine *uc, uint32_t addr, uint32_t len) {
    if (len == 0) {
        return;
    }
    uc_err err = uc_ctl_remove_cache(uc, (uint64_t)addr, (uint64_t)addr + (uint64_t)len);
    if (err) {
        uc_ctl_flush_tb(uc);
    }
}

static void unicorn2_flush_all(uc_engine *uc) {
    uc_ctl_flush_tb(uc);
    unicorn2_refresh_pc(uc);
}
#else
static void unicorn2_invalidate_range(uc_engine *uc, uint32_t addr, uint32_t len) {
    (void)uc;
    (void)addr;
    (void)len;
}
static void unicorn2_flush_all(uc_engine *uc) {
    (void)uc;
}
#endif

static void try_flush_tb_for_cache_sync(uc_engine *uc, const char *str) {
    unsigned int addr = 0;
    int len = 0;
    /* guest mythroad: LOGW("mr_cacheSync(0x%p, %d)", addr, len) */
    if (sscanf(str, "[WARN]mr_cacheSync(0x%x, %d)", &addr, &len) != 2 || len <= 0) {
        return;
    }
#if UC_API_MAJOR >= 2
    uc_err err = uc_ctl_remove_cache(uc, (uint64_t)addr, (uint64_t)addr + (uint64_t)len);
    if (err) {
        LOGW("mr_cacheSync remove_cache(0x%X,+%d): %u (%s), flush_tb",
             addr, len, err, uc_strerror(err));
        uc_ctl_flush_tb(uc);
    } else {
        LOGI("mr_cacheSync remove_cache(0x%X,+%d) ok", addr, len);
    }
    unicorn2_refresh_pc(uc);
#else
    (void)uc;
#endif
}

static uint32_t getArg(uc_engine *uc, uint32_t n) {
    uint32_t v;
    if (n <= 3) {
        uc_reg_read(uc, UC_ARM_REG_R0 + n, &v);
        return v;
    }
    uint32_t sp;
    uc_reg_read(uc, UC_ARM_REG_SP, &sp);
    uc_mem_read(uc, sp + (n - 4) * 4, &v, 4);
    return v;
}

static uint32_t uc_copyStrToMrp(const char *str) {
    if (!str) return 0;
    uint32_t len = strlen(str) + 1;
    void *p = uc_my_mallocExt(len);
    if (p) {
        memcpy(p, str, len);
        return uc_toMrpMemAddr(p);
    }
    return 0;
}

/* ===== mr_table bridge callbacks ===== */

static void br__mr_c_function_new(BridgeMap *o, uc_engine *uc) {
    uint32_t p_f, p_len;
    uc_reg_read(uc, UC_ARM_REG_R0, &p_f);
    uc_reg_read(uc, UC_ARM_REG_R1, &p_len);
    LOGI("_mr_c_function_new(0x%X, %u)", p_f, p_len);

    /* 与 mythroad _mr_c_function_new 一致：先释放旧块再分配；否则嵌套 mr_start 泄漏且返回父 MRP 后状态错乱 */
    if (mr_c_function_P) {
        uc_my_freeExt(mr_c_function_P);
        mr_c_function_P = NULL;
    }

    mr_extHelper_addr = p_f;
    mr_c_function_P = uc_my_mallocExt(p_len);
    if (!mr_c_function_P) {
        mr_extHelper_addr = 0;
        LOGE("_mr_c_function_new: alloc %u failed", p_len);
        SET_RET_V(MR_FAILED);
        return;
    }
    memset(mr_c_function_P, 0, p_len);

    uint32_t v = uc_toMrpMemAddr(mr_c_function_P);
    uc_mem_write(uc, UC_CODE_ADDRESS + 4, &v, 4);
    SET_RET_V(MR_SUCCESS);
}

static void br_mr_malloc(BridgeMap *o, uc_engine *uc) {
    uint32_t len;
    uc_reg_read(uc, UC_ARM_REG_R0, &len);
    void *p = uc_my_mallocExt(len);
    SET_RET_V(p ? uc_toMrpMemAddr(p) : 0);
}

static void br_mr_free(BridgeMap *o, uc_engine *uc) {
    uint32_t p, len;
    uc_reg_read(uc, UC_ARM_REG_R0, &p);
    uc_reg_read(uc, UC_ARM_REG_R1, &len);
    if (p) uc_my_freeExt(uc_getMrpMemPtr(p));
}

static void br_memcpy(BridgeMap *o, uc_engine *uc) {
    uint32_t dst, src, n;
    uc_reg_read(uc, UC_ARM_REG_R0, &dst);
    uc_reg_read(uc, UC_ARM_REG_R1, &src);
    uc_reg_read(uc, UC_ARM_REG_R2, &n);
    memcpy(uc_getMrpMemPtr(dst), uc_getMrpMemPtr(src), n);
    SET_RET_V(dst);
}

static void br_memset(BridgeMap *o, uc_engine *uc) {
    uint32_t dst, value, n;
    uc_reg_read(uc, UC_ARM_REG_R0, &dst);
    uc_reg_read(uc, UC_ARM_REG_R1, &value);
    uc_reg_read(uc, UC_ARM_REG_R2, &n);
    memset(uc_getMrpMemPtr(dst), value, n);
    SET_RET_V(dst);
}

/* ===== dsm_require_funcs bridge callbacks (platform functions) ===== */

static void br_test(BridgeMap *o, uc_engine *uc) {
    /* noop */
}

static void br_log(BridgeMap *o, uc_engine *uc) {
    uint32_t msg;
    uc_reg_read(uc, UC_ARM_REG_R0, &msg);
    char *str = (char *)uc_getMrpMemPtr(msg);
    try_flush_tb_for_cache_sync(uc, str);
    if (strstr(str, "appResume") != NULL) {
        /* pause→dump0→resume：上一个 MRP 代码可能已被宿主写回；全量刷 TB */
        unicorn2_flush_all(uc);
        LOGI("appResume: flushed TB for Unicorn2");
    }
    __android_log_print(ANDROID_LOG_INFO, "mythroad", "%s", str);
}

static void br_exit(BridgeMap *o, uc_engine *uc) {
    LOGI("mythroad exit called -> finish activity");
    emu_finish();
}

static void br_srand(BridgeMap *o, uc_engine *uc) {
    uint32_t seed;
    uc_reg_read(uc, UC_ARM_REG_R0, &seed);
    srand(seed);
}

static void br_rand(BridgeMap *o, uc_engine *uc) {
    SET_RET_V(rand());
}

static void br_mem_get(BridgeMap *o, uc_engine *uc) {
    uint32_t mem_base_ptr, mem_len_ptr;
    uc_reg_read(uc, UC_ARM_REG_R0, &mem_base_ptr);
    uc_reg_read(uc, UC_ARM_REG_R1, &mem_len_ptr);

    uint32_t len = 1024 * 1024 * 4;
    void *buf = uc_my_mallocExt(len);
    uint32_t bufAddr = uc_toMrpMemAddr(buf);

    LOGI("br_mem_get base=0x%X len=%d", bufAddr, len);
    uc_mem_write(uc, mem_base_ptr, &bufAddr, 4);
    uc_mem_write(uc, mem_len_ptr, &len, 4);
    SET_RET_V(MR_SUCCESS);
}

static void br_mem_free(BridgeMap *o, uc_engine *uc) {
    uint32_t mem, mem_len;
    uc_reg_read(uc, UC_ARM_REG_R0, &mem);
    uc_reg_read(uc, UC_ARM_REG_R1, &mem_len);
    if (mem) uc_my_freeExt(uc_getMrpMemPtr(mem));
    SET_RET_V(MR_SUCCESS);
}

static void br_timerStart(BridgeMap *o, uc_engine *uc) {
    uint32_t t;
    uc_reg_read(uc, UC_ARM_REG_R0, &t);
    LOGI("br_timerStart(%d)", t);
    SET_RET_V(emu_timerStart((uint16_t)t));
}

static void br_timerStop(BridgeMap *o, uc_engine *uc) {
    LOGI("br_timerStop()");
    SET_RET_V(emu_timerStop());
}

static uint64_t uptime_base_ms;
static void br_get_uptime_ms_init(BridgeMap *o, uc_engine *uc, uint32_t addr) {
    struct timeval t;
    gettimeofday(&t, NULL);
    uptime_base_ms = (uint64_t)t.tv_sec * 1000 + t.tv_usec / 1000;
    uc_mem_write(uc, addr, &addr, 4);
}

static void br_get_uptime_ms(BridgeMap *o, uc_engine *uc) {
    struct timeval t;
    gettimeofday(&t, NULL);
    uint64_t now = (uint64_t)t.tv_sec * 1000 + t.tv_usec / 1000;
    SET_RET_V((uint32_t)(now - uptime_base_ms));
}

static void br_getDatetime(BridgeMap *o, uc_engine *uc) {
    uint32_t dt_addr;
    uc_reg_read(uc, UC_ARM_REG_R0, &dt_addr);

    time_t now;
    struct tm *tm;
    time(&now);
    tm = localtime(&now);

    uc_mr_datetime dt;
    dt.year = tm->tm_year + 1900;
    dt.month = tm->tm_mon + 1;
    dt.day = tm->tm_mday;
    dt.hour = tm->tm_hour;
    dt.minute = tm->tm_min;
    dt.second = tm->tm_sec;

    memcpy(uc_getMrpMemPtr(dt_addr), &dt, sizeof(dt));
    SET_RET_V(MR_SUCCESS);
}

static void br_sleep(BridgeMap *o, uc_engine *uc) {
    uint32_t ms;
    uc_reg_read(uc, UC_ARM_REG_R0, &ms);
    usleep(ms * 1000);
    SET_RET_V(MR_SUCCESS);
}

static void br_mr_open(BridgeMap *o, uc_engine *uc) {
    uint32_t filename_addr, mode;
    uc_reg_read(uc, UC_ARM_REG_R0, &filename_addr);
    uc_reg_read(uc, UC_ARM_REG_R1, &mode);
    char *fn = (char *)uc_getMrpMemPtr(filename_addr);
    int32_t ret = uc_file_open(fn, mode);
    LOGI("br_open(%s, %d) = %d", fn, mode, ret);
    SET_RET_V(ret);
}

static void br_mr_close(BridgeMap *o, uc_engine *uc) {
    uint32_t f;
    uc_reg_read(uc, UC_ARM_REG_R0, &f);
    SET_RET_V(uc_file_close(f));
}

static void br_mr_read(BridgeMap *o, uc_engine *uc) {
    uint32_t f, p, l;
    uc_reg_read(uc, UC_ARM_REG_R0, &f);
    uc_reg_read(uc, UC_ARM_REG_R1, &p);
    uc_reg_read(uc, UC_ARM_REG_R2, &l);
    /* dump0/插件恢复：宿主写 guest 内存，UC2 必须失效对应 TB */
    int32_t ret = uc_file_read(f, uc_getMrpMemPtr(p), l);
    if (ret > 0) {
        unicorn2_invalidate_range(uc, p, (uint32_t)ret);
    }
    SET_RET_V(ret);
}

static void br_mr_write(BridgeMap *o, uc_engine *uc) {
    uint32_t f, p, l;
    uc_reg_read(uc, UC_ARM_REG_R0, &f);
    uc_reg_read(uc, UC_ARM_REG_R1, &p);
    uc_reg_read(uc, UC_ARM_REG_R2, &l);
    SET_RET_V(uc_file_write(f, uc_getMrpMemPtr(p), l));
}

static void br_mr_seek(BridgeMap *o, uc_engine *uc) {
    uint32_t f, pos, method;
    uc_reg_read(uc, UC_ARM_REG_R0, &f);
    uc_reg_read(uc, UC_ARM_REG_R1, &pos);
    uc_reg_read(uc, UC_ARM_REG_R2, &method);
    SET_RET_V(uc_file_seek(f, (int32_t)pos, method));
}

static void br_mr_info(BridgeMap *o, uc_engine *uc) {
    uint32_t fn;
    uc_reg_read(uc, UC_ARM_REG_R0, &fn);
    SET_RET_V(uc_file_info((const char *)uc_getMrpMemPtr(fn)));
}

static void br_mr_remove(BridgeMap *o, uc_engine *uc) {
    uint32_t fn;
    uc_reg_read(uc, UC_ARM_REG_R0, &fn);
    SET_RET_V(uc_file_remove((const char *)uc_getMrpMemPtr(fn)));
}

static void br_mr_rename(BridgeMap *o, uc_engine *uc) {
    uint32_t old_name, new_name;
    uc_reg_read(uc, UC_ARM_REG_R0, &old_name);
    uc_reg_read(uc, UC_ARM_REG_R1, &new_name);
    SET_RET_V(uc_file_rename((const char *)uc_getMrpMemPtr(old_name),
                             (const char *)uc_getMrpMemPtr(new_name)));
}

static void br_mr_mkDir(BridgeMap *o, uc_engine *uc) {
    uint32_t name;
    uc_reg_read(uc, UC_ARM_REG_R0, &name);
    SET_RET_V(uc_file_mkDir((const char *)uc_getMrpMemPtr(name)));
}

static void br_mr_rmDir(BridgeMap *o, uc_engine *uc) {
    uint32_t name;
    uc_reg_read(uc, UC_ARM_REG_R0, &name);
    SET_RET_V(uc_file_rmDir((const char *)uc_getMrpMemPtr(name)));
}

static void br_mr_opendir(BridgeMap *o, uc_engine *uc) {
    uint32_t name;
    uc_reg_read(uc, UC_ARM_REG_R0, &name);
    SET_RET_V(uc_file_opendir((const char *)uc_getMrpMemPtr(name)));
}

#define READDIR_SHARED_MEM_SIZE 128
static char *readdirSharedMem;
static void br_readdir_init(BridgeMap *o, uc_engine *uc, uint32_t addr) {
    readdirSharedMem = (char *)uc_my_mallocExt(READDIR_SHARED_MEM_SIZE);
    memset(readdirSharedMem, 0, READDIR_SHARED_MEM_SIZE);
    uc_mem_write(uc, addr, &addr, 4);
}

static void br_readdir(BridgeMap *o, uc_engine *uc) {
    uint32_t f;
    uc_reg_read(uc, UC_ARM_REG_R0, &f);
    char *r = uc_file_readdir(f);
    if (r != NULL) {
        strncpy(readdirSharedMem, r, READDIR_SHARED_MEM_SIZE - 1);
        SET_RET_V(uc_toMrpMemAddr(readdirSharedMem));
    } else {
        SET_RET_V(0);
    }
}

static void br_closedir(BridgeMap *o, uc_engine *uc) {
    uint32_t f;
    uc_reg_read(uc, UC_ARM_REG_R0, &f);
    SET_RET_V(uc_file_closedir(f));
}

static void br_mr_getLen(BridgeMap *o, uc_engine *uc) {
    uint32_t fn;
    uc_reg_read(uc, UC_ARM_REG_R0, &fn);
    SET_RET_V(uc_file_getLen((const char *)uc_getMrpMemPtr(fn)));
}

static void br_mr_drawBitmap(BridgeMap *o, uc_engine *uc) {
    uint32_t bmp_addr, x, y, w, h;
    uc_reg_read(uc, UC_ARM_REG_R0, &bmp_addr);
    uc_reg_read(uc, UC_ARM_REG_R1, &x);
    uc_reg_read(uc, UC_ARM_REG_R2, &y);
    uc_reg_read(uc, UC_ARM_REG_R3, &w);

    uint32_t sp;
    uc_reg_read(uc, UC_ARM_REG_SP, &sp);
    uc_mem_read(uc, sp, &h, 4);

    uint16_t *bmp = (uint16_t *)uc_getMrpMemPtr(bmp_addr);
    LOGI("br_drawBitmap(0x%X, %d, %d, %d, %d)", bmp_addr, x, y, w, h);
    emu_bitmapToscreen(bmp, (int16_t)x, (int16_t)y, (uint16_t)w, (uint16_t)h);
}

static void br_mr_getHostByName(BridgeMap *o, uc_engine *uc) {
    /* TODO: implement network callback bridge */
    SET_RET_V(MR_FAILED);
}

static void br_mr_initNetwork(BridgeMap *o, uc_engine *uc) {
    SET_RET_V(MR_SUCCESS);
}

static void br_mr_closeNetwork(BridgeMap *o, uc_engine *uc) {
    SET_RET_V(MR_SUCCESS);
}

static void br_mr_socket(BridgeMap *o, uc_engine *uc) {
    SET_RET_V(MR_FAILED);
}

static void br_mr_connect(BridgeMap *o, uc_engine *uc) {
    SET_RET_V(MR_FAILED);
}

static void br_mr_getSocketState(BridgeMap *o, uc_engine *uc) {
    SET_RET_V(MR_FAILED);
}

static void br_mr_closeSocket(BridgeMap *o, uc_engine *uc) {
    SET_RET_V(MR_SUCCESS);
}

static void br_mr_recv(BridgeMap *o, uc_engine *uc) {
    SET_RET_V(MR_FAILED);
}

static void br_mr_send(BridgeMap *o, uc_engine *uc) {
    SET_RET_V(MR_FAILED);
}

static void br_mr_recvfrom(BridgeMap *o, uc_engine *uc) {
    SET_RET_V(MR_FAILED);
}

static void br_mr_sendto(BridgeMap *o, uc_engine *uc) {
    SET_RET_V(MR_FAILED);
}

static void br_mr_startShake(BridgeMap *o, uc_engine *uc) {
    uint32_t ms;
    uc_reg_read(uc, UC_ARM_REG_R0, &ms);
    emu_startShake(ms);
    SET_RET_V(MR_SUCCESS);
}

static void br_mr_stopShake(BridgeMap *o, uc_engine *uc) {
    emu_stopShake();
    SET_RET_V(MR_SUCCESS);
}

static void br_mr_playSound(BridgeMap *o, uc_engine *uc) {
    uint32_t type, data, dataLen, loop;
    uc_reg_read(uc, UC_ARM_REG_R0, &type);
    uc_reg_read(uc, UC_ARM_REG_R1, &data);
    uc_reg_read(uc, UC_ARM_REG_R2, &dataLen);
    uc_reg_read(uc, UC_ARM_REG_R3, &loop);
    SET_RET_V(mr_playSound(type, uc_getMrpMemPtr(data), dataLen, loop));
}

static void br_mr_stopSound(BridgeMap *o, uc_engine *uc) {
    uint32_t type;
    uc_reg_read(uc, UC_ARM_REG_R0, &type);
    SET_RET_V(mr_stopSound(type));
}

static void br_mr_dialogCreate(BridgeMap *o, uc_engine *uc) {
    SET_RET_V(MR_FAILED);
}

static void br_mr_dialogRelease(BridgeMap *o, uc_engine *uc) {
    SET_RET_V(MR_FAILED);
}

static void br_mr_dialogRefresh(BridgeMap *o, uc_engine *uc) {
    SET_RET_V(MR_FAILED);
}

static void br_mr_textCreate(BridgeMap *o, uc_engine *uc) {
    SET_RET_V(MR_FAILED);
}

static void br_mr_textRelease(BridgeMap *o, uc_engine *uc) {
    SET_RET_V(MR_FAILED);
}

static void br_mr_textRefresh(BridgeMap *o, uc_engine *uc) {
    SET_RET_V(MR_FAILED);
}

static void br_mr_editCreate(BridgeMap *o, uc_engine *uc) {
    SET_RET_V(MR_FAILED);
}

static void br_mr_editRelease(BridgeMap *o, uc_engine *uc) {
    SET_RET_V(MR_FAILED);
}

static void br_mr_editGetText(BridgeMap *o, uc_engine *uc) {
    SET_RET_V(0);
}

/* ===== Function Maps (offsets match vmrp exactly) ===== */

static BridgeMap mr_table_funcMap[] = {
    BRIDGE_FUNC_MAP(0x0,   MAP_FUNC, mr_malloc,           NULL, br_mr_malloc, 0),
    BRIDGE_FUNC_MAP(0x4,   MAP_FUNC, mr_free,             NULL, br_mr_free, 0),
    BRIDGE_FUNC_MAP(0x8,   MAP_FUNC, mr_realloc,          NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0xC,   MAP_FUNC, memcpy,              NULL, br_memcpy, 0),
    BRIDGE_FUNC_MAP(0x10,  MAP_FUNC, memmove,             NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x14,  MAP_FUNC, strcpy,              NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x18,  MAP_FUNC, strncpy,             NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x1C,  MAP_FUNC, strcat,              NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x20,  MAP_FUNC, strncat,             NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x24,  MAP_FUNC, memcmp,              NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x28,  MAP_FUNC, strcmp,               NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x2C,  MAP_FUNC, strncmp,             NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x30,  MAP_FUNC, strcoll,             NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x34,  MAP_FUNC, memchr,              NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x38,  MAP_FUNC, memset,              NULL, br_memset, 0),
    BRIDGE_FUNC_MAP(0x3C,  MAP_FUNC, strlen,              NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x40,  MAP_FUNC, strstr,              NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x44,  MAP_FUNC, sprintf,             NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x48,  MAP_FUNC, atoi,                NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x4C,  MAP_FUNC, strtoul,             NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x50,  MAP_FUNC, rand,                NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x54,  MAP_DATA, reserve0,            NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x58,  MAP_DATA, reserve1,            NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x5C,  MAP_DATA, _mr_c_internal_table, NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x60,  MAP_DATA, _mr_c_port_table,    NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x64,  MAP_FUNC, _mr_c_function_new,  NULL, br__mr_c_function_new, 0),
    BRIDGE_FUNC_MAP(0x68,  MAP_FUNC, mr_printf,           NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x6C,  MAP_FUNC, mr_mem_get,          NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x70,  MAP_FUNC, mr_mem_free,         NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x74,  MAP_FUNC, mr_drawBitmap,       NULL, br_mr_drawBitmap, 0),
    BRIDGE_FUNC_MAP(0x78,  MAP_FUNC, mr_getCharBitmap,    NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x7C,  MAP_FUNC, mr_timerStart,       NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x80,  MAP_FUNC, mr_timerStop,        NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x84,  MAP_FUNC, mr_getTime,          NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x88,  MAP_FUNC, mr_getDatetime,      NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x8C,  MAP_FUNC, mr_getUserInfo,      NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x90,  MAP_FUNC, mr_sleep,            NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x94,  MAP_FUNC, mr_plat,             NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x98,  MAP_FUNC, mr_platEx,           NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x9C,  MAP_FUNC, mr_ferrno,           NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0xA0,  MAP_FUNC, mr_open,             NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0xA4,  MAP_FUNC, mr_close,            NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0xA8,  MAP_FUNC, mr_info,             NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0xAC,  MAP_FUNC, mr_write,            NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0xB0,  MAP_FUNC, mr_read,             NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0xB4,  MAP_FUNC, mr_seek,             NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0xB8,  MAP_FUNC, mr_getLen,           NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0xBC,  MAP_FUNC, mr_remove,           NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0xC0,  MAP_FUNC, mr_rename,           NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0xC4,  MAP_FUNC, mr_mkDir,            NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0xC8,  MAP_FUNC, mr_rmDir,            NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0xCC,  MAP_FUNC, mr_findStart,        NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0xD0,  MAP_FUNC, mr_findGetNext,      NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0xD4,  MAP_FUNC, mr_findStop,         NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0xD8,  MAP_FUNC, mr_exit,             NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0xDC,  MAP_FUNC, mr_startShake,       NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0xE0,  MAP_FUNC, mr_stopShake,        NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0xE4,  MAP_FUNC, mr_playSound,        NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0xE8,  MAP_FUNC, mr_stopSound,        NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0xEC,  MAP_FUNC, mr_sendSms,          NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0xF0,  MAP_FUNC, mr_call,             NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0xF4,  MAP_FUNC, mr_getNetworkID,     NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0xF8,  MAP_FUNC, mr_connectWAP,       NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0xFC,  MAP_FUNC, mr_menuCreate,       NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x100, MAP_FUNC, mr_menuSetItem,      NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x104, MAP_FUNC, mr_menuShow,         NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x108, MAP_DATA, reserve,             NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x10C, MAP_FUNC, mr_menuRelease,      NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x110, MAP_FUNC, mr_menuRefresh,      NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x114, MAP_FUNC, mr_dialogCreate,     NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x118, MAP_FUNC, mr_dialogRelease,    NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x11C, MAP_FUNC, mr_dialogRefresh,    NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x120, MAP_FUNC, mr_textCreate,       NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x124, MAP_FUNC, mr_textRelease,      NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x128, MAP_FUNC, mr_textRefresh,      NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x12C, MAP_FUNC, mr_editCreate,       NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x130, MAP_FUNC, mr_editRelease,      NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x134, MAP_FUNC, mr_editGetText,      NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x138, MAP_FUNC, mr_winCreate,        NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x13C, MAP_FUNC, mr_winRelease,       NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x140, MAP_FUNC, mr_getScreenInfo,    NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x144, MAP_FUNC, mr_initNetwork,      NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x148, MAP_FUNC, mr_closeNetwork,     NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x14C, MAP_FUNC, mr_getHostByName,    NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x150, MAP_FUNC, mr_socket,           NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x154, MAP_FUNC, mr_connect,          NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x158, MAP_FUNC, mr_closeSocket,      NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x15C, MAP_FUNC, mr_recv,             NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x160, MAP_FUNC, mr_recvfrom,         NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x164, MAP_FUNC, mr_send,             NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x168, MAP_FUNC, mr_sendto,           NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x16C, MAP_DATA, mr_screenBuf,        NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x170, MAP_DATA, mr_screen_w,         NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x174, MAP_DATA, mr_screen_h,         NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x178, MAP_DATA, mr_screen_bit,       NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x17C, MAP_DATA, mr_bitmap,           NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x180, MAP_DATA, mr_tile,             NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x184, MAP_DATA, mr_map,              NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x188, MAP_DATA, mr_sound,            NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x18C, MAP_DATA, mr_sprite,           NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x190, MAP_DATA, pack_filename,       NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x194, MAP_DATA, start_filename,      NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x198, MAP_DATA, old_pack_filename,   NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x19C, MAP_DATA, old_start_filename,  NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x1A0, MAP_DATA, mr_ram_file,         NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x1A4, MAP_DATA, mr_ram_file_len,     NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x1A8, MAP_DATA, mr_soundOn,          NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x1AC, MAP_DATA, mr_shakeOn,          NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x1B0, MAP_DATA, LG_mem_base,         NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x1B4, MAP_DATA, LG_mem_len,          NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x1B8, MAP_DATA, LG_mem_end,          NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x1BC, MAP_DATA, LG_mem_left,         NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x1C0, MAP_DATA, mr_sms_cfg_buf,      NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x1C4, MAP_FUNC, mr_md5_init,         NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x1C8, MAP_FUNC, mr_md5_append,       NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x1CC, MAP_FUNC, mr_md5_finish,       NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x1D0, MAP_FUNC, _mr_load_sms_cfg,    NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x1D4, MAP_FUNC, _mr_save_sms_cfg,    NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x1D8, MAP_FUNC, _DispUpEx,           NULL, br_mr_drawBitmap, 0),
    BRIDGE_FUNC_MAP(0x1DC, MAP_FUNC, _DrawPoint,          NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x1E0, MAP_FUNC, _DrawBitmap,         NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x1E4, MAP_FUNC, _DrawBitmapEx,       NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x1E8, MAP_FUNC, DrawRect,            NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x1EC, MAP_FUNC, _DrawText,           NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x1F0, MAP_FUNC, _BitmapCheck,        NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x1F4, MAP_FUNC, _mr_readFile,        NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x1F8, MAP_FUNC, mr_wstrlen,          NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x1FC, MAP_FUNC, mr_registerAPP,      NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x200, MAP_FUNC, _DrawTextEx,         NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x204, MAP_FUNC, _mr_EffSetCon,       NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x208, MAP_FUNC, _mr_TestCom,         NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x20C, MAP_FUNC, _mr_TestCom1,        NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x210, MAP_FUNC, c2u,                 NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x214, MAP_FUNC, _mr_div,             NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x218, MAP_FUNC, _mr_mod,             NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x21C, MAP_DATA, LG_mem_min,           NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x220, MAP_DATA, LG_mem_top,           NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x224, MAP_DATA, mr_updcrc,            NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x228, MAP_DATA, start_fileparameter,  NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x22C, MAP_DATA, mr_sms_return_flag,   NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x230, MAP_DATA, mr_sms_return_val,    NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x234, MAP_DATA, mr_unzip,             NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x238, MAP_DATA, mr_exit_cb,           NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x23C, MAP_DATA, mr_exit_cb_data,      NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x240, MAP_DATA, mr_entry,             NULL, NULL, 0),
    BRIDGE_FUNC_MAP(0x244, MAP_FUNC, mr_platDrawChar,      NULL, NULL, 0),
};

static BridgeMap dsm_require_funcs_funcMap[] = {
    BRIDGE_FUNC_MAP(0x00, MAP_FUNC, test,              NULL,                  br_test, 0),
    BRIDGE_FUNC_MAP(0x04, MAP_FUNC, log,               NULL,                  br_log, 0),
    BRIDGE_FUNC_MAP(0x08, MAP_FUNC, exit,              NULL,                  br_exit, 0),
    BRIDGE_FUNC_MAP(0x0c, MAP_FUNC, srand,             NULL,                  br_srand, 0),
    BRIDGE_FUNC_MAP(0x10, MAP_FUNC, rand,              NULL,                  br_rand, 0),
    BRIDGE_FUNC_MAP(0x14, MAP_FUNC, mem_get,           NULL,                  br_mem_get, 0),
    BRIDGE_FUNC_MAP(0x18, MAP_FUNC, mem_free,          NULL,                  br_mem_free, 0),
    BRIDGE_FUNC_MAP(0x1c, MAP_FUNC, timerStart,        NULL,                  br_timerStart, 0),
    BRIDGE_FUNC_MAP(0x20, MAP_FUNC, timerStop,         NULL,                  br_timerStop, 0),
    BRIDGE_FUNC_MAP(0x24, MAP_FUNC, get_uptime_ms,     br_get_uptime_ms_init, br_get_uptime_ms, 0),
    BRIDGE_FUNC_MAP(0x28, MAP_FUNC, getDatetime,       NULL,                  br_getDatetime, 0),
    BRIDGE_FUNC_MAP(0x2c, MAP_FUNC, sleep,             NULL,                  br_sleep, 0),
    BRIDGE_FUNC_MAP(0x30, MAP_FUNC, open,              NULL,                  br_mr_open, 0),
    BRIDGE_FUNC_MAP(0x34, MAP_FUNC, close,             NULL,                  br_mr_close, 0),
    BRIDGE_FUNC_MAP(0x38, MAP_FUNC, read,              NULL,                  br_mr_read, 0),
    BRIDGE_FUNC_MAP(0x3c, MAP_FUNC, write,             NULL,                  br_mr_write, 0),
    BRIDGE_FUNC_MAP(0x40, MAP_FUNC, seek,              NULL,                  br_mr_seek, 0),
    BRIDGE_FUNC_MAP(0x44, MAP_FUNC, info,              NULL,                  br_mr_info, 0),
    BRIDGE_FUNC_MAP(0x48, MAP_FUNC, remove,            NULL,                  br_mr_remove, 0),
    BRIDGE_FUNC_MAP(0x4c, MAP_FUNC, rename,            NULL,                  br_mr_rename, 0),
    BRIDGE_FUNC_MAP(0x50, MAP_FUNC, mkDir,             NULL,                  br_mr_mkDir, 0),
    BRIDGE_FUNC_MAP(0x54, MAP_FUNC, rmDir,             NULL,                  br_mr_rmDir, 0),
    BRIDGE_FUNC_MAP(0x58, MAP_FUNC, opendir,           NULL,                  br_mr_opendir, 0),
    BRIDGE_FUNC_MAP(0x5c, MAP_FUNC, readdir,           br_readdir_init,       br_readdir, 0),
    BRIDGE_FUNC_MAP(0x60, MAP_FUNC, closedir,          NULL,                  br_closedir, 0),
    BRIDGE_FUNC_MAP(0x64, MAP_FUNC, getLen,            NULL,                  br_mr_getLen, 0),
    BRIDGE_FUNC_MAP(0x68, MAP_FUNC, drawBitmap,        NULL,                  br_mr_drawBitmap, 0),
    BRIDGE_FUNC_MAP(0x6c, MAP_FUNC, getHostByName,     NULL,                  br_mr_getHostByName, 0),
    BRIDGE_FUNC_MAP(0x70, MAP_FUNC, initNetwork,       NULL,                  br_mr_initNetwork, 0),
    BRIDGE_FUNC_MAP(0x74, MAP_FUNC, mr_closeNetwork,   NULL,                  br_mr_closeNetwork, 0),
    BRIDGE_FUNC_MAP(0x78, MAP_FUNC, mr_socket,         NULL,                  br_mr_socket, 0),
    BRIDGE_FUNC_MAP(0x7c, MAP_FUNC, mr_connect,        NULL,                  br_mr_connect, 0),
    BRIDGE_FUNC_MAP(0x80, MAP_FUNC, mr_getSocketState, NULL,                  br_mr_getSocketState, 0),
    BRIDGE_FUNC_MAP(0x84, MAP_FUNC, mr_closeSocket,    NULL,                  br_mr_closeSocket, 0),
    BRIDGE_FUNC_MAP(0x88, MAP_FUNC, mr_recv,           NULL,                  br_mr_recv, 0),
    BRIDGE_FUNC_MAP(0x8c, MAP_FUNC, mr_send,           NULL,                  br_mr_send, 0),
    BRIDGE_FUNC_MAP(0x90, MAP_FUNC, mr_recvfrom,       NULL,                  br_mr_recvfrom, 0),
    BRIDGE_FUNC_MAP(0x94, MAP_FUNC, mr_sendto,         NULL,                  br_mr_sendto, 0),
    BRIDGE_FUNC_MAP(0x98, MAP_FUNC, mr_startShake,     NULL,                  br_mr_startShake, 0),
    BRIDGE_FUNC_MAP(0x9c, MAP_FUNC, mr_stopShake,      NULL,                  br_mr_stopShake, 0),
    BRIDGE_FUNC_MAP(0xa0, MAP_FUNC, mr_playSound,      NULL,                  br_mr_playSound, 0),
    BRIDGE_FUNC_MAP(0xa4, MAP_FUNC, mr_stopSound,      NULL,                  br_mr_stopSound, 0),
    BRIDGE_FUNC_MAP(0xa8, MAP_FUNC, mr_dialogCreate,   NULL,                  br_mr_dialogCreate, 0),
    BRIDGE_FUNC_MAP(0xac, MAP_FUNC, mr_dialogRelease,  NULL,                  br_mr_dialogRelease, 0),
    BRIDGE_FUNC_MAP(0xb0, MAP_FUNC, mr_dialogRefresh,  NULL,                  br_mr_dialogRefresh, 0),
    BRIDGE_FUNC_MAP(0xb4, MAP_FUNC, mr_textCreate,     NULL,                  br_mr_textCreate, 0),
    BRIDGE_FUNC_MAP(0xb8, MAP_FUNC, mr_textRelease,    NULL,                  br_mr_textRelease, 0),
    BRIDGE_FUNC_MAP(0xbc, MAP_FUNC, mr_textRefresh,    NULL,                  br_mr_textRefresh, 0),
    BRIDGE_FUNC_MAP(0xc0, MAP_FUNC, mr_editCreate,     NULL,                  br_mr_editCreate, 0),
    BRIDGE_FUNC_MAP(0xc4, MAP_FUNC, mr_editRelease,    NULL,                  br_mr_editRelease, 0),
    BRIDGE_FUNC_MAP(0xc8, MAP_FUNC, mr_editGetText,    NULL,                  br_mr_editGetText, 0),
};

/* ===== Hook and init ===== */

static void hook_code(uc_engine *uc, uint64_t address, uint32_t size, void *user_data) {
    uIntMap *mobj = uIntMap_search(&root, (uint32_t)address);
    if (mobj) {
        BridgeMap *obj = (BridgeMap *)mobj->data;
        if (obj->type == MAP_FUNC) {
            if (obj->fn == NULL) {
                LOGW("Not implemented: %s() at 0x%X", obj->name, (uint32_t)address);
                SET_RET_V(0);
            } else {
                obj->fn(obj, uc);
            }
            uint32_t _lr;
            uc_reg_read(uc, UC_ARM_REG_LR, &_lr);
            uc_reg_write(uc, UC_ARM_REG_PC, &_lr);
            return;
        }
    }
}

static void *hooks_init(uc_engine *uc, BridgeMap *map, uint32_t mapCount, uint32_t size) {
    uc_err err;
    uc_hook trace;
    BridgeMap *obj;
    uIntMap *mobj;
    uint32_t addr;
    void *ptr = uc_my_mallocExt(size);
    uint32_t startAddress = uc_toMrpMemAddr(ptr);

    err = uc_hook_add(uc, &trace, UC_HOOK_CODE, hook_code, NULL,
                      startAddress, startAddress + size, 0);
    if (err != UC_ERR_OK) {
        LOGE("uc_hook_add err %u (%s)", err, uc_strerror(err));
        uc_my_freeExt(ptr);
        return NULL;
    }

    for (uint32_t i = 0; i < mapCount; i++) {
        obj = &map[i];
        addr = startAddress + obj->pos;
        if (obj->initFn != NULL) {
            obj->initFn(obj, uc, addr);
        } else {
            if (obj->type == MAP_FUNC) {
                uc_mem_write(uc, addr, &addr, 4);
            }
        }
        mobj = (uIntMap *)malloc(sizeof(uIntMap));
        mobj->key = addr;
        mobj->data = obj;
        uIntMap_insert(&root, mobj);
    }
    return ptr;
}

static int uc_run_ok = 1;

static void uc_runCode(uc_engine *uc, uint32_t startAddr, uint32_t stopAddr, bool isThumb) {
    if (!uc_run_ok) return;

    uc_reg_write(uc, UC_ARM_REG_LR, &stopAddr);
    startAddr = isThumb ? (startAddr | 1) : startAddr;
    uc_err err = uc_emu_start(uc, startAddr, stopAddr, 0, 0);
    if (err) {
        uint32_t pc;
        uc_reg_read(uc, UC_ARM_REG_PC, &pc);
        LOGE("uc_emu_start failed: %u (%s) PC=0x%X start=0x%X stop=0x%X",
             err, uc_strerror(err), pc, startAddr, stopAddr);
    }
}

uc_err uc_bridge_init(uc_engine *uc) {
    root = (struct rb_root)RB_ROOT;
    uc_run_ok = 1;
    uc_file_reset();
    uc_mr_table_ptr = NULL;
    mr_c_function_P = NULL;
    dsm_require_funcs = NULL;
    mr_c_event = NULL;
    dsm_event = NULL;
    mr_start_dsm_param = NULL;
    mr_extHelper_addr = 0;

    uint32_t len = 4 * countof(mr_table_funcMap);
    uc_mr_table_ptr = hooks_init(uc, mr_table_funcMap, countof(mr_table_funcMap), len);
    if (!uc_mr_table_ptr) return UC_ERR_RESOURCE;

    dsm_require_funcs = hooks_init(uc, dsm_require_funcs_funcMap,
                                   countof(dsm_require_funcs_funcMap),
                                   sizeof(UC_DSM_REQUIRE_FUNCS));
    if (!dsm_require_funcs) return UC_ERR_RESOURCE;

    ((UC_DSM_REQUIRE_FUNCS *)dsm_require_funcs)->flags = FLAG_USE_UTF8_FS;

    mr_c_event = uc_my_mallocExt(sizeof(uc_event_t));
    dsm_event = uc_my_mallocExt(sizeof(uc_event_t));
    mr_start_dsm_param = uc_my_mallocExt(sizeof(uc_start_t));

    return UC_ERR_OK;
}

uc_err uc_bridge_ext_init(uc_engine *uc) {
    uint32_t v = uc_toMrpMemAddr(uc_mr_table_ptr);
    uc_mem_write(uc, UC_CODE_ADDRESS, &v, 4);

    v = 1;
    uc_reg_write(uc, UC_ARM_REG_R0, &v);

    uc_runCode(uc, UC_CODE_ADDRESS + 8, UC_CODE_ADDRESS, false);

    LOGI("bridge_ext_init done, r9(RW)=0x%X", mr_c_function_P ? mr_c_function_P->start_of_ER_RW : 0);
    return UC_ERR_OK;
}

static int uc_ext_sb_in_vm(uint32_t sb) {
    return sb != 0U && sb < (uint32_t)UC_END_ADDRESS;
}

void uc_bridge_clear_ext_state(void) {
    mr_c_function_P = NULL;
    mr_extHelper_addr = 0;
}

static int32_t bridge_mr_extHelper(uc_engine *uc, uint32_t code, uint32_t input, uint32_t input_len) {
    if (!mr_extHelper_addr || !mr_c_function_P) {
        LOGE("bridge_mr_extHelper: no ext (helper=0x%X cfp=%p)", mr_extHelper_addr, (void *)mr_c_function_P);
        return MR_FAILED;
    }
    if (mr_c_function_P->start_of_ER_RW != 0U &&
        !uc_ext_sb_in_vm(mr_c_function_P->start_of_ER_RW)) {
        LOGE("bridge_mr_extHelper: invalid start_of_ER_RW=0x%X (helper=0x%X)",
             mr_c_function_P->start_of_ER_RW, mr_extHelper_addr);
        return MR_FAILED;
    }

    uint32_t v = uc_toMrpMemAddr(mr_c_function_P);
    uc_reg_write(uc, UC_ARM_REG_R0, &v);
    uc_reg_write(uc, UC_ARM_REG_R1, &code);
    uc_reg_write(uc, UC_ARM_REG_R2, &input);
    uc_reg_write(uc, UC_ARM_REG_R3, &input_len);

    /* mythroad 用 r9 作 SB；Unicorn 里寄存器需每次调用前设好，否则 dsm_init 等会返回 0 */
    if (mr_c_function_P->start_of_ER_RW != 0U) {
        uint32_t r9sb = mr_c_function_P->start_of_ER_RW;
        uc_reg_write(uc, UC_ARM_REG_R9, &r9sb);
    }

    uc_runCode(uc, mr_extHelper_addr, UC_CODE_ADDRESS, false);
    uc_reg_read(uc, UC_ARM_REG_R0, &v);
    return (int32_t)v;
}

static int32_t bridge_mr_event(uc_engine *uc, int32_t code, int32_t param0, int32_t param1) {
    mr_c_event->code = code;
    mr_c_event->p0 = param0;
    mr_c_event->p1 = param1;
    return bridge_mr_extHelper(uc, 1, uc_toMrpMemAddr(mr_c_event), sizeof(uc_event_t));
}

/* mythroad 各分支用日期作协议版本；除 20210701 外兼容其它合理构建 */
static int uc_bridge_dsm_ver_ok(int32_t v) {
    if (v == VMRP_VER)
        return 1;
    if (v >= 20180101 && v <= 20391231)
        return 1;
    return 0;
}

int32_t uc_bridge_dsm_init(uc_engine *uc, int screenW, int screenH) {
    g_screenW = screenW;
    g_screenH = screenH;
    pthread_mutex_lock(&mutex);
    int32_t v = bridge_mr_event(uc, DSM_INIT, uc_toMrpMemAddr(dsm_require_funcs), 0);
    pthread_mutex_unlock(&mutex);

    if (uc_bridge_dsm_ver_ok(v)) {
        LOGI("dsm_init OK, ver=%d (expect %d or dated build)", v, VMRP_VER);
        return MR_SUCCESS;
    }
    LOGE("dsm_init failed, got %d expect %d (or dated 20180101-20391231)", v, VMRP_VER);
    return MR_FAILED;
}

int32_t uc_bridge_dsm_mr_start_dsm(uc_engine *uc, char *filename, char *ext, char *entry) {
    pthread_mutex_lock(&mutex);

    mr_start_dsm_param->filename = uc_copyStrToMrp(filename);
    mr_start_dsm_param->ext = uc_copyStrToMrp(ext);
    mr_start_dsm_param->entry = entry ? uc_copyStrToMrp(entry) : 0;

    LOGI("mr_start_dsm: filename=0x%X(%s) ext=0x%X(%s) entry=0x%X(%s)",
         mr_start_dsm_param->filename, filename,
         mr_start_dsm_param->ext, ext,
         mr_start_dsm_param->entry, entry ? entry : "NULL");

    int32_t v = bridge_mr_event(uc, UC_MR_START_DSM, uc_toMrpMemAddr(mr_start_dsm_param), 0);

    if (mr_start_dsm_param->filename)
        uc_my_freeExt(uc_getMrpMemPtr(mr_start_dsm_param->filename));
    if (mr_start_dsm_param->ext)
        uc_my_freeExt(uc_getMrpMemPtr(mr_start_dsm_param->ext));
    if (mr_start_dsm_param->entry)
        uc_my_freeExt(uc_getMrpMemPtr(mr_start_dsm_param->entry));

    pthread_mutex_unlock(&mutex);
    return v;
}

int32_t uc_bridge_dsm_mr_pauseApp(uc_engine *uc) {
    pthread_mutex_lock(&mutex);
    int32_t v = bridge_mr_event(uc, UC_MR_PAUSEAPP, 0, 0);
    pthread_mutex_unlock(&mutex);
    return v;
}

int32_t uc_bridge_dsm_mr_resumeApp(uc_engine *uc) {
    pthread_mutex_lock(&mutex);
    /* 回到上一个 MRP 前先刷 TB，避免执行 pause 期间被改写但仍缓存的旧翻译块 */
    unicorn2_flush_all(uc);
    int32_t v = bridge_mr_event(uc, UC_MR_RESUMEAPP, 0, 0);
    pthread_mutex_unlock(&mutex);
    return v;
}

int32_t uc_bridge_dsm_mr_timer(uc_engine *uc) {
    pthread_mutex_lock(&mutex);
    int32_t v = bridge_mr_event(uc, UC_MR_TIMER, 0, 0);
    pthread_mutex_unlock(&mutex);
    return v;
}

int32_t uc_bridge_dsm_mr_event(uc_engine *uc, int32_t code, int32_t p0, int32_t p1) {
    pthread_mutex_lock(&mutex);
    dsm_event->code = code;
    dsm_event->p0 = p0;
    dsm_event->p1 = p1;
    int32_t v = bridge_mr_event(uc, UC_MR_EVENT, uc_toMrpMemAddr(dsm_event), 0);
    pthread_mutex_unlock(&mutex);
    return v;
}

int32_t uc_bridge_dsm_network_cb(uc_engine *uc, uint32_t addr, int32_t p0, uint32_t p1) {
    pthread_mutex_lock(&mutex);
    uint32_t r9;
    uc_reg_read(uc, UC_ARM_REG_R9, &r9);

    if (mr_c_function_P && mr_c_function_P->start_of_ER_RW) {
        uint32_t r9val = mr_c_function_P->start_of_ER_RW;
        uc_reg_write(uc, UC_ARM_REG_R9, &r9val);
    }

    uint32_t ret;
    uc_reg_write(uc, UC_ARM_REG_R0, &p0);
    uc_reg_write(uc, UC_ARM_REG_R1, &p1);
    uc_runCode(uc, addr, UC_CODE_ADDRESS, false);
    uc_reg_read(uc, UC_ARM_REG_R0, &ret);

    uc_reg_write(uc, UC_ARM_REG_R9, &r9);
    pthread_mutex_unlock(&mutex);
    return (int32_t)ret;
}
