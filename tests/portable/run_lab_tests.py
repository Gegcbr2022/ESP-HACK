"""Compile and execute real lab_limits.h and test_decoder.h on the native host."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

parser=argparse.ArgumentParser()
parser.add_argument("--compiler")
parser.add_argument("--build-dir",type=Path)
args=parser.parse_args()
root=Path(__file__).resolve().parents[2]
compiler=args.compiler or shutil.which("g++") or shutil.which("clang++") or shutil.which("zig")
if not compiler: parser.error("Use --compiler with a native g++, clang++, or zig executable")
def run(build):
    build.mkdir(parents=True,exist_ok=True)
    binary=build/("lab_tests.exe" if __import__("os").name=="nt" else "lab_tests")
    command=[compiler]
    if Path(compiler).name.lower() in ("zig","zig.exe"): command.append("c++")
    command += ["-std=c++11","-Wall","-Wextra","-Werror","-O1","-g","-I",str(root/"src"/"research"),
                str(root/"tests"/"portable"/"lab_tests.cpp"),"-o",str(binary)]
    subprocess.run(command,check=True)
    subprocess.run([str(binary)],check=True)
if args.build_dir: run(args.build_dir.resolve())
else:
    with tempfile.TemporaryDirectory(prefix="esp-hack-lab-tests-") as directory: run(Path(directory))
