#include "boot_baseline.h"
#include "network_baseline.h"
#include "boot_state.h"
#include "boot_crc.h"
#include "boot_layout.h"
#include "led.h"
#include "board.h"
#include <stdbool.h>
#include <stdint.h>

volatile uint32_t g_baseline_request;
/* 0未请求，1提交并读回成功等待复位，2原状态非NORMAL，3App指纹不符，
 * 4提交或读回失败，5执行中。失败不自动重试，保留另一槽。 */
volatile uint32_t g_baseline_result;
volatile uint32_t g_baseline_actual_crc;
void boot_baseline_poll(void)
{
    static bool attempted;
    boot_state_info info, check;
    if (g_baseline_request != 1u) return;
    g_baseline_request = 0u;
    if (attempted) return;
    attempted = true;
    g_baseline_result = 5u;
    /* READY/INSTALLING禁止改为NORMAL，不能用迁移接口绕过待完成安装。 */
    if (boot_state_read(&info) != BOOT_STATE_VALID || info.state != BOOT_STATE_NORMAL)
    {
        g_baseline_result = 2u;
        return;
    }
    g_baseline_actual_crc = boot_crc32((const uint8_t *)BOOT_APP_BASE, NETWORK_BASELINE_SIZE);
    if (NETWORK_BASELINE_SIZE > 0x10000u || g_baseline_actual_crc != NETWORK_BASELINE_CRC)
    {
        g_baseline_result = 3u;
        return;
    }
    info.state = BOOT_STATE_NORMAL;
    info.version = 1u; /* 网络下载器教学基线；后续升级目标是LED V2。 */
    info.image_size = NETWORK_BASELINE_SIZE;
    info.image_crc = NETWORK_BASELINE_CRC;
    info.package_size = 0u; /* ST-Link部署，没有对应的外部下载包。 */
    info.package_crc = 0u;
    if (!boot_state_commit(&info, false) || boot_state_read(&check) != BOOT_STATE_VALID ||
        check.state != BOOT_STATE_NORMAL || check.image_size != info.image_size ||
        check.image_crc != info.image_crc || check.version != 1u)
    {
        g_baseline_result = 4u;
        return;
    }
    boot_state_probe();
    g_baseline_result = 1u;
    led_off(&led3);
    led_on(&led2);
    /* 留在Bootloader供Watch检查；用户复位后按NORMAL校验并启动新App。 */
}
