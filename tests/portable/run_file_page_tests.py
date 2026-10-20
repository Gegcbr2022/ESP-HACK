"""Exercise production page selection and complete filesystem path joining."""
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
if not compiler:parser.error("Use --compiler with a native C++ compiler")
def run(build):
    build.mkdir(parents=True,exist_ok=True)
    binary=build/("file_page_tests.exe" if __import__("os").name=="nt" else "file_page_tests")
    command=[compiler]
    if Path(compiler).name.lower() in ("zig","zig.exe"):command.append("c++")
    command += ["-std=c++11","-Wall","-Wextra","-Werror","-O1","-I",str(root/"src"/"research"),
                str(root/"tests"/"portable"/"file_page_tests.cpp"),"-o",str(binary)]
    subprocess.run(command,check=True);subprocess.run([str(binary)],check=True)
if args.build_dir:run(args.build_dir.resolve())
else:
    with tempfile.TemporaryDirectory(prefix="esp-hack-file-page-") as directory:run(Path(directory))
