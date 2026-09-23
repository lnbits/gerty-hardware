# Gerty expressions

The 15 faces are cropped from the supplied poster in `source/expressions.png`,
with captions removed and artwork converted to clean black and white. The
rounded frame and expression accents are retained. `preview.png` follows the
poster order, left to right and top to bottom.

Each named PNG is a 320×240 monochrome master. The `lcd` folder contains smooth
grayscale masters derived directly from the source. Device-ready versions are in these folders:

| Folder | Display |
| --- | --- |
| `240x240` | Waveshare ESP32-C6 LCD |
| `480x272` | Guition JC4827W543 |
| `480x320` | Guition JC3248W535 |
| `800x480` | Seeed TRMNL e-paper |
| `960x540` | LilyGO T5 e-paper |

Images preserve proportions with white margins and are non-interlaced PNGs.
Guition and Waveshare PNGs use antialiased grayscale edges. E-paper PNGs stay
strictly black and white. LCD firmware draws the native-resolution smooth
versions directly as RGB565 horizontal runs, avoiding pixelated scaling.
Regenerate with `python3 tools/prepare_expressions.py` (requires ImageMagick).
This also creates `include/expression_bitmaps.h`: 144 KB of packed monochrome
artwork for e-paper, plus `include/expression_lcd_*.h` with compressed grayscale
runs for LCDs. Only the selected display's artwork is linked into firmware;
neither renderer allocates an additional image buffer.

## Firmware behavior

- **Happy:** startup on all screens; e-paper timer wakes preserve existing pages.
- **Thinking:** a 64×48 badge, inset 8 pixels from the bottom right, over the
  current screen during every page request. LCD edges stay antialiased; e-paper
  uses black and white. The surrounding padding is transparent; the face
  interior stays white. A one-pixel white outline keeps the face visible on
  dark pages. The badge disappears when the request finishes.
- **Offline:** unavailable Wi-Fi connection or server, while no page is displayed.
- **Sad:** other update errors, while no page is displayed. Error text stays visible.
- **Sleeping:** server-requested sleep, while no page is displayed.

Downloaded pages are preserved during subsequent refreshes, errors and normal
power-saving sleep. The other ten expressions are available through
`Display::showExpression(Expressions::Face::...)` and as device-sized PNGs for
server pages. They have no automatic triggers yet: this firmware has no payment,
listening, or firmware-update event feed. Calling code that replaces a downloaded
page must also invalidate its cached image identity so that it can be restored.

The badge preserves the covered pixels on success, unchanged responses, failed
requests, and scheduled sleep. LilyGO retains the corner in RTC memory and
refreshes only that rectangle. Guition copies its canvas corner; Waveshare tracks
only that corner (6 KB), including error text. Seeed caches the last successfully
displayed full frame in its filesystem so it can recover it after deep sleep;
its current driver uses full refreshes to show and remove the badge. That adds
refresh flashes and power use during fetches. The temporary badge is never saved
to the frame cache; flash is written only when a page or status screen changes.
If the cache cannot be recovered, Seeed skips the badge until it displays a new
known frame. The generator also emits `include/thinking_badge.h`.
