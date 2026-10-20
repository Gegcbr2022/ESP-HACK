"""Merge a reproducible full image using PlatformIO's installed esptool."""
import re
import subprocess
from pathlib import Path

Import("env")


def merge_binaries(source, target, env):
    project = Path(env["PROJECT_DIR"])
    version = re.search(r'FIRMWARE\s*=\s*"([^"]+)"',
                        (project / "src/CONFIG.h").read_text(encoding="utf-8")).group(1)
    suffix = "sh1106" if env["PIOENV"] == "SH1106" else "ssd1306"
    build = Path(env.subst("$BUILD_DIR"))
    tool = Path(env.PioPlatform().get_package_dir("tool-esptoolpy")) / "esptool.py"
    framework = Path(env.PioPlatform().get_package_dir("framework-arduinoespressif32"))
    output = project / ("esp-hack_" + version + "-" + suffix + "-full.bin")
    command = [env.subst("$PYTHONEXE").strip('"'), str(tool), "--chip", "esp32",
               "merge_bin", "--flash_mode", "dio", "--flash_freq", "40m",
               "--flash_size", "4MB", "-o", str(output),
               "0x1000", str(build / "bootloader.bin"),
               "0x8000", str(build / "partitions.bin"),
               "0xe000", str(framework / "tools/partitions/boot_app0.bin"),
               "0x10000", str(build / "firmware.bin")]
    subprocess.run(command, check=True, shell=False)
    print("Full image:", output)
    print("Application only:", build / "firmware.bin", "at 0x10000")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge_binaries)
