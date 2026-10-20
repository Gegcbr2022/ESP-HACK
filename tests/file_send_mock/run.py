"""Exercise actual file-send UI functions with production codecs and a fake radio."""
import argparse
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

parser=argparse.ArgumentParser()
parser.add_argument("--compiler")
args=parser.parse_args()
root=Path(__file__).resolve().parents[2]
compiler=args.compiler or shutil.which("g++") or shutil.which("clang++") or shutil.which("zig")
if not compiler: parser.error("Use --compiler with a native C++ compiler")
source=(root/"src/research/lab_suite.cpp").read_text(encoding="utf-8")
def function(name):
    match=re.search(r"^(?:__attribute__\(\(noinline\)\) )?(?:void|bool|const char\*) "+name+r"\([^\n]*\) \{",source,re.M)
    if not match: raise RuntimeError("Production function absent: "+name)
    opening=source.index("{",match.start());depth=0
    for token in re.finditer(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*.*?\*/|[{}]',source[opening:],re.S):
        if token.group()=="{": depth+=1
        elif token.group()=="}":
            depth-=1
            if not depth: return source[match.start():opening+token.end()]
    raise RuntimeError("Unclosed production function: "+name)
functions="\n\n".join(function(name) for name in ("prepareOpenedFileSend","sendOpenedFile","serialPrincetonTestFrequency","saveCapture","captureProgressText"))
template=(root/"tests/file_send_mock/file_send_test.cpp").read_text(encoding="utf-8")
with tempfile.TemporaryDirectory(prefix="esp-hack-file-send-") as directory:
    build=Path(directory);generated=build/"file_send.cpp"
    generated.write_text(template.replace("// PRODUCTION_FUNCTIONS",functions),encoding="utf-8")
    binary=build/("file_send_test.exe" if __import__("os").name=="nt" else "file_send_test")
    command=[compiler]
    if Path(compiler).name.lower() in ("zig","zig.exe"):command.append("c++")
    code=root/"src/research"
    command += ["-std=c++11","-Wall","-Wextra","-Werror","-O1","-I",str(code),str(generated)]
    command += [str(code/name) for name in ("radio_config.cpp","sub_file.cpp","static_sub_encoder.cpp")]
    command += ["-o",str(binary)]
    subprocess.run(command,check=True);subprocess.run([str(binary)],check=True)
