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

//*********************************************************************************//
//                              内部函数                                           //
//*********************************************************************************//

/**
 * @brief 检查设备是否在线
 */
static int check_device_online(const char *dev_name)
{
    if (!dev_name) {
        return -1;
    }
    
    return dev_online(dev_name) ? 0 : -1;
}

/**
 * @brief 更新存储设备状态
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

int d_storage_device_ready(void)
{
    update_storage_status(current_storage);
    
    if (storage_info_table[current_storage].status == D_STORAGE_MOUNTED) {
        return 0;
    }
    return -1;
}

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

const char* d_storage_get_root_path(void)
{
    if (storage_info_table[current_storage].status == D_STORAGE_MOUNTED) {
        return storage_info_table[current_storage].root_path;
    }
    
    // 回退到Flash
    return CONFIG_MUSIC_PATH_FLASH;
}

const char* d_storage_get_music_path(void)
{
    return d_storage_get_root_path();
}

const char* d_storage_get_rec_path(void)
{
    return CONFIG_REC_PATH_0;
}

const char* d_storage_get_upgrade_path(void)
{
    if (current_storage == D_STORAGE_SD0 || current_storage == D_STORAGE_SD1) {
        return CONFIG_UPGRADE_PATH;
    } else if (current_storage == D_STORAGE_UDISK0 || current_storage == D_STORAGE_UDISK1) {
        return CONFIG_UDISK_UPGRADE_PATH;
    }
    return CONFIG_UPGRADE_PATH;
}

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

unsigned long d_storage_get_free_space(d_storage_type_t type)
{
    d_storage_info_t info;
    
    if (d_storage_get_info(type, &info) == 0) {
        return info.free_size;
    }
    
    return 0;
}

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
