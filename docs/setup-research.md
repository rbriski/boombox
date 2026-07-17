# Boombox — ESP32 Toolchain & Project Setup Research

**Date:** 2026-07-17 · **Status:** Accepted (framework + version pin) · **Scope:** classic ESP32 (LILYGO board, exact model unconfirmed)

This document records the research behind Boombox's initial toolchain choice, the
version-pinning policy, and the practices the scaffold encodes. Sources are
official/primary only and listed inline plus in [Sources](#sources).

---

## 1. Hardware context (what we actually know)

Established safely with `esptool` 5.3.1 before this scaffold was created:

| Fact | Value |
|---|---|
| Serial port (macOS) | `/dev/cu.usbserial-5B1F0057851` |
| USB bridge | WCH, VID:PID `1a86:55d4` (CH9102 family) |
| Chip | ESP32-D0WDQ6-V3, revision 3.1 |
| Cores / clock | Dual-core Xtensa LX6, 240 MHz |
| Crystal | 40 MHz |

Per the [ESP32 datasheet](https://www.espressif.com/sites/default/files/documentation/esp32_datasheet_en.pdf),
the D0WDQ6 variant is dual-core in a QFN 6×6 package with **no embedded
flash** — flash is an external SPI chip on the board, and its **size is
unknown** until probed. The USB descriptors do **not** identify the LILYGO
board model. Everything pin-related (display, audio, battery sense, buttons)
is therefore out of scope until the board is identified — see
[`docs/hardware.md`](hardware.md) for the identification checklist and the
constraints the scaffold honors meanwhile.

## 2. Framework choice

Three maintainable paths were compared, using only official project sources.

### 2.1 Native ESP-IDF (chosen)

ESP-IDF is Espressif's first-party framework. As of July 2026 the current
stable release is **v6.0.2**, with classic ESP32 fully supported
([versions page](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/versions.html),
[releases](https://github.com/espressif/esp-idf/releases)). Espressif's
guidance for new projects is to use the latest stable ("In Service") release,
and each minor release is supported for 30 months (12 months service + 18
months maintenance) per the
[support policy](https://github.com/espressif/esp-idf/blob/master/SUPPORT_POLICY.md).

- First-party: chip support, bugfixes, and security patches land here first.
- Full control of `sdkconfig`, partition tables, and the CMake build.
- Official CI story ([esp-idf-ci-action](https://github.com/espressif/esp-idf-ci-action)
  wrapping the `espressif/idf` Docker image) and official host-testing story
  ([POSIX/Linux simulator](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/host-apps.html)).
- Dependency management with lockfiles via the
  [IDF Component Manager](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/tools/idf-component-manager.html)
  and [components.espressif.com](https://components.espressif.com).
- Cost: steeper API than Arduino; C-first idioms; more boilerplate for quick
  experiments.

### 2.2 PlatformIO + ESP-IDF (rejected)

PlatformIO's build of ESP-IDF rides the `platform-espressif32` platform.
Espressif discontinued its support of that platform: the official platform
never adopted Arduino core 3.x / newer IDF lines
([platformio/platform-espressif32#1225](https://github.com/platformio/platform-espressif32/issues/1225),
[arduino-esp32 discussion #10039](https://github.com/espressif/arduino-esp32/discussions/10039)).
Current support continues in the community
[pioarduino fork](https://github.com/pioarduino/platform-espressif32), which
is capable but small-team and unofficial. Building a new long-lived project on
a vendor-abandoned integration layer — with the fork as load-bearing
infrastructure — is an avoidable risk. PIO's IDE ergonomics don't outweigh it.

### 2.3 Arduino-ESP32 (standalone or via PlatformIO) (rejected as default)

The official [arduino-esp32 core](https://github.com/espressif/arduino-esp32/releases)
is at **v3.3.x**, built on **ESP-IDF v5.5.x**; the IDF-6-based **v4.0.0 is
alpha**. So the Arduino path trails IDF stable by a major version, and pairing
it with PlatformIO re-imports the fork problem above. The Arduino API is great
for prototyping, and it remains available to us *later* as an IDF component if
we ever want Arduino libraries — without inverting the project structure.

**Decision: native ESP-IDF.** Best-supported, first-party, and the only option
where version pinning, CI, testing, and dependency locking are all official.

## 3. Version pin & upgrade policy

**Pinned: ESP-IDF `v6.0.2`** — recorded in [`.espidf-version`](../.espidf-version),
consumed by `scripts/bootstrap.sh` / `scripts/doctor.sh` / `scripts/build.sh`,
and mirrored in `.github/workflows/ci.yml` (comment there points back at the
pin file).

Why v6.0.2 and not v5.5.x:

- Espressif recommends the **latest stable** for new projects
  ([versions](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/versions.html)).
- The [roadmap](https://github.com/espressif/esp-idf/blob/master/ROADMAP.md)
  moves `release/5.5` into its maintenance period **July 2026** (now), while
  `release/6.0` has an active 2026 bugfix cadence (v6.0.3 ~Sep, v6.0.4 ~Nov)
  and v6.1/v6.2 follow later in 2026.
- Starting on 5.5 would mean adopting a branch already leaving service and
  scheduling a 5.x→6.x migration for later; starting on 6.0 buys the longest
  runway with no migration debt.

**Policy:** bump the pin deliberately (edit `.espidf-version` + the CI
mirror in one commit), preferring bugfix releases on `release/6.0` as they
appear; evaluate v6.1 once stable. Never track `master` or a moving branch.
The Docker image used in CI (`espressif/idf:v6.0.2`) pins the same tag, so CI
and local builds agree.

## 4. macOS setup (this dev machine)

Per the official
[macOS setup guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/macos-setup.html):

- **Python ≥ 3.10 is required.** The machine's system Python is 3.9.5, so
  Homebrew `python` is needed (`scripts/doctor.sh` checks this).
- Homebrew prerequisites (official list): `libgcrypt glib pixman sdl2
  libslirp dfu-util cmake python` — the first five exist for the QEMU
  simulator; the hard core for building is `cmake` + `python` (Ninja and the
  Xtensa toolchain are provisioned by ESP-IDF's own tool installer into
  `~/.espressif`).
- **Official recommended installer is now EIM** (Espressif Installation
  Manager): `brew tap espressif/eim && brew install eim`, then
  `eim install -i <version>` for a pinned install.

**What our scripts do:** `scripts/bootstrap.sh` uses the classic, fully
scriptable flow — `git clone -b v6.0.2 --recursive` into a
version-suffixed directory plus `install.sh esp32` — because it is
deterministic, non-interactive, and byte-for-byte the same mechanism the
official CI Docker image uses. EIM is a fine interactive alternative; if you
already manage IDF with EIM, point `BOOMBOX_IDF_PATH` at that install and
`doctor.sh` will verify the tag matches the pin.

### Serial port & the WCH bridge

- Port discovery on macOS is `ls /dev/cu.*`
  ([establish serial connection](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/establish-serial-connection.html)).
- On this machine the CH9102 already enumerates as
  `/dev/cu.usbserial-5B1F0057851` with **no extra driver** — current IDF docs
  note bridge drivers are normally bundled with the OS. If a fresh macOS
  machine does *not* enumerate the port, WCH's official driver
  ([WCHSoftGroup/ch34xser_macos](https://github.com/WCHSoftGroup/ch34xser_macos))
  covers CH9101/CH9102 on macOS 10.9→11+ (port then appears as
  `/dev/*wchusbserial*`).
- No `dialout`-style group permissions exist on macOS; serial devices are
  user-accessible by default (unlike Linux, where the user must join
  `dialout`/`uucp`).

## 5. Build / flash / monitor ergonomics

Everything flows through `idf.py` once the environment is exported
([get-started](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/index.html)).
The scaffold wraps the two fiddly parts — *which IDF* and *is my env sane* —
in three commands:

| Command | What it does |
|---|---|
| `scripts/bootstrap.sh` | One-time: clone ESP-IDF at the pinned tag, run its tool installer for `esp32` |
| `scripts/doctor.sh` | Verify host prerequisites, pinned IDF presence/tag, serial port visibility |
| `scripts/build.sh` | Source the pinned env, run `idf.py build` (extra args pass through, e.g. `scripts/build.sh size`) |

Flash/monitor remain explicit, deliberate actions
(`scripts/build.sh -p /dev/cu.usbserial-5B1F0057851 flash monitor`) — see the
safety rules in §11 and in `docs/hardware.md` before the first flash.
Monitor exit is `Ctrl-]`.

## 6. Project layout

Standard idf.py project shape (build system docs), kept minimal until the
board is identified:

```
CMakeLists.txt           # project entry (standard ESP-IDF template)
sdkconfig.defaults       # committed config seed: pins IDF_TARGET=esp32
main/                    # the app component
  CMakeLists.txt
  boombox_main.c         # board-neutral serial hello-world + heartbeat
components/              # (future) project-local components, one per concern
docs/                    # research + hardware notes
scripts/                 # bootstrap / doctor / build
```

Growth rule: new functionality lands as small components under `components/`
(or as managed dependencies), not as accretion onto `main/`.

## 7. Board configuration strategy

The scaffold is **board-neutral**: `sdkconfig.defaults` pins only
`CONFIG_IDF_TARGET="esp32"` and the app touches no GPIO — it uses just the
default console UART through the USB bridge. Once the physical board is
identified (checklist in `docs/hardware.md`), board specifics enter as:

1. a `components/boombox_board/` component exposing a pin-map header +
   Kconfig options (display/audio/battery rails), and
2. additions to `sdkconfig.defaults` gated on that identification.

Do **not** scatter `#define PIN_…` through app code; the board component is
the single seam.

## 8. Secrets

- `sdkconfig` (generated, may embed Wi-Fi credentials etc. later) is
  **gitignored**; only `sdkconfig.defaults` is committed and must stay
  secret-free.
- Local-only overrides belong in `sdkconfig.local.defaults`-style files or
  environment injection at provisioning time; runtime secrets belong in NVS
  (with NVS encryption + flash encryption as the eventual production posture —
  decide alongside the OTA/partition work in §10).
- `.gitignore` also blocks `secrets*`/`.env*` patterns preemptively.

## 9. Logging, watchdog, crash diagnostics

- Use `esp_log` (`ESP_LOGI/W/E` + per-tag levels) from day one — the scaffold
  does; no bare `printf` beyond the banner.
- IDF defaults keep the **task watchdog** on the idle tasks and panic output
  on the console — leave them on. When the product loop exists, feed/subscribe
  real tasks explicitly.
- When flash size is confirmed, add a **coredump partition**
  (`ESP_COREDUMP_ENABLE_TO_FLASH`) so field crashes are recoverable — folded
  into the §10 partition decision.

## 10. Partitions, NVS, OTA

Deferred deliberately: OTA layout is a function of **flash size, which is
unknown** (§1). Until probed:

- Build uses the default single-factory-app partition table (safe on any
  supported flash size).
- The first read-only probe (`esptool -p <port> flash-id`, safe/read-only per
  [esptool docs](https://docs.espressif.com/projects/esptool/en/latest/esp32/))
  will establish size; then pick: 4 MB+ → two OTA app slots + otadata + NVS +
  coredump; ≤2 MB → factory + NVS + coredump, OTA via serial only.
- NVS is the settings store either way; introduce a namespaced settings module
  rather than raw NVS calls at call sites.

## 11. Safe hardware workflows

Ground rules encoded in docs and honored by the scaffold:

1. **This task did not flash or erase the device**, and nothing here does so
   automatically — flashing is always an explicit human command.
2. First flash **erases the factory demo firmware** LILYGO ships; do it only
   after the board is identified and you accept losing the demo.
3. Never configure or drive a GPIO you can't name from a verified pin map —
   unknown pins can fight on-board circuits (display rails, battery
   management). Board-neutral firmware = console UART only.
4. Read-only probes (`flash-id`, MAC read) are fine *after* this task; keep a
   note of results in `docs/hardware.md`.
5. Keep the device on a data-capable cable directly attached (no hubs) when
   flashing; if a flash is interrupted, the ROM serial bootloader remains —
   recovery is re-running the flash, not panic.

## 12. Testing strategy

- **Host-first for logic:** ESP-IDF v6.0 supports running components/apps on
  the host via the FreeRTOS **POSIX/Linux simulator**
  ([host apps guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/host-apps.html))
  — fast, debuggable (gdb/valgrind), CI-friendly. Known limits: POSIX
  scheduling is simulated (blocking-call caveats, async-signal-safety), so
  keep host tests to pure logic + mocked interfaces (IDF ships a FreeRTOS
  mock component for that).
- **Target tests later:** once flashing is unblocked, add on-target smoke
  tests driven by pytest-embedded against the real chip.
- Structure code so logic lives in components with narrow hardware seams —
  that's what makes the host tier possible.

## 13. Formatting & static analysis

- `.clang-format` (LLVM base, 4-space, 120 col, Linux braces) +
  `.editorconfig` are committed; format new code before commit.
- IDF builds with warnings on by default — treat warnings as defects; CI
  builds will surface them.
- Optional deeper pass: `idf.py clang-check` (clang-tidy via the esp-clang
  toolchain) — adopt when there's enough code to justify it.

## 14. Dependency locking

- External components come from the official registry
  ([components.espressif.com](https://components.espressif.com)) via
  `idf_component.yml` per component; the component manager writes
  **`dependencies.lock`** — commit it, exactly like any lockfile
  ([component manager docs](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/tools/idf-component-manager.html)).
- `managed_components/` (downloaded copies) stays gitignored.
- The IDF version itself is pinned by `.espidf-version` (§3); Python tool
  environments live in `~/.espressif` keyed by IDF version, managed by
  `install.sh` — no per-repo venv to maintain.

## 15. CI

`.github/workflows/ci.yml` builds on every push/PR with
[espressif/esp-idf-ci-action](https://github.com/espressif/esp-idf-ci-action)
(pinned `@v1.2.0`), which wraps the official `espressif/idf` Docker image;
`esp_idf_version: v6.0.2` mirrors the repo pin and `target: esp32` matches
`sdkconfig.defaults`. Note: this repo currently has **no git remote**, so the
workflow is dormant scaffolding until the project lands on a GitHub remote —
it was written now so the pin discipline exists from the first push.

## 16. Open items (ordered)

1. **Identify the board** — photos/silkscreen per `docs/hardware.md`; this
   gates all pin work.
2. Probe flash size (`esptool flash-id`, read-only) → decide partition/OTA
   layout (§10) + coredump partition (§9).
3. First deliberate flash of the hello-world (accepting factory-demo loss),
   verify chip-info banner matches the esptool identification.
4. Board component + pin map; then display/audio/battery bring-up per the
   identified model, using the matching
   [Xinyuan-LilyGO](https://github.com/Xinyuan-LilyGO) board repo and
   [wiki.lilygo.cc](https://wiki.lilygo.cc) as pin-map sources.
5. Host-test harness for the first pure-logic component (§12).

## Sources

Official documentation and repositories consulted 2026-07-17:

- ESP-IDF versions & recommendations — <https://docs.espressif.com/projects/esp-idf/en/stable/esp32/versions.html>
- ESP-IDF support policy — <https://github.com/espressif/esp-idf/blob/master/SUPPORT_POLICY.md>
- ESP-IDF releases (v6.0.2 stable, v5.5.5, v6.1-beta1) — <https://github.com/espressif/esp-idf/releases>
- ESP-IDF roadmap (5.5 maintenance from Jul 2026; 6.0.x cadence) — <https://github.com/espressif/esp-idf/blob/master/ROADMAP.md>
- ESP-IDF get started — <https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/index.html>
- ESP-IDF macOS setup (EIM, brew packages, Python ≥ 3.10) — <https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/macos-setup.html>
- Serial connection / port discovery — <https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/establish-serial-connection.html>
- WCH official macOS driver (CH9102) — <https://github.com/WCHSoftGroup/ch34xser_macos>
- arduino-esp32 releases (v3.3.x on IDF 5.5.x; v4.0 alpha on IDF 6.0) — <https://github.com/espressif/arduino-esp32/releases>
- Arduino core 3.x PlatformIO status — <https://github.com/espressif/arduino-esp32/discussions/10039>, <https://github.com/platformio/platform-espressif32/issues/1225>
- pioarduino community fork — <https://github.com/pioarduino/platform-espressif32>
- esp-idf-ci-action (v1.2.0, espressif/idf image) — <https://github.com/espressif/esp-idf-ci-action>
- Host apps / POSIX simulator testing — <https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/host-apps.html>
- IDF Component Manager — <https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/tools/idf-component-manager.html>
- Component registry — <https://components.espressif.com>
- esptool docs — <https://docs.espressif.com/projects/esptool/en/latest/esp32/>
- ESP32 datasheet (D0WDQ6 variant table) — <https://www.espressif.com/sites/default/files/documentation/esp32_datasheet_en.pdf>
- LILYGO official repos / docs — <https://github.com/Xinyuan-LilyGO>, <https://wiki.lilygo.cc>
