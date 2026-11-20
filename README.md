# LUMINA STUDIO — Phase 5

The Effects Library shares the existing WS2812B output pipeline. See [Effects bench checks](EFFECTS.md) for expected behavior and acceptance tests for all 16 effects. The browser rail is a simulation, never a measurement of the strip.

Color Studio adds a color workspace, gradients, transition controls, and a palette library. The standalone preview is generated with `python3 scripts/bundle.py` from the files in `studio/`. Demo mode keeps its library in browser storage where available; the controller uses bounded NVS storage. The device rail reports the last completed RMT frame, not a reading from the strip.

ESP32-C3 firmware foundation with a self-hosted Wi-Fi access point, lightweight HTTP API, and embedded offline-capable control interface. Phase 3 drives a WS2812B-compatible strip using the ESP32-C3 RMT peripheral.

## Target profile

| Setting | Selection |
| --- | --- |
| Arduino board | ESP32C3 Dev Module (`esp32:esp32:esp32c3`), for documented 4 MiB boards |
| Flash | 4 MiB, DIO at 40 MHz; conservative common-board profile, not a universal profile |
| Partitions | Default 4 MiB: two 1.25 MiB OTA app slots, NVS, SPIFFS and coredump; no OTA service yet |
| CPU | 160 MHz; Wi-Fi-capable profile |
| Serial | USB CDC on boot for native USB Serial/JTAG, plus UART0 for bridge boards; JTAG debugging not tested |
| Toolchain | Arduino CLI 1.3.1, Espressif Arduino core 3.3.0, `riscv32-esp-elf-g++` from Espressif `esp-rv32` package 2411, bundled esptool 5.0.0 |
| Libraries | No external libraries. Wi-Fi, Preferences and the ESP-IDF HTTP server come from the pinned core and SDK; ESP32-C3 supports 2.4 GHz Wi-Fi, not 5 GHz. |

The [pinned board definitions](https://github.com/espressif/arduino-esp32/blob/3.3.0/boards.txt), [partition table](https://github.com/espressif/arduino-esp32/blob/3.3.0/tools/partitions/default.csv) and [build recipes](https://github.com/espressif/arduino-esp32/blob/3.3.0/platform.txt) define the profile. Verify the exact module, schematic, exposed GPIO and USB routing before committing to a physical pinout. Keep GPIO18/19 available for native USB where wired; avoid boot strapping pins GPIO2/8/9 and pins used by flash or the board's UART unless the board design permits it. Future addressable LEDs can use RMT subject to the selected strip timing and driver; analog RGB can use LEDC PWM with external drivers. Confirm signal levels, LED power and pin choices on actual hardware. GPIO4 is configured by default; see the wiring guide below. See the [ESP32-C3 datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-c3_datasheet_en.pdf).

## Repository setup and remote build

Put the **contents** of this directory at the root of a dedicated GitHub repository, including `.github/workflows/firmware-build.yml` and `.gitignore`. GitHub does not discover workflow files nested inside another project's subdirectory. Commit and push to the dedicated repository with Actions enabled. No Android compilation is needed.

On pushes and manual dispatches, an Ubuntu 24.04 x64 runner verifies the pinned Arduino CLI download by SHA-256, installs the pinned core/toolchain and any explicitly versioned libraries in `config/build-config.json`, stages the single sketch in Arduino's required matching directory, and compiles it. If a library later needs dependencies, pin them explicitly and list them before the dependent library. The script validates the bootloader and app using esptool (chip ID, checksum and SHA-256 image digest), checks merged components and padding, and reports both binary sizes. Errors stop the build before upload. Actions are pinned by commit. The runner image and upstream package index can change, and build date/run number intentionally vary; byte-identical output across runs is not promised.

The artifact contains `LUMINA_STUDIO.ino`, `LUMINA_STUDIO.html`, `LUMINA_STUDIO.bin`, `FLASHING.md`, `WIRING.md`, and `BUILD-INFO.json`. The core's merged binary is padded to 4 MiB and includes bootloader (`0x0`), partitions (`0x8000`), boot application/OTA initializer (`0xe000`) and application (`0x10000`). Only the merged image is published; flash it at **`0x0`**. `BUILD-INFO.json` identifies the source commit, build number, UTC build date, image SHA-256 and binary sizes.

For a release, set the version in `config/build-config.json`, push the commit, then publish a GitHub Release with the matching tag (e.g. `v0.3.0`). Publishing triggers a fresh build; after validation, the workflow attaches `LUMINA_STUDIO.zip` to the release. Tag pushes also produce a normal Actions artifact. Wait for the release-triggered run to finish before looking for its asset.

The version is defined in build config and injected through a temporary generated header. The build also generates a C++ byte array from `LUMINA_STUDIO.html`; the device serves those exact bytes from flash. Only a local file preview enters Demo Mode. Device-hosted pages use the real API and report failures rather than switching to simulated telemetry.

## Network and API

Join `LUMINA-STUDIO` (2.4 GHz, channel 6). Send `?` to the USB/UART serial terminal at 115200 to retrieve the generated per-device AP password. It persists in NVS and is never included in HTTP responses. Open **http://192.168.4.1/**; the gateway is `192.168.4.1` and subnet is `255.255.255.0`. No internet or captive portal is required. SSID and channel are source configuration; editing network settings over HTTP is intentionally deferred.

| Route | Purpose |
| --- | --- |
| `GET /api/status` | Firmware, build, uptime, clients, and AP status |
| `GET /api/system` | Chip, CPU, flash, current/minimum heap, ESP-IDF reset reason code |
| `GET /api/network` | SSID, IP, MAC, channel, mode, and clients |
| `GET /api/health` | Readiness |
| `GET /api/led` | Requested lighting state and last completed RMT frame (first pixel) |
| `PUT /api/led` | Replace state with exactly `power` (boolean), `brightness` (integer 0–255), and `color` (`#RRGGBB`) in an `application/json` body of at most 256 bytes |
| `POST /api/led/power` | `{"power":true}` or false |
| `POST /api/led/brightness` | `{"brightness":64}` (0–255 requested; output is capped) |
| `POST /api/led/color` | `{"r":255,"g":0,"b":0}` (each channel 0–255) |
| `POST /api/led/config` | `{"led_count":60,"color_order":"GRB","max_brightness":64}` (1–150 pixels, six RGB permutations, cap 1–128) |
| `POST /api/led/effect` | `{"effect":"solid"}` |
| `POST /api/led/test` | `{"test":"red"}`; also `green`, `blue`, `white`, `first`, or `off` to end the test |
| `GET /api/events` | Bounded session milestones, not a chronological audit log |

See [WIRING.md](WIRING.md) for wiring and strip checkout. A write response acknowledges the command; `rendering` and `output` report progress and the last completed first-pixel frame, not strip feedback.

The ESP-IDF HTTP server runs in its own task. The Arduino loop monitors the AP, handles local serial provisioning, and retries AP/server startup every ten seconds on failure. Reconnect to the AP and retry the page first; use RESET/EN to restart if needed. A normal restart retains the password. There is no remote reset endpoint; only intentionally reflashing the full image (after backing up data) clears the stored password and generates a new one. HTTP is local and unencrypted: any device admitted to the AP can change physical light output.

## Download on Android

1. Sign in to GitHub in your browser and open the LUMINA STUDIO repository.
2. Tap **Actions → Build LUMINA STUDIO firmware**. Open the latest successful run for the branch/commit you want. If no run exists, an authorized maintainer can use **Run workflow** after the workflow is on the default branch.
3. Under **Artifacts**, tap `LUMINA-STUDIO-<build number>` to download the ZIP. Artifacts request 30-day retention, subject to repository policy. For a published release, use **Releases → Assets → LUMINA_STUDIO.zip** instead.
4. Extract the ZIP in Android's Files app. Open `LUMINA_STUDIO.html` in a browser that accepts local HTML files, or use a local HTML viewer if your browser won't open it from Files.
5. Read [FLASHING.md](FLASHING.md) before writing the binary. It is a full flash image and will overwrite existing data.

## Acceptance checks

The firmware build and hardware smoke test are verified end to end when the dedicated repository shows a successful Actions run, its artifact contains the six files above, the downloaded image matches `BUILD-INFO.json`, and a known 4 MiB ESP32-C3 boots it and prints the expected identification. Then join the AP from Android, confirm the live interface and telemetry, apply a lighting state and reload to confirm it, disconnect/reconnect Wi-Fi, and watch free/minimum heap during repeated refreshes. A published release should also expose the ZIP asset. These checks require repository access and, for boot verification, the owner's board and Android device.
