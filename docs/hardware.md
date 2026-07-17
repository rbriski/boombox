# Boombox Hardware Notes

**Status: board model UNCONFIRMED — all pin-dependent work is blocked on the
checklist below.**

## What is verified (read-only identification, 2026-07-17)

| Property | Value | How established |
|---|---|---|
| Serial port (macOS) | `/dev/cu.usbserial-5B1F0057851` | USB enumeration, no extra driver |
| USB–UART bridge | WCH CH9102 family (VID:PID `1a86:55d4`) | USB descriptors |
| Chip | ESP32-D0WDQ6-V3, revision 3.1 | `esptool` 5.3.1 chip identification |
| CPU | Dual-core Xtensa LX6 @ 240 MHz | `esptool` 5.3.1 |
| Crystal | 40 MHz | `esptool` 5.3.1 |
| Flash | External SPI flash, **size unknown** | D0WDQ6 has no embedded flash (datasheet) |

The vendor is presumed LILYGO, but USB descriptors do not identify the board
model, and many LILYGO (and non-LILYGO) boards combine a classic ESP32 with a
CH9102 bridge. **We have identified the die, not the board.**

## What the unknown blocks

Until the model is confirmed with a pin map from an official source
([Xinyuan-LilyGO board repos](https://github.com/Xinyuan-LilyGO),
[wiki.lilygo.cc](https://wiki.lilygo.cc)), firmware must not:

- configure or drive **any GPIO** (display, backlight, audio codec/amp, LEDs,
  buttons, battery-sense dividers — wrong pins can fight on-board circuits);
- assume PSRAM, display controller, SD slot, or battery charger presence;
- assume a flash size beyond the 2 MB-safe default build.

The scaffold app is console-UART-only for exactly this reason.

## Identification checklist (what a human must provide)

Any one of these usually suffices; more is better:

1. **Silkscreen text** on the PCB, both sides — LILYGO prints the product
   name and revision (e.g. `T-Display V1.1`, `T8 V1.7`, `T-Energy`,
   `TTGO T-Koala`). Photograph front and back.
2. **Module vs bare chip**: is the ESP32 under a shielded module can (marking
   like `ESP32-WROOM-32E` / `WROVER-E`) or a bare QFN chip on the PCB?
   Photograph the marking.
3. **Peripherals visible**: display (size/ribbon marking), speaker/amp,
   battery connector (JST), SD slot, buttons count, antenna type.
4. Optionally, the **product listing / order page** it was purchased from.

Record findings here when known: board model = ______, board revision =
______, official repo/wiki page = ______.

## Safe-workflow rules for this device

- **No flash/erase was performed in the scaffolding task**, and none happens
  automatically — flashing is always an explicit human command.
- The first flash **overwrites the factory demo firmware**. Do it only after
  identification, deliberately.
- Read-only probes that are acceptable next steps (they do not write flash):
  - `esptool -p /dev/cu.usbserial-5B1F0057851 flash-id` → flash manufacturer
    + **size** (gates the partition/OTA decision — see
    `setup-research.md` §10)
  - `esptool -p /dev/cu.usbserial-5B1F0057851 chip-id` / MAC read
- Flash over a direct, data-capable USB cable (no hub). An interrupted flash
  is recoverable — the ROM serial bootloader always remains.
