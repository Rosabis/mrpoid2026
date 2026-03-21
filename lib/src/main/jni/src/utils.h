#ifndef utils_H__
#define utils_H__

int getFileType(const char *name);
long long getFileSize(const char *path);

/** 递归创建目录（类似 mkdir -p） */
int mkdir_p(const char *path);
/** 为即将创建的文件保证其所在各级父目录存在 */
int ensure_parent_dirs_for_file(const char *filepath);

#endif // utils_H__
