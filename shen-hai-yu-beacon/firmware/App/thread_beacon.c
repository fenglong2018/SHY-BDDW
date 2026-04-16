/**
 * @file  thread_beacon.c
 * @brief Beacon 主线程
 *
 * 运行模式：
 *   平时深度休眠（Stop 模式），SOS 按键 EXTI 唤醒
 *   短按 SOS → 电量显示（LED 闪烁 5s 后回休眠）
 *   长按 SOS → 启动救援模式，按三阶段上报：
 *     0~1h:   每 2min 上报，工作 10s + 休眠 110s
 *     1~3h:   每 5min 上报，工作 10s + 休眠 290s
 *     3~72h:  每15min 上报，工作 10s + 休眠 890s
 *     72h 后：停止上报，回到深度休眠
 */

#include <rtthread.h>
#include <string.h>
#include "msg_def.h"
#include "../RTThread/board.h"
#include "../RTThread/rtconfig.h"
#include "../Drivers/gpio.h"
#include "../Drivers/adc.h"
#include "../Drivers/pwr.h"

#define TAG                 "BEACON"
#define RESCUE_CENTER_ID    "0000001"

/* SOS 按键时间定义 */
#define SOS_SHORT_PRESS_MAX_MS  500U    /* < 500ms = 短按 */
#define SOS_LONG_PRESS_MIN_MS   1000U   /* 1s~3s = 长按(SOS) */
#define SOS_LONG_PRESS_MAX_MS   3000U   /* 3s~8s = 忽略 */
#define SOS_TEST_PRESS_MS       8000U   /* >= 8s = 测试模式 */
/* 单次工作窗口（定位+发报文）*/
#define WORK_WINDOW_MS      10000U
/* 电量显示持续时间 */
#define BAT_DISPLAY_MS      5000U

/* 三阶段参数表 */
typedef struct {
    rt_uint32_t duration_sec;   /* 本阶段持续时长（秒） */
    rt_uint32_t interval_sec;   /* 上报间隔（秒） */
    rt_uint32_t sleep_sec;      /* 每次休眠时长 = interval - 10s */
} PhaseParam_t;

static const PhaseParam_t PHASE_PARAMS[] = {
    /* 0~1h   */ { 3600U,   120U,  110U },
    /* 1~3h   */ { 7200U,   300U,  290U },
    /* 3~72h  */ { 248400U, 900U,  890U },
};
#define PHASE_COUNT  3

/* ---- 内部函数 ---- */

static void _set_phase(BeaconPhase_t p)
{
    rt_mutex_take(mtx_status, RT_WAITING_FOREVER);
    g_status.phase = p;
    rt_mutex_release(mtx_status);
}

/* 采集电量并更新全局状态 */
static void _update_battery(void)
{
    rt_uint16_t mv  = ADC_ReadBatMv();
    rt_bool_t   usb = IS_USB_INSERTED();
    /* 充电时用补偿版本，避免显示虚高 */
    rt_uint8_t  pct = ADC_BatPercentEx(mv, (bool)usb);
    rt_mutex_take(mtx_status, RT_WAITING_FOREVER);
    g_status.bat_mv  = mv;
    g_status.bat_pct = pct;
    g_status.usb_in  = usb;
    rt_mutex_release(mtx_status);
    rt_kprintf("[%s] BAT %dmV %d%% USB=%d%s\n",
               TAG, mv, pct, usb, usb ? "(charging,compensated)" : "");
}

/* 构造并入队短报文 */
static void _enqueue_position(void)
{
    rt_mutex_take(mtx_status, RT_WAITING_FOREVER);
    rt_bool_t valid = g_status.gnss_valid;
    float     lat   = g_status.latitude;
    float     lon   = g_status.longitude;
    rt_uint8_t pct  = g_status.bat_pct;
    rt_mutex_release(mtx_status);

    if (!valid) {
        rt_kprintf("[%s] no fix, skip\n", TAG);
        return;
    }

    MsgRdssTx_t msg;
    msg.type = RDSS_MSG_SOS;
    rt_strncpy(msg.dest_id, RESCUE_CENTER_ID, sizeof(msg.dest_id) - 1);
    rt_snprintf(msg.body, sizeof(msg.body),
                "SOS:%.5f,%.5f,BAT:%d%%", lat, lon, pct);

    if (rt_mq_send(mq_rdss_tx, &msg, sizeof(msg)) == RT_EOK) {
        rt_mutex_take(mtx_status, RT_WAITING_FOREVER);
        g_status.total_reports++;
        rt_mutex_release(mtx_status);
        rt_kprintf("[%s] queued #%lu: %.5f,%.5f\n",
                   TAG, g_status.total_reports, lat, lon);
    } else {
        rt_kprintf("[%s] mq full\n", TAG);
    }
}

/* 单次工作窗口：上电 → 等待定位（带重试）→ 发报文 → 关电 */
#define GNSS_FIX_RETRY      3U      /* 定位最多重试3次 */
#define GNSS_FIX_TIMEOUT_MS 10000U  /* 每次等待10s */

static void _do_work_window(void)
{
    rt_kprintf("[%s] work window start\n", TAG);

    GNSS_POWER_ON();
    RDSS_POWER_ON();

    /* 等待 GNSS 定位，最多重试 GNSS_FIX_RETRY 次 */
    bool fixed = false;
    for (uint8_t retry = 0; retry < GNSS_FIX_RETRY && !fixed; retry++) {
        if (retry > 0) rt_kprintf("[%s] GNSS retry %d\n", TAG, retry);
        rt_uint32_t t0 = rt_tick_get_millisecond();
        while ((rt_tick_get_millisecond() - t0) < GNSS_FIX_TIMEOUT_MS) {
            if (g_status.gnss_valid) { fixed = true; break; }
            rt_thread_mdelay(200);
        }
    }

    if (!fixed) rt_kprintf("[%s] GNSS no fix after %d retries\n", TAG, GNSS_FIX_RETRY);

    _update_battery();
    _enqueue_position();

    /* 等待短报文发送完成，最多 8s */
    rt_sem_take(sem_work_done, rt_tick_from_millisecond(8000));

    /* 关电：先关模块使能，再关 CVPOW5V 5V总线 */
    GNSS_POWER_OFF();
    RDSS_POWER_OFF();
    ALL_MODULE_POWER_OFF();
    rt_kprintf("[%s] work window end\n", TAG);
}

/* 检测 SOS 按键
 * 返回：0=未按/抖动/忽略, 1=短按(<500ms), 2=长按(1s~3s=SOS), 3=超长按(>=8s=测试模式)
 * 按住期间 LED 给出渐进反馈：
 *   1s亮LED1, 8s亮LED2+LED3(测试模式确认)
 */
static rt_uint8_t _detect_sos_key(void)
{
    if (!IS_FALL_DETECTED()) return 0;

    rt_uint32_t t0 = rt_tick_get_millisecond();
    rt_bool_t   fb1 = RT_FALSE, fb2 = RT_FALSE;

    while (IS_FALL_DETECTED()) {
        rt_uint32_t held = rt_tick_get_millisecond() - t0;

        /* 渐进 LED 反馈 */
        if (!fb1 && held >= SOS_LONG_PRESS_MIN_MS) {
            LED1_ON(); fb1 = RT_TRUE;   /* 1s: 亮LED1，SOS即将触发 */
        }
        if (!fb2 && held >= SOS_TEST_PRESS_MS) {
            LED2_ON(); LED3_ON(); fb2 = RT_TRUE;  /* 8s: 亮3颗，测试模式确认 */
        }

        rt_thread_mdelay(20);
    }

    rt_uint32_t dur = rt_tick_get_millisecond() - t0;
    LED1_OFF(); LED2_OFF(); LED3_OFF();  /* 清除反馈灯 */

    if (dur < 50U)                          return 0;  /* 抖动 */
    if (dur < SOS_SHORT_PRESS_MAX_MS)       return 1;  /* 短按 */
    if (dur < SOS_LONG_PRESS_MIN_MS)        return 0;  /* 500ms~1s 忽略 */
    if (dur < SOS_LONG_PRESS_MAX_MS)        return 2;  /* 1s~3s = SOS */
    if (dur < SOS_TEST_PRESS_MS)            return 0;  /* 3s~8s 忽略 */
    return 3;                                           /* >= 8s = 测试模式 */
}

static rt_uint32_t s_test_last_ms = 0;  /* 测试模式上次发送时间，进入时清零 */

void Beacon_ResetTestTimer(void) { s_test_last_ms = 0; }

/* ---- 主线程 ---- */
static void thread_beacon_entry(void *param)
{
    rt_kprintf("[%s] thread started, entering sleep\n", TAG);

    ADC_Init();
    _update_battery();

    while (1) {
        /* 充电时（USB接入）也要检测按键，临时切换LED显示 */
        rt_mutex_take(mtx_status, RT_WAITING_FOREVER);
        rt_bool_t usb_in = g_status.usb_in;
        BeaconPhase_t phase = g_status.phase;
        rt_mutex_release(mtx_status);

        if (usb_in && phase == PHASE_IDLE) {
            /* 充电待机：轮询按键，其余时间 LED 线程负责流水灯 */
            rt_uint8_t key = _detect_sos_key();
            if (key == 1) {
                _update_battery();
                LED_TriggerShortPress();
            } else if (key == 2) {
                LED_TriggerLongPress();
            }
            rt_thread_mdelay(50);
            continue;
        }

        /* ============================================================
         * PHASE_IDLE: 深度休眠，等待 SOS 按键
         * ============================================================ */
        if (phase == PHASE_IDLE) {
            rt_kprintf("[%s] deep sleep...\n", TAG);

            /* 关闭所有外设 */
            ALL_MODULE_POWER_OFF();
            LED1_OFF(); LED2_OFF(); LED3_OFF();

            /* 进入 Stop 模式，等待 EXTI 唤醒 */
            PWR_EnterStop();

            /* 唤醒后检测按键类型 */
            rt_uint8_t key = _detect_sos_key();
            if (key == 1) {
                /* 短按：LED显示电量5s */
                _update_battery();
                LED_TriggerShortPress();
                rt_thread_mdelay(5000);
                _set_phase(PHASE_IDLE);

            } else if (key == 2) {
                /* 长按3s：LED全闪5s，然后启动救援模式 */
                LED_TriggerLongPress();
                rt_kprintf("[%s] !!! SOS ACTIVATED !!!\n", TAG);
                rt_mutex_take(mtx_status, RT_WAITING_FOREVER);
                g_status.sos_active    = RT_TRUE;
                g_status.sos_start_sec = rt_tick_get_millisecond() / 1000U;
                g_status.total_reports = 0;
                g_status.phase         = PHASE_0_1H;
                rt_mutex_release(mtx_status);

            } else if (key == 3) {
                /* 超长按8s：进入测试模式，记录开始时间，重置发送计时 */
                rt_kprintf("[%s] >>> TEST MODE ACTIVATED <<<\n", TAG);
                rt_mutex_take(mtx_status, RT_WAITING_FOREVER);
                g_status.sos_start_sec = rt_tick_get_millisecond() / 1000U;
                g_status.phase         = PHASE_TEST;
                rt_mutex_release(mtx_status);
                /* 通知 LED 线程重置 static 计时器 */
                extern void Beacon_ResetTestTimer(void);
                Beacon_ResetTestTimer();
            }
            continue;
        }

        /* ============================================================
         * PHASE_TEST: 测试模式，每2min发送一次，任意按键退出
         *             最长运行 72h 自动退出，防止忘记关闭耗尽电量
         * ============================================================ */
        if (phase == PHASE_TEST) {
            #define TEST_INTERVAL_MS    (120000U)
            #define TEST_MAX_DURATION_S (72U * 3600U)

            rt_uint32_t test_elapsed = rt_tick_get_millisecond() / 1000U
                                     - g_status.sos_start_sec;
            if (test_elapsed >= TEST_MAX_DURATION_S) {
                rt_kprintf("[%s] TEST MODE timeout 72h, exit\n", TAG);
                ALL_MODULE_POWER_OFF();
                rt_mutex_take(mtx_status, RT_WAITING_FOREVER);
                g_status.sos_active = RT_FALSE;
                g_status.phase      = PHASE_IDLE;
                rt_mutex_release(mtx_status);
                continue;
            }

            if (IS_FALL_DETECTED()) {
                while (IS_FALL_DETECTED()) rt_thread_mdelay(10);
                rt_kprintf("[%s] TEST MODE exit by key\n", TAG);
                ALL_MODULE_POWER_OFF();
                rt_mutex_take(mtx_status, RT_WAITING_FOREVER);
                g_status.sos_active = RT_FALSE;
                g_status.phase      = PHASE_IDLE;
                rt_mutex_release(mtx_status);
                s_test_last_ms = 0;
                continue;
            }

            rt_uint32_t now_ms = rt_tick_get_millisecond();
            if (s_test_last_ms == 0 ||
                (now_ms - s_test_last_ms) >= TEST_INTERVAL_MS) {
                rt_kprintf("[%s] TEST: sending report\n", TAG);
                GNSS_POWER_ON();
                RDSS_POWER_ON();
                _do_work_window();
                s_test_last_ms = rt_tick_get_millisecond();
            }

            rt_thread_mdelay(100);
            continue;
        }

        /* ============================================================
         * PHASE_EXPIRED: 72h 结束，回休眠
         * ============================================================ */
        if (phase == PHASE_EXPIRED) {
            rt_kprintf("[%s] 72h expired, back to sleep\n", TAG);
            rt_mutex_take(mtx_status, RT_WAITING_FOREVER);
            g_status.sos_active = RT_FALSE;
            g_status.phase      = PHASE_IDLE;
            rt_mutex_release(mtx_status);
            continue;
        }

        /* ============================================================
         * 救援模式：PHASE_0_1H / PHASE_1_3H / PHASE_3_72H
         * ============================================================ */
        rt_uint32_t now_sec = rt_tick_get_millisecond() / 1000U;
        rt_uint32_t elapsed_sec;
        rt_mutex_take(mtx_status, RT_WAITING_FOREVER);
        elapsed_sec = now_sec - g_status.sos_start_sec;
        rt_mutex_release(mtx_status);

        /* 判断当前应处于哪个阶段 */
        BeaconPhase_t new_phase;
        rt_uint8_t    param_idx;
        if (elapsed_sec < 3600U) {
            new_phase  = PHASE_0_1H;
            param_idx  = 0;
        } else if (elapsed_sec < 10800U) {
            new_phase  = PHASE_1_3H;
            param_idx  = 1;
        } else if (elapsed_sec < 259200U) {  /* 72h = 259200s */
            new_phase  = PHASE_3_72H;
            param_idx  = 2;
        } else {
            _set_phase(PHASE_EXPIRED);
            continue;
        }

        if (new_phase != phase) {
            rt_kprintf("[%s] phase -> %d (elapsed %lus)\n",
                       TAG, new_phase, elapsed_sec);
            _set_phase(new_phase);
        }

        /* 低电量保护：电压有效且低于 3.4V 时停止发报文 */
        if (g_status.bat_mv > 0 && g_status.bat_mv < BAT_CRIT_THRESHOLD_MV) {
            rt_kprintf("[%s] critical battery %dmV, stop\n", TAG, g_status.bat_mv);
            _set_phase(PHASE_EXPIRED);
            continue;
        }

        /* 执行工作窗口 */
        _do_work_window();

        /* 进入定时休眠 */
        rt_uint32_t sleep_sec = PHASE_PARAMS[param_idx].sleep_sec;
        rt_kprintf("[%s] sleep %lus\n", TAG, sleep_sec);
        PWR_EnterStopSeconds(sleep_sec);
    }
}

static int beacon_thread_init(void)
{
    rt_thread_t tid = rt_thread_create(
        "beacon",
        thread_beacon_entry, RT_NULL,
        THREAD_STACK_BEACON,
        THREAD_PRIO_BEACON, 20
    );
    RT_ASSERT(tid != RT_NULL);
    rt_thread_startup(tid);
    return 0;
}
INIT_APP_EXPORT(beacon_thread_init);
