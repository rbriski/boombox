/*
 * boombox_ui — ST7789 display driver setup and screen-state rendering.
 * Phase 5 status rendering polls the bounded boombox_audio state surface;
 * it never participates in Bluetooth or I2S callbacks. Keep redraws
 * transition-driven, not a constant full-screen loop.
 */
#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bring up SPI bus, ST7789 panel, and backlight. Landscape orientation
 * (240x135) — the likely cassette-window fit; rotation is a call-site
 * decision pending physical confirmation, see boombox_ui_get_width/height. */
esp_err_t boombox_ui_init(void);

/* Panel geometry after the orientation applied by boombox_ui_init(). */
int boombox_ui_get_width(void);
int boombox_ui_get_height(void);

/* Backlight brightness, 0-100 (PWM duty on BOOMBOX_TFT_BL_GPIO). */
esp_err_t boombox_ui_set_backlight(uint8_t percent);

/* Fill the offscreen framebuffer with a single RGB565 color. Call
 * boombox_ui_present() to send the completed framebuffer to the panel. */
esp_err_t boombox_ui_clear(uint16_t rgb565_color);

/* Draw one line of text (5x7 glyphs, integer scale) at (x, y) in the given
 * color into the offscreen framebuffer. Call boombox_ui_present() after all
 * drawing is complete. Glyph set: space - / 0-9 A D E G I L P R T X (enough
 * for the Phase 5 boot screen; extend boombox_ui_font.c as needed). */
esp_err_t boombox_ui_draw_text(int x, int y, int scale, uint16_t rgb565_color, const char *text);

/* Send the current offscreen framebuffer to the panel. */
esp_err_t boombox_ui_present(void);

/* Phase 5 boot screen: "RX-5235" / "DIGITAL TAPE", centered. */
esp_err_t boombox_ui_show_boot_screen(void);

/* Start the low-priority, APP_CPU-pinned status task after audio is ready.
 * It polls bounded boombox_audio state at 10 Hz but presents only when the
 * screen state changes, capping display DMA work. */
esp_err_t boombox_ui_start_status_updates(void);

/* Number of complete framebuffer presents performed since boot. */
uint32_t boombox_ui_get_refresh_count(void);

/* Show the error screen on the next bounded status update. Passing NULL or
 * an empty string clears the error condition. */
void boombox_ui_set_error(const char *message);

#ifdef __cplusplus
}
#endif
