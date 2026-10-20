"""Compile the production installer against SD/UART/digest fault doubles."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--cxx", default="c++")
parser.add_argument("--zig", action="store_true", help="Use zig c++ as compiler")
args = parser.parse_args()
here = Path(__file__).resolve().parent
project = here.parents[1]
with tempfile.TemporaryDirectory(prefix="esp_app_install_test_") as temporary:
    executable = Path(temporary) / ("app_install_test.exe" if os.name == "nt" else "app_install_test")
    command = [args.cxx] + (["c++"] if args.zig else []) + [
        "-std=c++11", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
        "-I" + str(here), "-I" + str(project / "src/research"),
        str(here / "test_app_install.cpp"), str(project / "src/research/app_install.cpp"),
        "-o", str(executable),
    ]
    subprocess.run(command, check=True)
    subprocess.run([str(executable)], check=True)
