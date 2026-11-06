#!/usr/bin/env bash
# Сборка эталонного LTC под Linux из оригинальных MSVC-исходников.
#
# Запускается на чистом дереве: недостающие MSVC-инклюды (backslash-пути и другой
# регистр) разрешаются на лету скриптом autocompat.py, который создаёт
# относительные симлинки в compat/. Результат: ref-build/build/ltc.
set -euo pipefail
cd "$(dirname "$0")"

CXX=${CXX:-g++-14}
FLAGS=(-std=gnu++17 -w -fpermissive -D__LINUX -DLINUX
       -I. -Isrc/LTC -Isrc/ltamgr -Isrc/sdk/inc -Icompat -Ishim
       -include shim/wincompat.h)

mkdir -p build
TUS=(src/ltamgr/*.cpp src/LTC/*.cpp shim/tdguard_impl.cpp)
for f in "${TUS[@]}"; do
    python3 autocompat.py "$CXX" "${FLAGS[@]}" -c "$f" -o "build/$(basename "${f%.cpp}").o"
done
"$CXX" build/*.o -o build/ltc

{
    echo "binary:    ref-build/build/ltc"
    echo "sha256:    $(sha256sum build/ltc | cut -d' ' -f1)"
    echo "compiler:  $("$CXX" --version | head -1)"
    echo "objects:   $(ls build/*.o | wc -l)"
    echo "patches:   PATCHES.md (2 механические правки под gcc)"
    echo "date:      $(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > BUILD-INFO.txt
echo "built: build/ltc ($(ls build/*.o | wc -l) objects)"
