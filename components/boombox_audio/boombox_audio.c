/*
 * boombox_audio — see include/boombox_audio.h for the port summary.
 *
 * Ported from the pinned example
 * (examples/bluetooth/bluedroid/classic_bt/a2dp_sink_stream and its
 * common/ helper components: bredr_app_common_utils, bt_app_core_utils,
 * a2dp_utils/a2dp_sink_common_utils, a2dp_utils/a2dp_sink_int_codec_utils;
 * .espidf-version), consolidated into one component and trimmed to what
 * Gate C needs:
 *   - AVRCP, the external-codec variant, and the internal-DAC/idle output
 *     variants are not ported (out of scope for this prerequisite; M2 only
 *     exercised A2DP + external I2S).
 *   - GAP/device event logging is trimmed to what's useful without a UI
 *     (no Secure Simple Pairing prompt path — SSP is left at the example's
 *     default-disabled setting matching M2's fixed-pin-code pairing).
 *   - Connection/stream state and packet count are tracked in bounded
 *     atomics and exposed via boombox_audio_get_* for Gate C to poll.
 * The I2S sink (boombox_audio_i2s.c) carries the one behavioral delta from
 * the pinned example, documented there: Philips slot timing in place of
 * the stock MSB/left-justified macro, required by the PCM5102A's measured
 * FMT-low strap.
 */

#include "boombox_audio.h"
#include "boombox_audio_i2s.h"

#include <stdatomic.h>
#include <string.h>

#include "esp_a2dp_api.h"
#include "esp_bt.h"
#include "esp_bt_device.h"
#include "esp_bt_main.h"
#include "esp_check.h"
#include "esp_gap_bt_api.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char *TAG = "boombox_audio";

#define BOOMBOX_AUDIO_DEVICE_NAME "Boombox"

/*******************************
 * BOUNDED STATE (Gate C surface)
 ******************************/

static _Atomic boombox_audio_conn_state_t s_conn_state = BOOMBOX_AUDIO_DISCONNECTED;
static _Atomic boombox_audio_stream_state_t s_stream_state = BOOMBOX_AUDIO_STREAM_SUSPENDED;
static _Atomic uint32_t s_packet_count = 0;

boombox_audio_conn_state_t boombox_audio_get_connection_state(void)
{
    return atomic_load(&s_conn_state);
}

boombox_audio_stream_state_t boombox_audio_get_stream_state(void)
{
    return atomic_load(&s_stream_state);
}

uint32_t boombox_audio_get_packet_count(void)
{
    return atomic_load(&s_packet_count);
}

/*******************************
 * APP TASK WORK DISPATCH
 * Ported from bt_app_core_utils: Bluedroid callbacks run in the BT stack's
 * own context, so app-level handling is dispatched onto a dedicated queue
 * + task rather than run inline.
 ******************************/

typedef void (*bt_app_cb_t)(uint16_t event, void *param);

typedef struct {
    uint16_t event;
    bt_app_cb_t cb;
    void *param;
} bt_app_msg_t;

enum {
    BT_APP_EVT_STACK_UP = 0,
};

static QueueHandle_t s_bt_app_task_queue = NULL;
static TaskHandle_t s_bt_app_task_handle = NULL;

static bool bt_app_work_dispatch(bt_app_cb_t cb, uint16_t event, const void *params, int param_len)
{
    bt_app_msg_t msg = {
        .event = event,
        .cb = cb,
        .param = NULL,
    };

    if (param_len > 0 && params != NULL) {
        msg.param = malloc((size_t)param_len);
        if (msg.param == NULL) {
            return false;
        }
        memcpy(msg.param, params, (size_t)param_len);
    }

    if (xQueueSend(s_bt_app_task_queue, &msg, pdMS_TO_TICKS(10)) != pdTRUE) {
        ESP_LOGE(TAG, "work dispatch queue send failed, event 0x%x", event);
        free(msg.param);
        return false;
    }
    return true;
}

static void bt_app_task_handler(void *arg)
{
    bt_app_msg_t msg;
    for (;;) {
        if (xQueueReceive(s_bt_app_task_queue, &msg, portMAX_DELAY) == pdTRUE) {
            if (msg.cb) {
                msg.cb(msg.event, msg.param);
            }
            free(msg.param);
        }
    }
}

/*******************************
 * GAP / DEVICE CALLBACKS
 * Trimmed from bredr_app_common_utils: logging only, no SSP prompt path
 * (matches M2's fixed-pin-code pairing / SSP disabled).
 ******************************/

static void bt_app_dev_cb(esp_bt_dev_cb_event_t event, esp_bt_dev_cb_param_t *param)
{
    if (event == ESP_BT_DEV_NAME_RES_EVT) {
        if (param->name_res.status == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "local device name: %s", param->name_res.name);
        } else {
            ESP_LOGW(TAG, "get local device name failed, status %d", param->name_res.status);
        }
    }
}

static void bt_app_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_BT_GAP_AUTH_CMPL_EVT:
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "pairing success: %s", param->auth_cmpl.device_name);
        } else {
            ESP_LOGW(TAG, "pairing failed, status %d", param->auth_cmpl.stat);
        }
        break;
    case ESP_BT_GAP_ACL_CONN_CMPL_STAT_EVT:
        ESP_LOGI(TAG, "ACL connected, status 0x%x", param->acl_conn_cmpl_stat.stat);
        break;
    case ESP_BT_GAP_ACL_DISCONN_CMPL_STAT_EVT:
        ESP_LOGI(TAG, "ACL disconnected, reason 0x%x", param->acl_disconn_cmpl_stat.reason);
        break;
    default:
        break;
    }
}

/*******************************
 * A2DP CALLBACKS
 * Ported from a2dp_sink_common_utils.c / a2dp_sink_int_codec_utils.c.
 ******************************/

static const char *a2d_conn_state_str[] = {"Disconnected", "Connecting", "Connected", "Disconnecting"};
static const char *a2d_audio_state_str[] = {"Suspended", "Started"};

static void bt_a2d_evt_hdl(uint16_t event, void *param)
{
    esp_a2d_cb_param_t *a2d = (esp_a2d_cb_param_t *)param;

    switch (event) {
    case ESP_A2D_CONNECTION_STATE_EVT: {
        uint8_t *bda = a2d->conn_stat.remote_bda;
        ESP_LOGI(TAG, "A2DP connection state: %s, [%02x:%02x:%02x:%02x:%02x:%02x]",
                 a2d_conn_state_str[a2d->conn_stat.state], bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);

        switch (a2d->conn_stat.state) {
        case ESP_A2D_CONNECTION_STATE_CONNECTING:
            atomic_store(&s_conn_state, BOOMBOX_AUDIO_CONNECTING);
            boombox_audio_i2s_open();
            break;
        case ESP_A2D_CONNECTION_STATE_CONNECTED:
            atomic_store(&s_conn_state, BOOMBOX_AUDIO_CONNECTED);
            esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
            boombox_audio_i2s_start();
            break;
        case ESP_A2D_CONNECTION_STATE_DISCONNECTING:
            atomic_store(&s_conn_state, BOOMBOX_AUDIO_DISCONNECTING);
            break;
        case ESP_A2D_CONNECTION_STATE_DISCONNECTED:
        default:
            atomic_store(&s_conn_state, BOOMBOX_AUDIO_DISCONNECTED);
            esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
            boombox_audio_i2s_stop();
            boombox_audio_i2s_close();
            break;
        }
        break;
    }
    case ESP_A2D_AUDIO_STATE_EVT: {
        ESP_LOGI(TAG, "A2DP audio state: %s", a2d_audio_state_str[a2d->audio_stat.state]);
        atomic_store(&s_stream_state, a2d->audio_stat.state == ESP_A2D_AUDIO_STATE_STARTED
                                          ? BOOMBOX_AUDIO_STREAM_STARTED
                                          : BOOMBOX_AUDIO_STREAM_SUSPENDED);
        if (a2d->audio_stat.state == ESP_A2D_AUDIO_STATE_STARTED) {
            atomic_store(&s_packet_count, 0);
        }
        break;
    }
    case ESP_A2D_AUDIO_CFG_EVT:
        ESP_LOGI(TAG, "A2DP audio codec configured, type %d", a2d->audio_cfg.mcc.type);
        boombox_audio_i2s_reconfigure(&a2d->audio_cfg.mcc);
        break;
    case ESP_A2D_PROF_STATE_EVT:
        ESP_LOGI(TAG, "A2DP profile %s", a2d->a2d_prof_stat.init_state == ESP_A2D_INIT_SUCCESS ? "init complete" : "deinit complete");
        break;
    case ESP_A2D_SNK_GET_DELAY_VALUE_EVT:
        /* Application-layer delay (I2S ring buffer + task latency) added
         * on top of the stack-reported delay, per the pinned example. */
        esp_a2d_sink_set_delay_value(a2d->a2d_get_delay_value_stat.delay_value + 50);
        break;
    default:
        break;
    }
}

static void bt_app_a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param)
{
    bt_app_work_dispatch(bt_a2d_evt_hdl, event, param, sizeof(esp_a2d_cb_param_t));
}

static void bt_app_a2d_data_cb(const uint8_t *data, uint32_t len)
{
    boombox_audio_i2s_write(data, len);
    atomic_fetch_add(&s_packet_count, 1);
}

static void bt_av_hdl_stack_evt(uint16_t event, void *param)
{
    (void)param;
    if (event != BT_APP_EVT_STACK_UP) {
        return;
    }

    esp_bt_gap_set_device_name(BOOMBOX_AUDIO_DEVICE_NAME);
    esp_bt_dev_register_callback(bt_app_dev_cb);
    esp_bt_gap_register_callback(bt_app_gap_cb);

    esp_a2d_register_callback(bt_app_a2d_cb);
    ESP_ERROR_CHECK(esp_a2d_sink_init());
    esp_a2d_sink_register_data_callback(bt_app_a2d_data_cb);
    esp_a2d_sink_get_delay_value();

    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
    ESP_LOGI(TAG, "A2DP sink up, discoverable as \"%s\"", BOOMBOX_AUDIO_DEVICE_NAME);
}

/*******************************
 * INIT
 * Ported from bredr_app_common_utils.c.
 ******************************/

esp_err_t boombox_audio_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "nvs_flash_erase");
        err = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(err, TAG, "nvs_flash_init");

    /* Classic BT only; release the BLE controller memory the pinned
     * example also releases. */
    ESP_RETURN_ON_ERROR(esp_bt_controller_mem_release(ESP_BT_MODE_BLE), TAG, "controller_mem_release");

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_bt_controller_init(&bt_cfg), TAG, "controller_init");
    ESP_RETURN_ON_ERROR(esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT), TAG, "controller_enable");

    esp_bluedroid_config_t bluedroid_cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    bluedroid_cfg.ssp_en = false; /* fixed-pin-code pairing, matching M2 */
    ESP_RETURN_ON_ERROR(esp_bluedroid_init_with_cfg(&bluedroid_cfg), TAG, "bluedroid_init");
    ESP_RETURN_ON_ERROR(esp_bluedroid_enable(), TAG, "bluedroid_enable");

    esp_bt_pin_code_t pin_code = {'1', '2', '3', '4'};
    esp_bt_gap_set_pin(ESP_BT_PIN_TYPE_FIXED, 4, pin_code);

    s_bt_app_task_queue = xQueueCreate(10, sizeof(bt_app_msg_t));
    if (s_bt_app_task_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(bt_app_task_handler, "BoomboxAudioApp", 3072, NULL, 10, &s_bt_app_task_handle) != pdPASS) {
        vQueueDelete(s_bt_app_task_queue);
        s_bt_app_task_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    bt_app_work_dispatch(bt_av_hdl_stack_evt, BT_APP_EVT_STACK_UP, NULL, 0);
    return ESP_OK;
}
