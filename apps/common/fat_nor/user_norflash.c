/**
 * @file    user_norflash.c
 * @brief   使用通用SPI口操作NORFLASH的驱动代码，可接入JLFAT文件系统。
 * @version V1.0
 * @date    2026-03-11
 * @notice  1.驱动基于华邦W25Q256调试，其它型号或厂商的flash可能有区别，需要
 *          根据实际情况修改。
 *          2.通用SPI口操作Flash，如果外接杜邦线测试，可能需要手动设置CLK强驱
 *          3.程序末尾有测试例程，使用例程时在板级添加配置参数。
 */

#include "app_config.h"
#include "fs/fs.h"
#include "asm/spi.h"
#include "gpio.h"
#include "device.h"


#ifdef TCFG_USER_NORFLASH_ENABLE

/* user_norflash测试例程使能 */
#define USER_NORFLASH_TEST_CODE_ENABLE  1

/* 设置CLK强驱(如果外接杜邦线测试，可能需要手动设置强驱) */
#define USER_NORFLASH_SPI_CLK_PIN   IO_PORTB_06


/* 配置读norflash使用的线宽 */
// notice: A.791N最高支持2线，不支持4线
//         B.旧版SPI驱动不支持临时切换模式读，无法flash dual读(V1.2.13才开始支持)
#define NORFLASH_READ_MODE          0 // 0-normal读，1-dual读。


/* 配置擦除norflash使用的命令(不同命令擦除大小不一样) */
#define NORFLASH_ERASE_MODE         0 // 0-sector erase 4K
//                                       1-block  erase 32K
//                                       2-block  erase 64K



#if NORFLASH_ERASE_MODE == 0
#define NORFLASH_ERASE_SIZE  (4 * 1024)
#define NORFLASH_ERASE_CMD  WINBOND_SECTOR_ERASE
#elif NORFLASH_ERASE_MODE == 1
#define NORFLASH_ERASE_SIZE  (32 * 1024)
#define NORFLASH_ERASE_CMD  WINBOND_BLOCK_ERASE_32K
#elif NORFLASH_ERASE_MODE == 2
#define NORFLASH_ERASE_SIZE  (64 * 1024)
#define NORFLASH_ERASE_CMD  WINBOND_BLOCK_ERASE_64K
#else
#error "please config NORFLASH_ERASE_MODE right!!"
#endif


// user_norflash外部参数配置结构体
struct user_norflash_config {
    char *spi_port;
    u32 cs_pin;
};

// 驱动内部参数结构体
struct user_norflash_info {
    u32 flash_capacity;
    u32 flash_id;
    u8 flash_uuid[16];
    u8 *internal_buf;
    void *spi_hdl;
    struct user_norflash_config cfg;
};

// norflash命令
#define WINBOND_WRITE_ENABLE               0x06
// #define WINBOND_WRITE_DISABLE              0x04
#define WINBOND_READ_SR1                   0x05
// #define WINBOND_READ_SR2                   0x35
// #define WINBOND_READ_SR3                   0x15
// #define WINBOND_WRITE_SR1                  0x01
// #define WINBOND_WRITE_SR2                  0x31
// #define WINBOND_WRITE_SR3                  0x11
#define WINBOND_READ_DATA                  0x03
#define WINBOND_FAST_READ                  0x0B
#define WINBOND_FAST_READ_DUAL_OUTPUT      0x3B
// #define WINBOND_FAST_READ_QUAD_OUTPUT      0x6B
#define WINBOND_PAGE_PROGRAM               0x02
// #define WINBOND_PAGE_PROGRAM_QUAD          0x32
#define WINBOND_SECTOR_ERASE               0x20
#define WINBOND_BLOCK_ERASE_32K            0x52
#define WINBOND_BLOCK_ERASE_64K            0xD8
#define WINBOND_JEDEC_ID                   0x9F
#define WINBOND_GET_UNIQUE_ID              0x4B
#define WINBOND_ENTER_4BYTE_ADDR           0xB7
#define WINBOND_EXIT_4BYTE_ADDR            0xE9

#define FAT_BLOCK_SIZE                     (512) // FAT文件系统块大小(固定值)
#define NORFLASH_PROGRAM_SIZE              (256) // NORFLASH一次写入的大小(固定值)

static struct user_norflash_info norflash_info;
static struct device user_norflash_dev;

static void user_norflash_cs(u32 cs_pin, u8 status)
{
    if (status) {
        gpio_direction_output(cs_pin, 1);
    } else {
        gpio_direction_output(cs_pin, 0);
    }
}

static int user_norflash_wait_finish(void *spi_hdl, u32 cs_pin)
{
    volatile u32 timeout = 5000000;
    u8 sr;
    while (timeout) {
        user_norflash_cs(cs_pin, 0);
        dev_ioctl(spi_hdl, IOCTL_SPI_SEND_BYTE, WINBOND_READ_SR1);
        dev_ioctl(spi_hdl, IOCTL_SPI_READ_BYTE, (u32)&sr);
        user_norflash_cs(cs_pin, 1);
        if (!(sr & BIT(0))) {
            break;
        }
        timeout --;
        os_time_dly(1);
    }
    return 0;
}

static void user_norflash_send_addr(struct user_norflash_info *info, u32 addr)
{
    if (info->flash_capacity > 16 * 1024 * 1024) {
        dev_ioctl(info->spi_hdl, IOCTL_SPI_SEND_BYTE, addr >> 24);
    }
    dev_ioctl(info->spi_hdl, IOCTL_SPI_SEND_BYTE, addr >> 16);
    dev_ioctl(info->spi_hdl, IOCTL_SPI_SEND_BYTE, addr >> 8);
    dev_ioctl(info->spi_hdl, IOCTL_SPI_SEND_BYTE, addr);
}

static void spiflash_send_write_enable(void *spi_hdl, u32 cs_pin)
{
    user_norflash_cs(cs_pin, 0);
    dev_ioctl(spi_hdl, IOCTL_SPI_SEND_BYTE, WINBOND_WRITE_ENABLE);
    user_norflash_cs(cs_pin, 1);
}

static int user_norflash_dev_init(const struct dev_node *node, void *arg)
{
    memcpy(&norflash_info.cfg, arg, sizeof(struct user_norflash_config));

    user_norflash_cs(norflash_info.cfg.cs_pin, 1);

    return 0;
}

static int user_norflash_dev_open(const char *name, struct device **device, void *arg)
{
    if (!norflash_info.cfg.spi_port) {
        printf("[user_norflash]error:please select a spi port!!!\n");
        return -1;
    }

    // 开启spi通信口
    norflash_info.spi_hdl = dev_open(norflash_info.cfg.spi_port, NULL);
    if (!norflash_info.spi_hdl) {
        printf("[user_norflash]error: open %s fail\n", norflash_info.cfg.spi_port);
        return -1;
    }
    printf("[user_norflash]open %s success\n", norflash_info.cfg.spi_port);
    dev_ioctl(norflash_info.spi_hdl, IOCTL_SPI_SET_USE_SEM, 0); // 使用信号量
#ifdef USER_NORFLASH_SPI_CLK_PIN
    gpio_set_hd(USER_NORFLASH_SPI_CLK_PIN, 1);
#endif

    // 非对齐写操作时，回读备份flash数据的BUF
    norflash_info.internal_buf = malloc(NORFLASH_ERASE_SIZE);
    if (!norflash_info.internal_buf) {
        printf("[user_norflash]error: malloc %d fail\n", NORFLASH_ERASE_SIZE);
        dev_close(norflash_info.spi_hdl);
    }

    u8 temp;
    u32 i;
    // 获取flash的JEDEC ID
    norflash_info.flash_id = 0;
    user_norflash_cs(norflash_info.cfg.cs_pin, 0);
    dev_ioctl(norflash_info.spi_hdl, IOCTL_SPI_SEND_BYTE, WINBOND_JEDEC_ID);
    for (i = 0; i < 3; i++) {
        dev_ioctl(norflash_info.spi_hdl, IOCTL_SPI_READ_BYTE, (u32)&temp);
        norflash_info.flash_id |= temp;
        norflash_info.flash_id <<= 8;
    }
    user_norflash_cs(norflash_info.cfg.cs_pin, 1);
    norflash_info.flash_id >>= 8;
    printf("[user_norflash]flash id: 0x%x\n", norflash_info.flash_id);

    // 获取flash的UUID ID
    user_norflash_cs(norflash_info.cfg.cs_pin, 0);
    dev_ioctl(norflash_info.spi_hdl, IOCTL_SPI_SEND_BYTE, WINBOND_GET_UNIQUE_ID);
    dev_ioctl(norflash_info.spi_hdl, IOCTL_SPI_SEND_BYTE, 0); //dummy
    dev_ioctl(norflash_info.spi_hdl, IOCTL_SPI_SEND_BYTE, 0); //dummy
    dev_ioctl(norflash_info.spi_hdl, IOCTL_SPI_SEND_BYTE, 0); //dummy
    dev_ioctl(norflash_info.spi_hdl, IOCTL_SPI_SEND_BYTE, 0); //dummy
    for (i = 0; i < 16; i ++) {
        dev_ioctl(norflash_info.spi_hdl, IOCTL_SPI_READ_BYTE, (u32)&temp);
        norflash_info.flash_uuid[i] = temp;
    }
    user_norflash_cs(norflash_info.cfg.cs_pin, 1);
    u8 *p = norflash_info.flash_uuid;
    printf("[user_norflash]flash UUID: %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x\n", \
           p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], \
           p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);

    // 计算Flash的容量
    u32 capacity_id = norflash_info.flash_id & 0xff;
    if (capacity_id >= 0x30) {
        capacity_id = capacity_id - 0x30 + 0x10;
    } else if (capacity_id >= 0x20) {
        capacity_id = capacity_id - 0x20 + 0x1a;
    }
    norflash_info.flash_capacity = 64 * 1024 << (capacity_id - 0x10);
    printf("[user_norflash]flash capacity = %dMB\n", norflash_info.flash_capacity / 1024 / 1024);


    // 容量大于16M的flash需要设置4byte地址访问
    if (norflash_info.flash_capacity > 16 * 1024 * 1024) {
        user_norflash_cs(norflash_info.cfg.cs_pin, 0);
        dev_ioctl(norflash_info.spi_hdl, IOCTL_SPI_SEND_BYTE, WINBOND_ENTER_4BYTE_ADDR);
        user_norflash_cs(norflash_info.cfg.cs_pin, 1);
    }

    user_norflash_dev.private_data = (void *)&norflash_info;
    *device = &user_norflash_dev;

    return 0;
}

static int user_norflash_dev_close(struct device *device)
{
    struct user_norflash_info *info = (struct user_norflash_info *)device->private_data;

    if (info->spi_hdl) {
        dev_close(info->spi_hdl);
        info->spi_hdl = NULL;
    }

    if (info->internal_buf) {
        free(info->internal_buf);
        info->internal_buf = NULL;
    }

    return 0;
}

static int user_norflash_erase(struct user_norflash_info *info, u32 addr)
{
    spiflash_send_write_enable(info->spi_hdl, info->cfg.cs_pin);

    user_norflash_cs(info->cfg.cs_pin, 0);
    dev_ioctl(info->spi_hdl, IOCTL_SPI_SEND_BYTE, NORFLASH_ERASE_CMD);
    user_norflash_send_addr(info, addr);
    user_norflash_cs(info->cfg.cs_pin, 1);

    user_norflash_wait_finish(info->spi_hdl, info->cfg.cs_pin);
    return 0;
}

static int user_norflash_read(struct user_norflash_info *info, void *buf, u32 len, u32 addr)
{
    /* printf("r_addr = %d", addr); */
    /* printf("r_size = %d\n", len); */
    user_norflash_cs(info->cfg.cs_pin, 0);

#if NORFLASH_READ_MODE == 0 // noraml read
    dev_ioctl(info->spi_hdl, IOCTL_SPI_SEND_BYTE, WINBOND_READ_DATA);
    user_norflash_send_addr(info, addr);
    int ret = dev_read(info->spi_hdl, buf, len);
#elif NORFLASH_READ_MODE == 1 // dual read
    dev_ioctl(info->spi_hdl, IOCTL_SPI_SEND_BYTE, WINBOND_FAST_READ_DUAL_OUTPUT);
    user_norflash_send_addr(info, addr);
    dev_ioctl(info->spi_hdl, IOCTL_SPI_SEND_BYTE, 0); //dummy
    struct spi_2bit_data temp;
    temp.buf = (u8 *)buf;
    temp.len = len;
    int ret = dev_ioctl(info->spi_hdl, IOCTL_SPI_READ_PACKAGE_DUAL_MODE, &temp);
#else
#error "please config NORFLASH_READ_MODE right!!"
#endif

    user_norflash_cs(info->cfg.cs_pin, 1);
    return ret == 0 ? len : ret;
}

static int user_norflash_write(struct user_norflash_info *info, void *buf, u32 len, u32 addr)
{
    u32 already_write = 0;
    u32 cur_wr_len;
    u8 *p_buf = buf;
    int ret;
    while (already_write < len) {
        cur_wr_len = ((len - already_write) >= NORFLASH_PROGRAM_SIZE) ? \
                     NORFLASH_PROGRAM_SIZE : (len - already_write);

        spiflash_send_write_enable(info->spi_hdl, info->cfg.cs_pin);

        user_norflash_cs(info->cfg.cs_pin, 0);
        dev_ioctl(info->spi_hdl, IOCTL_SPI_SEND_BYTE, WINBOND_PAGE_PROGRAM);
        user_norflash_send_addr(info, addr + already_write);
        ret = dev_write(info->spi_hdl, &p_buf[already_write], cur_wr_len);
        user_norflash_cs(info->cfg.cs_pin, 1);

        user_norflash_wait_finish(info->spi_hdl, info->cfg.cs_pin);

        already_write += cur_wr_len;
    }
    return ret;
}

static int user_norflash_dev_read(struct device *device, void *buf, u32 len, u32 offset)
{
    struct user_norflash_info *info = (struct user_norflash_info *)device->private_data;
    if (!info->spi_hdl) {
        printf("[user_norflash]error: spi is not opened!\n");
        return -1;
    }

    u32 addr = offset * FAT_BLOCK_SIZE;
    len *= FAT_BLOCK_SIZE;
    int rlen = user_norflash_read(info, buf, len, addr) ;
    return (rlen / FAT_BLOCK_SIZE);
}

static int user_norflash_dev_write(struct device *device, void *buf, u32 len, u32 offset)
{
    struct user_norflash_info *info = (struct user_norflash_info *)device->private_data;
    if (!info->spi_hdl) {
        printf("[user_norflash]error: spi is not opened!\n");
        return -1;
    }

    u8 *pdata = (u8 *)buf;
    u32 addr = offset * FAT_BLOCK_SIZE;
    u32 size = len * FAT_BLOCK_SIZE;

    // printf("w_addr = %d", addr);
    // printf("w_size = %d", size);
    // put_buf(pdata, size);

    u32 beginBlock = (addr / NORFLASH_ERASE_SIZE);
    u32 endBlock = (((addr + size) / NORFLASH_ERASE_SIZE));

    u32 already_write = 0;
    u32 length = NORFLASH_ERASE_SIZE - (addr % NORFLASH_ERASE_SIZE);

    user_norflash_read(info, info->internal_buf, NORFLASH_ERASE_SIZE, beginBlock * NORFLASH_ERASE_SIZE) ;
    // put_buf(info->internal_buf, NORFLASH_ERASE_SIZE);
    if (length >= size) {
        memcpy(info->internal_buf + (addr % NORFLASH_ERASE_SIZE), pdata, size);
        user_norflash_erase(info, beginBlock * NORFLASH_ERASE_SIZE);
        user_norflash_write(info, info->internal_buf, NORFLASH_ERASE_SIZE, beginBlock * NORFLASH_ERASE_SIZE);
        return len;
    } else {
        memcpy(info->internal_buf + (addr % NORFLASH_ERASE_SIZE), pdata, length);
        user_norflash_erase(info, beginBlock * NORFLASH_ERASE_SIZE);
        user_norflash_write(info, info->internal_buf, NORFLASH_ERASE_SIZE, beginBlock * NORFLASH_ERASE_SIZE);
        already_write += length;
    }
    for (int i = beginBlock + 1; i < endBlock; i++) {
        user_norflash_erase(info, i * NORFLASH_ERASE_SIZE);
        user_norflash_write(info, pdata + already_write, NORFLASH_ERASE_SIZE, i * NORFLASH_ERASE_SIZE);
        already_write += NORFLASH_ERASE_SIZE;
    }
    if (beginBlock != endBlock && (size > already_write)) {
        user_norflash_read(info, info->internal_buf, NORFLASH_ERASE_SIZE, endBlock * NORFLASH_ERASE_SIZE) ;
        memcpy(info->internal_buf, pdata + already_write, size - already_write);
        user_norflash_erase(info, endBlock * NORFLASH_ERASE_SIZE);
        user_norflash_write(info, info->internal_buf, NORFLASH_ERASE_SIZE, endBlock * NORFLASH_ERASE_SIZE);
    }

    return len;
}

static int user_norflash_dev_ioctl(struct device *device, u32 cmd, u32 arg)
{
    struct user_norflash_info *info = (struct user_norflash_info *)device->private_data;
    int err = 0;

    switch (cmd) {
    case IOCTL_GET_ID:
        break;

    case IOCTL_GET_SECTOR_SIZE:
        break;

    case IOCTL_GET_BLOCK_SIZE:
        *((u32 *)arg) = FAT_BLOCK_SIZE;
        break;

    case IOCTL_GET_CAPACITY:
        *((u32 *)arg) = (info->flash_capacity) / FAT_BLOCK_SIZE;
        break;

    case IOCTL_GET_UNIQUE_ID:
        break;

    case IOCTL_ERASE_BLOCK:
        break;

    case IOCTL_ERASE_CHIP:
        break;

    case IOCTL_SET_WRITE_PROTECT:
        break;

    case IOCTL_GET_BLOCK_NUMBER:
        *((u32 *)arg) = (info->flash_capacity) / FAT_BLOCK_SIZE;
        break;

    case IOCTL_GET_STATUS:
        *(u32 *)arg = 1;
        break;

    case IOCTL_CMD_RESUME:
        break;

    case IOCTL_CMD_SUSPEND:
        break;

    default:
        err = -EINVAL;
        break;
    }

    return err;
}

const struct device_operations user_norflash_dev_ops = {
    .init   = user_norflash_dev_init,
    .open   = user_norflash_dev_open,
    .read   = user_norflash_dev_read,
    .write  = user_norflash_dev_write,
    .ioctl  = user_norflash_dev_ioctl,
    .close  = user_norflash_dev_close,
};



#if USER_NORFLASH_TEST_CODE_ENABLE


// 板级配置参考
/* 1. SPI板级配置 */
// SPI1_PLATFORM_DATA_BEGIN(spi1_data)
//     .clk    = 10000000,
//     .mode   = SPI_STD_MODE, //只能使用SPI_STD_MODE
//     .port   = 'A',
//     .attr   = (SPI_SCLK_L_UPL_SMPH | SPI_BIDIR_MODE),
// SPI1_PLATFORM_DATA_END()

/* 2. USER_NORFLASH板级配置 */
// struct user_norflash_config {
//     char *spi_port;
//     u32 cs_pin;
// };
// static struct user_norflash_config user_norflash_cfg = {
//     .spi_port     = "spi1",
//     .cs_pin       = IO_PORTA_10,
// };

/* 3. 注册设备 */
// extern const struct device_operations user_norflash_dev_ops;
// REGISTER_DEVICES(device_table) = {
// 	{"spi1", &spi_dev_ops, (void *)&spi1_data },
//     {"user_norflash",  &user_norflash_dev_ops, (void *)&user_norflash_cfg},
// };


#define FILE_SYSTEM_NAME "jlfat"
#define FAT_CACHE_NUM     5

#include "init.h"
void user_norflash_mount_to_fs_test(void *val)
{
    FILE *fp;
    u8 buf[] = "user_norflash write test";

    int err = 0;
    err = f_format("user_norflash", FILE_SYSTEM_NAME, 4 * 1024);
    printf("format err : %d\n", err);

    if (err == 0 && mount("user_norflash", "mnt/user_norflash", \
                          FILE_SYSTEM_NAME, FAT_CACHE_NUM, NULL)) {
        printf("user_norflash mount succ\n");
        fp = fopen("mnt/user_norflash/C/test.txt", "w+");
        if (fp) {
            printf("fopen successful\n");
            fwrite(buf, sizeof(buf), 1, fp);
            fclose(fp);

            memset(buf, 0, sizeof(buf));
            fp = fopen("mnt/user_norflash/C/test.txt", "r");
            fread(buf, sizeof(buf), 1, fp);
            printf("test.txt : %s\n", buf);
            fclose(fp);
        }
    } else {
        printf("user_norflash mount failed!!!\n");
    }
}

static int demo_user_norflash(void)
{
    return thread_fork("user_norflash_mount_to_fs_test", 10, 1000, 0, NULL, user_norflash_mount_to_fs_test, NULL);
}

late_initcall(demo_user_norflash);
#endif


#endif
