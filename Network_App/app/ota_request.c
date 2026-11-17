#include "ota_request.h"
#include "http_package_test.h"
#include "boot_state.h"
#include "boot_crc.h"
#include "w25q.h"
#include "console.h"
#include "stm32f4xx.h"
#include <stdbool.h>

volatile uint32_t g_ota_request;
volatile uint32_t g_ota_reset_request;
/* 0未请求，1 READY已持久提交等待复位，2下载包无效，3源包不符，
 * 4当前NORMAL/App不符，5提交或读回失败；失败不重试。 */
volatile uint32_t g_ota_result;
static uint8_t ota_readback[0xA00];
static uint32_t ready_sequence;
static bool ready;
static uint32_t little_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void ota_request_poll(void)
{
    static bool attempted;
    const uint8_t *data;
    uint32_t length, id, i;
    boot_state_info info, check;
    /* READY已持久化，真正复位前再核对记录，避免使用过期的RAM标志。 */
    if (g_ota_reset_request == 1u)
    {
        g_ota_reset_request = 0u;
        if (ready && boot_state_read(&check) == BOOT_STATE_VALID &&
            check.state == BOOT_STATE_READY && check.sequence == ready_sequence)
        {
            console_write("[OTA] Reset -> Bootloader installs LED V2\r\n");
            __DSB();
            NVIC_SystemReset();
        }
        else
            console_write("[OTA] Reset refused: no confirmed READY\r\n");
    }
    if (g_ota_request != 1u) return;
    g_ota_request = 0u;
    if (attempted) return;
    attempted = true;
    if (!http_package_test_view(&data, &length) || length > sizeof(ota_readback) ||
        length < 40u || little_u32(data + 8) != 2u)
    {
        g_ota_result = 2u;
        goto fail;
    }
    w25q_init();
    if (!w25q_read_jedec_id(1u, &id) || id != 0xEF4018u ||
        !w25q_wait_ready(1u, 100000u) ||
        !w25q_read_data(1u, 0xFFF000u, ota_readback, length))
    {
        g_ota_result = 3u;
        goto fail;
    }
    /* 不依赖此前暂存测试的RAM结果，提交前重新比较外部完整包。 */
    for (i = 0u; i < length; ++i)
        if (ota_readback[i] != data[i])
        {
            g_ota_result = 3u;
            goto fail;
        }
    if (boot_state_read(&info) != BOOT_STATE_VALID || info.state != BOOT_STATE_NORMAL ||
        info.image_size < 8u || info.image_size > 0x10000u ||
        boot_crc32((const uint8_t *)0x08010000u, info.image_size) != info.image_crc)
    {
        g_ota_result = 4u;
        goto fail;
    }
    info.state = BOOT_STATE_READY;
    info.version = little_u32(data + 8);
    info.image_size = little_u32(data + 16);
    info.image_crc = little_u32(data + 20);
    info.package_size = length;
    info.package_crc = boot_crc32(data, length);
    /* 复用Bootloader同一双槽协议；最后写提交字，不另造一份状态格式。 */
    if (!boot_state_commit(&info, false) || boot_state_read(&check) != BOOT_STATE_VALID ||
        check.state != BOOT_STATE_READY || check.version != info.version ||
        check.image_size != info.image_size || check.image_crc != info.image_crc ||
        check.package_size != length || check.package_crc != info.package_crc)
    {
        g_ota_result = 5u;
        goto fail;
    }
    ready_sequence = check.sequence;
    ready = true;
    g_ota_result = 1u;
    boot_state_probe();
    console_write("[PASS] OTA READY committed; set g_ota_reset_request=1 to install\r\n");
    return;
fail:
    /* 提交失败不代表没有生效；保留现场，不擦状态也不盲目重试。 */
    console_write("[FAIL] OTA: inspect g_ota_result, no retry\r\n");
}
