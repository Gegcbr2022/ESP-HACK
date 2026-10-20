"""Compile and execute legacy payload packing and replay/configuration guards."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

parser=argparse.ArgumentParser()
parser.add_argument("--compiler",help="Native g++, clang++, or Zig executable")
parser.add_argument("--build-dir",type=Path)
args=parser.parse_args()
root=Path(__file__).resolve().parents[2]
compiler=args.compiler or shutil.which("g++") or shutil.which("clang++") or shutil.which("zig")
if not compiler: parser.error("Provide a native C++ compiler with --compiler")
def run(build):
    build.mkdir(parents=True,exist_ok=True)
    executable=build/("legacy_guard_tests.exe" if __import__("os").name=="nt" else "legacy_guard_tests")
    command=[compiler]
    if Path(compiler).name.lower() in ("zig","zig.exe"): command.append("c++")
    command += ["-std=c++11","-Wall","-Wextra","-Werror","-O1","-g","-I",str(root/"src"),
                str(root/"tests"/"portable"/"legacy_guard_tests.cpp"),
                str(root/"src"/"research"/"radio_config.cpp"),
                str(root/"src"/"research"/"sub_file.cpp"),"-o",str(executable)]
    subprocess.run(command,check=True)
    subprocess.run([str(executable)],check=True)
if args.build_dir: run(args.build_dir.resolve())
else:
    with tempfile.TemporaryDirectory(prefix="esp-hack-legacy-tests-") as directory: run(Path(directory))
