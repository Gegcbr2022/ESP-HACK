"""Compile the real RAW engine against IRQ/GPIO mocks; requires a native C++ compiler."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--cxx', default='c++')
parser.add_argument('--zig', action='store_true', help='Use zig c++ as compiler')
args = parser.parse_args()
here = Path(__file__).resolve().parent
project = here.parents[1]
with tempfile.TemporaryDirectory(prefix='esp_raw_test_') as tmp:
    executable = Path(tmp) / 'raw_capture_test.exe'
    command = [args.cxx] + (['c++'] if args.zig else []) + [
        '-std=c++11', '-Wall', '-Wextra', '-Werror', '-O1', '-g',
        '-I' + str(here), '-I' + str(project / 'src'),
        str(here / 'raw_capture_test.cpp'), str(project / 'src/research/raw_capture.cpp'),
        '-o', str(executable),
    ]
    subprocess.run(command, check=True)
    subprocess.run([str(executable)], check=True)
