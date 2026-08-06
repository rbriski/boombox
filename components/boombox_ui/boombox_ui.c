#include "boombox_ui.h"
#include "boombox_ui_font.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "boombox_audio.h"
#include "boombox_board.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "boombox_ui";

/* Native panel is 135x240 portrait; landscape is the likely
 * cassette-window fit (docs/rx5235-build-plan.html Phase 5). Swap+mirror
 * realizes the rotation; gap offsets follow the panel's non-visible
 * border in the rotated frame and may need adjustment once the physical
 * render is confirmed on the bench. */
#define BOOMBOX_UI_WIDTH BOOMBOX_TFT_HEIGHT_NATIVE
#define BOOMBOX_UI_HEIGHT BOOMBOX_TFT_WIDTH_NATIVE
#define BOOMBOX_UI_GAP_X 40
#define BOOMBOX_UI_GAP_Y 53

#define BOOMBOX_UI_SPI_HOST SPI2_HOST
#define BOOMBOX_UI_SPI_PCLK_HZ (20 * 1000 * 1000)
#define BOOMBOX_UI_BL_LEDC_CH LEDC_CHANNEL_0
#define BOOMBOX_UI_BL_LEDC_TMR LEDC_TIMER_0
#define BOOMBOX_UI_BL_DUTY_BITS LEDC_TIMER_10_BIT
#define BOOMBOX_UI_STATUS_PERIOD_MS 100
#define BOOMBOX_UI_TASK_PRIORITY 2
#define BOOMBOX_UI_TASK_STACK_SIZE 3072

typedef enum {
    BOOMBOX_UI_SCREEN_BOOT = 0,
    BOOMBOX_UI_SCREEN_WAITING,
    BOOMBOX_UI_SCREEN_PAIRING,
    BOOMBOX_UI_SCREEN_CONNECTED,
    BOOMBOX_UI_SCREEN_PLAYING,
    BOOMBOX_UI_SCREEN_PAUSED,
    BOOMBOX_UI_SCREEN_DISCONNECTED,
    BOOMBOX_UI_SCREEN_ERROR,
} boombox_ui_screen_t;

static esp_lcd_panel_handle_t s_panel = NULL;
static esp_lcd_panel_io_handle_t s_io = NULL;
static bool s_backlight_ready = false;
static uint16_t *s_framebuffer = NULL;
static TaskHandle_t s_status_task = NULL;
static _Atomic uint32_t s_refresh_count = 0;
static _Atomic bool s_error_requested = false;
static char s_error_message[24] = "ERROR";

static const boombox_ui_glyph_t *find_glyph(char ch)
{
    for (size_t i = 0; i < BOOMBOX_UI_FONT_GLYPH_COUNT; i++) {
        if (boombox_ui_font5x7[i].ch == ch) {
            return &boombox_ui_font5x7[i];
        }
    }
    ESP_LOGW(TAG, "no glyph for '%c' (0x%02x), rendering space", ch, (unsigned)ch);
    return &boombox_ui_font5x7[0]; /* space */
}

int boombox_ui_get_width(void)
{
    return BOOMBOX_UI_WIDTH;
}

int boombox_ui_get_height(void)
{
    return BOOMBOX_UI_HEIGHT;
}

esp_err_t boombox_ui_set_backlight(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    if (!s_backlight_ready) {
        ledc_timer_config_t timer_cfg = {
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .duty_resolution = BOOMBOX_UI_BL_DUTY_BITS,
            .timer_num = BOOMBOX_UI_BL_LEDC_TMR,
            .freq_hz = 5000,
            .clk_cfg = LEDC_AUTO_CLK,
        };
        ESP_RETURN_ON_ERROR(ledc_timer_config(&timer_cfg), TAG, "ledc timer");

        ledc_channel_config_t ch_cfg = {
            .gpio_num = BOOMBOX_TFT_BL_GPIO,
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = BOOMBOX_UI_BL_LEDC_CH,
            .timer_sel = BOOMBOX_UI_BL_LEDC_TMR,
            .duty = 0,
            .hpoint = 0,
        };
        ESP_RETURN_ON_ERROR(ledc_channel_config(&ch_cfg), TAG, "ledc channel");
        s_backlight_ready = true;
    }

    uint32_t max_duty = (1u << BOOMBOX_UI_BL_DUTY_BITS) - 1;
    uint32_t duty = (max_duty * percent) / 100;
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, BOOMBOX_UI_BL_LEDC_CH, duty), TAG, "ledc duty");
    ESP_RETURN_ON_ERROR(ledc_update_duty(LEDC_LOW_SPEED_MODE, BOOMBOX_UI_BL_LEDC_CH), TAG, "ledc update");
    ESP_LOGI(TAG, "backlight: %u%% (duty %" PRIu32 "/%" PRIu32 ")", percent, duty, max_duty);
    return ESP_OK;
}

esp_err_t boombox_ui_init(void)
{
    spi_bus_config_t bus_cfg = {
        .sclk_io_num = BOOMBOX_TFT_SCLK_GPIO,
        .mosi_io_num = BOOMBOX_TFT_MOSI_GPIO,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = BOOMBOX_UI_WIDTH * BOOMBOX_UI_HEIGHT * sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(BOOMBOX_UI_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO), TAG, "spi_bus_initialize");

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = BOOMBOX_TFT_CS_GPIO,
        .dc_gpio_num = BOOMBOX_TFT_DC_GPIO,
        .spi_mode = 0,
        .pclk_hz = BOOMBOX_UI_SPI_PCLK_HZ,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BOOMBOX_UI_SPI_HOST, &io_cfg, &s_io), TAG,
                        "esp_lcd_new_panel_io_spi");

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = BOOMBOX_TFT_RST_GPIO,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .data_endian = LCD_RGB_DATA_ENDIAN_BIG,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(s_io, &panel_cfg, &s_panel), TAG, "esp_lcd_new_panel_st7789");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "panel_reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "panel_init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(s_panel, true), TAG, "swap_xy");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel, true, false), TAG, "mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(s_panel, BOOMBOX_UI_GAP_X, BOOMBOX_UI_GAP_Y), TAG, "set_gap");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_panel, true), TAG, "invert_color");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "disp_on");

    s_framebuffer = heap_caps_calloc(BOOMBOX_UI_WIDTH * BOOMBOX_UI_HEIGHT, sizeof(uint16_t), MALLOC_CAP_DMA);
    ESP_RETURN_ON_FALSE(s_framebuffer != NULL, ESP_ERR_NO_MEM, TAG, "framebuffer alloc");

    ESP_LOGI(TAG, "ST7789 init done: %dx%d landscape, gap (%d,%d)", BOOMBOX_UI_WIDTH, BOOMBOX_UI_HEIGHT,
             BOOMBOX_UI_GAP_X, BOOMBOX_UI_GAP_Y);

    ESP_RETURN_ON_ERROR(boombox_ui_set_backlight(0), TAG, "backlight off");
    return ESP_OK;
}

esp_err_t boombox_ui_clear(uint16_t rgb565_color)
{
    if (s_framebuffer == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    for (int i = 0; i < BOOMBOX_UI_WIDTH * BOOMBOX_UI_HEIGHT; i++)
        s_framebuffer[i] = rgb565_color;
    return ESP_OK;
}

static esp_err_t draw_char(int x, int y, int scale, uint16_t color, char ch)
{
    const boombox_ui_glyph_t *g = find_glyph(ch);
    int w = 5 * scale;
    int h = 7 * scale;
    if (x < 0 || y < 0 || x + w > BOOMBOX_UI_WIDTH || y + h > BOOMBOX_UI_HEIGHT) {
        ESP_LOGW(TAG, "glyph '%c' at (%d,%d) size %dx%d off-panel (%dx%d), skipped", ch, x, y, w, h, BOOMBOX_UI_WIDTH,
                 BOOMBOX_UI_HEIGHT);
        return ESP_OK;
    }

    if (s_framebuffer == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    for (int row = 0; row < 7; row++) {
        uint8_t bits = g->rows[row];
        for (int col = 0; col < 5; col++) {
            bool on = (bits >> (4 - col)) & 0x1;
            uint16_t px = on ? color : 0x0000;
            for (int sy = 0; sy < scale; sy++) {
                for (int sx = 0; sx < scale; sx++) {
                    int bx = col * scale + sx;
                    int by = row * scale + sy;
                    s_framebuffer[(y + by) * BOOMBOX_UI_WIDTH + x + bx] = px;
                }
            }
        }
    }
    return ESP_OK;
}

esp_err_t boombox_ui_draw_text(int x, int y, int scale, uint16_t rgb565_color, const char *text)
{
    if (text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    int cursor_x = x;
    for (const char *p = text; *p != '\0'; p++) {
        esp_err_t err = draw_char(cursor_x, y, scale, rgb565_color, *p);
        if (err != ESP_OK) {
            return err;
        }
        cursor_x += 6 * scale; /* 5 wide + 1 spacing column */
    }
    return ESP_OK;
}

esp_err_t boombox_ui_present(void)
{
    if (s_panel == NULL || s_framebuffer == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel, 0, 0, BOOMBOX_UI_WIDTH, BOOMBOX_UI_HEIGHT, s_framebuffer);
    if (err == ESP_OK) {
        atomic_fetch_add(&s_refresh_count, 1);
    }
    return err;
}

esp_err_t boombox_ui_show_boot_screen(void)
{
    const uint16_t white = 0xFFFF;
    const char *line1 = "RX-5235";
    const char *line2 = "DIGITAL TAPE";
    const int scale1 = 3;
    const int scale2 = 2;

    ESP_RETURN_ON_ERROR(boombox_ui_clear(0x0000), TAG, "clear");

    int w1 = (int)strlen(line1) * 6 * scale1 - scale1;
    int w2 = (int)strlen(line2) * 6 * scale2 - scale2;
    int x1 = (BOOMBOX_UI_WIDTH - w1) / 2;
    int x2 = (BOOMBOX_UI_WIDTH - w2) / 2;
    int y1 = BOOMBOX_UI_HEIGHT / 2 - 7 * scale1 - 4;
    int y2 = BOOMBOX_UI_HEIGHT / 2 + 4;

    ESP_RETURN_ON_ERROR(boombox_ui_draw_text(x1 < 0 ? 0 : x1, y1 < 0 ? 0 : y1, scale1, white, line1), TAG, "line1");
    ESP_RETURN_ON_ERROR(boombox_ui_draw_text(x2 < 0 ? 0 : x2, y2 < 0 ? 0 : y2, scale2, white, line2), TAG, "line2");
    ESP_RETURN_ON_ERROR(boombox_ui_present(), TAG, "present framebuffer");

    ESP_RETURN_ON_ERROR(boombox_ui_set_backlight(60), TAG, "backlight on");
    ESP_LOGI(TAG, "boot screen drawn: \"%s\" / \"%s\"", line1, line2);
    return ESP_OK;
}

static const char *screen_name(boombox_ui_screen_t screen)
{
    static const char *const names[] = {
        "BOOT", "WAITING", "PAIRING", "CONNECTED", "PLAYING", "PAUSED", "DISCONNECTED", "ERROR",
    };

    return names[screen];
}

static boombox_ui_screen_t get_status_screen(void)
{
    if (atomic_load(&s_error_requested)) {
        return BOOMBOX_UI_SCREEN_ERROR;
    }

    boombox_audio_conn_state_t connection = boombox_audio_get_connection_state();
    boombox_audio_stream_state_t stream = boombox_audio_get_stream_state();

    if (connection == BOOMBOX_AUDIO_CONNECTING) {
        return BOOMBOX_UI_SCREEN_PAIRING;
    }
    if (connection == BOOMBOX_AUDIO_CONNECTED) {
        if (stream == BOOMBOX_AUDIO_STREAM_STARTED) {
            return BOOMBOX_UI_SCREEN_PLAYING;
        }
        return stream == BOOMBOX_AUDIO_STREAM_SUSPENDED ? BOOMBOX_UI_SCREEN_PAUSED : BOOMBOX_UI_SCREEN_CONNECTED;
    }
    if (connection == BOOMBOX_AUDIO_DISCONNECTING) {
        return BOOMBOX_UI_SCREEN_DISCONNECTED;
    }
    return BOOMBOX_UI_SCREEN_WAITING;
}

static esp_err_t render_status_screen(boombox_ui_screen_t screen)
{
    const char *detail = screen == BOOMBOX_UI_SCREEN_ERROR ? s_error_message : screen_name(screen);
    const int title_scale = 3;
    const int detail_scale = 2;
    const int title_width = ((int)strlen("RX-5235") * 6 - 1) * title_scale;
    const int detail_width = ((int)strlen(detail) * 6 - 1) * detail_scale;
    const int title_x = (BOOMBOX_UI_WIDTH - title_width) / 2;
    const int detail_x = (BOOMBOX_UI_WIDTH - detail_width) / 2;

    ESP_RETURN_ON_ERROR(boombox_ui_clear(0x0000), TAG, "clear status screen");
    ESP_RETURN_ON_ERROR(boombox_ui_draw_text(title_x < 0 ? 0 : title_x, 35, title_scale, 0xFFFF, "RX-5235"), TAG,
                        "draw title");
    ESP_RETURN_ON_ERROR(boombox_ui_draw_text(detail_x < 0 ? 0 : detail_x, 82, detail_scale,
                                             screen == BOOMBOX_UI_SCREEN_ERROR ? 0xF800 : 0x07E0, detail),
                        TAG, "draw status");
    return boombox_ui_present();
}

static void boombox_ui_status_task(void *arg)
{
    (void)arg;
    boombox_ui_screen_t previous = BOOMBOX_UI_SCREEN_BOOT;

    for (;;) {
        boombox_ui_screen_t current = get_status_screen();
        if (current != previous) {
            esp_err_t err = render_status_screen(current);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "status screen %s failed: %s", screen_name(current), esp_err_to_name(err));
            } else {
                ESP_LOGI(TAG, "status screen: %s, refreshes=%" PRIu32, screen_name(current),
                         boombox_ui_get_refresh_count());
            }
            previous = current;
        }
        vTaskDelay(pdMS_TO_TICKS(BOOMBOX_UI_STATUS_PERIOD_MS));
    }
}

esp_err_t boombox_ui_start_status_updates(void)
{
    if (s_panel == NULL || s_framebuffer == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_status_task != NULL) {
        return ESP_OK;
    }
    if (xTaskCreatePinnedToCore(boombox_ui_status_task, "BoomboxUi", BOOMBOX_UI_TASK_STACK_SIZE, NULL,
                                BOOMBOX_UI_TASK_PRIORITY, &s_status_task, 1) != pdPASS) {
        s_status_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

uint32_t boombox_ui_get_refresh_count(void)
{
    return atomic_load(&s_refresh_count);
}

void boombox_ui_set_error(const char *message)
{
    if (message == NULL || message[0] == '\0') {
        atomic_store(&s_error_requested, false);
        return;
    }
    snprintf(s_error_message, sizeof(s_error_message), "%s", message);
    atomic_store(&s_error_requested, true);
}
