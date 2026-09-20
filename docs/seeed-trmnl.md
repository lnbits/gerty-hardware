# Seeed TRMNL 7.5-inch OG DIY Kit

## Supported hardware and references

The target `seeed-TRMNL-7_5` is for the Seeed kit with the XIAO ESP32-S3 Plus,
800 × 480 monochrome UC8179 panel and ribbon extension. It does not cover the
XIAO ESP32-C3 kit, reTerminal, or replacement three/six-colour panels.

The hardware identification and configuration come from:

- [Seeed kit overview and assembly](https://wiki.seeedstudio.com/trmnl_7inch5_diy_kit_main_page/).
- [Seeed XIAO S3/Plus specifications](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/):
  240 MHz ESP32-S3, 8 MB OPI PSRAM, **16 MB flash on Plus**.
- [Seeed Arduino cookbook](https://wiki.seeedstudio.com/ogdiy_kit_works_with_arduino/):
  UC8179, three active-low user buttons and switched battery measurement.
- [Seeed ESPHome example](https://wiki.seeedstudio.com/ogdiy_kit_works_with_esphome/):
  SPI and active-low BUSY wiring.
- [Seeed board schematic](https://files.seeedstudio.com/wiki/XIAO_Gadget/TRMNL_Kit_Pic/XIAO_ePaper_driver_board_sch.pdf)
  and [Seeed_GFX board pin definitions](https://github.com/Seeed-Studio/Seeed_GFX/blob/0dfdd7135425be82b5bb4b4b58d74dd51ab29a59/User_Setups/EPaper_Board_Pins_Setups.h):
  the display-board pin mapping, including panel enable.
- [Pinned GxEPD2 UC8179 driver](https://github.com/ZinggJM/GxEPD2/blob/1.6.4/src/gdey/GxEPD2_750_GDEY075T7.h).

| Signal | ESP32 GPIO |
| --- | --- |
| SPI clock | 7 |
| SPI MOSI | 9 |
| Panel CS | 44 |
| Panel DC | 10 |
| Panel reset | 38 |
| Panel BUSY (active low) | 4 |
| Panel supply enable (active high) | 43 |
| Battery measurement enable | 6 (kept low) |

GPIO numbers are used explicitly. The older Arduino XIAO variant does not expose
all Plus pins as `D` aliases. The environment inherits its OPI PSRAM/USB setup,
overrides flash to 16 MB, and selects `default_16MB.csv`. It uses the same pinned
Espressif32 6.12.0 toolchain as the existing S3 boards.

## Firmware behavior

The display backend receives exactly 800 × 480 non-interlaced PNGs through the
existing shared download/decode path. A 48,000-byte, white=1, MSB-first buffer
holds the completed monochrome image. RGB565 scanlines are converted using
luminance and a 4 × 4 ordered dither; exact black and white stay unchanged.
Disable `Config::DITHER` for threshold-only conversion.

GxEPD2 1.6.4 supplies the UC8179 initialization, full-refresh waveform and sleep
commands. Adafruit GFX supplies setup/error text. These dependencies are pinned
only for this environment. GxEPD2 is GPL-3.0; preserve upstream notices and
observe its license when distributing firmware.

No image is sent to the panel until PNG dimensions, decode completeness and CRC
checks pass. The BUSY callback latches failures before the driver's ten-second
timeout, and the backend checks BUSY before accepting each operation. Failed
refreshes return false, so the shared protocol does not save the new image
identity or normal next page. SPI cannot detect every disconnected-panel fault;
physical output must still be verified.

The backend hibernates the panel after a successful refresh, disconnects SPI,
drives its signal pins low, and holds supply enable low through MCU deep sleep.
The battery divider stays disabled. It does not measure battery state or claim
a battery lifetime. The visible image survives panel power-off.

The previous framebuffer is retained in PSRAM only for the current boot. Errors
can be drawn over that known frame. After deep sleep or a reset, the backend
leaves the physical page untouched on failure and relies on USB diagnostics and
the shared retry schedule. It does not attempt partial refresh using lost panel
RAM. A successful retry redraws the whole page. Normal updates and LNbits
scheduled sleep use timer wake; user buttons are not assigned page actions.

## LNbits extension

The companion extension must provide `epaper_800x480` in its display selector.
The profile returns grayscale PNGs at 800 × 480. Generic screens render at the
requested dimensions, gallery and wallet-history renderers use the profile,
and the block-explorer chart is fitted proportionally with white margins.
The firmware applies final black/white dithering. API URLs, pagination and sleep
responses use the existing schema; no credentials or device parameters are added.

## Verification before hardware release

Compile/package the environment with `GERTY_RELEASE=1`, then check these on the
actual kit. Automated tests and a successful compiler run cannot establish
display orientation, waveform suitability or battery current.

1. Flash the kit, erasing old TRMNL firmware settings on the first installation.
   Confirm readable setup instructions and save Wi-Fi/API settings through USB.
2. Display an 800 × 480 image with labelled corners, black/white text, a QR code
   and gray ramps. Confirm orientation, polarity, legibility and full refresh.
3. Check representative LNbits pages, gallery images and the block explorer.
   Check next-page progression through timer wake and power cycling.
4. Keep a single page's revision unchanged: confirm no refresh after timer wake.
   Change the revision: confirm one refresh. Reset: confirm a fresh download.
5. Interrupt Wi-Fi and serve truncated, CRC-damaged and wrong-size PNGs. The old
   page must survive; recovery must redraw even with the same revision.
6. Test scheduled sleep from both manifest and image endpoints. Confirm the
   pending page is retained and a fresh manifest is requested after wake.
7. Simulate BUSY held low with an appropriate test fixture. Check timeout logs,
   panel supply shutdown, retry and absence of a successful-page commit.
8. Measure sleep and refresh current on battery, and check USB recovery with
   BOOT/RST. Test repeated wake/refresh cycles before unattended deployment.

The host checks are `tests/monochrome_test.cpp`, `tests/sleep_response_test.cpp`,
`tests/seeed_display_test.py` (panel timeout, allocation and power fault injection),
the existing protocol/setup/installer/packaging/HTTPS tests, and the companion
extension's rendering/API tests. The repository's release workflow builds this
target alongside the four existing devices; it does not flash attached hardware.
