"""Run real RAW worker steps/startup against timestamped IRQ and SPI doubles."""
import argparse
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
parser=argparse.ArgumentParser()
parser.add_argument('--compiler')
parser.add_argument('--sanitize',action='store_true')
args=parser.parse_args()
root=Path(__file__).resolve().parents[2]
compiler=args.compiler or shutil.which('g++') or shutil.which('clang++') or shutil.which('zig')
if not compiler:parser.error('Use --compiler with a native C++ compiler')
source=(root/'src/research/lab_service.cpp').read_text(encoding='utf-8')
def function(name):
    match=re.search(r'^(?:void|bool|size_t|RawTraceInfo) '+name+r'\([^;]*?\) \{',source,re.M)
    if not match:raise RuntimeError('Production function absent: '+name)
    opening=source.index('{',match.start());depth=0
    for token in re.finditer(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*.*?\*/|[{}]',source[opening:],re.S):
        if token.group()=='{':depth+=1
        elif token.group()=='}':
            depth-=1
            if not depth:return source[match.start():opening+token.end()]
    raise RuntimeError('Unclosed function '+name)
production='\n\n'.join(function(name) for name in ('traceRaw','freezeRawTrace','publishRaw','drainRaw','rawCaptureTick','prepareRawThreshold',
    'publishCaptureHistory','restartRawCandidate','waitRawHistoryBoundary','retainRawCandidate','stopRawCandidate','labCopyCaptureHistory','labClearCaptureHistory',
    'labRawTraceEnable','labRawTraceInfo','labRawTraceCopy','labRawTraceRelease'))
production='namespace research {\n'+production+'\n}\n'
template=(root/'tests/raw_worker_mock/worker_test.cpp').read_text(encoding='utf-8')
with tempfile.TemporaryDirectory(prefix='esp-raw-worker-') as directory:
    build=Path(directory);generated=build/'worker.cpp';generated.write_text(template.replace('// PRODUCTION_FUNCTIONS',production),encoding='utf-8')
    binary=build/('worker.exe' if __import__('os').name=='nt' else 'worker')
    command=[compiler]
    if Path(compiler).name.lower() in ('zig','zig.exe'):command.append('c++')
    command+=['-std=c++11','-Wall','-Wextra','-Werror','-O1','-g','-I',str(root/'src/research'),str(generated),str(root/'src/research/radio_config.cpp'),'-o',str(binary)]
    if args.sanitize:command+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    subprocess.run(command,check=True);subprocess.run([str(binary)],check=True)
