"""Verify release packaging for both toolchains without running an MCU compiler."""
import json
import os
import struct
from pathlib import Path
import runpy
import tempfile
import unittest
from unittest.mock import patch

SCRIPT = Path(__file__).resolve().parents[1] / 'tools/package_firmware.py'


class Environment(dict):
    def __init__(self, chip, uploader, memory='qio_opi'):
        super().__init__(FLASH_EXTRA_IMAGES=[('0x0', 'bootloader.bin'), ('0x8000', 'partitions.bin')])
        self.chip, self.uploader, self.memory = chip, uploader, memory

    def subst(self, value):
        return {'$BUILD_DIR': 'build', '$PIOENV': self.chip,
                '$UPLOADER': self.uploader, '$PYTHONEXE': 'python',
                '$ESP32_APP_OFFSET': '0x10000'}.get(value, value)

    def BoardConfig(self):
        return {'build.mcu': self.chip, 'build.flash_mode': 'qio',
                'build.arduino.memory_type': self.memory,
                'build.f_flash': '80000000L', 'upload.flash_size': '4MB'}


class PackagingTests(unittest.TestCase):
    def check_package(self, chip, uploader, expected_prefix):
        env = Environment(chip, uploader)
        scope = runpy.run_path(str(SCRIPT), init_globals={'Import': lambda _: None, 'env': env})
        with tempfile.TemporaryDirectory() as directory:
            previous = os.getcwd()
            try:
                os.chdir(directory)
                Path("build").mkdir()
                Path("build/firmware.bin").write_bytes(b"test image")
                Path("bootloader.bin").write_bytes(b"original boot")
                table = struct.pack("<HBBII16sI", 0x50AA, 1, 2, 0x9000, 0x5000, b"nvs", 0)
                Path("partitions.bin").write_bytes(table)
                Path("build/partitions.bin").write_bytes(table)
                def merge(command, **kwargs):
                    data = bytearray(b"\xff" * (0x10000 + 10))
                    data[:13] = b"patched boot!"
                    data[0x8000:0x8000+len(table)] = table
                    data[0x10000:] = b"test image"
                    Path(command[command.index("-o") + 1]).write_bytes(data)
                with patch('subprocess.run', side_effect=merge) as run:
                    scope['package']([], [], env)
                args = run.call_args.args[0]
                self.assertEqual(args[:len(expected_prefix)], expected_prefix)
                self.assertEqual(args[-6:], ['0x0', 'bootloader.bin', '0x8000', 'partitions.bin', '0x10000', 'build/firmware.bin'])
                self.assertEqual(args[args.index('--flash_mode') + 1], 'dio')
                manifest = json.loads(Path(f'web/firmware/{chip}/manifest.json').read_text())
                self.assertEqual(manifest['builds'][0]['chipFamily'], 'ESP32-C6' if chip == 'esp32c6' else 'ESP32-S3')
                self.assertTrue(manifest['new_install_prompt_erase'])
                parts = manifest['builds'][0]['parts']
                self.assertEqual([p['offset'] for p in parts], [0, 0x8000, 0x10000])
                output = Path(f'web/firmware/{chip}')
                self.assertEqual((output / parts[0]['path']).read_bytes(), b'patched boot!')
                # Simulate the sector erases and writes performed during an update.
                flash = bytearray(b'\x42' * 0x20000)
                original_nvs = bytes(flash[0x9000:0xe000])
                for part in parts:
                    data = (output / part['path']).read_bytes()
                    offset = part['offset']
                    start = offset // 4096 * 4096
                    end = (offset + len(data) + 4095) // 4096 * 4096
                    flash[start:end] = b'\xff' * (end - start)
                    flash[offset:offset+len(data)] = data
                self.assertEqual(bytes(flash[0x9000:0xe000]), original_nvs)
            finally:
                os.chdir(previous)

    def test_private_defaults_block_publication(self):
        env = Environment('esp32s3', '/tools/esptool.py')
        scope = runpy.run_path(str(SCRIPT), init_globals={'Import': lambda _: None, 'env': env})
        with tempfile.TemporaryDirectory() as directory:
            previous = os.getcwd()
            try:
                os.chdir(directory)
                Path('build').mkdir()
                Path('include').mkdir()
                Path('include/config.h').write_text('constexpr char MANIFEST_URL[] = "https://private.example/feed";')
                Path('build/firmware.bin').write_bytes(b'image https://private.example/feed')
                with patch('subprocess.run') as run, self.assertRaisesRegex(RuntimeError, 'private configuration'):
                    scope['package']([], [], env)
                run.assert_not_called()
            finally:
                os.chdir(previous)

    def test_s3_python_esptool(self):
        self.check_package('esp32s3', '/tools/esptool.py', ['python', '/tools/esptool.py'])

    def test_c6_executable_esptool(self):
        self.check_package('esp32c6', '"/tools with spaces/esptool"', ['/tools with spaces/esptool'])


if __name__ == '__main__':
    unittest.main()
