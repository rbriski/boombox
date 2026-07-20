/*
 * Boombox — display + audio bring-up (Phase 5/M3, Gate C prerequisite
 * bb-7gl.1).
 *
 * Board identity and the onboard ST7789 pin set are confirmed
 * (docs/hardware.html, docs/rx5235-build-plan.html §10.1/§10.4); the M2
 * A2DP/I2S path is confirmed via components/boombox_audio. main stays
 * thin: init the display, draw the boot screen, init audio, then
 * heartbeat. This session builds and documents the port only — it does not
 * flash or claim physical verification (that remains bb-qea/bb-475).
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

#include "boombox_audio.h"
#include "boombox_ui.h"

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

    esp_err_t ui_err = boombox_ui_init();
    if (ui_err == ESP_OK) {
        ui_err = boombox_ui_show_boot_screen();
    }
    if (ui_err != ESP_OK) {
        ESP_LOGE(TAG, "display bring-up failed: %s", esp_err_to_name(ui_err));
    }

    esp_err_t audio_err = boombox_audio_init();
    if (audio_err != ESP_OK) {
        ESP_LOGE(TAG, "audio bring-up failed: %s", esp_err_to_name(audio_err));
    }

    for (;;) {
        int64_t uptime_s = esp_timer_get_time() / 1000000;
        ESP_LOGI(TAG, "heartbeat: uptime %" PRId64 " s, free heap %" PRIu32 " bytes, "
                      "audio conn %d stream %d packets %" PRIu32,
                 uptime_s, esp_get_free_heap_size(),
                 boombox_audio_get_connection_state(), boombox_audio_get_stream_state(),
                 boombox_audio_get_packet_count());
        vTaskDelay(pdMS_TO_TICKS(HEARTBEAT_PERIOD_MS));
    }
}
