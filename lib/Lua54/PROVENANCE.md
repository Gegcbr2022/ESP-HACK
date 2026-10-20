# Lua 5.4.9

Official source archive: https://www.lua.org/ftp/lua-5.4.9.tar.gz

SHA256 verified against the official https://www.lua.org/ftp/ index:
`2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6`.

Vendored files are official headers, Lua VM/compiler core, auxiliary
library and base/math/string/table libraries. The command-line interpreter,
compiler executable, io/os/package/debug/coroutine/utf8 library implementations
and automatic all-library initializer are omitted. The embedded build lowers
`LUAI_MAXCCALLS` to40 to bound native C recursion on the ESP32 task stack.
One portability patch in `luaconf.h` supplies hidden C99 long-long limit macros
from GCC's `__LONG_LONG_MAX__` when Arduino/newlib omits them in C++ mode. Lua
integers remain64-bit and Lua numbers remain double in both C and C++ units.
Runtime restrictions remove additional dangerous or long-running functions;
see `docs/APPS_SDK.md`. No JIT, dynamic native loader or native FAP loader exists.

The original Lua copyright and MIT license are retained in the source and LICENSE.
