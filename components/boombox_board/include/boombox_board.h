/*
 * boombox_board — the single source of truth for board-specific pin
 * assignments. Per AGENTS.md: no scattered `#define PIN_...` in app code.
 *
 * Board: LILYGO T-Display Q125 16 MB (classic ESP32-D0WDQ6-V3).
 * Pin values below are CONFIRMED against the official T-Display V18 pin
 * table and schematic (docs/rx5235-build-plan.html §10.1, §10.4). They are
 * the board's own onboard display pins (not general-purpose candidates),
 * so they carry board-identity confirmation rather than a §10.4 wiring
 * sign-off row.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* --- Display (ST7789, 4-wire SPI, 3.3 V) --- */
#define BOOMBOX_TFT_MOSI_GPIO 19
#define BOOMBOX_TFT_SCLK_GPIO 18
#define BOOMBOX_TFT_CS_GPIO   5
#define BOOMBOX_TFT_DC_GPIO   16
#define BOOMBOX_TFT_RST_GPIO  23
#define BOOMBOX_TFT_BL_GPIO   4

/* Panel geometry, native (portrait) orientation. */
#define BOOMBOX_TFT_WIDTH_NATIVE  135
#define BOOMBOX_TFT_HEIGHT_NATIVE 240

/* --- Audio (PCM5102A, external I2S, Philips mode) --- */
/* Verified 2026-07-18 (§10.4 map B): BCK/LRCK/DIN. Board-level record only —
 * boombox_audio owns actually driving these. */
#define BOOMBOX_I2S_BCK_GPIO  26
#define BOOMBOX_I2S_LRCK_GPIO 27
#define BOOMBOX_I2S_DIN_GPIO  25

#ifdef __cplusplus
}
#endif
