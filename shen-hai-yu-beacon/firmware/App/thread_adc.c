/**
 * @file  thread_adc.c
 * @brief ADC 线程 — 仅在工作窗口内采集电池电压
 *        休眠期间此线程挂起，不消耗功耗
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
        rt_mutex_take(mtx_status, RT_WAITING_FOREVER);
        phase = g_status.phase;
        rt_mutex_release(mtx_status);

        /* 只在救援模式工作窗口或电量显示时采集 */
        if (phase == PHASE_IDLE || phase == PHASE_EXPIRED) {
            rt_thread_mdelay(500);
            continue;
        }

        rt_uint16_t mv  = ADC_ReadBatMv();
        rt_uint8_t  pct = ADC_BatPercent();

        rt_mutex_take(mtx_status, RT_WAITING_FOREVER);
        g_status.bat_mv  = mv;
        g_status.bat_pct = pct;
        rt_mutex_release(mtx_status);

        rt_kprintf("[%s] %dmV %d%%\n", TAG, mv, pct);
        rt_thread_mdelay(5000);  /* 工作窗口内每 5s 采一次 */
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
