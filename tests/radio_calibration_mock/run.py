"""Exercise production trim arithmetic and Preferences persistence with NVS faults."""
import argparse
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

parser=argparse.ArgumentParser()
parser.add_argument("--compiler")
parser.add_argument("--build-dir",type=Path)
args=parser.parse_args()
here=Path(__file__).resolve().parent
root=here.parents[1]
compiler=args.compiler or shutil.which("g++") or shutil.which("clang++") or shutil.which("zig")
if not compiler: parser.error("Provide --compiler with a native C++ compiler")
def run(build):
    build.mkdir(parents=True,exist_ok=True)
    output=build/("calibration_test.exe" if os.name=="nt" else "calibration_test")
    command=[compiler]
    if Path(compiler).name.lower() in ("zig","zig.exe"): command.append("c++")
    source=root/"src"/"research"
    command += ["-std=c++11","-Wall","-Wextra","-Werror","-O1","-I",str(here),"-I",str(source),
                str(source/"radio_calibration.cpp"),str(here/"calibration_test.cpp"),"-o",str(output)]
    subprocess.run(command,check=True)
    subprocess.run([str(output)],check=True)
if args.build_dir: run(args.build_dir.resolve())
else:
    with tempfile.TemporaryDirectory(prefix="esp-hack-calibration-") as directory: run(Path(directory))
