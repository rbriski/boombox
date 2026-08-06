#!/usr/bin/env bash
set -euo pipefail

# Static regression contract for bb-6fh. The real pairing behavior requires a
# physical Bluetooth source, but these source-level invariants prevent a
# return to the legacy-only configuration that modern hosts reject.
source_file="components/boombox_audio/boombox_audio.c"

require() {
    local pattern="$1"
    if ! grep -Eq "$pattern" "$source_file"; then
        echo "Bluetooth pairing contract missing: $pattern" >&2
        exit 1
    fi
}

reject() {
    local pattern="$1"
    if grep -Eq "$pattern" "$source_file"; then
        echo "Bluetooth pairing contract violated: $pattern" >&2
        exit 1
    fi
}

require 'esp_bt_io_cap_t io_capability = ESP_BT_IO_CAP_NONE;'
require 'esp_bt_gap_set_security_param\(ssp_param, &io_capability, sizeof\(io_capability\)\)'
require 'esp_bt_gap_set_pin\(ESP_BT_PIN_TYPE_FIXED, 4, pin_code\)'
reject 'ssp_en[[:space:]]*=[[:space:]]*false'

echo "Bluetooth pairing security contract: PASS"
