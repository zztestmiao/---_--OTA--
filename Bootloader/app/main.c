/* 先按持久状态启动；失败留在引导区，可显式登记已由ST-Link部署的新基线。 */
#include "stm32f4xx.h"
#include "boot_board.h"
#include "boot_jump.h"
#include "boot_state.h"
#include "boot_control.h"
#include "boot_baseline.h"
#include "led.h"
#include "board.h"
int main(void)
{
    boot_board_init();
    boot_board_show_startup();
    boot_state_probe();
    /* 不再调用旧内嵌演示包写入和首次seed接口，避免误擦外部包或状态。 */
    if (boot_control_process())
        boot_jump_to_app();
    led_on(&led3);
    while (1)
    {
        boot_baseline_poll();
    }
}
