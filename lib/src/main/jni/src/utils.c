#include <jni.h>
#include <fcntl.h>
#include <asm-generic/fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "mr_helper.h"
#include "emulator.h"

/**
* 获取文件类型
* @param name 文件路径
* @return MR_IS_FILE | MR_IS_DIR | MR_IS_INVALID
*/
int getFileType(const char *name)
{
	struct stat s1;
	int ret;

	//返回 0 成功
	ret = stat(name, &s1);
	if(ret != 0) {
		LOGE("getFileType errno=%d", errno);
		return MR_IS_INVALID;
	}

	if (s1.st_mode & S_IFDIR)
		return MR_IS_DIR;
	else if (s1.st_mode & S_IFREG)
		return MR_IS_FILE;
	else
		return MR_IS_INVALID;
}

long long getFileSize(const char *path)
{
	struct stat s1;
	int ret;

	ret = stat(path, &s1);
	if (ret != 0) {
		LOGE("getFileSize errno=%d", errno);
		return -1;
	}

	return s1.st_size;
}

int mkdir_p(const char *path)
{
	char tmp[DSM_MAX_FILE_LEN + 8];
	char *p;
	size_t len;

	if (!path || !path[0])
		return -1;
	if (strlen(path) >= sizeof(tmp))
		return -1;
	snprintf(tmp, sizeof(tmp), "%s", path);
	len = strlen(tmp);
	while (len > 1 && tmp[len - 1] == '/') {
		tmp[--len] = '\0';
	}
	for (p = tmp + 1; *p; p++) {
		if (*p == '/') {
			*p = '\0';
			if (tmp[0] && mkdir(tmp, 0777) != 0 && errno != EEXIST)
				return -1;
			*p = '/';
		}
	}
	if (mkdir(tmp, 0777) != 0 && errno != EEXIST)
		return -1;
	return 0;
}

int ensure_parent_dirs_for_file(const char *filepath)
{
	char tmp[DSM_MAX_FILE_LEN + 8];
	char *last_slash;

	if (!filepath || !filepath[0])
		return -1;
	if (strlen(filepath) >= sizeof(tmp))
		return -1;
	snprintf(tmp, sizeof(tmp), "%s", filepath);
	last_slash = strrchr(tmp, '/');
	if (!last_slash || last_slash == tmp)
		return 0;
	*last_slash = '\0';
	return mkdir_p(tmp);
}
