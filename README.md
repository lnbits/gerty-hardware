# Gerty display firmware

One PlatformIO project supports four displays with shared Wi-Fi, HTTP(S), PNG,
logging, persistent pagination, and error handling.

| Environment | Display | PNG size | Power between checks |
| --- | --- | --- | --- |
| `T5-ePaper-S3` (default) | LilyGO 4.7-inch e-paper | 960 × 540 grayscale | Panel off; configurable deep sleep |
| `guition-JC3248W535` | Guition 3.5-inch AXS15231B LCD | 480 × 320 colour | LCD and backlight stay on; no deep sleep |
| `guition-JC4827W543` | Guition 4.3-inch NV3041A LCD | 480 × 272 colour | LCD and backlight stay on; no deep sleep |
| `waveshare-ESP32-C6-LCD-1_3` | Waveshare 1.3-inch ST7789V2 LCD | 240 × 240 colour | LCD and backlight stay on; no deep sleep |

## Waveshare ESP32-C6-LCD-1.3

Set the `GERTY_WAVESHARE_C6` branch of `MANIFEST_URL` in `include/config.h`
to a Gerty feed producing **240 × 240** non-interlaced PNGs (up to 8 bits per
channel). It has a separate URL setting from the Guition and LilyGO builds.
The JSON structure and pagination are unchanged, and image URLs are used verbatim.

This ESP32-C6 has 4 MB flash and no PSRAM. Images are downloaded to a temporary
LittleFS file (maximum 512 KiB), then Wi-Fi is stopped and the PNG is decoded into
a 115,200-byte RGB565 frame. Only a complete, CRC-checked frame reaches the LCD;
failed downloads or decodes preserve the displayed image with an error label.
The temporary file is removed after each attempt. No SD card is needed.
Flash staging incurs a write on each changed image, so prefer longer refresh
intervals for feeds that change constantly. There is no touch input or slide
animation on this model; it advances on the JSON refresh schedule.

The C6 environment alone uses pinned pioarduino/Arduino 3.x tooling. Its dedicated
partition table provides a 2 MiB application and a temporary filesystem, without OTA.
Existing S3 environments keep their original toolchain. Wiring follows the
[Waveshare demo](https://docs.waveshare.com/ESP32-C6-LCD-1.3/Resources-And-Documents):
SPI CLK 7, MOSI 6, MISO 5, LCD CS 14, DC 15, reset 21, backlight 22;
SD CS 4 is held high. LCD transfer speed is 40 MHz.

```sh
uv tool run --from platformio --with intelhex pio run -e waveshare-ESP32-C6-LCD-1_3 -t upload
uv tool run --from platformio --with intelhex pio device monitor -e waveshare-ESP32-C6-LCD-1_3
```

## Guition JC4827W543C

Select `guition-JC4827W543` for the capacitive-touch model: ESP32-S3 N4R8
(4 MB flash, 8 MB OPI PSRAM), native landscape NV3041A display and GT911 touch.
It shares the colour rendering, slide transition, tap-to-advance and always-awake
behaviour of the 3.5-inch board. Set the Guition `MANIFEST_URL` in
`include/config.h` to a feed returning **480 × 272** PNGs; 480 × 320 images
are rejected rather than resized. Image URLs are used exactly as returned.

QSPI pins are CS 45, SCK 47, D0 21, D1 48, D2 40, D3 39, at 32 MHz;
backlight is GPIO 1. GT911 uses SDA 8, SCL 4, reset 38 and interrupt 3,
with address 0x5D selected during reset. The resistive-touch R variant is
not supported by this touch driver.
Hardware reference: [JC4827W543 example](https://github.com/profi-max/JC4827W543_4.3inch_ESP32S3_board).

```sh
uv tool run --from platformio --with intelhex pio run -e guition-JC4827W543 -t upload
uv tool run --from platformio --with intelhex pio device monitor -e guition-JC4827W543
```

PNG downloads finish decoding before a visible frame is changed. Failures show
an error at bottom right while preserving the rest of the image. Identical errors
are not redrawn. Recovery restores the image even when its revision is unchanged.

## Guition JC3248W535

The Guition uses ESP32-S3, 16 MB flash and 8 MB OPI PSRAM. Its native 320 × 480
AXS15231B QSPI panel is rotated in software to 480 × 320 landscape using
Arduino_GFX's canvas. The image remains RGB565 colour; e-paper dithering is not
applied. QSPI pins are CS 45, SCK 47, D0 21, D1 48, D2 40, D3 39;
backlight is GPIO 1. Touch uses I2C SDA 4, SCL 8, address 0x3B (IRQ 3).
Tap anywhere while a page is displayed to request the saved next page immediately.
Touches are debounced and holding a finger down triggers only once. Touch is
polled between updates; taps during an active download/render are not queued.

Set `MANIFEST_URL` in `include/config.h` to the Gerty feed producing 480 × 320
PNGs. The Guition endpoint is set to the local colour feed and the LilyGO keeps its
existing endpoint; Wi-Fi settings are shared. The firmware does not
resize images, change URLs, or add device parameters to the API request.

```sh
uv tool run --from platformio --with intelhex pio run -e guition-JC3248W535 -t upload
uv tool run --from platformio --with intelhex pio device monitor -e guition-JC3248W535
```

The LCD build cannot enter deep sleep even if the shared sleep option is changed.
It turns Wi-Fi off while waiting, leaves panel/backlight power on, and reconnects
for the next check. The current image survives failed requests while powered;
after power loss or reset it must download again.

The LilyGO remains the default for commands without `-e`. In VS Code select
**guition-JC3248W535 → Upload** under PlatformIO Project Tasks for the new board.
Use `-e T5-ePaper-S3` explicitly when uploading to the e-paper board.

## Setup

1. Copy `include/secrets.example.h` to `include/secrets.h` and enter your Wi-Fi
   credentials. This file is ignored by Git.
2. Set `MANIFEST_URL` in `include/config.h`.
3. Install PlatformIO, or use `uv tool run --from platformio --with intelhex pio`
   in place of `pio` (includes a dependency needed by the ESP32 tooling).
4. Run `pio run`, then `pio run -t upload`, then `pio device monitor`.

The example header allows compilation before credentials are configured, but
will not connect. HTTPS certificate verification is disabled: traffic is
encrypted, but the server's identity is not authenticated. No root certificate
or NTP clock synchronization is required. Direct HTTP or HTTPS URLs are required;
redirects are rejected.

If no upload port appears, hold BOOT, press/release RST, then release BOOT.
Deep sleep disconnects USB, so manual bootloader entry may be needed for upload.
Serial monitoring may need reconnecting after each wake.

### Logging

Both device environments use the same logging configuration. Set `LOG_LEVEL` in
`include/config.h` and rebuild:

```cpp
constexpr LogLevel LOG_LEVEL = LogLevel::INFO;
```

- `NONE`: no application logs; on-screen errors still appear.
- `ERROR`: failures only.
- `INFO` (default): errors, Wi-Fi connection, request URLs/results, image updates, sleep.
- `DEBUG`: INFO plus display initialization and PNG format/decode details.

For LilyGO, `DEEP_SLEEP_ENABLED` in `include/config.h` controls deep sleep.
For Guition it is forced off and the display backend also forbids deep sleep.
The same refresh/retry intervals apply in either mode. Wi-Fi is turned off
between checks; only the e-paper panel is powered off. Logging level does not
change the sleep setting. When LilyGO deep sleep is enabled, USB disconnects, so the
monitor may need reconnecting on wake. Logging allows up to 1.5 seconds for USB
attachment on a reset, but never adds that wait on a timer wake. Arduino library
logging is disabled to avoid unrelated TLS chatter; ROM boot messages are outside
this application setting.

```sh
uv tool run --from platformio --with intelhex pio run -t upload
uv tool run --from platformio --with intelhex pio device monitor
```

## LNbits Gerty extension

`MANIFEST_URL` should be a base pages endpoint, for example:

```text
http://192.168.8.104:5001/gerty/api/v1/gerty/pages/7qYskkyGXQAZnw89ywtLqj
```

For a local server use the LNbits computer's LAN address, not localhost. LNbits must listen on
its LAN interface (or `0.0.0.0`), not only `127.0.0.1`, and port 5001 must be
reachable from the ESP32's Wi-Fi network.

The tested API is zero-based: the base URL returns page 0, `/1` returns page 1,
and page 7 of 8 returns `next_page: 0`. On first use the firmware requests the base
URL. After successfully displaying a page, it saves both `next_page` and
`page_count` together in ESP32 persistent storage, then requests the saved next
page on the next wake. This survives deep sleep, resets, power loss, and ordinary
firmware uploads. Changing the configured endpoint starts at page zero. Failures retry the same page except HTTP 503: a manifest 503 advances one page
(wrapping with the saved page count), and an image 503 saves the manifest
`next_page`. The new position is persisted before sleeping. If the count is not
yet known, a manifest 503 probes the following page. The error message and retry
delay still apply. A missing
page (HTTP 404) resets the next attempt to the base endpoint, allowing recovery
when pages are removed. Display duration follows the positive `refresh_seconds`
value supplied by the extension, without a 30–300 second clamp.

Image URLs are used exactly as returned by LNbits. The extension must return
absolute HTTP(S) URLs reachable from the ESP32; the firmware never rewrites them.

Page metadata (`page`, `page_count`, `next_page`) is optional for compatibility
with the standalone test server. If supplied, all three must be nonnegative
integers, `page_count` must be positive, and both page indices must be below it.

Protocol checks:

```sh
c++ -std=c++11 -I include tests/gerty_protocol_test.cpp -o /tmp/gerty-protocol-test
/tmp/gerty-protocol-test
```

## Local HTTPS test server

Use your computer's LAN IP, reachable from the device, in place of
`192.168.1.100`. Both devices must be on a network that permits communication.
Allow incoming port 8443 if your computer asks. Install `uv` if needed.

```sh
uv run tools/test_server.py --host 192.168.8.108 --setup
uv run tools/test_server.py --host 192.168.1.100 --refresh 30
```

Setup generates a one-year self-signed certificate with the correct IP/DNS
identity, its private key, and a diagnostic PNG. The certificate stays on the
server; no certificate needs copying into the firmware. Set
`MANIFEST_URL` to `https://192.168.1.100:8443/manifest.json`, then build/upload.
If the computer's address changes, restart the server with the new `--host`
and rebuild firmware with the new endpoint.

Replace `test-server/image.png` with a 960 × 540 PNG while serving, or start with
`--image /absolute/path/photo.png`. The next manifest request computes a new
revision automatically. PNG snapshots keep revisions consistent during edits;
they accumulate under the ignored `test-server/` directory. The server exposes
only its manifest and image snapshots, not Wi-Fi settings or the private key.

## JSON contract

```json
{
  "schema_version": 1,
  "image_url": "https://example.com/display.png",
  "image_revision": "42",
  "refresh_seconds": 300,
  "page": 0,
  "page_count": 8,
  "next_page": 1
}
```

The four original fields are required; page metadata is optional as described above.
Change `image_revision` whenever image bytes change.
The firmware compares the URL and revision, and retains that identity across
deep sleep. A reset or firmware upload forces a new download. Pagination is written to flash only when the saved state changes;
there is no filesystem image cache, and the e-paper physically retains
its last image without power. It cannot restore an image after a interrupted
physical screen refresh until a later successful fetch.

`refresh_seconds` is the sleep duration **after** each successful check;
connection, download, and display time are additional. Zero is rejected.
A changed interval is honoured even when the image is unchanged.
Invalid JSON, TLS errors, download errors, unsupported PNGs, and decode failures
all use the retry schedule above. A successful check resets it.

PNG requirements: exactly the dimensions listed for each board above,
non-interlaced, at most 8 bits per channel,
maximum 2 MiB compressed (512 KiB on Waveshare C6). RGB, RGBA, indexed and grayscale inputs are handled by
PNGdec; transparency is composited onto white. LCD boards display RGB565 colour.
LilyGO images are converted to 16-level grayscale with subtle ordered dithering. Set `DITHER=false` in `config.h` for
already-dithered/server-quantized artwork. PNG decode and CRC checks finish
before clearing the screen. JSON is limited to 4 KiB. Download bodies are
bounded in memory and time, including chunked transfers.

## Hardware verification

Build verified with PlatformIO. Local server integration tests cover trusted
TLS, downloads with certificate verification disabled, unchanged and changed revisions, immutable
image snapshots, and private-file isolation. Run them with:

```sh
uv run --with 'pillow>=11,<12' --with 'cryptography>=44,<46' python -m unittest discover -s tests -p '*_test.py'
```

Physical screen output and battery behaviour still require testing on the board:

- Show the sample: confirm readable text, all gray steps, and correct orientation.
- On LilyGO, try a photograph and compare dithering enabled/disabled.
- On Guition, check a 480 × 320 colour PNG with labelled corners and RGB swatches.
- On Guition, wait past a refresh interval: backlight and USB must remain on.
- Leave revision unchanged: confirm “Image unchanged” and no screen flash.
- Replace the PNG: confirm exactly one update, followed by unchanged checks.
- Stop the server or Wi-Fi: an error appears at bottom right; the rest stays intact.
- Serve malformed JSON, a truncated PNG, or a wrong-size image: check the error reason.
- Restore service with the same revision: the image should replace the error box.
- Use a self-signed certificate: the firmware should download successfully.
- Restart: it should refetch. Timer wake: it should retain revision state.
- Measure battery current during sleep and active updates before estimating life.

At 30-second polling, Wi-Fi/TLS and screen updates can dominate battery usage.
Start with 300 seconds when possible. This POC has no battery telemetry, OTA,
partial refresh, provisioning portal, or low-battery policy.

## Dependencies

Uses the [official LilyGO S3 driver and board definition](https://github.com/Xinyuan-LilyGO/LilyGo-EPD47),
pinned to commit `50224221fb81a8fcf9068532374b775af84a38d4`,
Espressif32 PlatformIO platform 6.12.0, ArduinoJson 7.4.2, and PNGdec 1.1.6.
The decoder buffer is enlarged for 960-pixel RGBA scanlines.
LilyGO's repository is GPL-3.0 licensed; preserve its notices and follow its
licensing terms when distributing firmware.

Guition uses [Arduino_GFX](https://github.com/moononournation/Arduino_GFX) 1.4.7.
The two hardware implementations live in `src/display_lilygo.cpp` and
`src/display_guition.cpp`; shared image downloading and page handling remain
in `src/main.cpp`. Both builds have been compiled; Guition panel output still
requires on-device verification.

## Browser installer and tagged releases

For maintainer setup, release tagging, Pages deployment and recovery, see the
[development and release guide](docs/development.md).

The `web/` directory is a GitHub Pages installer for all four boards. In the
repository settings, select **Pages → Build and deployment → GitHub Actions**.
Push a new Git tag to build all four firmware images, attach merged `.bin` files
and SHA256 checksums to a GitHub Release, and deploy the installer. The workflow
can also be run manually. Each successful run replaces the site’s offered version
with the version built by that run; older downloads remain on GitHub Releases.
The site URL is shown in the workflow’s `github-pages` deployment.

Use desktop Chrome or Edge with a USB data cable:

1. Select the exact display model and install firmware. The chip check cannot
   distinguish the three S3 display models. Leave the installer’s **Erase device** checkbox unchecked to retain settings.
2. If settings were kept, the device can reconnect using them. For first setup,
   after erasing, or to change settings, close the installation dialog and choose
   **Connect to configure**.
3. Enter a 2.4 GHz Wi-Fi network, password (blank for an open network), and the
   LNbits Gerty base pages endpoint. Save; the display restarts and starts using the saved settings.
4. Connect again to view or download serial logs. Saved settings confirm storage;
   logs confirm whether Wi-Fi and the endpoint actually work.

On startup, configured always-on colour devices show a centred `¯\_(ツ)_/¯`
while the first image loads. The LilyGO T5 e-paper device skips this splash
screen, including after a reset; timer wakes keep an existing image.

New release devices show an on-screen setup guide with the installer address
and USB configuration steps, and wait for configuration indefinitely. Configured
devices start without a setup delay, including local builds with valid compiled
settings. To change settings, connect over USB while the device is awake.
Configuration is serviced between updates and during Wi-Fi connection attempts.
LilyGO disconnects USB during deep sleep; press RST without BOOT and reconnect to
wake it. If connecting before sleep is difficult, reinstall with **Erase device**
to return to the setup screen (this removes all saved device storage).

Settings are saved atomically in the device’s NVS flash and survive ordinary
PlatformIO app-only updates unless flash is erased. Browser installation preserves settings by default by flashing separate firmware
segments around NVS. Selecting **Erase device** erases all device storage; configure
again afterwards. They are not stored in the browser or sent to
GitHub. They are not encrypted in device flash. Existing HTTPS certificate
verification behavior described above remains unchanged. Logs can contain private
endpoint URLs; review them before sharing.

Release builds (`GERTY_RELEASE=1`) ignore `secrets.h` and compiled endpoint
settings. Developer builds still use the existing defaults when no saved settings
exist. Saved USB settings take precedence. The build script derives flash
parts and settings from PlatformIO and merges a full image with esptool, then
extracts patched segments for the browser without writing over NVS. Full merged
release downloads remain factory images and can overwrite settings.

To build release images locally:

```sh
GERTY_RELEASE=1 GERTY_VERSION=local pio run -e T5-ePaper-S3 -e guition-JC3248W535 -e guition-JC4827W543
GERTY_RELEASE=1 GERTY_VERSION=local pio run -e waveshare-ESP32-C6-LCD-1_3
python3 -m http.server 8000 --directory web
```

Open `http://localhost:8000` (localhost supports Web Serial). Public hosting
requires HTTPS. The installer loads the pinned ESP Web Tools 10.1.1 module from
unpkg; internet access is required. Firmware is served from the same Pages site.
Physical USB flashing, persistent settings, screen output and serial reconnects
should be verified on each board before distributing a release.

Run installer checks with `node --test tests/web_installer_test.cjs` and
`python3 -m unittest discover -s tests -p package_firmware_test.py`. Avoid running
the S3 and C6 toolchain installations concurrently in a shared PlatformIO home.
