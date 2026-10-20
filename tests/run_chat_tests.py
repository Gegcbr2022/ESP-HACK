"""Compile and run the shared transport tests as C99 and C++11, without hardware.

Example: python tests/run_chat_tests.py --compiler /path/to/zig.exe
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--compiler", help="Native gcc, clang, or zig executable")
parser.add_argument("--build-dir", type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
compiler = args.compiler or shutil.which("cc") or shutil.which("gcc") or shutil.which("clang") or shutil.which("zig")
if not compiler:
    parser.error("A native C/C++ compiler is required; provide --compiler")


def run(build):
    build.mkdir(parents=True, exist_ok=True)
    for language, standard in (("c", "c99"), ("c++", "c++11")):
        output = build / ("chat_" + standard.replace("+", "p") + (".exe" if os.name == "nt" else ""))
        command = [compiler]
        if Path(compiler).name.lower() in ("zig", "zig.exe"):
            command.append("cc" if language == "c" else "c++")
        command += ["-x", language, "-std=" + standard, "-Wall", "-Wextra", "-Werror", "-O1", "-g",
                    str(root / "tests" / "test_chat.c"), "-o", str(output)]
        subprocess.run(command, check=True)
        subprocess.run([str(output)], check=True)


if args.build_dir:
    run(args.build_dir.resolve())
else:
    with tempfile.TemporaryDirectory(prefix="esp-hack-chat-") as directory:
        run(Path(directory))
