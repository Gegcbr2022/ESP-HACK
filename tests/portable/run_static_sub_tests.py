"""Compile real static .sub encoders against the portable radio/parser code."""
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
if not compiler: parser.error("Use --compiler with a native C++ compiler")
def run(build):
    build.mkdir(parents=True,exist_ok=True)
    binary=build/("static_sub_tests.exe" if __import__("os").name=="nt" else "static_sub_tests")
    command=[compiler]
    if Path(compiler).name.lower() in ("zig","zig.exe"):command.append("c++")
    source=root/"src"/"research"
    command += ["-std=c++11","-Wall","-Wextra","-Werror","-O1","-I",str(source)]
    command += [str(source/name) for name in ("radio_config.cpp","sub_file.cpp","static_sub_encoder.cpp")]
    command += [str(root/"tests"/"portable"/"static_sub_tests.cpp"),"-o",str(binary)]
    subprocess.run(command,check=True);subprocess.run([str(binary)],check=True)
if args.build_dir:run(args.build_dir.resolve())
else:
    with tempfile.TemporaryDirectory(prefix="esp-hack-static-sub-") as directory:run(Path(directory))
