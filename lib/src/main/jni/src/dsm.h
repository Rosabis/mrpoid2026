#ifndef _DSM_H
#define _DSM_H


extern char dsmType[8];
extern char dsmFactory[8];


#define DSM_ENWORD_H	16
#define DSM_ENWORD_W	8
#define DSM_CHWORD_H	16
#define DSM_CHWORD_W	16

#define DSM_SUPPORT_SOC_NUM                        (5)

/**
 * 协议：SDCARD 作为跟目录 以 /结尾
 *		dsmWorkPath 以 / 结尾，切换到跟路径后为空
 */
#define DSM_ROOT_PATH		"mythroad/"
#define SDCARD_PATH			"/storage/emulated/0/"

#define DSM_HIDE_DRIVE		".disk/"
#define DSM_DRIVE_A			"a/"
#define DSM_DRIVE_B			"b/"
#define DSM_ROOT_PATH_SYS	"mythroad/"

#define TMP_PATH			".tmp"

//------------------------------------------------
#define DSM_FACTORY			"mrpoid"
#define DSM_TYPE			"android"


/*请不要做修改*/
#define DSM_FACTORY_ID      "mrpej"				/*厂商标识，最多七个小写字符*/

#define MT6235

/*请不要修改这些值*/
#if (defined(MT6223P)||defined(MT6223)||defined(MT6223P_S00))
#define DSM_PLAT_VERSION                                       (2) /*手机平台区分(1~99)*/
#elif (defined(MT6226)||defined(MT6226M)||defined(MT6226D))
#define DSM_PLAT_VERSION                                       (4)  /*手机平台区分(1~99)*/
#elif (defined(MT6228))
#define DSM_PLAT_VERSION                                       (5)  /*手机平台区分(1~99)*/
#elif (defined(MT6225))
#define DSM_PLAT_VERSION                                        (3)  /*手机平台区分(1~99)*/
#elif (defined(MT6230))
#define DSM_PLAT_VERSION                                        (6)  /*手机平台区分(1~99)*/
#elif (defined(MT6227)||defined(MT6227D))
#define DSM_PLAT_VERSION                                        (7)
#elif (defined(MT6219))
#define DSM_PLAT_VERSION                                        (1)
#elif(defined(MT6235)||defined(MT6235B))
#define DSM_PLAT_VERSION                                        (8)
#elif(defined(MT6229))
#define DSM_PLAT_VERSION                                        (9)
#elif(defined(MT6253)||defined(MT6253T))
#define DSM_PLAT_VERSION                                        (10)
#elif(defined(MT6238))
#define DSM_PLAT_VERSION                                         (11)
#elif(defined(MT6239))
#define DSM_PLAT_VERSION                                          (12)
#else
#error PLATFORM NOT IN LIST PLEASE CALL SKY TO ADD THE PLATFORM
#endif

#ifdef DSM_IDLE_APP
#define DSM_FAE_VERSION                                       (180)    /*由平台组统一分配版本号，有需求请联系平台组*/
#else
#define DSM_FAE_VERSION                                       (182)    /*由平台组统一分配版本号，有需求请联系平台组*/
#endif


#define DSM_HANDSET_ID                                  "mtk"				 /*手机标识，最多七个小写字符*/




//---------------------------------
void dsm_init(void);
void dsm_reset(void);


int32 mr_exit(void);
int mr_getSocketState(int s);
void mr_cacheFlush(int id);
int32 mr_cacheSync(void* addr, int32 len);

/** 设置 SD卡路径，参数底层不做错误检查 */
void SetDsmSDPath(const char * path);
void SetDsmWorkPath(const char *path);

int32 mr_open(const char* filename, uint32 mode);
int32 mr_close(int32 f);
int32 mr_read(int32 f, void *p, uint32 l);
int32 mr_write(int32 f, void *p, uint32 l);
int32 mr_seek(int32 f, int32 pos, int method);
int32 mr_info(const char* filename);
int32 mr_getLen(const char* filename);
int32 mr_remove(const char* filename);
int32 mr_rename(const char* oldname, const char* newname);
int32 mr_mkDir(const char* name);
int32 mr_rmDir(const char* name);
int32 mr_findStart(const char* name, char* buffer, uint32 len);
int32 mr_findGetNext(int32 search_handle, char* buffer, uint32 len);
int32 mr_findStop(int32 search_handle);
int32 mr_playSound(int type, const void* data, uint32 dataLen, int32 loop);
int32 mr_stopSound(int type);

#endif
