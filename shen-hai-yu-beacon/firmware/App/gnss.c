#include "gnss.h"
#include "uart.h"
#include "gpio.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define NMEA_BUF_SIZE   128
#define GNSS_TIMEOUT_MS 60000U  /* 60s 无定位超时 */

static GnssData_t s_gnss = {0};
static char       s_nmea_buf[NMEA_BUF_SIZE];
static uint8_t    s_nmea_idx = 0;
static bool       s_powered  = false;

/* 解析 NMEA 度分格式 ddmm.mmmm → 十进制度 */
static float _parse_coord(const char *str)
{
    float raw = atof(str);
    int   deg = (int)(raw / 100);
    float min = raw - (deg * 100.0f);
    return deg + min / 60.0f;
}

/* 计算 NMEA 校验和 */
static uint8_t _nmea_checksum(const char *sentence)
{
    uint8_t cs = 0;
    for (const char *p = sentence + 1; *p && *p != '*'; p++)
        cs ^= (uint8_t)*p;
    return cs;
}

/* 解析 $GNGGA 语句 */
static void _parse_gga(char *sentence)
{
    char *fields[15];
    char  copy[NMEA_BUF_SIZE];
    strncpy(copy, sentence, sizeof(copy) - 1);

    /* 分割字段 */
    uint8_t n = 0;
    char *p = strtok(copy, ",");
    while (p && n < 15) { fields[n++] = p; p = strtok(NULL, ","); }
    if (n < 10) return;

    /* 校验和验证 */
    char *star = strchr(sentence, '*');
    if (star) {
        uint8_t expected = (uint8_t)strtol(star + 1, NULL, 16);
        if (_nmea_checksum(sentence) != expected) return;
    }

    s_gnss.fix_quality = (uint8_t)atoi(fields[6]);
    if (s_gnss.fix_quality == 0) { s_gnss.valid = false; return; }

    strncpy(s_gnss.utc_time, fields[1], sizeof(s_gnss.utc_time) - 1);
    s_gnss.latitude   = _parse_coord(fields[2]);
    if (fields[3][0] == 'S') s_gnss.latitude  = -s_gnss.latitude;
    s_gnss.longitude  = _parse_coord(fields[4]);
    if (fields[5][0] == 'W') s_gnss.longitude = -s_gnss.longitude;
    s_gnss.satellites = (uint8_t)atoi(fields[7]);
    s_gnss.altitude   = atof(fields[9]);
    s_gnss.valid      = true;
    s_gnss.timestamp  = HAL_GetTick();
}

/* 解析 $GNRMC 语句（获取速度和日期） */
static void _parse_rmc(char *sentence)
{
    char *fields[12];
    char  copy[NMEA_BUF_SIZE];
    strncpy(copy, sentence, sizeof(copy) - 1);

    uint8_t n = 0;
    char *p = strtok(copy, ",");
    while (p && n < 12) { fields[n++] = p; p = strtok(NULL, ","); }
    if (n < 10) return;

    if (fields[2][0] != 'A') return;  /* 数据无效 */
    s_gnss.speed = atof(fields[7]);
    strncpy(s_gnss.utc_date, fields[9], sizeof(s_gnss.utc_date) - 1);
}

static void _process_sentence(char *sentence)
{
    if (strncmp(sentence, "$GNGGA", 6) == 0 ||
        strncmp(sentence, "$GPGGA", 6) == 0) {
        _parse_gga(sentence);
    } else if (strncmp(sentence, "$GNRMC", 6) == 0 ||
               strncmp(sentence, "$GPRMC", 6) == 0) {
        _parse_rmc(sentence);
    }
}

void GNSS_Init(void)
{
    memset(&s_gnss, 0, sizeof(s_gnss));
    UART_Init(UART_CH1, 9600);
}

void GNSS_PowerOn(void)
{
    if (s_powered) return;
    GNSS_POWER_ON();
    s_powered = true;
    HAL_Delay(500);  /* 等待模块上电稳定 */
}

void GNSS_PowerOff(void)
{
    GNSS_POWER_OFF();
    s_powered  = false;
    s_gnss.valid = false;
}

void GNSS_Process(void)
{
    uint8_t byte;
    while (UART_ReadByte(UART_CH1, &byte)) {
        if (byte == '$') {
            s_nmea_idx = 0;
        }
        if (s_nmea_idx < NMEA_BUF_SIZE - 1) {
            s_nmea_buf[s_nmea_idx++] = (char)byte;
            s_nmea_buf[s_nmea_idx]   = '\0';
        }
        if (byte == '\n' && s_nmea_idx > 6) {
            _process_sentence(s_nmea_buf);
            s_nmea_idx = 0;
        }
    }

    /* 超时清除有效标志 */
    if (s_gnss.valid && (HAL_GetTick() - s_gnss.timestamp) > GNSS_TIMEOUT_MS) {
        s_gnss.valid = false;
    }
}

bool GNSS_IsValid(void)
{
    return s_gnss.valid;
}

GnssData_t *GNSS_GetData(void)
{
    return &s_gnss;
}

void GNSS_FormatPosition(char *buf, uint16_t len)
{
    if (!s_gnss.valid) {
        snprintf(buf, len, "NO FIX");
        return;
    }
    snprintf(buf, len, "%.6f,%.6f,%.1fm,%dSAT",
             s_gnss.latitude, s_gnss.longitude,
             s_gnss.altitude, s_gnss.satellites);
}
