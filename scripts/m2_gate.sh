#!/usr/bin/env bash
# Гейт M2: компрессор 1:1 (порт CLZSSWindow) — байтовый паритет с эталоном.
#
#   scripts/m2_gate.sh                 полный прогон (пересборка + 300 фазз-случаев)
#   scripts/m2_gate.sh --no-build      без пересборки
#   scripts/m2_gate.sh --fuzz 1500     больше фазз-случаев
#
# Критерии приёмки M2 (docs/roadmap.md):
#   * все 56 золотых .ltc воспроизводятся портом байт в байт;
#   * фазз-сверка против эталона на N детерминированных случаях — 0 расхождений;
#   * файлы по 1 МБ (монотонные, случайные, текстовые, периодические) совпадают;
#   * инварианты формата (dump_tokens.py --assert) держатся на корпусе;
#   * предыдущие гейты (M0, M1) остаются зелёными.
set -uo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
REF="$ROOT/ref-build/build/ltc"
PORT="$ROOT/port/build/ltc"
BUILD=1
FUZZ=300
while [[ $# -gt 0 ]]; do
    case "$1" in
        --no-build) BUILD=0; shift ;;
        --fuzz)     FUZZ=$2; shift 2 ;;
        *)          echo "неизвестный аргумент: $1"; exit 2 ;;
    esac
done

fails=0
step() { printf '\n=== %s\n' "$*"; }
ok()   { printf '  [ OK ] %s\n' "$*"; }
bad()  { printf '  [FAIL] %s\n' "$*"; fails=$((fails + 1)); }
space() { printf '  место: %s\n' "$(df -h "$ROOT" | tail -1 | awk '{print $4" свободно в "$6}')"; }
t0=$(date +%s)

if (( BUILD )); then
    step "1/8 сборка: эталон с нуля + порт"
    rm -rf "$ROOT/ref-build/build"
    if "$ROOT/ref-build/build.sh" > /tmp/m2_ref.log 2>&1; then
        ok "эталон: $(tail -1 /tmp/m2_ref.log)"
    else
        bad "эталон не собрался"; tail -20 /tmp/m2_ref.log; exit 1
    fi
    rm -rf "$ROOT/port/build"
else
    step "1/8 сборка — пропущена (--no-build)"
fi
if [[ ! -x "$PORT" ]] || (( BUILD )); then
    if cmake -S "$ROOT/port" -B "$ROOT/port/build" -G Ninja -DCMAKE_BUILD_TYPE=Release > /tmp/m2_cm.log 2>&1 \
       && cmake --build "$ROOT/port/build" >> /tmp/m2_cm.log 2>&1; then
        warns=$(grep -c 'warning:' /tmp/m2_cm.log || true)
        ok "порт собран, предупреждений: $warns"
        (( warns == 0 )) || bad "предупреждения при сборке порта"
    else
        bad "порт не собрался — /tmp/m2_cm.log"; tail -25 /tmp/m2_cm.log; exit 1
    fi
fi

step "2/8 юнит-тесты порта (M1 + M2)"
if "$ROOT/port/build/ltc_tests_m1" > /tmp/m2_u1.log 2>&1; then
    ok "M1: $(cat /tmp/m2_u1.log)"
else
    bad "юнит-тесты M1"; cat /tmp/m2_u1.log
fi
if "$ROOT/port/build/ltc_tests_m2" > /tmp/m2_u2.log 2>&1; then
    ok "M2: $(cat /tmp/m2_u2.log)"
else
    bad "юнит-тесты M2"; cat /tmp/m2_u2.log
fi

step "3/8 регрессия: гейт M0 (эталон, корпус, золотые)"
if "$ROOT/scripts/m0_gate.sh" --no-build > /tmp/m2_m0.log 2>&1; then
    ok "$(grep '^=== ИТОГ' /tmp/m2_m0.log | sed 's/^=== //')"
else
    bad "M0 сломан"; tail -15 /tmp/m2_m0.log
fi

step "4/8 регрессия: гейт M1 (CLI 1:1)"
if "$ROOT/scripts/m1_gate.sh" --no-build > /tmp/m2_m1.log 2>&1; then
    ok "$(grep -oE 'CLI parity: [0-9]+ OK, [0-9]+ FAIL, [0-9]+ SKIP' /tmp/m2_m1.log | head -1)"
    ok "$(grep 'ref(port(x))' /tmp/m2_m1.log | sed 's/\[ OK \] //')"
else
    bad "M1 сломан"; grep -E 'FAIL|провалились' /tmp/m2_m1.log | head -10
fi

step "5/8 байтовый паритет на корпусе (56 файлов)"
tmp=$(mktemp -d)
same=0; total=0; first=""
for f in "$ROOT"/golden/fixtures/*.bin; do
    b=$(basename "$f" .bin)
    cp "$f" "$tmp/$b.lta"
    "$REF"  "$tmp/$b.lta" -out "$tmp/$b.ref.ltc"  >/dev/null 2>&1
    "$PORT" "$tmp/$b.lta" -out "$tmp/$b.port.ltc" >/dev/null 2>&1
    total=$((total + 1))
    if cmp -s "$tmp/$b.ref.ltc" "$tmp/$b.port.ltc"; then
        same=$((same + 1))
    else
        [[ -z $first ]] && first=$b
    fi
done
if (( same == total )); then
    ok "$same/$total совпадают байт в байт"
else
    bad "$((total - same)) из $total расходятся (первый: $first)"
fi

step "6/8 инварианты формата на золотых файлах (независимый разборщик)"
if python3 "$ROOT/golden/dump_tokens.py" --assert "$ROOT"/golden/expected/*.ltc > /tmp/m2_tok.log 2>&1; then
    ok "$(tail -1 /tmp/m2_tok.log)"
    printf '  %s\n' "$(python3 "$ROOT/golden/dump_tokens.py" "$ROOT/golden/expected/text_lta_like.ltc" | sed 's|.*/||')"
else
    bad "инварианты нарушены"; grep FAIL /tmp/m2_tok.log | head -5
fi

step "7/8 фазз-сверка с эталоном ($FUZZ случаев) + файлы по 1 МБ"
if python3 "$ROOT/scripts/fuzz_parity.py" --seeds "$FUZZ" --ref "$REF" --port "$PORT" > /tmp/m2_fuzz.log 2>&1; then
    ok "$(tail -1 /tmp/m2_fuzz.log)"
else
    bad "фазз нашёл расхождения"; grep -E 'MISMATCH|fuzz:' /tmp/m2_fuzz.log | head -8
fi

big_bad=0
for spec in "same:4194304:A" "rand:1048576:" "text:1048576:text" "period17:524288:p17"; do
    name=${spec%%:*}; rest=${spec#*:}; size=${rest%%:*}; kind=${rest#*:}
    python3 - "$tmp/big.lta" "$size" "$kind" <<'PY'
import sys, random
path, size, kind = sys.argv[1], int(sys.argv[2]), sys.argv[3]
rng = random.Random(7)
if kind == 'A':      data = b'\x41' * size
elif kind == 'text': data = (b'the quick brown fox jumps over the lazy dog. ' * (size // 45 + 1))[:size]
elif kind == 'p17':  data = (bytes(range(17)) * (size // 17 + 1))[:size]
else:                data = rng.randbytes(size)
open(path, 'wb').write(data)
PY
    "$REF"  "$tmp/big.lta" -out "$tmp/big.ref.ltc"  >/dev/null 2>&1
    "$PORT" "$tmp/big.lta" -out "$tmp/big.port.ltc" >/dev/null 2>&1
    if cmp -s "$tmp/big.ref.ltc" "$tmp/big.port.ltc"; then
        printf '  %-10s %8s байт -> %8s, совпадает\n' "$name" "$(stat -c%s "$tmp/big.lta")" "$(stat -c%s "$tmp/big.ref.ltc")"
    else
        bad "большой файл $name расходится"; big_bad=1
    fi
done
(( big_bad == 0 )) && ok "4 больших файла совпадают байт в байт"
rm -rf "$tmp"

# санитайзеры: отдельная сборка порта, корпус + 100 фазз-случаев под ASan/UBSan
if (( BUILD )); then
    step "8/8 ASan/UBSan на порте (корпус + 100 фазз-случаев)"
    asan_dir="$ROOT/port/build-asan"
    rm -rf "$asan_dir"
    if cmake -S "$ROOT/port" -B "$asan_dir" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
             -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g" \
             > /tmp/m2_asan_cm.log 2>&1 && cmake --build "$asan_dir" >> /tmp/m2_asan_cm.log 2>&1; then
        san_bad=0
        adir=$(mktemp -d)
        for f in "$ROOT"/golden/fixtures/*.bin; do
            b=$(basename "$f" .bin)
            cp "$f" "$adir/$b.lta"
            "$asan_dir/ltc" "$adir/$b.lta" -out "$adir/$b.ltc" > "$adir/$b.log" 2>&1 || san_bad=$((san_bad + 1))
            grep -qE 'runtime error|AddressSanitizer|LeakSanitizer' "$adir/$b.log" && san_bad=$((san_bad + 1))
        done
        python3 "$ROOT/scripts/fuzz_parity.py" --seeds 100 --port "$asan_dir/ltc" >> /tmp/m2_asan.log 2>&1
        fuzz_rc=$?
        (( fuzz_rc == 0 )) || san_bad=$((san_bad + 1))
        rm -rf "$adir"
        if (( san_bad == 0 )); then
            ok "56 файлов корпуса + 100 фазз-случаев без единого отчёта санитайзеров"
        else
            bad "санитайзеры: $san_bad проблем"
        fi
    else
        bad "санитайзерная сборка не собралась"
    fi
fi

dt=$(( $(date +%s) - t0 ))
printf '\n=== ИТОГ M2: %s (%d с)\n' "$([[ $fails -eq 0 ]] && echo PASS || echo "FAIL ($fails)")" "$dt"
exit $(( fails > 0 ))
