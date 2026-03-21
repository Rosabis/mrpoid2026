#include "uc_vm.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <android/log.h>

#include "uc_bridge.h"
#include "uc_memory.h"
#include "uc_fileLib.h"

#define LOG_TAG "uc_vm"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static uint8_t *mrpMem;
static uint8_t *lowMem = NULL;
static uc_engine *g_uc = NULL;

void *uc_getMrpMemPtr(uint32_t addr) {
    return mrpMem + (addr - UC_START_ADDRESS);
}

uint32_t uc_toMrpMemAddr(void *ptr) {
    return (uint32_t)((uint8_t *)ptr - mrpMem) + UC_START_ADDRESS;
}

uc_engine *uc_getEngine(void) {
    return g_uc;
}

static bool hook_mem_invalid_cb(uc_engine *uc, uc_mem_type type,
                                uint64_t address, int size, int64_t value, void *user_data) {
    LOGE("mem_invalid type:%d at 0x%" PRIx64 ", size:0x%x, value:0x%" PRIx64,
         type, address, size, value);
    return false;
}

uc_engine *uc_initVmrp(void) {
    uc_engine *uc;
    uc_err err;
    uc_hook trace;

    err = uc_open(UC_ARCH_ARM, UC_MODE_ARM, &uc);
    if (err) {
        LOGE("uc_open() failed: %u (%s)", err, uc_strerror(err));
        return NULL;
    }

    mrpMem = malloc(UC_TOTAL_MEMORY);
    if (!mrpMem) {
        LOGE("Failed to allocate %d bytes for mrpMem", UC_TOTAL_MEMORY);
        uc_close(uc);
        return NULL;
    }
    memset(mrpMem, 0, UC_TOTAL_MEMORY);

    err = uc_mem_map_ptr(uc, UC_START_ADDRESS, UC_TOTAL_MEMORY, UC_PROT_ALL, mrpMem);
    if (err) {
        LOGE("uc_mem_map_ptr failed: %u (%s)", err, uc_strerror(err));
        free(mrpMem);
        uc_close(uc);
        return NULL;
    }

    uc_mem_manager_init(UC_MEM_MGR_ADDRESS, UC_MEM_MGR_SIZE);

    err = uc_bridge_init(uc);
    if (err) {
        LOGE("uc_bridge_init() failed: %u (%s)", err, uc_strerror(err));
        free(mrpMem);
        uc_close(uc);
        return NULL;
    }

    uc_hook_add(uc, &trace, UC_HOOK_MEM_INVALID, hook_mem_invalid_cb, NULL, 1, 0, 0);

    /* Map low memory (0 to CODE_ADDRESS) - some MRP apps read/write here */
    if (lowMem) {
        free(lowMem);
        lowMem = NULL;
    }
    lowMem = malloc(UC_CODE_ADDRESS);
    if (lowMem) {
        memset(lowMem, 0, UC_CODE_ADDRESS);
        uc_mem_map_ptr(uc, 0, UC_CODE_ADDRESS, UC_PROT_ALL, lowMem);
        LOGI("Mapped low memory 0x0 - 0x%X", UC_CODE_ADDRESS);
    }

    uint32_t sp = UC_STACK_ADDRESS + UC_STACK_SIZE;
    uc_reg_write(uc, UC_ARM_REG_SP, &sp);

    LOGI("uc_initVmrp OK: mem=%p, total=%d", mrpMem, UC_TOTAL_MEMORY);
    return uc;
}

int uc_freeVmrp(uc_engine *uc) {
    /* Reset file handles and bridge state */
    uc_file_reset();
    
    if (lowMem) {
        free(lowMem);
        lowMem = NULL;
    }
    
    if (mrpMem) {
        free(mrpMem);
        mrpMem = NULL;
    }
    
    if (uc)
        uc_close(uc);
    g_uc = NULL;
    return 0;
}

int32_t uc_loadCode(const void *extData, int32_t extLen) {
    if (!g_uc || !extData || extLen <= 0)
        return MR_FAILED;
    uc_mem_write(g_uc, UC_CODE_ADDRESS, extData, extLen);
    LOGI("uc_loadCode: loaded %d bytes to 0x%X", extLen, UC_CODE_ADDRESS);
    return MR_SUCCESS;
}

int uc_startVmrp(const void *extData, int32_t extLen,
                 const char *mrpFile, int screenW, int screenH) {
    if (g_uc) {
        LOGI("Cleaning up previous Unicorn instance");
        uc_freeVmrp(g_uc);
        g_uc = NULL;
    }

    g_uc = uc_initVmrp();
    if (!g_uc) {
        LOGE("uc_initVmrp() failed");
        return MR_FAILED;
    }

    if (uc_loadCode(extData, extLen) == MR_FAILED) {
        LOGE("uc_loadCode failed");
        return MR_FAILED;
    }

    uc_bridge_ext_init(g_uc);

    if (uc_bridge_dsm_init(g_uc, screenW, screenH) != MR_SUCCESS) {
        LOGE("uc_bridge_dsm_init failed");
        return MR_FAILED;
    }

    if (chdir("/storage/emulated/0/") != 0) {
        LOGE("chdir to /storage/emulated/0/ failed");
    } else {
        LOGI("chdir to /storage/emulated/0/ OK");
    }

    LOGE("==== START MRP: mrpFile='%s' ====", mrpFile ? mrpFile : "NULL");

    int32_t ret = uc_bridge_dsm_mr_start_dsm(g_uc,
        (char *)mrpFile,
        "start.mr",
        (char *)mrpFile
    );

    if (ret != 0) {
        LOGE("Direct launch failed (ret=%d), falling back to DSM GM", ret);
        ret = uc_bridge_dsm_mr_start_dsm(g_uc, "dsm_gm.mrp", "start.mr", NULL);
    }
    LOGI("uc_bridge_dsm_mr_start_dsm ret: 0x%X", ret);

    return MR_SUCCESS;
}

int32_t uc_event(int32_t code, int32_t p1, int32_t p2) {
    if (g_uc)
        return uc_bridge_dsm_mr_event(g_uc, code, p1, p2);
    return MR_FAILED;
}

int32_t uc_timer(void) {
    if (g_uc)
        return uc_bridge_dsm_mr_timer(g_uc);
    return MR_FAILED;
}

int32_t uc_pauseApp(void) {
    if (g_uc)
        return uc_bridge_dsm_mr_pauseApp(g_uc);
    return MR_FAILED;
}

int32_t uc_resumeApp(void) {
    if (g_uc)
        return uc_bridge_dsm_mr_resumeApp(g_uc);
    return MR_FAILED;
}

void uc_stopApp(void) {
    if (g_uc) {
        uc_freeVmrp(g_uc);
    }
}
