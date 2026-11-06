#!/usr/bin/env bash
# Гейт M4: инварианты, второй компилятор, документация.
#
#   scripts/m4_gate.sh                 полный прогон
#   scripts/m4_gate.sh --no-build      без пересборки (быстрая проверка)
#
# Критерии приёмки M4 (docs/roadmap.md):
#   * порт собирается без единого предупреждения под gcc и clang с расширенным
#     набором флагов (-Wconversion, -Wsign-conversion, -Wshadow, -Wcast-qual,
#     -Wold-style-cast, …) и -Werror;
#   * все юнит-тесты (M1–M4) зелёные под обоими компиляторами;
#   * инварианты формата держатся на выходе порта, а не только на золотых файлах;
#   * глубокий фазз (1500 случаев) против эталона — без расхождений;
#   * документация согласована с деревом (scripts/check_docs.py).
set -uo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD=1
[[ "${1:-}" == "--no-build" ]] && BUILD=0

fails=0
step() { printf '\n=== %s\n' "$*"; }
ok()   { printf '  [ OK ] %s\n' "$*"; }
bad()  { printf '  [FAIL] %s\n' "$*"; fails=$((fails + 1)); }
space() { printf '  место: %s\n' "$(df -h "$ROOT" | tail -1 | awk '{print $4" свободно в "$6}')"; }
t0=$(date +%s)

if (( BUILD )); then
    step "1/6 сборка двумя компиляторами с -Werror"
    for cxx in g++-14 clang++; do
        dir="$ROOT/port/build-$(echo "$cxx" | tr -d '+-')"
        rm -rf "$dir"
        if cmake -S "$ROOT/port" -B "$dir" -G Ninja -DCMAKE_CXX_COMPILER="$cxx" \
                 -DCMAKE_BUILD_TYPE=Release > "/tmp/m4_$cxx.log" 2>&1 \
           && cmake --build "$dir" >> "/tmp/m4_$cxx.log" 2>&1; then
            warns=$(grep -c 'warning:' "/tmp/m4_$cxx.log" || true)
            ok "$cxx: собрано, предупреждений: $warns"
            (( warns == 0 )) || bad "$cxx: предупреждения"
        else
            bad "$cxx: сборка не удалась"; grep -E 'error|warning' "/tmp/m4_$cxx.log" | head -8
        fi
    done
    # рабочая сборка (используется остальными гейтами)
    cmake -S "$ROOT/port" -B "$ROOT/port/build" -G Ninja -DCMAKE_BUILD_TYPE=Release > /dev/null 2>&1
    cmake --build "$ROOT/port/build" >> /tmp/m4_build.log 2>&1
else
    step "1/6 сборка — пропущена (--no-build)"
fi

step "2/6 юнит-тесты под gcc и clang"
for cxx in g++-14 clang++; do
    dir="$ROOT/port/build-$(echo "$cxx" | tr -d '+-')"
    [[ -d "$dir" ]] || continue
    total_checks=0
    suite_fail=0
    for suite in m1 m2 m3 m4; do
        out=$("$dir/ltc_tests_$suite" 2>&1)
        rc=$?
        n=$(printf '%s' "$out" | grep -oE 'checks: [0-9]+' | grep -oE '[0-9]+')
        total_checks=$((total_checks + ${n:-0}))
        (( rc == 0 )) || suite_fail=1
    done
    if (( suite_fail == 0 )); then
        ok "$cxx: 4 набора, $total_checks проверок, 0 падений"
    else
        bad "$cxx: юнит-тесты провалились"
    fi
done

step "3/6 регрессия: гейты M0–M3"
if "$ROOT/scripts/m3_gate.sh" --no-build > /tmp/m4_m3.log 2>&1; then
    ok "M3 (вместе с M0, M1, M2): $(grep '^=== ИТОГ' /tmp/m4_m3.log | sed 's/^=== //')"
    ok "$(grep -oE 'CLI parity: [0-9]+ OK, [0-9]+ FAIL, [0-9]+ SKIP' /tmp/m4_m3.log | head -1)"
else
    bad "регрессия M3"; grep -E 'FAIL' /tmp/m4_m3.log | head -8
fi

step "4/6 глубокий фазз против эталона (1500 случаев)"
if python3 "$ROOT/scripts/fuzz_parity.py" --seeds 1500 > /tmp/m4_fuzz.log 2>&1; then
    ok "$(tail -1 /tmp/m4_fuzz.log)"
else
    bad "фазз нашёл расхождения"; grep MISMATCH /tmp/m4_fuzz.log | head -5
fi

step "5/6 инварианты формата на выходе порта (не только на золотых файлах)"
tmp=$(mktemp -d)
python3 - "$tmp" <<'PY'
import random, sys, os
d = sys.argv[1]
rng = random.Random(31)
for i in range(20):
    n = rng.choice([0, 1, 2, 17, 18, 100, 4096, 4097, 9000])
    mode = i % 4
    if mode == 0:   data = rng.randbytes(n)
    elif mode == 1: data = bytes(rng.randrange(4) for _ in range(n))
    elif mode == 2: data = b'Q' * n
    else:           data = bytes(i & 0xFF for i in range(n))
    open(os.path.join(d, 'g%02d.lta' % i), 'wb').write(data)
PY
inv_bad=0
for f in "$tmp"/g*.lta; do
    b=$(basename "$f" .lta)
    "$ROOT/port/build/ltc" "$f" -out "$tmp/$b.ltc" >/dev/null 2>&1 || inv_bad=$((inv_bad + 1))
done
if python3 "$ROOT/golden/dump_tokens.py" --assert "$tmp"/*.ltc > /tmp/m4_inv.log 2>&1; then
    ok "$(tail -1 /tmp/m4_inv.log)"
else
    bad "инварианты нарушены на выходе порта"; grep FAIL /tmp/m4_inv.log | head -5
fi
(( inv_bad == 0 )) || bad "порт не смог сжать $inv_bad сгенерированных файлов"
rm -rf "$tmp"

step "6/6 документация согласована с деревом"
if python3 "$ROOT/scripts/check_docs.py" > /tmp/m4_docs.log 2>&1; then
    ok "$(tail -1 /tmp/m4_docs.log)"
else
    bad "документация рассинхронизирована:"; grep '!' /tmp/m4_docs.log | head -8
fi

dt=$(( $(date +%s) - t0 ))
printf '\n=== ИТОГ M4: %s (%d с)\n' "$([[ $fails -eq 0 ]] && echo PASS || echo "FAIL ($fails)")" "$dt"
exit $(( fails > 0 ))
