#include "shared/display_data.h"
#include "shared/config.h"
#include "drivers/oled_ssd1306.h"
#include "fonts/font_8x16.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <stdio.h>

/* 4行布局，每行16px高，互不重叠:
   y=0:  WiFi + 时间 + 电池
   y=16: 城市名
   y=32: 温度(左) + 天气中文(右)
   y=48: 湿度 + 体感温度
*/

static void render_status(void)
{
    if (g_display_data.wifi_connected)
        draw_status_icon(0, 0, icon_wifi_on);
    else
        draw_status_icon(0, 0, icon_wifi_off);

    char t[8];
    snprintf(t, sizeof(t), "%02d:%02d", g_display_data.hour, g_display_data.minute);
    oled_draw_string(48, 0, t, font_8x16);

    char b[4];
    snprintf(b, sizeof(b), "%d", g_display_data.battery_pct);
    oled_draw_string(96, 0, b, font_8x16);
}

static void render_city(void)
{
    if (g_display_data.city[0] && g_display_data.city[0] >= 'A') {
        oled_draw_string(0, 16, g_display_data.city, font_8x16);
    } else {
        draw_chinese(32, 16, CH_IDX_BEI);
        draw_chinese(48, 16, CH_IDX_JING);
    }
}

static const char *weather_str(uint8_t code)
{
    switch (code) {
    case 0: return "Sun";
    case 1: return "PCloud";
    case 2: return "Cloud";
    case 3: return "Rain";
    case 4: return "Snow";
    default: return "Cloud";
    }
}

static void render_temp(void)
{
    char buf[8];
    if (g_display_data.weather_valid)
        snprintf(buf, sizeof(buf), "%dC", g_display_data.temperature);
    else
        snprintf(buf, sizeof(buf), "--C");
    oled_draw_string(0, 32, buf, font_8x16);

    /* 天气图标 + 英文标签 */
    if (g_display_data.weather_valid) {
        draw_weather_icon(48, 32, g_display_data.weather_code);
        oled_draw_string(64, 32, weather_str(g_display_data.weather_code), font_8x16);
    }
}

static void render_bottom(void)
{
    char buf[24];
    if (g_display_data.weather_valid)
        snprintf(buf, sizeof(buf), "H:%02d%% F:%dC", g_display_data.humidity, g_display_data.feels_like);
    else
        snprintf(buf, sizeof(buf), "H:--%% F:--C");
    oled_draw_string(10, 48, buf, font_8x16);
}

void display_task(void *pvParam)
{
    (void)pvParam;
    uint32_t note;
    while (1) {
        xTaskNotifyWait(0, 0xFFFFFFFF, &note, pdMS_TO_TICKS(1000));
        if (xSemaphoreTake(g_data_sem, pdMS_TO_TICKS(10)) == pdTRUE) {
            oled_clear();
            render_status();
            render_city();
            render_temp();
            render_bottom();
            oled_refresh();
            xSemaphoreGive(g_data_sem);
        }
        g_heartbeat_display++;
    }
}
