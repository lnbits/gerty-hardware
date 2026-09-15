"""Verify the T5 firmware links separate LilyGO and PNGdec decompressors.

Run after building T5-ePaper-S3: python3 tests/lilygo_font_test.py
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def main():
    link_map = (ROOT / '.pio/build/T5-ePaper-S3/firmware.map').read_text()
    for symbol, library in [('inflateInit2_', 'PNGdec'), ('z_inflateInit2_', 'LilyGo-EPD47'),
                            ('z_uncompress', 'LilyGo-EPD47')]:
        assert re.search(r'^' + re.escape(symbol) + r'\s+.*lib' + re.escape(library) + r'\.a',
                         link_map, re.MULTILINE), f'{symbol} linked to the wrong library'
    print('T5 firmware links separate LilyGO and PNGdec decompressors')


if __name__ == '__main__':
    main()
