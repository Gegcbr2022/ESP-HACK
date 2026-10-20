"""Compile the production ASCII keyboard model and validate the real UI alphabet."""
import argparse
import ast
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
parser=argparse.ArgumentParser()
parser.add_argument("--compiler")
parser.add_argument("--sanitize",action="store_true",help="ASan/UBSan; use native Linux GCC/Clang")
args=parser.parse_args()
root=Path(__file__).resolve().parents[2]
compiler=args.compiler or shutil.which("g++") or shutil.which("clang++") or shutil.which("zig")
if not compiler:parser.error("Use --compiler with a native C++ compiler")
source=(root/"src/research/lab_suite.cpp").read_text(encoding="utf-8")
match=re.search(r'static const char keys\[\] = ("(?:\\.|[^"\\])*");',source)
if not match:raise RuntimeError("Production text-entry default alphabet not found")
alphabet=ast.literal_eval(match.group(1))
with tempfile.TemporaryDirectory(prefix="esp-hack-keyboard-") as directory:
    build=Path(directory);(build/"production_default.inc").write_text("static const char productionDefault[]="+json.dumps(alphabet)+";\n",encoding="ascii")
    binary=build/("model_test.exe" if __import__("os").name=="nt" else "model_test")
    command=[compiler]
    if Path(compiler).name.lower() in ("zig","zig.exe"):command.append("c++")
    command += ["-std=c++11","-Wall","-Wextra","-Werror","-O1","-g","-I",str(root/"src/interface"),"-I",str(build),str(root/"tests/keyboard_mock/model_test.cpp"),"-o",str(binary)]
    if args.sanitize:command += ["-fsanitize=address,undefined","-fno-omit-frame-pointer"]
    subprocess.run(command,check=True);subprocess.run([str(binary)],check=True)
