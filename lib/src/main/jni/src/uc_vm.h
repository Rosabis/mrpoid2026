#ifndef __UC_VM_H__
#define __UC_VM_H__

#include <stdint.h>
#include <stdbool.h>
#include <unicorn/unicorn.h>

#define UC_CODE_ADDRESS      0x80000
#define UC_CODE_SIZE         (1024 * 1024 * 1)

#define UC_STACK_ADDRESS     (UC_CODE_ADDRESS + UC_CODE_SIZE)
#define UC_STACK_SIZE        (1024 * 1024 * 1)

#define UC_MEM_MGR_ADDRESS   (UC_STACK_ADDRESS + UC_STACK_SIZE)
#define UC_MEM_MGR_SIZE      (1024 * 1024 * 16)

#define UC_START_ADDRESS     UC_CODE_ADDRESS
#define UC_END_ADDRESS       (UC_MEM_MGR_ADDRESS + UC_MEM_MGR_SIZE)
#define UC_TOTAL_MEMORY      (UC_END_ADDRESS - UC_START_ADDRESS)

#define UC_SCREEN_WIDTH  240
#define UC_SCREEN_HEIGHT 320

#define MR_SUCCESS  0
#define MR_FAILED  -1
#define MR_IGNORE   1
#define MR_WAITING  2

void *uc_getMrpMemPtr(uint32_t addr);
uint32_t uc_toMrpMemAddr(void *ptr);

uc_engine *uc_initVmrp(void);
int uc_freeVmrp(uc_engine *uc);
int32_t uc_loadCode(const void *extData, int32_t extLen);

int uc_startVmrp(const void *extData, int32_t extLen,
                 const char *mrpFile, int screenW, int screenH);
int32_t uc_event(int32_t code, int32_t p1, int32_t p2);
int32_t uc_timer(void);
int32_t uc_pauseApp(void);
int32_t uc_resumeApp(void);
void uc_stopApp(void);

uc_engine *uc_getEngine(void);

#endif
