"""PlatformIO post-build: merge using the actual board's flash layout/settings."""
Import("env")
import json
import os
import shlex
import re
import struct
from pathlib import Path


def package(source, target, env):
    build = Path(env.subst("$BUILD_DIR"))
    output = Path("web/firmware") / env.subst("$PIOENV")
    output.mkdir(parents=True, exist_ok=True)
    board = env.BoardConfig()
    chip = board.get("build.mcu")
    mode = board.get("build.flash_mode", "dio")
    memory = board.get("build.arduino.memory_type", "")
    mode = "dout" if memory in ("opi_opi", "opi_qspi") else ("dio" if mode in ("qio", "qout") else mode)
    freq = str(int(board.get("build.f_flash", "80000000L").rstrip("L")) // 1000000) + "m"
    parts = []
    for offset, path in env.get("FLASH_EXTRA_IMAGES", []):
        parts.extend([str(offset), env.subst(path)])
    parts.extend([env.subst("$ESP32_APP_OFFSET"), str(build / "firmware.bin")])
    # Refuse to publish accidentally compiled private configuration defaults.
    image = (build / "firmware.bin").read_bytes()
    for header in (Path("include/config.h"), Path("include/secrets.h")):
        if header.exists():
            defaults = re.findall(r'(?:MANIFEST_URL|WIFI_SSID|WIFI_PASSWORD)\[\]\s*=\s*"([^"\n]+)"', header.read_text())
            if any(value.encode() in image for value in defaults):
                raise RuntimeError("Release firmware contains private configuration defaults")
    import subprocess
    uploader = env.subst("$UPLOADER")
    command = [env.subst("$PYTHONEXE"), uploader] if uploader.endswith(".py") else shlex.split(uploader)
    subprocess.run([*command, "--chip", chip,
                    "merge_bin", "-o", str(output / "firmware.bin"),
                    "--flash_mode", mode, "--flash_freq", freq,
                    "--flash_size", board.get("upload.flash_size"), *parts], check=True)
    # Full merged images contain padding over NVS. Flash only the original
    # segments in the browser, taking their patched bytes from the merged image.
    # Keep firmware.bin as the full factory image for release downloads.
    table = (build / "partitions.bin").read_bytes()
    protected = []
    for entry in struct.iter_unpack("<HBBII16sI", table[:len(table) // 32 * 32]):
        magic, kind, subtype, offset, size, label, flags = entry
        if magic != 0x50AA:
            break
        if kind == 1 and subtype in (2, 4):  # NVS and NVS encryption keys
            protected.append((offset, offset + size))
    if not protected:
        raise RuntimeError("Cannot locate NVS in partition table; refusing unsafe installer image")
    merged = (output / "firmware.bin").read_bytes()
    browser_parts = []
    for index in range(0, len(parts), 2):
        offset = int(parts[index], 0)
        length = Path(parts[index + 1]).stat().st_size
        # Flash erases complete 4 KiB sectors, even for a shorter segment.
        erase_start = offset // 4096 * 4096
        erase_end = (offset + length + 4095) // 4096 * 4096
        if any(erase_start < end and erase_end > start for start, end in protected):
            raise RuntimeError("Installer segment would erase NVS settings")
        if offset + length > len(merged):
            raise RuntimeError("Merged image does not contain the complete flash segment")
        name = f"part-{offset:06x}.bin"
        (output / name).write_bytes(merged[offset:offset + length])
        browser_parts.append({"path": name, "offset": offset})
    manifest = {"name": "Gerty " + env.subst("$PIOENV"),
                "version": os.environ.get("GERTY_VERSION", "local"),
                "new_install_prompt_erase": True, "new_install_improv_wait_time": 0,
                "builds": [{"chipFamily": "ESP32-C6" if chip == "esp32c6" else "ESP32-S3",
                            "parts": browser_parts}]}
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")

if os.environ.get("GERTY_RELEASE") == "1":
    env.Append(CPPDEFINES=["GERTY_RELEASE"])
    env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", package)
