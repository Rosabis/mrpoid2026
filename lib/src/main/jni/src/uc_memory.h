#ifndef __UC_MEMORY_H__
#define __UC_MEMORY_H__

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void uc_mem_manager_init(uint32_t baseAddress, uint32_t len);
void *uc_my_mallocExt(uint32_t len);
void uc_my_freeExt(void *p);
void *uc_my_reallocExt(void *p, uint32_t newLen);
void uc_printMemoryInfo(void);

#endif
