"""Compile the actual name-only page scan and full-path resolver without SD hardware."""
import argparse
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

parser=argparse.ArgumentParser()
parser.add_argument('--compiler')
args=parser.parse_args()
root=Path(__file__).resolve().parents[2]
compiler=args.compiler or shutil.which('g++') or shutil.which('clang++') or shutil.which('zig')
if not compiler:parser.error('Use --compiler with a native C++ compiler')
source=(root/'src/research/lab_suite.cpp').read_text(encoding='utf-8')

def function(name):
    match=re.search(r'^(?:void|bool) '+name+r'\([^\n]*\) \{',source,re.M)
    if not match:raise RuntimeError('Production function absent: '+name)
    opening=source.index('{',match.start());depth=0
    for token in re.finditer(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|//[^\n]*|/\*.*?\*/|[{}]',source[opening:],re.S):
        if token.group()=='{':depth+=1
        elif token.group()=='}':
            depth-=1
            if not depth:return source[match.start():opening+token.end()]
    raise RuntimeError('Unclosed production function: '+name)

controller='\n\n'.join(function(name) for name in ('safePath','listFiles','resolveFileEntry'))
template=(Path(__file__).parent/'browser_test.cpp').read_text(encoding='utf-8')
with tempfile.TemporaryDirectory(prefix='esp-file-browser-') as directory:
    build=Path(directory)
    generated=build/'browser.cpp';generated.write_text(template.replace('// PRODUCTION_FUNCTIONS',controller),encoding='utf-8')
    binary=build/('browser.exe' if __import__('os').name=='nt' else 'browser')
    command=[compiler]
    if Path(compiler).name.lower() in ('zig','zig.exe'):command.append('c++')
    command+=['-std=c++11','-Wall','-Wextra','-Werror','-O1','-I',str(root/'src/research'),str(generated),'-o',str(binary)]
    subprocess.run(command,check=True);subprocess.run([str(binary)],check=True)
