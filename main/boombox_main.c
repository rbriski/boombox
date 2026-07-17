/*
 * Boombox — board-neutral serial hello-world.
 *
 * The physical board model is unconfirmed (docs/hardware.html), so this app must
 * not touch any GPIO: it uses only the default console UART via the on-board
 * USB bridge. It prints what the chip can report about itself, then heartbeats
 * so a monitor session shows the firmware is alive.
 */

#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#define HEARTBEAT_PERIOD_MS 5000

static const char *TAG = "boombox";

void app_main(void)
{
    const esp_app_desc_t *app = esp_app_get_description();
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    unsigned major_rev = chip_info.revision / 100;
    unsigned minor_rev = chip_info.revision % 100;

    ESP_LOGI(TAG, "app: %s version %s (board-neutral, console UART only)", app->project_name, app->version);
    ESP_LOGI(TAG, "ESP-IDF: %s", esp_get_idf_version());
    ESP_LOGI(TAG, "Chip: %s, %d core(s), revision v%u.%u", CONFIG_IDF_TARGET, chip_info.cores, major_rev, minor_rev);
    ESP_LOGI(TAG, "Features:%s%s%s%s",
             (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? " WiFi" : "",
             (chip_info.features & CHIP_FEATURE_BT) ? " BT" : "",
             (chip_info.features & CHIP_FEATURE_BLE) ? " BLE" : "",
             (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? " embedded-flash" : " external-flash");

    /* Flash size is unknown for this board until first probed on hardware
     * (docs/setup-research.html §10) — report it whenever we do run. */
    uint32_t flash_size = 0;
    esp_err_t flash_err = esp_flash_get_size(NULL, &flash_size);
    if (flash_err == ESP_OK) {
        ESP_LOGI(TAG, "Flash size: %" PRIu32 " MB", flash_size / (1024 * 1024));
    } else {
        ESP_LOGW(TAG, "Flash size unavailable: %s", esp_err_to_name(flash_err));
    }

    ESP_LOGI(TAG, "Minimum free heap: %" PRIu32 " bytes", esp_get_minimum_free_heap_size());
    ESP_LOGI(TAG, "Board model unconfirmed — no peripherals enabled (see docs/hardware.html)");

    for (;;) {
        int64_t uptime_s = esp_timer_get_time() / 1000000;
        ESP_LOGI(TAG, "heartbeat: uptime %" PRId64 " s, free heap %" PRIu32 " bytes",
                 uptime_s, esp_get_free_heap_size());
        vTaskDelay(pdMS_TO_TICKS(HEARTBEAT_PERIOD_MS));
    }
}
