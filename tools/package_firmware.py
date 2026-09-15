"""PlatformIO post-build: merge using the actual board's flash layout/settings."""
Import("env")
import json
import os
import shlex
import re
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
    manifest = {"name": "Gerty " + env.subst("$PIOENV"),
                "version": os.environ.get("GERTY_VERSION", "local"),
                "new_install_prompt_erase": False, "new_install_improv_wait_time": 0,
                "builds": [{"chipFamily": "ESP32-C6" if chip == "esp32c6" else "ESP32-S3",
                            "parts": [{"path": "firmware.bin", "offset": 0}]}]}
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")

if os.environ.get("GERTY_RELEASE") == "1":
    env.Append(CPPDEFINES=["GERTY_RELEASE"])
    env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", package)
