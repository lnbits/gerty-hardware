"""Exercise LilyGO font decompression alongside PNGdec's incompatible inflater.

Run after pio has installed the T5 libraries: python3 tests/lilygo_font_test.py
"""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
LIBS = ROOT / '.pio/libdeps/T5-ePaper-S3'
LILYGO = LIBS / 'LilyGo-EPD47/src'
PNG = LIBS / 'PNGdec/src'


def main():
    link_map = (ROOT / '.pio/build/T5-ePaper-S3/firmware.map').read_text()
    for symbol, library in [('inflateInit2_', 'PNGdec'), ('z_inflateInit2_', 'LilyGo-EPD47'),
                            ('z_uncompress', 'LilyGo-EPD47')]:
        assert re.search(r'^' + re.escape(symbol) + r'\s+.*lib' + re.escape(library) + r'\.a',
                         link_map, re.MULTILINE), f'{symbol} linked to the wrong library'
    font = (LILYGO / 'firasans.h').read_text()
    bitmaps = bytes(int(v, 16) for v in re.findall(r'0x([0-9A-Fa-f]{2})', font.split('const GFXglyph')[0]))
    glyphs = [list(map(int, row.split(','))) for row in re.findall(
        r'\{ ([\d, -]+) \}', font.split('const GFXglyph FiraSansGlyphs[] = {')[1])][:95]
    cases = []
    for index, (width, height, advance, left, top, size, offset) in enumerate(glyphs):
        if not width or not height:
            continue
        compressed = bitmaps[offset:offset + size]
        expected = zlib.decompress(compressed)
        assert len(expected) == (width // 2 + width % 2) * height
        literal = lambda data: ','.join(str(b) for b in data)
        cases.append(f'''{{
          unsigned char input[] = {{{literal(compressed)}}};
          unsigned char expected[] = {{{literal(expected)}}};
          unsigned char output[sizeof(expected)];
          unsigned long size = sizeof(output);
          assert(uncompress(output, &size, input, sizeof(input)) == Z_OK);
          assert(size == sizeof(expected));
          assert(memcmp(output, expected, size) == 0);
        }}''')
    with tempfile.TemporaryDirectory() as temp:
        work = Path(temp)
        compiler = os.environ.get('CC', 'cc')
        objects = []
        for directory, prefix, names in (
            (LILYGO / 'zlib', True, ['adler32', 'crc32', 'inffast', 'inflate', 'inftrees', 'uncompr', 'zutil']),
            (PNG, False, ['adler32', 'crc32', 'inffast', 'inflate', 'inftrees', 'zutil']),
        ):
            for name in names:
                obj = work / f'{"font" if prefix else "png"}-{name}.o'
                subprocess.run([compiler, '-std=c99', '-O1', '-w', '-include', 'stdio.h', *(['-DZ_PREFIX', '-Dz_errmsg=lilygo_z_errmsg'] if prefix else []),
                                '-c', str(directory / f'{name}.c'), '-o', str(obj)], check=True)
                objects.append(str(obj))
        source = work / 'test.c'
        source.write_text('#include <assert.h>\n#include <string.h>\n#include "zlib.h"\nint main(void) {\n'
                          + '\n'.join(cases) + '\nreturn 0;\n}\n')
        binary = work / 'font-test'
        subprocess.run([compiler, '-DZ_PREFIX', '-I', str(LILYGO / 'zlib'), str(source),
                        *objects, '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    print(f'{len(cases)} FiraSans glyphs decoded correctly with both libraries linked')


if __name__ == '__main__':
    main()
