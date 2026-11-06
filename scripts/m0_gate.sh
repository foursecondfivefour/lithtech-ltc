#!/usr/bin/env bash
# Гейт M0: эталонная сборка, корпус, золотые .ltc, детерминизм.
#
#   scripts/m0_gate.sh                 полный прогон (включая пересборку эталона)
#   scripts/m0_gate.sh --no-build      без пересборки (быстро, для итераций)
#
# Критерий приёмки M0 (docs/roadmap.md):
#   * ref/ltc собирается из оригинальных исходников «с нуля»;
#   * 50+ fixture'ов, корпус воспроизводим;
#   * 50+ золотых .ltc, все воспроизводятся эталоном байт в байт;
#   * два независимых прогона совпадают (детерминизм);
#   * -verbose не меняет результат (§6.1 квирк-листа);
#   * двойное сжатие обратимо за два шага.
set -uo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
REF="$ROOT/ref-build/build/ltc"
GOLDEN="$ROOT/golden"
BUILD=1
[[ "${1:-}" == "--no-build" ]] && BUILD=0

fails=0
step() { printf '\n=== %s\n' "$*"; }
ok()   { printf '  [ OK ] %s\n' "$*"; }
bad()  { printf '  [FAIL] %s\n' "$*"; fails=$((fails + 1)); }
t0=$(date +%s)

if (( BUILD )); then
    step "1/6 сборка эталона с нуля (ref-build/build.sh)"
    rm -rf "$ROOT/ref-build/build"
    if "$ROOT/ref-build/build.sh" > /tmp/m0_build.log 2>&1; then
        ok "$(tail -1 /tmp/m0_build.log) — $(du -h "$REF" | cut -f1)"
    else
        bad "эталон не собрался — /tmp/m0_build.log"; tail -20 /tmp/m0_build.log; exit 1
    fi
else
    step "1/6 сборка эталона — пропущена (--no-build)"
fi

step "2/6 эталон отвечает"
if "$REF" -help | head -1 | grep -q 'compression and decompression of LTA'; then
    ok "$("$REF" -help | sed -n 2p)"
else
    bad "-help не даёт ожидаемой строки"; exit 1
fi
"$REF" >/dev/null 2>&1; [[ $? -eq 1 ]] && ok "нет аргументов -> код 1" || bad "нет аргументов: код не 1"

step "3/6 корпус воспроизводим (detерминированные seed'ы)"
if python3 "$GOLDEN/make_corpus.py" --check | tail -1 | grep -q ', 0 problems'; then
    n=$(ls "$GOLDEN/fixtures"/*.bin | wc -l)
    ok "$n fixture'ов, $(du -sh "$GOLDEN/fixtures" | cut -f1), --check чистый"
    (( n >= 50 )) && ok "порог 50+ fixture'ов выполнен" || bad "fixture'ов меньше 50"
else
    bad "корпус разошёлся с генератором"; python3 "$GOLDEN/make_corpus.py" --check | head -20
fi

step "4/6 золотые .ltc: пересборка эталоном + сверка + независимый декодер"
if python3 "$GOLDEN/generate.py" --check --spec > /tmp/m0_golden.log 2>&1; then
    ok "$(grep '^check: OK' /tmp/m0_golden.log | sed 's/^check: //')"
    ok "$(grep '^spec-check' /tmp/m0_golden.log | sed 's/^spec-check: //')"
else
    bad "золотые не совпадают с эталоном:"; tail -25 /tmp/m0_golden.log
fi

step "5/6 детерминизм: независимый второй прогон + флаг -verbose"
tmp=$(mktemp -d)
det_bad=0
for f in "$GOLDEN"/fixtures/*.bin; do
    b=$(basename "$f" .bin)
    "$REF" "$f" -out "$tmp/$b.a" >/dev/null 2>&1 || det_bad=$((det_bad + 1))
    "$REF" "$f" -out "$tmp/$b.b" >/dev/null 2>&1 || det_bad=$((det_bad + 1))
    "$REF" "$f" -verbose -out "$tmp/$b.v" >/dev/null 2>&1 || det_bad=$((det_bad + 1))
    cmp -s "$tmp/$b.a" "$tmp/$b.b" || { bad "не детерминировано: $b"; det_bad=$((det_bad + 1)); }
    cmp -s "$tmp/$b.a" "$tmp/$b.v" || { bad "-verbose изменил вывод: $b"; det_bad=$((det_bad + 1)); }
    cmp -s "$tmp/$b.a" "$GOLDEN/expected/$b.ltc" || { bad "второй прогон != golden: $b"; det_bad=$((det_bad + 1)); }
done
(( det_bad == 0 )) && ok "56 файлов × 3 прогона: идентичны" || bad "расхождений: $det_bad"

step "6/6 двойное сжатие (.ltc как данные, а не как .ltc)"
# важно: тип ВХОДА определяется только расширением (§6.3), поэтому золотой .ltc
# переименовываем в .bin — иначе LTC прочитает его как сжатый и распакует.
cp "$GOLDEN/expected/text_lta_like.ltc" "$tmp/nested.bin"
if "$REF" "$tmp/nested.bin" -out "$tmp/double.ltc" >/dev/null 2>&1 \
   && "$REF" "$tmp/double.ltc" -out "$tmp/back.bin" >/dev/null 2>&1 \
   && cmp -s "$tmp/back.bin" "$GOLDEN/expected/text_lta_like.ltc"; then
    ok "ltc(ltc(x)) обратимо: $(stat -c%s "$GOLDEN/expected/text_lta_like.ltc") -> $(stat -c%s "$tmp/double.ltc") байт"
else
    bad "двойное сжатие не обратимо"
fi
rm -rf "$tmp"

dt=$(( $(date +%s) - t0 ))
printf '\n=== ИТОГ M0: %s (%d с)\n' "$([[ $fails -eq 0 ]] && echo PASS || echo "FAIL ($fails)")" "$dt"
exit $(( fails > 0 ))
