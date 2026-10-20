"""Compile the actual vendored driver against deterministic Arduino/SPI mocks."""
import argparse
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--compiler", help="Native g++, clang++, or Zig executable")
parser.add_argument("--build-dir", type=Path)
args = parser.parse_args()
here = Path(__file__).resolve().parent
root = here.parents[1]
compiler = args.compiler or shutil.which("g++") or shutil.which("clang++") or shutil.which("zig")
if not compiler:
    parser.error("Provide a native C++ compiler with --compiler")


def run(build):
    build.mkdir(parents=True, exist_ok=True)
    output = build / ("spi_driver_tests.exe" if os.name == "nt" else "spi_driver_tests")
    command = [compiler]
    if Path(compiler).name.lower() in ("zig", "zig.exe"):
        command.append("c++")
    library = root / "lib" / "SmartRC-CC1101"
    command += ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-g", "-DESP32",
                "-I", str(here), "-I", str(library), str(library / "SmartRC_CC1101.cpp"),
                str(here / "spi_driver_test.cpp"), "-o", str(output)]
    subprocess.run(command, check=True)
    subprocess.run([str(output)], check=True)


if args.build_dir:
    run(args.build_dir.resolve())
else:
    with tempfile.TemporaryDirectory(prefix="esp-hack-spi-") as directory:
        run(Path(directory))
