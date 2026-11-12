#!/bin/bash
# Usage: bash build.sh [install]   (install: copies the DLL into the game folder; the game must be closed)
set -e
cd "$(dirname "$0")"
# per-user temp folders: /tmp files from an earlier session may belong to another user
B=/tmp/bbr2_$(id -u); mkdir -p $B
TC=/tmp/bbr2tc; [ -x $TC/ziglang/zig ] || TC=$B/tc
if [ ! -x $TC/ziglang/zig ]; then
  mkdir -p $TC; for w in toolchain/wheels/*.whl; do python3 -m zipfile -e "$w" $TC >/dev/null; done; chmod +x $TC/ziglang/zig
fi
export ZIG_GLOBAL_CACHE_DIR=$B/zigcache ZIG_LOCAL_CACHE_DIR=$B/zigcache
Z=$TC/ziglang/zig; O=$B/obj; mkdir -p $O build
T=third_party; TGT="-target x86_64-windows-gnu"
INC="-Isrc -I$T/enet/include"
# third-party code: compiled only when missing
for f in callbacks compress host list packet peer protocol win32; do
  [ -f $O/enet_$f.o ] || $Z cc $TGT -O2 -c -I$T/enet/include $T/enet/$f.c -o $O/enet_$f.o
done
OBJS=""
for f in main util proxy rtti repl netsim shadow fastc race hook steamsvc steamlink net online ui; do
  $Z cc $TGT -O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers $INC -c src/$f.c -o $O/$f.o; OBJS="$OBJS $O/$f.o"
done
$Z cc $TGT -shared -s -o $O/version.dll $OBJS $O/enet_*.o src/version.def \
  -lws2_32 -lwinmm -liphlpapi -lgdi32 -luser32 -lkernel32
cp $O/version.dll build/version.dll
# Spacewar (AppID 480) helper: a separate process (Steam treats one process as one AppID)
$Z cc $TGT -O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Isrc src/helper.c src/steamsvc.c -o $O/bbr2steam.exe -lws2_32 -lkernel32
cp $O/bbr2steam.exe build/bbr2steam.exe
ls -la build/version.dll
if [ "$1" = install ]; then
  cp build/version.dll ../version.dll && mkdir -p steam && cp build/bbr2steam.exe steam/bbr2steam.exe && printf '480\n' > steam/steam_appid.txt && echo "installed into the game folder"
fi
