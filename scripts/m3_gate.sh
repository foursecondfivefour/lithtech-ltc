#!/usr/bin/env bash
# Гейт M3: декодер 1:1 (-view, распаковка, round-trip, поведение на обрезках).
#
#   scripts/m3_gate.sh                 полный прогон (пересборка + санитайзеры)
#   scripts/m3_gate.sh --no-build      без пересборки
#
# Критерии приёмки M3 (docs/roadmap.md):
#   * порт декодирует все 56 золотых .ltc в исходные байты;
#   * -view совпадает с содержимым эталонного файла для каждого золотого .ltc;
#   * round-trip «сжал портом — распаковал портом» на всём корпусе;
#   * CLI-гейт против эталона не имеет ни одного SKIP (все сценарии декодирования);
#   * на обрезках потока расхождения с эталоном возможны только в зоне, где он
#     читает неинициализированную память (scripts/truncation_sweep.py);
#   * ASan/UBSan на путях декодирования чистые.
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
    step "1/7 сборка: эталон с нуля + порт"
    rm -rf "$ROOT/ref-build/build"
    if "$ROOT/ref-build/build.sh" > /tmp/m3_ref.log 2>&1; then
        ok "эталон: $(tail -1 /tmp/m3_ref.log)"
    else
        bad "эталон не собрался"; tail -20 /tmp/m3_ref.log; exit 1
    fi
    rm -rf "$ROOT/port/build"
else
    step "1/7 сборка — пропущена (--no-build)"
fi

if [[ ! -x "$PORT" ]] || (( BUILD )); then
    if cmake -S "$ROOT/port" -B "$ROOT/port/build" -G Ninja -DCMAKE_BUILD_TYPE=Release > /tmp/m3_cm.log 2>&1 \
       && cmake --build "$ROOT/port/build" >> /tmp/m3_cm.log 2>&1; then
        warns=$(grep -c 'warning:' /tmp/m3_cm.log || true)
        ok "порт собран, предупреждений: $warns"
        (( warns == 0 )) || bad "предупреждения при сборке порта"
    else
        bad "порт не собрался — /tmp/m3_cm.log"; tail -25 /tmp/m3_cm.log; exit 1
    fi
fi

step "2/7 юнит-тесты порта (M1 + M2 + M3)"
for suite in m1 m2 m3; do
    if "$ROOT/port/build/ltc_tests_$suite" > "/tmp/m3_u$suite.log" 2>&1; then
        ok "$suite: $(cat /tmp/m3_u$suite.log)"
    else
        bad "юнит-тесты $suite"; cat "/tmp/m3_u$suite.log"
    fi
done

step "3/7 регрессия: гейты M0/M1/M2"
if "$ROOT/scripts/m2_gate.sh" --no-build > /tmp/m3_m2.log 2>&1; then
    ok "M2 (вместе с M0 и M1): $(grep '^=== ИТОГ' /tmp/m3_m2.log | sed 's/^=== //')"
    ok "$(grep -oE 'CLI parity: [0-9]+ OK, [0-9]+ FAIL, [0-9]+ SKIP' /tmp/m3_m2.log | head -1)"
    skips=$(grep -oE 'CLI parity: [0-9]+ OK, [0-9]+ FAIL, [0-9]+ SKIP' /tmp/m3_m2.log | head -1 | grep -oE '[0-9]+ SKIP' | grep -oE '[0-9]+')
    if [[ "$skips" == "0" ]]; then
        ok "в CLI-гейте нет ни одного SKIP — все декодирующие сценарии сравниваются"
    else
        bad "в CLI-гейте осталось $skips SKIP"
    fi
else
    bad "регрессия M2"; grep -E 'FAIL' /tmp/m3_m2.log | head -8
fi

step "4/7 декодирование золотых .ltc портом"
tmp=$(mktemp -d)
ok_count=0; total=0; first=""
for f in "$ROOT"/golden/expected/*.ltc; do
    b=$(basename "$f" .ltc)
    total=$((total + 1))
    if "$PORT" "$f" -out "$tmp/$b.out" >/dev/null 2>&1 && cmp -s "$tmp/$b.out" "$ROOT/golden/fixtures/$b.bin"; then
        ok_count=$((ok_count + 1))
    else
        [[ -z $first ]] && first=$b
    fi
done
if (( ok_count == total )); then
    ok "$ok_count/$total: порт восстановил исходные файлы из эталонных .ltc"
else
    bad "$((total - ok_count)) из $total не совпали (первый: $first)"
fi

step "5/7 -view на сжатых файлах"
view_ok=0
for f in "$ROOT"/golden/expected/*.ltc; do
    b=$(basename "$f" .ltc)
    "$PORT" "$f" -view > "$tmp/$b.view" 2>/dev/null
    cmp -s "$tmp/$b.view" "$ROOT/golden/fixtures/$b.bin" && view_ok=$((view_ok + 1))
done
(( view_ok == total )) && ok "$view_ok/$total: -view выдал ровно содержимое исходника" \
                       || bad "$((total - view_ok)) файлов: -view расходится"

step "6/7 round-trip силами одного порта (сжал -> распаковал)"
rt_ok=0
for f in "$ROOT"/golden/fixtures/*.bin; do
    b=$(basename "$f" .bin)
    cp "$f" "$tmp/$b.lta"
    if "$PORT" "$tmp/$b.lta" -out "$tmp/$b.p.ltc" >/dev/null 2>&1 \
       && "$PORT" "$tmp/$b.p.ltc" -out "$tmp/$b.p.back" >/dev/null 2>&1 \
       && cmp -s "$tmp/$b.p.back" "$f"; then
        rt_ok=$((rt_ok + 1))
    fi
done
(( rt_ok == total )) && ok "$rt_ok/$total: round-trip порт->порт байт в байт" \
                     || bad "$((total - rt_ok)) из $total не прошли round-trip"

step "7/7 обрезки потока: расхождения только в зоне неопределённости эталона"
python3 - "$tmp" <<'PY'
import random, sys, os
d = sys.argv[1]
r = random.Random(5)
open(os.path.join(d, 'sw_small.lta'), 'wb').write(b'abcabcABCD' * 40)
open(os.path.join(d, 'sw_text.lta'), 'wb').write((b'the quick brown fox jumps over the lazy dog. ' * 20)[:900])
blob = bytearray()
for _ in range(60):
    blob += bytes([r.randrange(256)]) * r.choice([1, 2, 5, 17, 40])
open(os.path.join(d, 'sw_mixed.lta'), 'wb').write(bytes(blob))
PY
sweep_bad=0
for f in "$tmp/sw_small.lta" "$tmp/sw_text.lta" "$tmp/sw_mixed.lta"; do
    if python3 "$ROOT/scripts/truncation_sweep.py" "$f" > /tmp/m3_sweep.log 2>&1; then
        printf '  %s\n' "$(grep 'расхождений' /tmp/m3_sweep.log)"
    else
        sweep_bad=1
        echo "  [FAIL] $(basename "$f"): расхождения вне зоны неопределённости"
        grep -E 'ВНИМАНИЕ|cut=' /tmp/m3_sweep.log | head -5
    fi
done
(( sweep_bad == 0 )) || bad "свип по обрезкам нашёл расхождения вне зоны Q6/Q10"

if (( BUILD )); then
    step "8/8 ASan/UBSan на путях декодирования"
    asan_dir=/tmp/ltc_asan_build
    rm -rf "$asan_dir"
    if cmake -S "$ROOT/port" -B "$asan_dir" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
             -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g" \
             > /tmp/m3_asan_cm.log 2>&1 && cmake --build "$asan_dir" >> /tmp/m3_asan_cm.log 2>&1; then
        san_bad=0
        for f in "$ROOT"/golden/expected/*.ltc; do
            b=$(basename "$f" .ltc)
            "$asan_dir/ltc" "$f" -out "$tmp/$b.asan.out" > "$tmp/$b.asan.log" 2>&1 || san_bad=$((san_bad + 1))
            grep -qE 'runtime error|AddressSanitizer|LeakSanitizer' "$tmp/$b.asan.log" && san_bad=$((san_bad + 1))
            "$asan_dir/ltc" "$f" -view > /dev/null 2> "$tmp/$b.asanv.log"
            grep -qE 'runtime error|AddressSanitizer|LeakSanitizer' "$tmp/$b.asanv.log" && san_bad=$((san_bad + 1))
        done
        # код возврата тестов — единственный критерий (в выводе есть слово "failures")
        "$asan_dir/ltc_tests_m3" > /tmp/m3_asan_tests.log 2>&1 || san_bad=$((san_bad + 1))
        (( san_bad == 0 )) && ok "56 распаковок + 56 -view + юнит-тесты M3 без отчётов санитайзеров" \
                           || bad "санитайзеры: $san_bad проблем"
    else
        bad "санитайзерная сборка не собралась"
    fi
fi

rm -rf "$tmp"
dt=$(( $(date +%s) - t0 ))
printf '\n=== ИТОГ M3: %s (%d с)\n' "$([[ $fails -eq 0 ]] && echo PASS || echo "FAIL ($fails)")" "$dt"
exit $(( fails > 0 ))
