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
- **Thinking:** initial LCD fetch, while no downloaded page is displayed.
- **Offline:** unavailable Wi-Fi connection or server, while no page is displayed.
- **Sad:** other update errors, while no page is displayed. Error text stays visible.
- **Sleeping:** server-requested sleep, while no page is displayed.

Downloaded pages are preserved during subsequent refreshes, errors and normal
power-saving sleep. The other ten expressions are available through
`Display::showExpression(Expressions::Face::...)` and as device-sized PNGs for
server pages. They have no automatic triggers yet: this firmware has no payment,
listening, or firmware-update event feed. Calling code that replaces a downloaded
page must also invalidate its cached image identity so that it can be restored.
