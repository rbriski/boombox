/*
 * Ported from the pinned example's internal-codec I2S sink
 * (examples/bluetooth/bluedroid/classic_bt/common/a2dp_utils/
 * a2dp_sink_int_codec_utils/audio_sink_service_i2s.c, .espidf-version),
 * trimmed to only the external-I2S output path (this board has no internal
 * DAC or idle-output build variant) and re-pinned to boombox_board.
 *
 * Local delta from the stock file (the one that matters on this hardware):
 * every I2S_STD_MSB_SLOT_DEFAULT_CONFIG call site below is
 * I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG instead. The stock MSB/left-justified
 * macro does not match the PCM5102A's measured FMT-low strap
 * (docs/rx5235-build-plan.html §10.4, M2) — M2's hardware pass required
 * this exact substitution on the throwaway v6.0.2 project this component
 * ports from.
 */

#include "boombox_audio_i2s.h"

#include <string.h>

#include "boombox_board.h"

#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "boombox_audio_i2s";

#define RINGBUF_HIGHEST_WATER_LEVEL  (32 * 1024)
#define RINGBUF_PREFETCH_WATER_LEVEL (20 * 1024)

typedef enum {
    RINGBUFFER_MODE_PROCESSING,  /* draining while buffering incoming audio */
    RINGBUFFER_MODE_PREFETCHING, /* buffering before draining resumes */
    RINGBUFFER_MODE_DROPPING,    /* buffer full: dropping incoming audio */
} ringbuffer_mode_t;

typedef enum {
    CHANNEL_STATUS_IDLE,
    CHANNEL_STATUS_OPENED,
    CHANNEL_STATUS_ENABLED,
} chan_status_t;

typedef struct {
    i2s_chan_handle_t tx_chan;
    chan_status_t chan_st;
    TaskHandle_t write_task_handle;
    RingbufHandle_t ringbuf;
    SemaphoreHandle_t write_semaphore;
    ringbuffer_mode_t ringbuffer_mode;
} boombox_audio_i2s_cb_t;

static boombox_audio_i2s_cb_t s_cb;

static void boombox_audio_i2s_task_handler(void *arg)
{
    uint8_t *data = NULL;
    size_t item_size = 0;
    /* `dma_frame_num * dma_desc_num` bytes per drain call is the trade-off
     * this pinned example uses; ported unchanged. */
    const size_t item_size_upto = 240 * 6;
    size_t bytes_written = 0;

    for (;;) {
        if (xSemaphoreTake(s_cb.write_semaphore, portMAX_DELAY) == pdTRUE) {
            for (;;) {
                item_size = 0;
                data = (uint8_t *)xRingbufferReceiveUpTo(s_cb.ringbuf, &item_size, pdMS_TO_TICKS(20), item_size_upto);
                if (item_size == 0) {
                    ESP_LOGI(TAG, "ringbuffer underflowed, mode -> PREFETCHING");
                    s_cb.ringbuffer_mode = RINGBUFFER_MODE_PREFETCHING;
                    break;
                }
                if (s_cb.chan_st == CHANNEL_STATUS_ENABLED) {
                    i2s_channel_write(s_cb.tx_chan, data, item_size, &bytes_written, portMAX_DELAY);
                }
                vRingbufferReturnItem(s_cb.ringbuf, (void *)data);
            }
        }
    }
}

void boombox_audio_i2s_open(void)
{
    if (s_cb.chan_st != CHANNEL_STATUS_IDLE) {
        ESP_LOGW(TAG, "already open, skipping");
        return;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(44100),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = BOOMBOX_I2S_BCK_GPIO,
            .ws = BOOMBOX_I2S_LRCK_GPIO,
            .dout = BOOMBOX_I2S_DIN_GPIO,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &s_cb.tx_chan, NULL));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_cb.tx_chan, &std_cfg));
    s_cb.chan_st = CHANNEL_STATUS_OPENED;
    ESP_LOGI(TAG, "I2S opened: BCK=%d LRCK=%d DIN=%d, Philips 44.1kHz/16-bit stereo",
             BOOMBOX_I2S_BCK_GPIO, BOOMBOX_I2S_LRCK_GPIO, BOOMBOX_I2S_DIN_GPIO);
}

void boombox_audio_i2s_close(void)
{
    boombox_audio_i2s_stop();

    if (s_cb.write_task_handle) {
        vTaskDelete(s_cb.write_task_handle);
        s_cb.write_task_handle = NULL;
    }
    if (s_cb.ringbuf) {
        vRingbufferDelete(s_cb.ringbuf);
        s_cb.ringbuf = NULL;
    }
    if (s_cb.write_semaphore) {
        vSemaphoreDelete(s_cb.write_semaphore);
        s_cb.write_semaphore = NULL;
    }
    if (s_cb.chan_st == CHANNEL_STATUS_OPENED) {
        ESP_ERROR_CHECK(i2s_del_channel(s_cb.tx_chan));
        s_cb.chan_st = CHANNEL_STATUS_IDLE;
    }
    memset(&s_cb, 0, sizeof(s_cb));
}

void boombox_audio_i2s_start(void)
{
    if (s_cb.chan_st != CHANNEL_STATUS_OPENED) {
        ESP_LOGE(TAG, "%s: wrong channel state %d", __func__, s_cb.chan_st);
        return;
    }
    ESP_ERROR_CHECK(i2s_channel_enable(s_cb.tx_chan));

    s_cb.ringbuffer_mode = RINGBUFFER_MODE_PREFETCHING;
    if (s_cb.write_semaphore == NULL && (s_cb.write_semaphore = xSemaphoreCreateBinary()) == NULL) {
        ESP_LOGE(TAG, "%s: semaphore create failed", __func__);
        goto err_sem;
    }
    if (s_cb.ringbuf == NULL &&
        (s_cb.ringbuf = xRingbufferCreate(RINGBUF_HIGHEST_WATER_LEVEL, RINGBUF_TYPE_BYTEBUF)) == NULL) {
        ESP_LOGE(TAG, "%s: ringbuffer create failed", __func__);
        goto err_rb;
    }
    if (s_cb.write_task_handle == NULL) {
        if (xTaskCreate(boombox_audio_i2s_task_handler, "BoomboxAudioI2S", 4 * 1024, NULL,
                         configMAX_PRIORITIES - 3, &s_cb.write_task_handle) != pdPASS) {
            ESP_LOGE(TAG, "%s: task create failed", __func__);
            goto err_task;
        }
    }
    s_cb.chan_st = CHANNEL_STATUS_ENABLED;
    return;

err_task:
    vRingbufferDelete(s_cb.ringbuf);
    s_cb.ringbuf = NULL;
err_rb:
    vSemaphoreDelete(s_cb.write_semaphore);
    s_cb.write_semaphore = NULL;
err_sem:
    i2s_channel_disable(s_cb.tx_chan);
}

void boombox_audio_i2s_stop(void)
{
    if (s_cb.chan_st == CHANNEL_STATUS_ENABLED) {
        ESP_ERROR_CHECK(i2s_channel_disable(s_cb.tx_chan));
        s_cb.chan_st = CHANNEL_STATUS_OPENED;
    }
}

void boombox_audio_i2s_reconfigure(const esp_a2d_mcc_t *mcc)
{
    boombox_audio_i2s_stop();

    /* Only SBC is registered (see boombox_audio.c); other types are
     * unreachable in practice but guarded rather than assumed. */
    if (mcc->type != ESP_A2D_MCT_SBC) {
        ESP_LOGW(TAG, "unexpected codec type %d, leaving I2S at prior config", mcc->type);
        return;
    }

    int sample_rate = 16000;
    if (mcc->cie.sbc_info.samp_freq & ESP_A2D_SBC_CIE_SF_32K) {
        sample_rate = 32000;
    } else if (mcc->cie.sbc_info.samp_freq & ESP_A2D_SBC_CIE_SF_44K) {
        sample_rate = 44100;
    } else if (mcc->cie.sbc_info.samp_freq & ESP_A2D_SBC_CIE_SF_48K) {
        sample_rate = 48000;
    }
    int ch_count = (mcc->cie.sbc_info.ch_mode & ESP_A2D_SBC_CIE_CH_MODE_MONO) ? 1 : 2;

    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate);
    i2s_std_slot_config_t slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, ch_count);
    ESP_ERROR_CHECK(i2s_channel_reconfig_std_clock(s_cb.tx_chan, &clk_cfg));
    ESP_ERROR_CHECK(i2s_channel_reconfig_std_slot(s_cb.tx_chan, &slot_cfg));
    ESP_LOGI(TAG, "reconfigured: %d Hz, %d channel(s), Philips slot timing", sample_rate, ch_count);
}

size_t boombox_audio_i2s_write(const uint8_t *data, size_t len)
{
    size_t item_size = 0;
    BaseType_t sent = pdFALSE;

    if (s_cb.ringbuf == NULL) {
        return 0;
    }

    if (s_cb.ringbuffer_mode == RINGBUFFER_MODE_DROPPING) {
        vRingbufferGetInfo(s_cb.ringbuf, NULL, NULL, NULL, NULL, &item_size);
        if (item_size <= RINGBUF_PREFETCH_WATER_LEVEL) {
            ESP_LOGI(TAG, "ringbuffer drained, mode -> PROCESSING");
            s_cb.ringbuffer_mode = RINGBUFFER_MODE_PROCESSING;
        }
        return 0;
    }

    sent = xRingbufferSend(s_cb.ringbuf, (void *)data, len, 0);
    if (!sent) {
        ESP_LOGW(TAG, "ringbuffer full, mode -> DROPPING");
        s_cb.ringbuffer_mode = RINGBUFFER_MODE_DROPPING;
    }

    if (s_cb.ringbuffer_mode == RINGBUFFER_MODE_PREFETCHING) {
        vRingbufferGetInfo(s_cb.ringbuf, NULL, NULL, NULL, NULL, &item_size);
        if (item_size >= RINGBUF_PREFETCH_WATER_LEVEL) {
            ESP_LOGI(TAG, "ringbuffer primed, mode -> PROCESSING");
            s_cb.ringbuffer_mode = RINGBUFFER_MODE_PROCESSING;
            if (xSemaphoreGive(s_cb.write_semaphore) == pdFALSE) {
                ESP_LOGE(TAG, "semaphore give failed");
            }
        }
    }

    return sent ? len : 0;
}
