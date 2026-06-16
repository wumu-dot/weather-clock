#include "shared/display_data.h"
#include "shared/config.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "wdog";

void watchdog_task(void *pvParam)
{
    (void)pvParam;
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_task_wdt_add(NULL);
    ESP_LOGI(TAG, "WDT timeout=%ds", WDT_TIMEOUT_S);

    uint32_t s_clk = g_heartbeat_clock, s_dpy = g_heartbeat_display;
    uint32_t s_net = g_heartbeat_network, s_pwr = g_heartbeat_power;
    TickType_t t_clk = 0, t_dpy = 0, t_net = 0, t_pwr = 0;
    TickType_t now = xTaskGetTickCount();
    t_clk = t_dpy = t_net = t_pwr = now;

    while (1) {
        now = xTaskGetTickCount();

        if (g_heartbeat_clock != s_clk)   { s_clk = g_heartbeat_clock;   t_clk = now; }
        else if ((now - t_clk) > pdMS_TO_TICKS(WDT_TIMEOUT_S * 1000))
            ESP_LOGW(TAG, "ClockTask stall!");

        if (g_heartbeat_display != s_dpy) { s_dpy = g_heartbeat_display; t_dpy = now; }
        else if ((now - t_dpy) > pdMS_TO_TICKS(WDT_TIMEOUT_S * 1000))
            ESP_LOGW(TAG, "DisplayTask stall!");

        if (g_heartbeat_network != s_net) { s_net = g_heartbeat_network; t_net = now; }
        else if ((now - t_net) > pdMS_TO_TICKS(WDT_TIMEOUT_S * 1000))
            ESP_LOGW(TAG, "NetworkTask stall!");

        if (g_heartbeat_power != s_pwr)   { s_pwr = g_heartbeat_power;   t_pwr = now; }
        else if ((now - t_pwr) > pdMS_TO_TICKS(WDT_TIMEOUT_S * 1000))
            ESP_LOGW(TAG, "PowerTask stall!");

        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
