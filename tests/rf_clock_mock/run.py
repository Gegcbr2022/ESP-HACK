"""Build the real finite clock probe against native hardware doubles. No devices."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile
parser=argparse.ArgumentParser()
parser.add_argument("--compiler")
args=parser.parse_args()
root=Path(__file__).resolve().parents[2]
compiler=args.compiler or shutil.which("g++") or shutil.which("clang++") or shutil.which("zig")
if not compiler: parser.error("Use --compiler with a native C++ compiler")
with tempfile.TemporaryDirectory(prefix="esp-hack-clock-tests-") as directory:
    binary=Path(directory)/("clock_test.exe" if __import__("os").name=="nt" else "clock_test")
    command=[compiler]
    if Path(compiler).name.lower() in ("zig","zig.exe"):command.append("c++")
    # Shared CONFIG.h declares its firmware-version pointer in every consumer.
    command += ["-std=c++11","-Wall","-Wextra","-Werror","-Wno-unused-variable","-O1","-I",str(root/"tests/rf_clock_mock/fake"),"-I",str(root/"src/research"),
                str(root/"src/research/rf_clock_probe.cpp"),str(root/"tests/rf_clock_mock/probe_test.cpp"),"-o",str(binary)]
    subprocess.run(command,check=True)
    subprocess.run([str(binary)],check=True)
