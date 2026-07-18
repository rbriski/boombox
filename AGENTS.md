# AGENTS.md — operating Boombox

Operational ground rules and canonical commands for agents (and humans)
working in this repo. Background and rationale live in
[`docs/setup-research.html`](docs/setup-research.html); device status in
[`docs/hardware.html`](docs/hardware.html); SoC details in
[`docs/chip-architecture.html`](docs/chip-architecture.html); the RX-5235
build/integration plan in
[`docs/rx5235-build-plan.html`](docs/rx5235-build-plan.html).

## Facts you may rely on

- Target: **classic ESP32** (`esp32`), chip ESP32-D0WDQ6-V3 rev 3.1, external
  SPI flash (16 MB Winbond), CH9102 USB bridge. Board identity is
  **user-confirmed: LILYGO T-Display Q125 16 MB** (official listing + V18
  pin table recorded in `docs/hardware.html`); the physical silkscreen is now
  photo-verified (T-Display V1.1, 2019-06-28) and TFT_RST is resolved from the
  official schematic (GPIO23). The DAC powered-state check and M2 pin sign-off
  passed on 2026-07-18: BCK/LRCK/DIN = GPIO26/27/25, FMT low (Philips I2S),
  XSMT high, and SCK grounded. The audited M2 image is now flashed; Bluetooth,
  44.1 kHz external-I2S configuration, and five packet-bearing playback cycles
  passed. Physical PCM5102A line output through an amp or meter remains to test.
- Toolchain: **native ESP-IDF**, version pinned in [`.espidf-version`](.espidf-version).
  All scripts read that file; CI mirrors it in `.github/workflows/ci.yml`.
- No git remote exists; work lands on **local `main`**. Do not create or
  invent a remote.

## Canonical commands

```sh
scripts/bootstrap.sh      # one-time: install pinned ESP-IDF (~1.5 GB) + esp32 tools
scripts/doctor.sh         # health check (read-only, safe anytime) — must pass before building
scripts/build.sh          # idf.py build in the pinned env
scripts/build.sh size     # any idf.py subcommand passes through
scripts/build.sh -p /dev/cu.usbserial-XXXX flash monitor   # deliberate flash+monitor
```

- Port discovery: `ls /dev/cu.usbserial* /dev/cu.wchusbserial*`
- Monitor exit: `Ctrl-]`. Serial console runs at 115200 on UART0.
- Tests: none configured yet. When they exist they will be host-first
  (POSIX simulator) — see `docs/setup-research.html` §12 — and wired into
  these scripts + CI; run whatever `scripts/` exposes at that time.

## Layout conventions

- `main/` stays thin (wiring + app_main). New functionality = a new component
  under `components/<name>/`, or a managed dependency (`idf_component.yml`;
  commit `dependencies.lock`; never commit `managed_components/`).
- The future board pin map lives in **one** place: `components/boombox_board/`
  (created only after board identification). No `#define PIN_…` scattered in
  app code.
- `sdkconfig.defaults` is the only committed config; it must stay
  board-neutral until identification, and secret-free forever.

## C style & static analysis

- C, 4-space indent, 120 columns, `.clang-format` committed — run
  `clang-format -i` on files you touch. `.editorconfig` covers the rest.
- Follow ESP-IDF idioms: `esp_err_t` returns checked (`ESP_ERROR_CHECK` only
  where abort-on-failure is truly intended), `ESP_LOGx` with a per-file `TAG`,
  no bare `printf` outside deliberate banners.
- Keep warnings at zero; treat new warnings as defects. `idf.py clang-check`
  is available once code volume justifies it.

## Secrets & generated artifacts

- Never commit: credentials, Wi-Fi SSIDs/passwords, tokens, `sdkconfig`
  (generated; gitignored), `build/`, `managed_components/`, `.env*`, anything
  under `secrets/`, or **flash dump/backup binaries** (they can embed NVS
  contents and credentials).
- Runtime secrets belong in NVS at provisioning time, not in source or
  committed config.
- Generated `.beads/` runtime files are untracked by policy — only
  `.beads/identity.toml` is in git. Do not re-add them.

## Hardware safety (HARD RULES)

The attached device is the only unit we have. In order:

1. **Never use an unverified pin.** The board identity is confirmed and the
   official V18 pin table is recorded in `docs/hardware.html`, but do not
   write board-specific GPIO, display, audio/amplifier, or battery/power
   code until that specific pin assignment has been checked against the
   official Q125/V18 pinout, the official schematic, boot straps, header
   exposure, and onboard functions, and recorded in the verified table
   (`docs/rx5235-build-plan.html` §10). The verified M2 I2S rows satisfy this
   rule only for GPIO26/27/25; every other new pin remains gated. Until a row
   is verified, board-neutral means console UART only and zero GPIO
   configuration.
2. **Back up before writing.** Before the first write of a session that
   changes flash contents, take (or confirm) a full flash backup to
   `~/Library/Application Support/Boombox/backups/` (timestamped, outside the
   repo). Record path + size + SHA-256 in the work log — never the binary in
   git.
3. **No bulk erase without explicit user approval.** `idf.py erase-flash` /
   `esptool erase-flash` (and region erases beyond what a normal
   `idf.py flash` performs) are forbidden unless the user has approved that
   specific action in that session.
4. Flashing is always an explicit command, never a side effect of build or
   test tooling.
5. Direct, data-capable USB connection when flashing (no hubs). An
   interrupted flash is recoverable via the ROM serial bootloader — retry;
   don't improvise.
6. Read-only operations (`esptool chip-id`, `flash-id`, reading flash to a
   file, monitoring) are allowed and are the preferred way to learn about the
   device.

## Beads / workflow

- Work is tracked in beads (`bd`/`gc bd`); this repo's `.beads` redirects to
  the shared store. Commit messages reference the work bead id, imperative
  subject + explanatory body (see git log for the house style).
- Discovered out-of-scope work: file a bead (`gc bd create`), don't fix it
  in-branch.
