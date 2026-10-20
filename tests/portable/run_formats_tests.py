"""Compile and execute the portable radio/.sub tests without Arduino hardware.

Examples:
  python tests/portable/run_formats_tests.py --compiler /path/to/g++
  python tests/portable/run_formats_tests.py --compiler /path/to/zig.exe
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--compiler", help="Native g++, clang++, or zig executable")
parser.add_argument("--build-dir", type=Path)
args = parser.parse_args()
here = Path(__file__).resolve().parent
root = here.parents[1] if here.name == "portable" else here
source = root / "src" / "research" if here.name == "portable" else here
compiler = args.compiler or shutil.which("g++") or shutil.which("clang++") or shutil.which("zig")
if not compiler:
    parser.error("A native C++ compiler is required; use --compiler with g++, clang++, or zig")

def run(build):
    build.mkdir(parents=True, exist_ok=True)
    executable = build / ("formats_tests.exe" if __import__("os").name == "nt" else "formats_tests")
    command = [compiler]
    if Path(compiler).name.lower() in ("zig", "zig.exe"):
        command.append("c++")
    command += ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-g", "-I", str(source),
                str(source / "radio_config.cpp"), str(source / "sub_file.cpp"),
                str(here / "formats_tests.cpp"), "-o", str(executable)]
    subprocess.run(command, check=True)
    fixtures = root / "tests" / "fixtures" if here.name == "portable" else here / "fixtures"
    subprocess.run([str(executable), str(fixtures)], check=True)

if args.build_dir:
    run(args.build_dir.resolve())
else:
    with tempfile.TemporaryDirectory(prefix="esp-hack-formats-") as directory:
        run(Path(directory))
