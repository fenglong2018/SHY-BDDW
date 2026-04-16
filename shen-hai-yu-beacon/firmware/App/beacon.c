#include "beacon.h"
#include "gnss.h"
#include "rdss.h"
#include "gpio.h"
#include "adc.h"
#include "uart.h"

static BeaconState_t  s_state  = BEACON_STATE_IDLE;
static BeaconConfig_t s_config = {
    .report_interval_ms = 300000,   /* 5 分钟 */
    .sos_interval_ms    = 30000,    /* 30 秒 */
    .low_bat_threshold  = 15,
    .rescue_center_id   = "0000001",
};

static uint32_t s_last_report_ms = 0;
static bool     s_sos_active     = false;

/* LED 状态指示 */
static void _update_leds(void)
{
    static uint32_t s_blink_ms = 0;
    uint32_t now = HAL_GetTick();

    switch (s_state) {
    case BEACON_STATE_LOCATING:
        /* LED1 慢闪 1Hz */
        if ((now - s_blink_ms) >= 500) { LED1_TOGGLE(); s_blink_ms = now; }
        break;
    case BEACON_STATE_REPORTING:
        /* LED1 快闪 4Hz */
        if ((now - s_blink_ms) >= 125) { LED1_TOGGLE(); s_blink_ms = now; }
        break;
    case BEACON_STATE_SOS:
        /* LED1+LED3 交替快闪 */
        if ((now - s_blink_ms) >= 200) {
            LED1_TOGGLE(); LED3_TOGGLE(); s_blink_ms = now;
        }
        break;
    case BEACON_STATE_CHARGING:
        LED1_ON(); LED2_ON();
        break;
    case BEACON_STATE_LOW_BAT:
        /* LED2 慢闪 */
        if ((now - s_blink_ms) >= 1000) { LED2_TOGGLE(); s_blink_ms = now; }
        break;
    default:
        LED1_OFF(); LED2_OFF(); LED3_OFF();
        break;
    }

    /* 电量 LED */
    uint8_t pct = ADC_BatPercent();
    if (pct > 50) { LED2_ON(); }
    else if (pct > 20) {
        if ((now % 1000) < 500) LED2_ON(); else LED2_OFF();
    } else {
        if ((now % 2000) < 200) LED2_ON(); else LED2_OFF();
    }
}

static void _check_fall_detection(void)
{
    static uint32_t s_fall_start = 0;
    static bool     s_fall_prev  = false;

    bool fall = IS_FALL_DETECTED();
    if (fall && !s_fall_prev) {
        s_fall_start = HAL_GetTick();
    }
    /* 持续 500ms 确认落水，防抖 */
    if (fall && (HAL_GetTick() - s_fall_start) > 500) {
        Beacon_TriggerSOS();
    }
    s_fall_prev = fall;
}

static void _check_mag_key(void)
{
    static uint32_t s_mag_start = 0;
    static bool     s_mag_prev  = false;
    static bool     s_on        = false;

    bool mag = IS_MAGKEY_ACTIVE();
    if (mag && !s_mag_prev) s_mag_start = HAL_GetTick();

    /* 长按 2s 切换开关机 */
    if (mag && (HAL_GetTick() - s_mag_start) > 2000 && !s_mag_prev) {
        s_on = !s_on;
        if (!s_on) {
            GNSS_PowerOff();
            RDSS_PowerOff();
            s_state = BEACON_STATE_IDLE;
            LOG("Mag key: power off");
        } else {
            s_state = BEACON_STATE_INIT;
            LOG("Mag key: power on");
        }
    }
    s_mag_prev = mag;
}

void Beacon_Init(void)
{
    GPIO_Init();
    ADC_Init();
    GNSS_Init();
    RDSS_Init();
    UART_Init(UART_CH3, 115200);

    LOG("=== %s %s ===", PRODUCT_NAME, FW_VERSION_STR);
    LOG("Init complete");

    s_state = BEACON_STATE_INIT;
}

void Beacon_Run(void)
{
    uint32_t now = HAL_GetTick();

    _check_fall_detection();
    _check_mag_key();
    _update_leds();

    GNSS_Process();
    RDSS_Process();

    switch (s_state) {
    case BEACON_STATE_INIT:
        LOG("Starting GNSS...");
        GNSS_PowerOn();
        s_state = BEACON_STATE_LOCATING;
        break;

    case BEACON_STATE_LOCATING:
        if (GNSS_IsValid()) {
            LOG("GNSS fix acquired");
            RDSS_PowerOn();
            s_state = BEACON_STATE_REPORTING;
        }
        /* 低电量检查 */
        if (ADC_BatPercent() < s_config.low_bat_threshold) {
            s_state = BEACON_STATE_LOW_BAT;
        }
        break;

    case BEACON_STATE_REPORTING: {
        uint32_t interval = s_sos_active
                          ? s_config.sos_interval_ms
                          : s_config.report_interval_ms;

        if ((now - s_last_report_ms) >= interval) {
            GnssData_t *gps = GNSS_GetData();
            if (gps->valid) {
                RdssErr_t err = RDSS_SendPosition(gps->latitude, gps->longitude);
                if (err == RDSS_OK) {
                    LOG("Position sent: %.5f, %.5f", gps->latitude, gps->longitude);
                } else {
                    ERR("Send failed: %d", err);
                }
            }
            s_last_report_ms = now;
        }

        if (ADC_BatPercent() < s_config.low_bat_threshold) {
            s_state = BEACON_STATE_LOW_BAT;
        }
        break;
    }

    case BEACON_STATE_SOS:
        /* SOS 模式：强制上报，不受低电量限制 */
        if ((now - s_last_report_ms) >= s_config.sos_interval_ms) {
            GnssData_t *gps = GNSS_GetData();
            if (gps->valid) {
                RDSS_SendPosition(gps->latitude, gps->longitude);
                LOG("SOS sent: %.5f, %.5f", gps->latitude, gps->longitude);
            }
            s_last_report_ms = now;
        }
        break;

    case BEACON_STATE_LOW_BAT:
        /* 低电量：关闭 GNSS 和 RDSS，仅保持 LED 闪烁 */
        GNSS_PowerOff();
        RDSS_PowerOff();
        LOG("Low battery! %d%%", ADC_BatPercent());
        /* 充电后恢复 */
        if (IS_USB_INSERTED()) {
            s_state = BEACON_STATE_CHARGING;
        }
        break;

    case BEACON_STATE_CHARGING:
        if (!IS_USB_INSERTED()) {
            /* 充电完成，重新启动 */
            s_state = BEACON_STATE_INIT;
        }
        break;

    default:
        break;
    }
}

void Beacon_TriggerSOS(void)
{
    if (s_sos_active) return;
    s_sos_active     = true;
    s_state          = BEACON_STATE_SOS;
    s_last_report_ms = 0;  /* 立即发送 */
    LOG("!!! SOS TRIGGERED !!!");

    /* 确保模块已上电 */
    GNSS_PowerOn();
    RDSS_PowerOn();
}

BeaconState_t Beacon_GetState(void)
{
    return s_state;
}

BeaconConfig_t *Beacon_GetConfig(void)
{
    return &s_config;
}
