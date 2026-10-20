"""Compile real menu renderers and appearance policy; write native128x64 PGM frames."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--compiler", help="Native GCC, Clang, or Zig C++ executable")
parser.add_argument("--snapshots", type=Path)
args = parser.parse_args()
compiler = args.compiler or shutil.which("g++") or shutil.which("clang++") or shutil.which("zig")
if not compiler:
    parser.error("Provide a native compiler with --compiler")
here = Path(__file__).resolve().parent
root = here.parents[1]


def run(build):
    snapshots = args.snapshots.resolve() if args.snapshots else build / "snapshots"
    snapshots.mkdir(parents=True, exist_ok=True)
    executable = build / ("pocket_test.exe" if os.name == "nt" else "pocket_test")
    command = [compiler]
    if Path(compiler).name.lower() in ("zig", "zig.exe"):
        command.append("c++")
    command += ["-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-g", "-DDISPLAY_TYPE=0",
                "-I" + str(here), "-I" + str(root / "src"), "-include", str(here / "config_usage.h"),
                str(here / "test_pocket.cpp")]
    command += [str(root / "src/interface/menu" / name) for name in
                ("pocket_menu.cpp", "list_menu.cpp", "pages_menu.cpp", "wii_menu.cpp", "dsi_menu.cpp")]
    command += [str(root / "src/interface/submenu" / name) for name in ("list_submenu.cpp", "roll_submenu.cpp")]
    command += ["-o", str(executable)]
    subprocess.run(command, check=True)
    subprocess.run([str(executable), str(snapshots)], check=True)
    if args.snapshots:
        print("Snapshots:", snapshots)


with tempfile.TemporaryDirectory(prefix="esp-pocket-test-") as directory:
    run(Path(directory))
