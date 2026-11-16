/**
 * @file d_storage.c
 * @brief 实现存储状态查询与 SDK 文件系统适配，不新增介质管理任务。
 */

//*********************************************************************************//
//                              D_Code 存储设备管理                                //
//                                                                                 //
//  提供SD卡、U盘等存储设备的统一管理接口                                          //
//*********************************************************************************//

#include "d_storage.h"
#include "system/includes.h"
#include "fs/fs.h"
#include "device/device.h"
#include "os/os_api.h"
#include "storage_device.h"
#include "system/spinlock.h"
#include <string.h>
#include <sys/stat.h>  /* mkdir函数声明 */
// #include <stdio.h>  // 与SDK的fs.h冲突，SDK已有printf

//*********************************************************************************//
//                              内部变量定义                                       //
//*********************************************************************************//

// 存储设备信息表
static d_storage_info_t storage_info_table[D_STORAGE_MAX] = {
    [D_STORAGE_SD0] = {
        .type = D_STORAGE_SD0,
        .status = D_STORAGE_OFFLINE,
        .dev_name = "sd0",
        .root_path = CONFIG_MUSIC_PATH_SD0,
    },
    [D_STORAGE_SD1] = {
        .type = D_STORAGE_SD1,
        .status = D_STORAGE_OFFLINE,
        .dev_name = "sd1",
        .root_path = CONFIG_MUSIC_PATH_SD1,
    },
    [D_STORAGE_UDISK0] = {
        .type = D_STORAGE_UDISK0,
        .status = D_STORAGE_OFFLINE,
        .dev_name = "udisk0",
        .root_path = CONFIG_MUSIC_PATH_UDISK0,
    },
    [D_STORAGE_UDISK1] = {
        .type = D_STORAGE_UDISK1,
        .status = D_STORAGE_OFFLINE,
        .dev_name = "udisk1",
        .root_path = CONFIG_MUSIC_PATH_UDISK1,
    },
    [D_STORAGE_FLASH] = {
        .type = D_STORAGE_FLASH,
        .status = D_STORAGE_MOUNTED,  // 沿用 SDK 已挂载假设；这里不检查路径可读性
        .dev_name = "flash",
        .root_path = CONFIG_MUSIC_PATH_FLASH,
    },
};

// 当前活动的存储设备
static d_storage_type_t current_storage = D_STORAGE_SD0;

static DEFINE_SPINLOCK(s_sd_state_lock);
static d_storage_sd_snapshot_t s_sd_state[2];

/**
 * @brief 在 SDK 插拔通知到来时立即更新 SD 介质代次及在线事实。 完整合同见 d_storage.h。
 */
void d_storage_sd_notify(uint8_t id, uint8_t online)
{
    if (id >= 2U) return;
    spin_lock(&s_sd_state_lock);
    if (++s_sd_state[id].generation == 0U) ++s_sd_state[id].generation;
    s_sd_state[id].online = online != 0U;
    s_sd_state[id].mounted = 0U;
    spin_unlock(&s_sd_state_lock);
}

/**
 * @brief 查询 SD 设备在线及挂载事实，并复制带代次的值类型快照。 完整合同见 d_storage.h。
 */
d_err_t d_storage_sd_snapshot(uint8_t id, d_storage_sd_snapshot_t *snapshot)
{
    uint8_t online, mounted;
    uint32_t generation;
    if (id >= 2U || snapshot == NULL) return D_ERR_INVALID_ARG;
    spin_lock(&s_sd_state_lock);
    generation = s_sd_state[id].generation;
    spin_unlock(&s_sd_state_lock);
    online = (id == 0U ? TCFG_SD0_ENABLE : TCFG_SD1_ENABLE) &&
        dev_online(id == 0U ? "sd0" : "sd1");
    mounted = online && fmount_exist(id == 0U ? "storage/sd0" : "storage/sd1");
    spin_lock(&s_sd_state_lock);
    /* SDK 探测期间发生插拔时，保留更新后的通知事实，不能被旧探测结果覆盖。 */
    if (generation == s_sd_state[id].generation) {
        if (online != s_sd_state[id].online || !generation) {
            if (++s_sd_state[id].generation == 0U) ++s_sd_state[id].generation;
        }
        s_sd_state[id].online = online;
        s_sd_state[id].mounted = mounted;
    }
    *snapshot = s_sd_state[id];
    spin_unlock(&s_sd_state_lock);
    return D_OK;
}

/**
 * @brief 检查 SD 代次与设备在线状态是否仍匹配。 完整合同见 d_storage.h。
 */
uint8_t d_storage_sd_matches(uint8_t id, uint32_t generation)
{
    uint8_t matches;
    if (id >= 2U) return 0U;
    spin_lock(&s_sd_state_lock);
    matches = generation == s_sd_state[id].generation && s_sd_state[id].online;
    spin_unlock(&s_sd_state_lock);
    if (!matches || !dev_online(id == 0U ? "sd0" : "sd1")) return 0U;
    /* SDK 设备探测不占状态锁；期间可能换卡，因此回锁后再次核对代次。 */
    spin_lock(&s_sd_state_lock);
    matches = generation == s_sd_state[id].generation && s_sd_state[id].online;
    spin_unlock(&s_sd_state_lock);
    return matches;
}

/**
 * @brief 在工作任务中沿 SDK 串行路径准备 SD，并复核介质未变化。 完整合同见 d_storage.h。
 */
d_err_t d_storage_sd_prepare(uint8_t id, d_storage_sd_snapshot_t *snapshot)
{
    d_err_t result = d_storage_sd_snapshot(id, snapshot);
    uint32_t generation;
    if (result != D_OK) return result;
    if (!snapshot->online) return D_ERR_NOT_READY;
    generation = snapshot->generation;
    if (!sdcard_storage_device_ready(id == 0U ? "sd0" : "sd1")) return D_ERR_IO;
    result = d_storage_sd_snapshot(id, snapshot);
    if (result != D_OK) return result;
    return snapshot->mounted && generation == snapshot->generation ? D_OK : D_ERR_NOT_READY;
}

//*********************************************************************************//
//                              内部函数                                           //
//*********************************************************************************//

/**
 * @brief 使用 SDK 设备查询检查名称对应的设备是否在线。
 * @param[in] dev_name 借用的设备名称。
 * @return 在线为 0；空名称或离线为 -1。
 */
static int check_device_online(const char *dev_name)
{
    if (!dev_name) {
        return -1;
    }
    
    return dev_online(dev_name) ? 0 : -1;
}

/**
 * @brief 刷新静态存储表的在线及挂载状态，不主动挂载。
 * @param[in] type 存储类型；越界时直接返回。
 * @note Flash 继续沿用已挂载假设；未编译的 SD 或 U 盘能力被记为离线。旧状态表不提供跨线程互斥。
 */
static void update_storage_status(d_storage_type_t type)
{
    if (type < D_STORAGE_SD0 || type >= D_STORAGE_MAX) {
        return;
    }
    
    d_storage_info_t *info = &storage_info_table[type];
    
    // Flash 文件系统沿用 SDK 已挂载假设，不在此查询物理 NOR 状态。
    if (type == D_STORAGE_FLASH) {
        info->status = D_STORAGE_MOUNTED;
        return;
    }

#if !TCFG_SD0_ENABLE
    if (type == D_STORAGE_SD0) {
        info->status = D_STORAGE_OFFLINE;
        return;
    }
#endif
#if !TCFG_SD1_ENABLE
    if (type == D_STORAGE_SD1) {
        info->status = D_STORAGE_OFFLINE;
        return;
    }
#endif
#if !TCFG_UDISK_ENABLE
    if (type == D_STORAGE_UDISK0 || type == D_STORAGE_UDISK1) {
        info->status = D_STORAGE_OFFLINE;
        return;
    }
#endif

    const char *mount_path = NULL;
    switch (type) {
    case D_STORAGE_SD0:
        mount_path = "storage/sd0";
        break;
    case D_STORAGE_SD1:
        mount_path = "storage/sd1";
        break;
    case D_STORAGE_UDISK0:
        mount_path = "storage/udisk0";
        break;
    case D_STORAGE_UDISK1:
        mount_path = "storage/udisk1";
        break;
    default:
        break;
    }
    
    // 检查设备是否在线
    if (check_device_online(info->dev_name) == 0) {
        if (mount_path && fmount_exist(mount_path)) {
            info->status = D_STORAGE_MOUNTED;
        } else {
            info->status = D_STORAGE_ONLINE;
        }
    } else {
        info->status = D_STORAGE_OFFLINE;
    }
}

//*********************************************************************************//
//                              公共 API 实现                                      //
//*********************************************************************************//

/**
 * @brief 查询各设备状态，并按编译期开关选择默认活动存储。 完整合同见 d_storage.h。
 */
int d_storage_init(void)
{
    printf("[D_STORAGE] Storage module init\r\n");
    
    // 初始化各存储设备状态
    for (int i = 0; i < D_STORAGE_MAX; i++) {
        update_storage_status((d_storage_type_t)i);
    }
    
    // 设置默认存储设备
#if TCFG_SD0_ENABLE
    current_storage = D_STORAGE_SD0;
#elif TCFG_SD1_ENABLE
    current_storage = D_STORAGE_SD1;
#elif TCFG_UDISK_ENABLE
    current_storage = D_STORAGE_UDISK0;
#else
    current_storage = D_STORAGE_FLASH;
#endif
    
    printf("[D_STORAGE] Default storage: %s\r\n", 
           storage_info_table[current_storage].dev_name);
    
    return 0;
}

/**
 * @brief 刷新并检查当前活动存储是否被记录为已挂载。 完整合同见 d_storage.h。
 */
int d_storage_device_ready(void)
{
    update_storage_status(current_storage);
    
    if (storage_info_table[current_storage].status == D_STORAGE_MOUNTED) {
        return 0;
    }
    return -1;
}

/**
 * @brief 刷新并查询指定 SD 是否已挂载。 完整合同见 d_storage.h。
 */
int d_sdcard_ready(const char *sd_name)
{
    if (!sd_name) {
        return -1;
    }
    
    d_storage_type_t type;
    if (strcmp(sd_name, "sd0") == 0) {
        type = D_STORAGE_SD0;
    } else if (strcmp(sd_name, "sd1") == 0) {
        type = D_STORAGE_SD1;
    } else {
        return -1;
    }
    
    update_storage_status(type);
    
    if (storage_info_table[type].status == D_STORAGE_MOUNTED) {
        return 0;
    }
    return -1;
}

/**
 * @brief 刷新并查询指定 U 盘是否已挂载。 完整合同见 d_storage.h。
 */
int d_udisk_ready(int id)
{
    if (id < 0 || id > 1) {
        return -1;
    }
    
    d_storage_type_t type = (id == 0) ? D_STORAGE_UDISK0 : D_STORAGE_UDISK1;
    update_storage_status(type);
    
    if (storage_info_table[type].status == D_STORAGE_MOUNTED) {
        return 0;
    }
    return -1;
}

/**
 * @brief 同步请求 SDK 格式化给定 SD 路径。 完整合同见 d_storage.h。
 */
int d_sdcard_format(const char *sd_name)
{
    if (!sd_name) {
        return -1;
    }
    
    printf("[D_STORAGE] Format SD card: %s\r\n", sd_name);
    
    // 构建存储路径
    char path[64];
    snprintf(path, sizeof(path), "storage/%s", sd_name);
    
    // 调用文件系统格式化接口
    // 注意：实际使用时需要确认SDK中的格式化API
    int ret = -1;
#if defined(CONFIG_JLFAT_ENABLE)
    extern int fat_format(const char *path, int cluster_size);
    ret = fat_format(path, 32 * 1024);
#endif
    if (ret == 0) {
        printf("[D_STORAGE] Format success\r\n");
    } else {
        printf("[D_STORAGE] Format failed: %d\r\n", ret);
    }
    
    return ret;
}

/**
 * @brief 同步请求 SDK 格式化指定 U 盘。 完整合同见 d_storage.h。
 */
int d_udisk_format(int id)
{
    if (id < 0 || id > 1) {
        return -1;
    }
    
    printf("[D_STORAGE] Format U-disk: udisk%d\r\n", id);
    
    char path[64];
    snprintf(path, sizeof(path), "storage/udisk%d", id);
    
    int ret = -1;
#if defined(CONFIG_JLFAT_ENABLE)
    extern int fat_format(const char *path, int cluster_size);
    ret = fat_format(path, 32 * 1024);
#endif
    if (ret == 0) {
        printf("[D_STORAGE] Format success\r\n");
    } else {
        printf("[D_STORAGE] Format failed: %d\r\n", ret);
    }
    
    return ret;
}

/**
 * @brief 刷新设备状态并复制其基本信息。 完整合同见 d_storage.h。
 */
int d_storage_get_info(d_storage_type_t type, d_storage_info_t *info)
{
    if (type < D_STORAGE_SD0 || type >= D_STORAGE_MAX || !info) {
        return -1;
    }
    
    update_storage_status(type);
    
    // 复制基本信息
    memcpy(info, &storage_info_table[type], sizeof(d_storage_info_t));
    
    // 尚未接入设备容量后端；不把文件属性 fsize 当作总容量。
    info->total_size = 0;
    info->free_size = 0;
    
    return 0;
}

/**
 * @brief 取得当前记录为已挂载的活动存储根路径，否则回退到 Flash 配置路径。 完整合同见 d_storage.h。
 */
const char* d_storage_get_root_path(void)
{
    if (storage_info_table[current_storage].status == D_STORAGE_MOUNTED) {
        return storage_info_table[current_storage].root_path;
    }
    
    // 回退到Flash
    return CONFIG_MUSIC_PATH_FLASH;
}

/**
 * @brief 取得当前音乐根路径，沿用活动存储的路径选择。 完整合同见 d_storage.h。
 */
const char* d_storage_get_music_path(void)
{
    return d_storage_get_root_path();
}

/**
 * @brief 取得编译期录像输出路径。 完整合同见 d_storage.h。
 */
const char* d_storage_get_rec_path(void)
{
    return CONFIG_REC_PATH_0;
}

/**
 * @brief 按当前活动存储类型选择升级文件配置路径。 完整合同见 d_storage.h。
 */
const char* d_storage_get_upgrade_path(void)
{
    if (current_storage == D_STORAGE_SD0 || current_storage == D_STORAGE_SD1) {
        return CONFIG_UPGRADE_PATH;
    } else if (current_storage == D_STORAGE_UDISK0 || current_storage == D_STORAGE_UDISK1) {
        return CONFIG_UDISK_UPGRADE_PATH;
    }
    return CONFIG_UPGRADE_PATH;
}

/**
 * @brief 沿历史适配路径探测现有根目录，必要时尝试 SDK 挂载。 完整合同见 d_storage.h。
 */
int d_storage_mount(d_storage_type_t type)
{
    if (type < D_STORAGE_SD0 || type >= D_STORAGE_MAX) {
        return -1;
    }
    
    d_storage_info_t *info = &storage_info_table[type];
    
    // 本适配层不挂载 Flash 文件系统，仅沿用 SDK 已挂载假设。
    if (type == D_STORAGE_FLASH) {
        info->status = D_STORAGE_MOUNTED;
        return 0;
    }
    
    printf("[D_STORAGE] Mount device: %s\r\n", info->dev_name);
    
    // 尝试挂载 - SDK通常会自动挂载，这里只是更新状态
    char path[64];
    snprintf(path, sizeof(path), "storage/%s", info->dev_name);
    
    // 检查是否可以访问来判断是否已挂载
    FILE *fp = fopen(info->root_path, "r");
    if (fp) {
        fclose(fp);
        info->status = D_STORAGE_MOUNTED;
        printf("[D_STORAGE] Mount success (already mounted)\r\n");
        return 0;
    }
    
    // 尝试手动挂载（如果SDK支持）
    int ret = -1;
#if defined(CONFIG_JLFAT_ENABLE)
    extern int fat_mount(const char *dev_name, const char *path);
    ret = fat_mount(info->dev_name, path);
#endif
    
    if (ret == 0) {
        info->status = D_STORAGE_MOUNTED;
        printf("[D_STORAGE] Mount success\r\n");
    } else {
        printf("[D_STORAGE] Mount failed: %d\r\n", ret);
    }
    
    return ret;
}

/**
 * @brief 沿历史适配路径尝试卸载非 Flash 设备。 完整合同见 d_storage.h。
 */
int d_storage_unmount(d_storage_type_t type)
{
    if (type < D_STORAGE_SD0 || type >= D_STORAGE_MAX || type == D_STORAGE_FLASH) {
        return -1;
    }
    
    d_storage_info_t *info = &storage_info_table[type];
    
    printf("[D_STORAGE] Unmount device: %s\r\n", info->dev_name);
    
    char path[64];
    snprintf(path, sizeof(path), "storage/%s", info->dev_name);
    
    // 尝试卸载（如果SDK支持）
    int ret = -1;
#if defined(CONFIG_JLFAT_ENABLE)
    extern int fat_unmount(const char *path);
    ret = fat_unmount(path);
#endif
    
    if (ret == 0) {
        info->status = D_STORAGE_ONLINE;
        printf("[D_STORAGE] Unmount success\r\n");
    } else {
        // 即使卸载失败，也更新状态
        info->status = D_STORAGE_ONLINE;
        printf("[D_STORAGE] Unmount: %d\r\n", ret);
    }
    
    return ret;
}

/**
 * @brief 查询历史空闲容量字段，当前尚未接入实际容量后端。 完整合同见 d_storage.h。
 */
unsigned long d_storage_get_free_space(d_storage_type_t type)
{
    d_storage_info_t info;
    
    if (d_storage_get_info(type, &info) == 0) {
        return info.free_size;
    }
    
    return 0;
}

/**
 * @brief 利用 SDK 写文件时自动建目录的行为，创建并删除目录下的 .tmp 文件。 完整合同见 d_storage.h。
 */
int d_storage_mkdir(const char *path)
{
    if (!path) {
        return -1;
    }

    /*
     * SDK的fopen会自动创建目录，这里通过创建一个临时文件来实现目录创建
     * 如果目录已存在，此操作会成功；如果目录不存在，fopen会自动创建
     */
    char temp_file[128];
    snprintf(temp_file, sizeof(temp_file), "%s/.tmp", path);

    FILE *fp = fopen(temp_file, "w");
    if (fp) {
        fclose(fp);
        // 删除临时文件
        fdelete_by_name(temp_file);
        printf("[D_STORAGE] mkdir success: %s\r\n", path);
        return 0;
    }

    printf("[D_STORAGE] mkdir failed: %s\r\n", path);
    return -1;
}

/**
 * @brief 尝试只读打开文件，失败后再尝试打开目录。 完整合同见 d_storage.h。
 */
int d_storage_exists(const char *path)
{
    if (!path) {
        return 0;
    }
    
    FILE *fp = fopen(path, "r");
    if (fp) {
        fclose(fp);
        return 1;
    }
    
    // 尝试作为目录打开
    void *dir = opendir(path);
    if (dir) {
        closedir(dir);
        return 1;
    }
    
    return 0;
}
