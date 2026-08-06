/*
 * boombox_audio_i2s — private I2S sink for boombox_audio. Not a public
 * component header; only boombox_audio.c includes this.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_a2dp_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Open the external I2S TX channel on the boombox_board pin mapping
 * (BCK/LRCK/DIN = GPIO26/27/25), Philips slot timing at 44.1 kHz / 16-bit
 * stereo — the M2-verified match for the PCM5102A's measured FMT-low strap
 * (docs/rx5235-build-plan.html §10.4, M2). A no-op if already open. */
void boombox_audio_i2s_open(void);

/* Stop and free the I2S channel, its ring buffer, and its drain task. */
void boombox_audio_i2s_close(void);

/* Enable the I2S channel and start the ring-buffer drain task. */
void boombox_audio_i2s_start(void);

/* Disable the I2S channel (the channel handle stays allocated). */
void boombox_audio_i2s_stop(void);

/* Reconfigure sample rate / channel count from the negotiated A2DP codec.
 * Only SBC is registered by this stack, matching the pinned example.
 * Philips slot timing is preserved across reconfiguration. */
void boombox_audio_i2s_reconfigure(const esp_a2d_mcc_t *mcc);

/* Queue decoded PCM audio for output. Returns the number of bytes accepted
 * (0 if dropped because the ring buffer is full). Never blocks the caller. */
size_t boombox_audio_i2s_write(const uint8_t *data, size_t len);

/* Mark whether A2DP is actively streaming. The drain task uses this to
 * distinguish real playback starvation from its normal prefetch transition. */
void boombox_audio_i2s_set_streaming(bool active);

/* Number of drain-task ring-buffer underflows while A2DP was actively
 * streaming, since boot. */
uint32_t boombox_audio_i2s_get_underrun_count(void);

#ifdef __cplusplus
}
#endif
