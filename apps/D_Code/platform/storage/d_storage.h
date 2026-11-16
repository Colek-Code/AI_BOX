/**
 * @file d_storage.h
 * @brief 提供存储设备适配及 SD 介质代次快照，不解释音乐、相册或出厂业务字段。
 */

#ifndef D_STORAGE_H
#define D_STORAGE_H

#include "config/app_config.h"
#include "framework/d_error.h"
#include <stdint.h>

/** @brief SD 介质值类型快照；插拔及再次插入都会使旧代次身份失效。 */
typedef struct {
    uint32_t generation; /**< 非零介质代次；不是文件编号。 */
    uint8_t online;      /**< 最近通知或探测的设备在线事实。 */
    uint8_t mounted;     /**< 最近探测的挂载事实，不能替代本次文件操作结果。 */
} d_storage_sd_snapshot_t;

/**
 * @brief 在 SDK 插拔通知到来时立即更新 SD 介质代次及在线事实。
 * @param[in] id SD 编号，只接受 0 或 1。
 * @param[in] online 非零表示插入或在线，零表示拔出。
 * @note 不调用文件系统；应先于尽力而为的事件总线通知执行。每次合法通知都推进非零代次，并使挂载标记失效。
 */
void d_storage_sd_notify(uint8_t id, uint8_t online);
/**
 * @brief 查询 SD 设备在线及挂载事实，并复制带代次的值类型快照。
 * @param[in] id SD 编号，只接受 0 或 1。
 * @param[out] snapshot 接收当前介质事实的缓冲区。
 * @return D_OK 表示取得快照，不代表设备在线或已挂载；参数错误为 D_ERR_INVALID_ARG。
 * @note 供工作任务查询，不触发挂载；会调用 SDK 设备与文件系统查询，不得在文件系统扫描回调中重入。
 * @note SDK 查询在状态锁外执行，回锁后复核代次；并发插拔通知不会被旧探测结果覆盖。
 */
d_err_t d_storage_sd_snapshot(uint8_t id, d_storage_sd_snapshot_t *snapshot);
/**
 * @brief 检查 SD 代次与设备在线状态是否仍匹配。
 * @param[in] id SD 编号，0 或 1。
 * @param[in] generation 操作开始时保存的介质代次。
 * @return 匹配且设备仍在线时为 1；编号无效、离线或代次变化时为 0。
 * @note 可供扫描回调使用，只查代次及设备在线位，不重入文件系统。设备探测后再次核对代次，但不提供后续文件操作期间的占用锁。
 */
uint8_t d_storage_sd_matches(uint8_t id, uint32_t generation);
/**
 * @brief 在工作任务中沿 SDK 串行路径准备 SD，并复核介质未变化。
 * @param[in] id SD 编号，0 或 1。
 * @param[out] snapshot 调用中更新的介质快照，可能带回失败时已观察的事实。
 * @return D_OK 表示同一代次已挂载；离线、换卡或未挂载为 D_ERR_NOT_READY；SDK 准备失败为 D_ERR_IO；参数无效为 D_ERR_INVALID_ARG。
 * @note 可能触发 SDK 挂载及等待，不供 UI 或文件系统回调调用；从不格式化介质。
 */
d_err_t d_storage_sd_prepare(uint8_t id, d_storage_sd_snapshot_t *snapshot);

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
 * @brief 查询各设备状态，并按编译期开关选择默认活动存储。
 * @return 当前实现始终为 0。
 * @note 启动阶段调用；不证明介质可读，不在此发起 SD 格式化或独立挂载管理任务。
 */
int d_storage_init(void);

/**
 * @brief 刷新并检查当前活动存储是否被记录为已挂载。
 * @return 0 为已挂载，-1 为未就绪。
 * @note Flash 沿用 SDK 已挂载的历史假设；不等于验证每个文件或路径可读。
 */
int d_storage_device_ready(void);

/**
 * @brief 刷新并查询指定 SD 是否已挂载。
 * @param[in] sd_name 仅接受 sd0 或 sd1，调用中读取，不保存指针。
 * @return 0 为已挂载，-1 为名称无效或未就绪。
 */
int d_sdcard_ready(const char *sd_name);

/**
 * @brief 刷新并查询指定 U 盘是否已挂载。
 * @param[in] id U 盘编号，只接受 0 或 1。
 * @return 0 为已挂载，-1 为编号无效或未就绪。
 */
int d_udisk_ready(int id);

/**
 * @brief 同步请求 SDK 格式化给定 SD 路径。
 * @param[in] sd_name 用于构造 storage/<名称> 的设备名称；调用方须保证目标正确。
 * @return 返回 SDK fat_format 结果；空名称或未启用 JLFAT 时为 -1。
 * @note 破坏性操作，调用前必须取得设备操作授权并确保没有文件消费者；本接口不协调媒体资源，不自动挂载。
 */
int d_sdcard_format(const char *sd_name);

/**
 * @brief 同步请求 SDK 格式化指定 U 盘。
 * @param[in] id U 盘编号，只接受 0 或 1。
 * @return 返回 SDK fat_format 结果；编号无效或未启用 JLFAT 时为 -1。
 * @note 破坏性操作，调用前必须取得设备操作授权并确保没有文件消费者；本接口不协调业务资源。
 */
int d_udisk_format(int id);

/**
 * @brief 刷新设备状态并复制其基本信息。
 * @param[in] type 存储类型。
 * @param[out] info 接收信息的结构体；字符串字段为借用的静态配置。
 * @return 0 为已复制，-1 为类型或输出指针无效。
 * @note 容量后端尚未接入，总容量和空闲容量均返回 0 表示未知，不能判断介质已满。
 */
int d_storage_get_info(d_storage_type_t type, d_storage_info_t *info);

/**
 * @brief 取得当前记录为已挂载的活动存储根路径，否则回退到 Flash 配置路径。
 * @return 借用的静态路径字符串。
 * @note 本函数不主动刷新设备状态，回退路径不证明实际文件系统可读。
 */
const char* d_storage_get_root_path(void);

/**
 * @brief 取得当前音乐根路径，沿用活动存储的路径选择。
 * @return 与 d_storage_get_root_path 相同的借用静态路径，不转移所有权。
 */
const char* d_storage_get_music_path(void);

/**
 * @brief 取得编译期录像输出路径。
 * @return CONFIG_REC_PATH_0 的静态字符串，不验证目标已就绪。
 */
const char* d_storage_get_rec_path(void);

/**
 * @brief 按当前活动存储类型选择升级文件配置路径。
 * @return SD 或默认升级路径，U 盘类型返回其专用升级路径；不验证文件存在。
 */
const char* d_storage_get_upgrade_path(void);

/**
 * @brief 沿历史适配路径探测现有根目录，必要时尝试 SDK 挂载。
 * @param[in] type 要准备的存储类型。
 * @return 已可访问或挂载成功时为 0；无效类型、未支持挂载或 SDK 失败时为负值。
 * @note 可能执行文件系统操作；Flash 仅沿用已挂载假设。SD 新消费者优先使用带代次复核的 d_storage_sd_prepare。
 */
int d_storage_mount(d_storage_type_t type);

/**
 * @brief 沿历史适配路径尝试卸载非 Flash 设备。
 * @param[in] type 待卸载的非 Flash 存储类型。
 * @return 返回 SDK 卸载结果；无效类型、Flash 或未支持卸载时为 -1。
 * @note SDK 失败时仍会把本地状态写成 ONLINE，不能据该状态判断卸载成功或文件资源已释放。消费者必须核对返回值及自身资源完成。
 */
int d_storage_unmount(d_storage_type_t type);

/**
 * @brief 查询历史空闲容量字段，当前尚未接入实际容量后端。
 * @param[in] type 待查询存储类型。
 * @return 当前为 0，既可能表示未知，也可能表示类型无效；不是容量耗尽证明。
 */
unsigned long d_storage_get_free_space(d_storage_type_t type);

/**
 * @brief 利用 SDK 写文件时自动建目录的行为，创建并删除目录下的 .tmp 文件。
 * @param[in] path 目标目录路径，不保存指针。
 * @return 0 为临时文件创建成功；路径为空或创建失败时为 -1。
 * @note 会写介质；内部固定路径缓冲区为 128 字节，调用方须保证路径可容纳。当前不检查临时文件删除结果。
 */
int d_storage_mkdir(const char *path);

/**
 * @brief 尝试只读打开文件，失败后再尝试打开目录。
 * @param[in] path 待查询的文件或目录路径。
 * @return 任一种打开成功为 1；路径为空或均打不开为 0，不细分不存在与 IO 错误。
 */
int d_storage_exists(const char *path);

#endif // D_STORAGE_H
