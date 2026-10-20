"""Exercise the production Flipper plaintext adapter as C99 and C++11."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--compiler", help="Native GCC, Clang, or Zig executable")
parser.add_argument("--build-dir", type=Path)
args = parser.parse_args()
compiler = args.compiler or shutil.which("gcc") or shutil.which("clang") or shutil.which("zig")
if not compiler:
    parser.error("Provide a native C/C++ compiler with --compiler")
here = Path(__file__).resolve().parent


def run(build):
    build.mkdir(parents=True, exist_ok=True)
    for language, standard in (("c", "c99"), ("c++", "c++11")):
        output = build / ("compat_" + standard.replace("+", "p") + (".exe" if os.name == "nt" else ""))
        command = [compiler]
        if Path(compiler).name.lower() in ("zig", "zig.exe"):
            command.append("cc" if language == "c" else "c++")
        command += ["-x", language, "-std=" + standard, "-Wall", "-Wextra", "-Werror", "-O1", "-g",
                    str(here / "test_compat.c"), "-o", str(output)]
        subprocess.run(command, check=True)
        subprocess.run([str(output)], check=True)


if args.build_dir:
    run(args.build_dir.resolve())
else:
    with tempfile.TemporaryDirectory(prefix="esp-hack-flipper-compat-") as directory:
        run(Path(directory))
