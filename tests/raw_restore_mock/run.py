"""Execute the production RAW TX body/restoration adapter with hardware doubles."""
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

def block(pattern):
    match=re.search(pattern,source,re.M)
    if not match:raise RuntimeError('Production declaration absent: '+pattern)
    opening=source.index('{',match.start());depth=0
    for token in re.finditer(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*.*?\*/|[{}]',source[opening:],re.S):
        if token.group()=='{':depth+=1
        elif token.group()=='}':
            depth-=1
            if not depth:
                end=opening+token.end()
                if pattern.startswith('^struct'):end=source.index(';',end)+1
                return source[match.start():end]
    raise RuntimeError('Unclosed production declaration')

production='\n\n'.join(block(pattern) for pattern in (
    r'^struct RawRestoreBackend \{',r'^struct RawSoftwareRestore \{',r'^bool labTransmitRaw\('))
template=(root/'tests/raw_restore_mock/restore_test.cpp').read_text(encoding='utf-8')
with tempfile.TemporaryDirectory(prefix='esp-raw-restore-') as directory:
    build=Path(directory);generated=build/'restore.cpp'
    generated.write_text(template.replace('// PRODUCTION_FUNCTIONS',production),encoding='utf-8')
    binary=build/('restore.exe' if __import__('os').name=='nt' else 'restore')
    command=[compiler]
    if Path(compiler).name.lower() in ('zig','zig.exe'):command.append('c++')
    command+=['-std=c++11','-Wall','-Wextra','-Werror','-O1','-g','-I',str(root/'src/research'),str(generated),str(root/'src/research/radio_config.cpp'),'-o',str(binary)]
    if args.sanitize:command+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
    subprocess.run(command,check=True);subprocess.run([str(binary)],check=True)
