# Gerty e-paper proof of concept

PlatformIO / Arduino firmware for the **LilyGO T5-ePaper-S3 4.7-inch,
960 × 540** board. Not the older ESP32 model.

On each wake: connect to Wi-Fi → fetch HTTPS JSON → download a changed PNG →
decode into a grayscale framebuffer → fully refresh → power off the panel and
deep sleep. Unchanged images skip download and refresh. Failures show the reason
in a white box at the bottom right, preserving the rest of the image, and retry
after 30, 60, 120, 240, then 300 seconds. Identical errors are not redrawn.
Recovery downloads and restores the full image, even with an unchanged revision.

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
or NTP clock synchronization is required. Direct HTTPS URLs are required;
redirects are rejected.

If no upload port appears, hold BOOT, press/release RST, then release BOOT.
Deep sleep disconnects USB, so manual bootloader entry may be needed for upload.
Serial monitoring may need reconnecting after each wake.

### USB diagnostics

If the monitor is silent, use the `usb-debug` environment. It prints startup
and Wi-Fi progress, stays awake between checks, and emits a heartbeat every
five seconds so a monitor opened late still receives output. Use it on USB;
it consumes more battery power than the normal build.

Close existing monitors. Hold BOOT, press/release RST, then release BOOT to
enter upload mode. Upload and monitor with:

```sh
uv tool run --from platformio --with intelhex pio run -e usb-debug -t upload
uv tool run --from platformio --with intelhex pio device monitor -e usb-debug
```

Upload the default `T5-ePaper-S3` environment again to restore deep sleep.

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
  "refresh_seconds": 30
}
```

All fields are required. Change `image_revision` whenever image bytes change.
The firmware compares the URL and revision, and retains that identity across
deep sleep. A reset or firmware upload forces a new download. No repeated flash
writes or filesystem image cache are needed; the e-paper physically retains
its last image without power. It cannot restore an image after a interrupted
physical screen refresh until a later successful fetch.

`refresh_seconds` is the sleep duration **after** each successful check;
connection, download, and display time are additional. Values are clamped to
30–300 seconds. A changed interval is honoured even when the image is unchanged.
Invalid JSON, TLS errors, download errors, unsupported PNGs, and decode failures
all use the retry schedule above. A successful check resets it.

PNG requirements: exactly 960 × 540, non-interlaced, at most 8 bits per channel,
maximum 2 MiB compressed. RGB, RGBA, indexed and grayscale inputs are handled by
PNGdec; transparency is composited onto white. Images are converted to 16-level
grayscale with subtle ordered dithering. Set `DITHER=false` in `config.h` for
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
- Try a photograph, then compare dithering enabled/disabled.
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
