#ifndef D_STORAGE_H
#define D_STORAGE_H

#include "config/app_config.h"

//*********************************************************************************//
//                              存储设备类型定义                                   //
//*********************************************************************************//
typedef enum {
    D_STORAGE_SD0 = 0,      // SD卡0
    D_STORAGE_SD1,          // SD卡1
    D_STORAGE_UDISK0,       // U盘0
    D_STORAGE_UDISK1,       // U盘1
    D_STORAGE_FLASH,        // Flash 文件系统；当前 GC9307/HYN 使用外挂 NOR
    D_STORAGE_MAX
} d_storage_type_t;

//*********************************************************************************//
//                              存储设备状态                                       //
//*********************************************************************************//
typedef enum {
    D_STORAGE_OFFLINE = 0,  // 设备离线
    D_STORAGE_ONLINE,       // 设备在线
    D_STORAGE_MOUNTED,      // 已挂载
    D_STORAGE_ERROR         // 错误状态
} d_storage_status_t;

//*********************************************************************************//
//                              存储设备信息结构                                   //
//*********************************************************************************//
typedef struct {
    d_storage_type_t type;          // 设备类型
    d_storage_status_t status;      // 设备状态
    const char *dev_name;           // 设备名称 (如 "sd0", "udisk0")
    const char *root_path;          // 根路径 (如 "storage/sd0/C/")
    unsigned long total_size;       // 总容量 (KB)；当前未接入查询，值为 0（未知）
    unsigned long free_size;        // 空闲容量 (KB)；当前未接入查询，值为 0（未知）
} d_storage_info_t;

//*********************************************************************************//
//                              存储设备管理 API                                   //
//*********************************************************************************//

/**
 * @brief 初始化存储设备管理模块
 * @return 0:成功 <0:失败
 */
int d_storage_init(void);

/**
 * @brief 检查存储设备是否就绪
 * @return 0:就绪 <0:未就绪
 */
int d_storage_device_ready(void);

/**
 * @brief 检查指定SD卡是否就绪
 * @param sd_name SD卡设备名 ("sd0" 或 "sd1")
 * @return 0:就绪 <0:未就绪
 */
int d_sdcard_ready(const char *sd_name);

/**
 * @brief 检查指定U盘是否就绪
 * @param id U盘ID (0 或 1)
 * @return 0:就绪 <0:未就绪
 */
int d_udisk_ready(int id);

/**
 * @brief 格式化SD卡
 * @param sd_name SD卡设备名 ("sd0" 或 "sd1")
 * @return 0:成功 <0:失败
 */
int d_sdcard_format(const char *sd_name);

/**
 * @brief 格式化U盘
 * @param id U盘ID (0 或 1)
 * @return 0:成功 <0:失败
 */
int d_udisk_format(int id);

/**
 * @brief 获取存储设备信息
 * @param type 存储设备类型
 * @param info 输出设备信息结构指针
 * @return 0:已复制设备状态和路径 <0:参数错误
 * @note 当前容量查询未完整接入，返回 0 不表示容量字段有效。
 *       free_size 为 0 表示未知；不能据此判断设备已满。
 */
int d_storage_get_info(d_storage_type_t type, d_storage_info_t *info);

/**
 * @brief 获取当前活动存储设备的根路径
 * @return 路径字符串，失败返回NULL
 */
const char* d_storage_get_root_path(void);

/**
 * @brief 获取音乐文件存储路径
 * @return 路径字符串
 */
const char* d_storage_get_music_path(void);

/**
 * @brief 获取录像文件存储路径
 * @return 路径字符串
 */
const char* d_storage_get_rec_path(void);

/**
 * @brief 获取升级文件路径
 * @return 路径字符串
 */
const char* d_storage_get_upgrade_path(void);

/**
 * @brief 挂载存储设备
 * @param type 存储设备类型
 * @return 0:成功 <0:失败
 */
int d_storage_mount(d_storage_type_t type);

/**
 * @brief 卸载存储设备
 * @param type 存储设备类型
 * @return 0:成功 <0:失败
 */
int d_storage_unmount(d_storage_type_t type);

/**
 * @brief 查询存储设备剩余空间（当前未接入实际容量查询）
 * @param type 存储设备类型
 * @return 当前实现返回 0，既可能是容量未知，也可能是参数错误。
 * @note 不能把 0 当作磁盘已满；本接口没有独立的容量有效性标记。
 */
unsigned long d_storage_get_free_space(d_storage_type_t type);

/**
 * @brief 创建目录（递归创建）
 * @param path 目录路径
 * @return 0:成功 <0:失败
 */
int d_storage_mkdir(const char *path);

/**
 * @brief 检查文件或目录是否存在
 * @param path 路径
 * @return 1:存在 0:不存在
 */
int d_storage_exists(const char *path);

#endif // D_STORAGE_H
