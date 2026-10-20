"""Compile and execute the portable RSSI-triggered RAW capture policy."""
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
    output = build / ("triggered_capture_tests.exe" if os.name == "nt" else "triggered_capture_tests")
    command = [compiler]
    if Path(compiler).name.lower() in ("zig", "zig.exe"):
        command.append("c++")
    command += ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
                "-I", str(root / "src" / "research"), str(here / "triggered_capture_test.cpp"), "-o", str(output)]
    subprocess.run(command, check=True)
    subprocess.run([str(output)], check=True)


if args.build_dir:
    run(args.build_dir.resolve())
else:
    with tempfile.TemporaryDirectory(prefix="esp-hack-triggered-") as directory:
        run(Path(directory))
