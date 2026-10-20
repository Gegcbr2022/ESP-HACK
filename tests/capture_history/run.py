"""Compile the actual borrowed-memory RAW candidate history with native C++11."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--compiler", help="Native g++, clang++, or Zig executable")
parser.add_argument("--build-dir", type=Path)
parser.add_argument("--sanitize", action="store_true", help="Enable address/undefined sanitizers")
args = parser.parse_args()
here = Path(__file__).resolve().parent
root = here.parents[1]
compiler = args.compiler or shutil.which("g++") or shutil.which("clang++") or shutil.which("zig")
if not compiler:
    parser.error("Provide a native C++ compiler with --compiler")


def run(build):
    build.mkdir(parents=True, exist_ok=True)
    output = build / ("capture_history_test.exe" if os.name == "nt" else "capture_history_test")
    command = [compiler]
    if Path(compiler).name.lower() in ("zig", "zig.exe"):
        command.append("c++")
    command += ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-g"]
    if args.sanitize:
        command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    command += ["-I", str(root / "src" / "research"), str(here / "history_test.cpp"), "-o", str(output)]
    subprocess.run(command, check=True)
    subprocess.run([str(output)], check=True)


if args.build_dir:
    run(args.build_dir.resolve())
else:
    with tempfile.TemporaryDirectory(prefix="esp-hack-history-") as directory:
        run(Path(directory))
