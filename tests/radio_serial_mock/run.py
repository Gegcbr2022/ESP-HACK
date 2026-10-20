"""Execute actual UART/deferred-controller bodies without Arduino or a device.

The source slice excludes diagnostic implementations/graphics dependencies;
controller functions are extracted verbatim, never reimplemented in the test.
"""
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
if not compiler:parser.error("Use --compiler with a native C++ compiler")
source=(root/"src/research/lab_suite.cpp").read_text(encoding="utf-8")
def function(name):
    match=re.search(r"^(?:__attribute__\(\(noinline\)\) )?(?:void|bool|uint16_t) "+name+r"\([^\n]*\) \{",source,re.M)
    if not match:raise RuntimeError("Production function absent: "+name)
    opening=source.index("{",match.start());depth=0
    for token in re.finditer(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*.*?\*/|[{}]',source[opening:],re.S):
        if token.group()=="{":depth+=1
        elif token.group()=="}":
            depth-=1
            if not depth:return source[match.start():opening+token.end()]
    raise RuntimeError("Unclosed production function: "+name)
names=("currentChatPeer","simpleChatSend","serialRadioWorkerActive","serialChatMainIdle","serialChatCanSend","serialChatListenFrequency","startSerialChatListen","radioLabSerialPoll","radioLabHandlePending")
controller="\n\n".join(function(name) for name in names)
main=(root/"src/main.cpp").read_text(encoding="utf-8")
loop=main[main.index("void loop() {"):]
if not re.match(r"void loop\(\) \{\s*if\(inMenu\) \{\s*radioLabSerialPoll\(\);\s*radioLabHandlePending\(\);\s*\}",loop):
    raise RuntimeError("Main loop must dispatch only after UART poll returns")
template=(root/"tests/radio_serial_mock/serial_test.cpp").read_text(encoding="utf-8")
with tempfile.TemporaryDirectory(prefix="esp-hack-radio-serial-") as directory:
    build=Path(directory);generated=build/"controller.cpp";generated.write_text(template.replace("// PRODUCTION_FUNCTIONS",controller),encoding="utf-8")
    binary=build/("serial_test.exe" if __import__("os").name=="nt" else "serial_test")
    command=[compiler]
    zig=Path(compiler).name.lower() in ("zig","zig.exe")
    if zig:command.append("c++")
    command += ["-std=c++11","-Wall","-Wextra","-Werror","-O1","-fno-inline","-I",str(root/"src/research"),str(generated),str(root/"src/research/radio_config.cpp"),"-o",str(binary)]
    if not zig:command.insert(1,"-fstack-usage")
    subprocess.run(command,check=True);subprocess.run([str(binary)],check=True)
    if not zig:
        frames={}
        for path in build.glob("*.su"):
            for line in path.read_text().splitlines():
                for name in ("radioLabSerialPoll","radioLabHandlePending"):
                    if name+"()" in line:
                        fields=line.split("\t");frames[name]=int(fields[1])
        if len(frames)!=2 or any(size>256 for size in frames.values()):raise RuntimeError("Native controller frame regression: "+str(frames))
        print("Native GCC frame bytes:",frames,"(not an ESP32 stack measurement)")
