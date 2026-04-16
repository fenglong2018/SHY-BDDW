/**
 * @file  thread_adc.c
 * @brief ADC 线程 — 电池电压采集
 *
 * 采集策略：
 *   PHASE_IDLE     : 不采集（深度休眠中，由 beacon 线程唤醒后单次采集）
 *   PHASE_LOW_BAT  : 每 10s 采一次，供充电检测使用
 *   PHASE_EXPIRED  : 不采集
 *   其他（救援/测试）: 每 5s 采一次
 */

#include <rtthread.h>
#include "msg_def.h"
#include "../RTThread/board.h"
#include "../Drivers/adc.h"
#include "../RTThread/rtconfig.h"

#define TAG  "ADC"

static void thread_adc_entry(void *param)
{
    rt_kprintf("[%s] thread started\n", TAG);
    ADC_Init();

    while (1) {
        BeaconPhase_t phase;
        rt_bool_t     usb_in;
        rt_mutex_take(mtx_status, RT_WAITING_FOREVER);
        phase  = g_status.phase;
        usb_in = g_status.usb_in;
        rt_mutex_release(mtx_status);

        /* IDLE：深度休眠中，beacon 线程唤醒后会自行单次采集 */
        if (phase == PHASE_IDLE || phase == PHASE_EXPIRED) {
            rt_thread_mdelay(500);
            continue;
        }

        /* 低电保护模式：每 10s 采一次，供充电检测使用 */
        rt_uint32_t delay_ms = (phase == PHASE_LOW_BAT) ? 10000U : 5000U;

        rt_uint16_t mv  = ADC_ReadBatMv();
        rt_uint8_t  pct = ADC_BatPercentEx(mv, (bool)usb_in);

        rt_mutex_take(mtx_status, RT_WAITING_FOREVER);
        g_status.bat_mv  = mv;
        g_status.bat_pct = pct;
        rt_mutex_release(mtx_status);

        rt_kprintf("[%s] %dmV %d%%%s\n", TAG, mv, pct,
                   (phase == PHASE_LOW_BAT) ? " [LOW_BAT]" : "");
        rt_thread_mdelay(delay_ms);
    }
}

static int adc_thread_init(void)
{
    rt_thread_t tid = rt_thread_create(
        "adc",
        thread_adc_entry, RT_NULL,
        THREAD_STACK_ADC,
        THREAD_PRIO_ADC, 20
    );
    RT_ASSERT(tid != RT_NULL);
    rt_thread_startup(tid);
    return 0;
}
INIT_APP_EXPORT(adc_thread_init);
