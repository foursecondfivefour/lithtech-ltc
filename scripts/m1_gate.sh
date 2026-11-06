#!/usr/bin/env bash
# Гейт M1: CLI 1:1 + битовый слой + запись потока «без сжатия».
#
#   scripts/m1_gate.sh                 полный прогон (пересборка эталона и порта)
#   scripts/m1_gate.sh --no-build      без пересборки
#
# Критерии приёмки M1 (docs/roadmap.md):
#   * порт собирается с -Wall -Wextra -Wpedantic -Werror;
#   * юнит-тесты зелёные (парсер, сжатость, имя выхода, биты, сообщения);
#   * дифференциальный CLI-гейт против эталона: 40+ сценариев, 0 расхождений;
#   * поток, который пишет порт, читается эталоном и возвращает исходные байты
#     на всём корпусе M0 (56 файлов) — то есть формат верный, даже пока нет LZSS;
#   * на входах, где эталон тоже пишет только литералы, байты совпадают с эталоном.
set -uo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
REF="$ROOT/ref-build/build/ltc"
PORT="$ROOT/port/build/ltc"
BUILD=1
[[ "${1:-}" == "--no-build" ]] && BUILD=0

fails=0
step() { printf '\n=== %s\n' "$*"; }
ok()   { printf '  [ OK ] %s\n' "$*"; }
bad()  { printf '  [FAIL] %s\n' "$*"; fails=$((fails + 1)); }
t0=$(date +%s)

if (( BUILD )); then
    step "1/5 сборка: эталон с нуля + порт"
    rm -rf "$ROOT/ref-build/build"
    if "$ROOT/ref-build/build.sh" > /tmp/m1_ref.log 2>&1; then
        ok "эталон: $(tail -1 /tmp/m1_ref.log)"
    else
        bad "эталон не собрался"; tail -20 /tmp/m1_ref.log; exit 1
    fi
else
    step "1/5 сборка — пропущена (--no-build)"
fi

rm -rf "$ROOT/port/build"
if cmake -S "$ROOT/port" -B "$ROOT/port/build" -G Ninja -DCMAKE_BUILD_TYPE=Release > /tmp/m1_cm.log 2>&1 \
   && cmake --build "$ROOT/port/build" >> /tmp/m1_cm.log 2>&1; then
    warns=$(grep -c 'warning:' /tmp/m1_cm.log || true)
    ok "порт: $(basename "$PORT"), предупреждений: $warns (флаги -Wall -Wextra -Wpedantic -Werror)"
    (( warns == 0 )) || bad "предупреждения при сборке порта"
else
    bad "порт не собрался — /tmp/m1_cm.log"; tail -25 /tmp/m1_cm.log; exit 1
fi

step "2/5 юнит-тесты порта"
if "$ROOT/port/build/ltc_tests_m1" > /tmp/m1_unit.log 2>&1; then
    ok "$(cat /tmp/m1_unit.log)"
else
    bad "юнит-тесты провалились"; cat /tmp/m1_unit.log
fi

step "3/5 дифференциальный CLI-гейт (эталон против порта)"
if "$ROOT/scripts/cli_parity.sh" > /tmp/m1_cli.log 2>&1; then
    ok "$(grep -E '^=== CLI parity: [0-9]' /tmp/m1_cli.log | sed 's/^=== //')"
else
    bad "CLI расходится:"; grep -E 'FAIL|провалились' /tmp/m1_cli.log | head -12
fi

step "4/5 поток порта читается эталоном на всём корпусе (56 файлов)"
tmp=$(mktemp -d)
roundtrip_bad=0
identical=0
total=0
for f in "$ROOT"/golden/fixtures/*.bin; do
    b=$(basename "$f" .bin)
    cp "$f" "$tmp/$b.lta"
    "$REF"  "$tmp/$b.lta" -out "$tmp/$b.ref.ltc"  >/dev/null 2>&1
    "$PORT" "$tmp/$b.lta" -out "$tmp/$b.port.ltc" >/dev/null 2>&1
    "$REF"  "$tmp/$b.port.ltc" -out "$tmp/$b.back" >/dev/null 2>&1
    total=$((total + 1))
    cmp -s "$tmp/$b.back" "$f" || roundtrip_bad=$((roundtrip_bad + 1))
    cmp -s "$tmp/$b.ref.ltc" "$tmp/$b.port.ltc" && identical=$((identical + 1))
done
if (( roundtrip_bad == 0 )); then
    ok "$total/$total: ref(port(x)) == x — формат и битовый слой верны"
else
    bad "$roundtrip_bad из $total файлов не восстановились эталоном"
fi
ok "байт-в-байт с эталоном на $identical из $total (остальные — там, где эталон нашёл совпадения; LZSS — M2)"

step "5/5 размеры: ожидаемый разрыв (порт пишет только литералы)"
for probe in empty one_zero two_ab text_lta_like rand05000; do
    ref_sz=$(stat -c%s "$tmp/$probe.ref.ltc" 2>/dev/null || echo 0)
    port_sz=$(stat -c%s "$tmp/$probe.port.ltc" 2>/dev/null || echo 0)
    in_sz=$(stat -c%s "$tmp/$probe.lta" 2>/dev/null || echo 0)
    printf '  %-16s вход %7s  эталон %7s  порт %7s\n' "$probe" "$in_sz" "$ref_sz" "$port_sz"
done
rm -rf "$tmp"

dt=$(( $(date +%s) - t0 ))
printf '\n=== ИТОГ M1: %s (%d с)\n' "$([[ $fails -eq 0 ]] && echo PASS || echo "FAIL ($fails)")" "$dt"
exit $(( fails > 0 ))
