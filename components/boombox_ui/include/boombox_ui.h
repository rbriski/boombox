/*
 * boombox_ui — ST7789 display driver setup and screen-state rendering.
 * Display-only for now (Phase 5, docs/rx5235-build-plan.html): no
 * Bluetooth/audio coupling here. Keep redraws event-driven, not a
 * constant full-screen loop, once this grows past the boot screen.
 */
#pragma once

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

/* Fill the whole panel with a single RGB565 color. */
esp_err_t boombox_ui_clear(uint16_t rgb565_color);

/* Draw one line of text (5x7 glyphs, integer scale) at (x, y) in the given
 * color on a black background. Glyph set: space - / 0-9 A D E G I L P R T X
 * (enough for the Phase 5 boot screen; extend boombox_ui_font.c as needed). */
esp_err_t boombox_ui_draw_text(int x, int y, int scale, uint16_t rgb565_color, const char *text);

/* Phase 5 boot screen: "RX-5235" / "DIGITAL TAPE", centered. */
esp_err_t boombox_ui_show_boot_screen(void);

#ifdef __cplusplus
}
#endif
