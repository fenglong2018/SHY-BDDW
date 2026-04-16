/**
 * @file  thread_led.c
 * @brief LED 指示线程
 *
 * 3颗 LED (LED1/LED2/LED3) 全部用于电量/状态显示
 *
 * 显示模式优先级（高→低）：
 *   1. 按键临时显示（5s，覆盖充电显示）
 *      - 短按：3颗按电量常亮5s后熄灭
 *      - 长按：3颗同时快闪5s后熄灭
 *   2. 充电中（USB接入）：PWM呼吸流水 → 充满3颗常亮
 *   3. 测试模式：三颗交替流水
 *   4. 其他（IDLE/救援休眠）：全灭
 *
 * 充电PWM呼吸流水（80%电量举例）：
 *   LED1: 由灭渐亮到最亮 → LED2: 由灭渐亮 → LED3: 由灭渐亮
 *   → 全灭 → 重新从LED1开始循环
 *   充满(>=95%)：3颗常亮
 *
 * 软件PWM：线程周期 10ms，PWM周期 = 32步 × 10ms = 320ms
 *   亮度0~31，每步对应占空比 0/31 ~ 31/31
 */

#include <rtthread.h>
#include "msg_def.h"
#include "../RTThread/board.h"
#include "../Drivers/gpio.h"
#include "../RTThread/rtconfig.h"

/* ---- 软件PWM参数 ---- */
#define PWM_STEPS       32U     /* 亮度级数 0~31 */
#define PWM_TICK_MS     10U     /* 线程周期 ms */
/* 每颗LED呼吸一次的步数：渐亮32步 + 保持8步 = 40步 × 10ms = 400ms */
#define BREATH_RISE     32U     /* 渐亮步数 */
#define BREATH_HOLD     8U      /* 最亮保持步数 */
#define BREATH_TOTAL    (BREATH_RISE + BREATH_HOLD)  /* 40步/颗 */
/* 全灭间隔步数 */
#define BREATH_GAP      16U     /* 全灭 160ms */

/* ---- 临时显示触发源 ---- */
typedef enum {
    TEMP_NONE = 0,
    TEMP_SHORT_PRESS,
    TEMP_LONG_PRESS,
} TempMode_t;

static volatile TempMode_t  s_temp_mode  = TEMP_NONE;
static volatile rt_uint32_t s_temp_start = 0;
#define TEMP_DURATION_MS  5000U

void LED_TriggerShortPress(void)
{
    s_temp_mode  = TEMP_SHORT_PRESS;
    s_temp_start = rt_tick_get_millisecond();
}

void LED_TriggerLongPress(void)
{
    s_temp_mode  = TEMP_LONG_PRESS;
    s_temp_start = rt_tick_get_millisecond();
}

/* ---- 基础操作 ---- */
static void _all_off(void) { LED1_OFF(); LED2_OFF(); LED3_OFF(); }
static void _all_on(void)  { LED1_ON();  LED2_ON();  LED3_ON();  }

/* 按电量点亮对应数量的LED（常亮） */
static void _show_bat_level(rt_uint8_t pct)
{
    if      (pct == 0)  { _all_off(); }
    else if (pct <= 33) { LED1_ON();  LED2_OFF(); LED3_OFF(); }
    else if (pct <= 66) { LED1_ON();  LED2_ON();  LED3_OFF(); }
    else                { _all_on(); }
}

/* ---- 软件PWM：控制单颗LED亮度 ----
 * brightness: 0=灭, PWM_STEPS-1=最亮
 * pwm_counter: 调用方维护的计数器 0~PWM_STEPS-1
 * LED低有效：亮度越高，低电平占空比越大
 */
static void _pwm_led(GPIO_TypeDef *port, rt_uint8_t pin,
                     rt_uint8_t brightness, rt_uint8_t pwm_counter)
{
    if (brightness == 0) {
        GPIO_SetPin(port, pin);   /* 全灭 */
    } else if (brightness >= PWM_STEPS - 1) {
        GPIO_ResetPin(port, pin); /* 全亮 */
    } else {
        /* 低有效：counter < brightness 时拉低（亮） */
        if (pwm_counter < brightness)
            GPIO_ResetPin(port, pin);
        else
            GPIO_SetPin(port, pin);
    }
}

/* ---- 充电呼吸流水状态 ---- */
/* 总步数 = 3颗 × BREATH_TOTAL + GAP */
#define WAVE_TOTAL  (3U * BREATH_TOTAL + BREATH_GAP)

static rt_uint32_t s_wave_step  = 0;   /* 流水当前步 0~WAVE_TOTAL-1 */
static rt_uint8_t  s_pwm_cnt    = 0;   /* PWM计数器 0~PWM_STEPS-1 */

/* 根据当前步计算各LED亮度 */
static void _calc_wave_brightness(rt_uint8_t bat_pct,
                                  rt_uint8_t *b1,
                                  rt_uint8_t *b2,
                                  rt_uint8_t *b3)
{
    /* 充满常亮 */
    if (bat_pct >= 95) {
        *b1 = *b2 = *b3 = PWM_STEPS - 1;
        return;
    }

    /* 按电量决定流水最多亮几颗 */
    rt_uint8_t max_led;
    if      (bat_pct <= 33) max_led = 1;
    else if (bat_pct <= 66) max_led = 2;
    else                    max_led = 3;

    /* 实际流水总步数 */
    rt_uint32_t total = (rt_uint32_t)max_led * BREATH_TOTAL + BREATH_GAP;
    rt_uint32_t step  = s_wave_step % total;

    *b1 = 0; *b2 = 0; *b3 = 0;

    /* LED1 阶段：step 0 ~ BREATH_TOTAL-1 */
    if (step < BREATH_TOTAL) {
        rt_uint8_t s = (rt_uint8_t)step;
        *b1 = (s < BREATH_RISE) ? (rt_uint8_t)(s * (PWM_STEPS - 1) / (BREATH_RISE - 1))
                                 : (PWM_STEPS - 1);
        return;
    }
    /* LED1 之后保持最亮 */
    if (max_led >= 1) *b1 = PWM_STEPS - 1;

    /* LED2 阶段 */
    if (max_led >= 2) {
        rt_uint32_t off2 = BREATH_TOTAL;
        if (step >= off2 && step < off2 + BREATH_TOTAL) {
            rt_uint8_t s = (rt_uint8_t)(step - off2);
            *b2 = (s < BREATH_RISE) ? (rt_uint8_t)(s * (PWM_STEPS - 1) / (BREATH_RISE - 1))
                                     : (PWM_STEPS - 1);
            return;
        }
        if (step >= off2 + BREATH_TOTAL) *b2 = PWM_STEPS - 1;
    }

    /* LED3 阶段 */
    if (max_led >= 3) {
        rt_uint32_t off3 = 2U * BREATH_TOTAL;
        if (step >= off3 && step < off3 + BREATH_TOTAL) {
            rt_uint8_t s = (rt_uint8_t)(step - off3);
            *b3 = (s < BREATH_RISE) ? (rt_uint8_t)(s * (PWM_STEPS - 1) / (BREATH_RISE - 1))
                                     : (PWM_STEPS - 1);
            return;
        }
        if (step >= off3 + BREATH_TOTAL) *b3 = PWM_STEPS - 1;
    }
    /* GAP 阶段：全灭，等待下一轮（b1/b2/b3 已为0） */
}

/* ---- 主线程 ---- */
static void thread_led_entry(void *param)
{
    rt_uint32_t blink_phase = 0;

    while (1) {
        rt_mutex_take(mtx_status, RT_WAITING_FOREVER);
        BeaconPhase_t phase   = g_status.phase;
        rt_uint8_t    bat_pct = g_status.bat_pct;
        rt_bool_t     usb_in  = g_status.usb_in;
        rt_mutex_release(mtx_status);

        rt_uint32_t now = rt_tick_get_millisecond();

        /* ---- 检查临时显示超时 ---- */
        TempMode_t temp = s_temp_mode;
        if (temp != TEMP_NONE &&
            (now - s_temp_start) >= TEMP_DURATION_MS) {
            s_temp_mode = TEMP_NONE;
            temp = TEMP_NONE;
            _all_off();
        }

        /* ============================================================
         * 优先级1：按键临时显示（5s）
         * ============================================================ */
        if (temp == TEMP_SHORT_PRESS) {
            _show_bat_level(bat_pct);
            s_wave_step = 0; s_pwm_cnt = 0; blink_phase = 0;
            rt_thread_mdelay(PWM_TICK_MS);
            continue;
        }

        if (temp == TEMP_LONG_PRESS) {
            /* 3颗同时快闪 200ms on/off */
            blink_phase += PWM_TICK_MS;
            if ((blink_phase % 400U) < 200U) _all_on();
            else                              _all_off();
            s_wave_step = 0; s_pwm_cnt = 0;
            rt_thread_mdelay(PWM_TICK_MS);
            continue;
        }

        /* ============================================================
         * 优先级2：充电中 — PWM呼吸流水
         * ============================================================ */
        if (usb_in) {
            blink_phase = 0;

            if (bat_pct >= 95) {
                /* 充满：3颗常亮 */
                _all_on();
                s_wave_step = 0; s_pwm_cnt = 0;
            } else {
                /* 计算各LED目标亮度 */
                rt_uint8_t b1, b2, b3;
                _calc_wave_brightness(bat_pct, &b1, &b2, &b3);

                /* 软件PWM输出 */
                _pwm_led(LED1_PORT, LED1_PIN, b1, s_pwm_cnt);
                _pwm_led(LED2_PORT, LED2_PIN, b2, s_pwm_cnt);
                _pwm_led(LED3_PORT, LED3_PIN, b3, s_pwm_cnt);

                /* 推进PWM计数器 */
                s_pwm_cnt++;
                if (s_pwm_cnt >= PWM_STEPS) {
                    s_pwm_cnt = 0;
                    /* 每完成一个PWM周期推进一步流水 */
                    s_wave_step++;
                }
            }
            rt_thread_mdelay(PWM_TICK_MS);
            continue;
        }

        /* ============================================================
         * 优先级3：测试模式 — 三颗交替流水
         * ============================================================ */
        if (phase == PHASE_TEST) {
            blink_phase += PWM_TICK_MS;
            rt_uint32_t t = blink_phase % 600U;
            if      (t < 200U) { LED1_ON();  LED2_OFF(); LED3_OFF(); }
            else if (t < 400U) { LED1_OFF(); LED2_ON();  LED3_OFF(); }
            else               { LED1_OFF(); LED2_OFF(); LED3_ON();  }
            s_wave_step = 0; s_pwm_cnt = 0;
            rt_thread_mdelay(PWM_TICK_MS);
            continue;
        }

        /* ============================================================
         * 优先级4：全灭（IDLE/救援休眠/EXPIRED）
         * ============================================================ */
        _all_off();
        s_wave_step = 0; s_pwm_cnt = 0; blink_phase = 0;
        rt_thread_mdelay(PWM_TICK_MS);
    }
}

static int led_thread_init(void)
{
    rt_thread_t tid = rt_thread_create(
        "led",
        thread_led_entry, RT_NULL,
        THREAD_STACK_LED,
        THREAD_PRIO_LED, 50
    );
    RT_ASSERT(tid != RT_NULL);
    rt_thread_startup(tid);
    return 0;
}
INIT_APP_EXPORT(led_thread_init);
