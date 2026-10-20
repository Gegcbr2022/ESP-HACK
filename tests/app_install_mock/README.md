# Installer fault tests

This compiles the production `app_install.cpp` against deterministic SD, UART,
clock and mbedTLS API doubles. It checks that stored-file readback, length and
digest failures cannot promote an upload or overwrite the working app, and that
rename failures preserve or identify the recoverable backup. The digest double
tests the installer's control flow; it does not validate SHA-256 mathematics or
the physical SD card. Firmware uses mbedTLS SHA-256.

From the repository root, with a native C++11 compiler:

```sh
python tests/app_install_mock/run.py --cxx g++
```

For Zig, use `--cxx /path/to/zig --zig`. There are 22 integrity and recovery
scenarios, including the full 32 KiB source limit.
