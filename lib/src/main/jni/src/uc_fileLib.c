#include "uc_fileLib.h"
#include "rbtree.h"
#include "utils.h"

#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>

#define MR_SUCCESS  0
#define MR_FAILED  -1
#define UC_DSM_ROOT_PATH "mythroad/"
#define UC_DSM_MAX_FILE_LEN 128

extern const char *GetDsmWorkPath(void);

static struct rb_root filef_map = RB_ROOT;
static uint32_t filef_count = 0;

static struct rb_root dirf_map = RB_ROOT;
static uint32_t dirf_count = 0;

static void normalize_path(const char *filename, char *out, size_t out_size) {
    const char *name = filename;

    if (!filename || !out || out_size == 0) return;
    while (name[0] == '.' && name[1] == '/') name += 2;
    if (name[0] == '/' || strncmp(name, UC_DSM_ROOT_PATH, strlen(UC_DSM_ROOT_PATH)) == 0) {
        snprintf(out, out_size, "%s", name);
    } else {
        snprintf(out, out_size, "%s%s", GetDsmWorkPath(), name);
    }
}

void uc_file_reset(void) {
    filef_map = (struct rb_root)RB_ROOT;
    filef_count = 0;
    dirf_map = (struct rb_root)RB_ROOT;
    dirf_count = 0;
}

int32_t uc_file_open(const char *filename, uint32_t mode) {
    int f;
    int new_mode = 0;
    char path[UC_DSM_MAX_FILE_LEN + 8] = {0};

    normalize_path(filename, path, sizeof(path));

    if (mode & MR_FILE_RDONLY) new_mode = O_RDONLY;
    if (mode & MR_FILE_WRONLY) new_mode = O_WRONLY;
    if (mode & MR_FILE_RDWR) new_mode = O_RDWR;
    if (mode & MR_FILE_CREATE) {
        ensure_parent_dirs_for_file(path);
        new_mode |= O_CREAT;
    }

    f = open(path, new_mode, 0777);
    if (f == -1)
        return 0;

    filef_count++;
    uIntMap *obj = malloc(sizeof(uIntMap));
    obj->key = filef_count;
    obj->data = (void *)(intptr_t)f;
    uIntMap_insert(&filef_map, obj);
    return filef_count;
}

int32_t uc_file_close(int32_t f) {
    uIntMap *obj = uIntMap_delete(&filef_map, f);
    if (obj == NULL) return MR_FAILED;
    int fh = (int)(intptr_t)obj->data;
    free(obj);
    if (close(fh) != 0) return MR_FAILED;
    return MR_SUCCESS;
}

int32_t uc_file_seek(int32_t f, int32_t pos, int method) {
    uIntMap *obj = uIntMap_search(&filef_map, f);
    if (obj == NULL) return MR_FAILED;
    off_t ret = lseek((int)(intptr_t)obj->data, (off_t)pos, method);
    if (ret == -1) return MR_FAILED;
    return MR_SUCCESS;
}

int32_t uc_file_read(int32_t f, void *p, uint32_t l) {
    uIntMap *obj = uIntMap_search(&filef_map, f);
    if (obj == NULL) return MR_FAILED;
    int32_t readnum = read((int)(intptr_t)obj->data, p, (size_t)l);
    if (readnum == -1) return MR_FAILED;
    return readnum;
}

int32_t uc_file_write(int32_t f, void *p, uint32_t l) {
    uIntMap *obj = uIntMap_search(&filef_map, f);
    if (obj == NULL) return MR_FAILED;
    int32_t writenum = write((int)(intptr_t)obj->data, p, (size_t)l);
    if (writenum == -1) return MR_FAILED;
    return writenum;
}

int32_t uc_file_rename(const char *oldname, const char *newname) {
    char old_path[UC_DSM_MAX_FILE_LEN + 8] = {0};
    char new_path[UC_DSM_MAX_FILE_LEN + 8] = {0};
    normalize_path(oldname, old_path, sizeof(old_path));
    normalize_path(newname, new_path, sizeof(new_path));
    ensure_parent_dirs_for_file(new_path);
    return rename(old_path, new_path) == 0 ? MR_SUCCESS : MR_FAILED;
}

int32_t uc_file_remove(const char *filename) {
    char path[UC_DSM_MAX_FILE_LEN + 8] = {0};
    normalize_path(filename, path, sizeof(path));
    return remove(path) == 0 ? MR_SUCCESS : MR_FAILED;
}

int32_t uc_file_getLen(const char *filename) {
    struct stat s1;
    char path[UC_DSM_MAX_FILE_LEN + 8] = {0};
    normalize_path(filename, path, sizeof(path));
    if (stat(path, &s1) != 0) return -1;
    return s1.st_size;
}

int32_t uc_file_mkDir(const char *name) {
    char path[UC_DSM_MAX_FILE_LEN + 8] = {0};
    normalize_path(name, path, sizeof(path));
    if (access(path, F_OK) == 0) return MR_SUCCESS;
    return mkdir_p(path) == 0 ? MR_SUCCESS : MR_FAILED;
}

int32_t uc_file_rmDir(const char *name) {
    char path[UC_DSM_MAX_FILE_LEN + 8] = {0};
    normalize_path(name, path, sizeof(path));
    return rmdir(path) == 0 ? MR_SUCCESS : MR_FAILED;
}

int32_t uc_file_info(const char *filename) {
    struct stat s1;
    char path[UC_DSM_MAX_FILE_LEN + 8] = {0};
    normalize_path(filename, path, sizeof(path));
    if (stat(path, &s1) != 0) return MR_IS_INVALID;
    if (s1.st_mode & S_IFDIR) return MR_IS_DIR;
    if (s1.st_mode & S_IFREG) return MR_IS_FILE;
    return MR_IS_INVALID;
}

int32_t uc_file_opendir(const char *name) {
    char path[UC_DSM_MAX_FILE_LEN + 8] = {0};
    normalize_path(name, path, sizeof(path));
    DIR *pDir = opendir(path);
    if (pDir != NULL) {
        dirf_count++;
        uIntMap *obj = malloc(sizeof(uIntMap));
        obj->key = dirf_count;
        obj->data = (void *)pDir;
        uIntMap_insert(&dirf_map, obj);
        return dirf_count;
    }
    return MR_FAILED;
}

char *uc_file_readdir(int32_t f) {
    uIntMap *obj = uIntMap_search(&dirf_map, f);
    if (obj == NULL) return NULL;
    struct dirent *pDt = readdir((DIR *)obj->data);
    if (pDt != NULL) return pDt->d_name;
    return NULL;
}

int32_t uc_file_closedir(int32_t f) {
    uIntMap *obj = uIntMap_delete(&dirf_map, f);
    if (obj == NULL) return MR_FAILED;
    DIR *pDir = (DIR *)obj->data;
    free(obj);
    return closedir(pDir) == 0 ? MR_SUCCESS : MR_FAILED;
}
