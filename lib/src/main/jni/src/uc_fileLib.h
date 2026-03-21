#ifndef __UC_FILELIB_H__
#define __UC_FILELIB_H__

#include <stdint.h>

#define MR_FILE_RDONLY  1
#define MR_FILE_WRONLY  2
#define MR_FILE_RDWR    4
#define MR_FILE_CREATE  8

#define MR_IS_FILE    1
#define MR_IS_DIR     2
#define MR_IS_INVALID 0

int32_t uc_file_open(const char *filename, uint32_t mode);
int32_t uc_file_close(int32_t f);
int32_t uc_file_seek(int32_t f, int32_t pos, int method);
int32_t uc_file_read(int32_t f, void *p, uint32_t l);
int32_t uc_file_write(int32_t f, void *p, uint32_t l);
int32_t uc_file_rename(const char *oldname, const char *newname);
int32_t uc_file_remove(const char *filename);
int32_t uc_file_getLen(const char *filename);
int32_t uc_file_mkDir(const char *name);
int32_t uc_file_rmDir(const char *name);
int32_t uc_file_info(const char *filename);
int32_t uc_file_opendir(const char *name);
char *uc_file_readdir(int32_t f);
int32_t uc_file_closedir(int32_t f);
void uc_file_reset(void);

#endif
