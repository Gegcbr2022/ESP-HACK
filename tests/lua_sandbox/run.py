"""Build real vendored Lua as C, then exercise the production sandbox on host."""
import argparse
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import shutil
import subprocess
import tempfile

parser=argparse.ArgumentParser()
parser.add_argument("--compiler",help="Native zig, clang, or gcc executable")
parser.add_argument("--build-dir",type=Path)
args=parser.parse_args()
root=Path(__file__).resolve().parents[2]
compiler=args.compiler or shutil.which("zig") or shutil.which("clang") or shutil.which("gcc")
if not compiler: parser.error("Use --compiler with a native Zig/Clang/GCC executable")
zig=Path(compiler).name.lower() in ("zig","zig.exe")
def run(build):
    build.mkdir(parents=True,exist_ok=True)
    library=root/"lib"/"Lua54"/"src"
    sources=sorted(library.glob("*.c"))
    if not sources: raise RuntimeError("Vendored Lua C sources not found")
    def compile_c(source):
        obj=build/(source.stem+".o")
        command=[compiler]+(["cc"] if zig else [])
        command += ["-std=c99","-DLUAI_MAXCCALLS=40","-O1","-I",str(library),"-c",str(source),"-o",str(obj)]
        subprocess.run(command,check=True)
        return obj
    with ThreadPoolExecutor(max_workers=4) as executor: objects=list(executor.map(compile_c,sources))
    executable=build/("lua_sandbox_test.exe" if __import__("os").name=="nt" else "lua_sandbox_test")
    if zig: command=[compiler,"c++"]
    else:
        name=Path(compiler).name
        cxx=name.replace("clang","clang++") if "clang" in name else name.replace("gcc","g++")
        command=[str(Path(compiler).with_name(cxx)) if Path(compiler).parent!=Path('.') else cxx]
    command += ["-std=c++11","-Wall","-Wextra","-Werror","-O1","-g","-I",str(library),
                "-I",str(root/"src"/"research"),str(root/"tests"/"lua_sandbox"/"sandbox_test.cpp")]
    command += [str(obj) for obj in objects]+["-o",str(executable)]
    subprocess.run(command,check=True)
    examples=root/"examples"/"lua_apps"
    names=("hello.lua","stopwatch.lua","rf_dashboard.lua","snake.lua","game2048.lua",
           "countdown.lua","calculator.lua","unit_converter.lua","resistor_codes.lua","blockfall.lua","mines.lua")
    subprocess.run([str(executable)]+[str(examples/name) for name in names],check=True)
if args.build_dir: run(args.build_dir.resolve())
else:
    with tempfile.TemporaryDirectory(prefix="esp-hack-lua-tests-") as directory: run(Path(directory))
