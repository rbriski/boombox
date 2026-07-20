/*
 * boombox_audio — Bluetooth Classic A2DP sink + external I2S output for the
 * PCM5102A, ported from the pinned ESP-IDF example
 * (examples/bluetooth/bluedroid/classic_bt/a2dp_sink_stream, internal-codec
 * + external-I2S configuration; version in .espidf-version) after the M2
 * hardware pass (docs/rx5235-build-plan.html, M2 / Gate C prerequisite,
 * bb-7gl.1). The only functional delta from the stock example's I2S sink is
 * the slot timing: I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG in place of the
 * stock I2S_STD_MSB_SLOT_DEFAULT_CONFIG, required by the PCM5102A's
 * measured FMT-low (Philips I2S) strap state. See boombox_audio_i2s.c for
 * the two call sites and components/boombox_board for the pin mapping this
 * drives (BCK/LRCK/DIN = GPIO26/27/25).
 *
 * main stays thin: call boombox_audio_init() once at boot. Gate C polls the
 * bounded state getters below; it does not touch A2DP/I2S internals.
 */
#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BOOMBOX_AUDIO_DISCONNECTED = 0,
    BOOMBOX_AUDIO_CONNECTING,
    BOOMBOX_AUDIO_CONNECTED,
    BOOMBOX_AUDIO_DISCONNECTING,
} boombox_audio_conn_state_t;

typedef enum {
    BOOMBOX_AUDIO_STREAM_SUSPENDED = 0,
    BOOMBOX_AUDIO_STREAM_STARTED,
} boombox_audio_stream_state_t;

/* Bring up NVS (if not already initialized), the classic-BT controller,
 * Bluedroid, and the A2DP sink profile; open (but do not yet enable) the
 * external I2S TX channel on the boombox_board pin mapping. Device becomes
 * discoverable/connectable as "Boombox" on return. Software-only: does not
 * touch flash beyond NVS, does not assert physical playback. */
esp_err_t boombox_audio_init(void);

/* Current A2DP ACL connection state, updated from the Bluedroid event
 * callback. Bounded, non-blocking, safe to poll from any task. */
boombox_audio_conn_state_t boombox_audio_get_connection_state(void);

/* Current A2DP audio datapath state (decoded-PCM stream started/suspended). */
boombox_audio_stream_state_t boombox_audio_get_stream_state(void);

/* Running count of decoded-PCM audio packets delivered to the I2S sink
 * since boot. A liveness counter for Gate C, not a precise accounting —
 * it saturates rather than reporting meaningfully past UINT32_MAX. */
uint32_t boombox_audio_get_packet_count(void);

#ifdef __cplusplus
}
#endif
