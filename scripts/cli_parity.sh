#!/usr/bin/env bash
# Дифференциальная проверка CLI: эталон (ref-build/build/ltc) против порта (port/build/ltc).
#
# Каждый сценарий прогоняется дважды — в двух чистых каталогах с одинаковыми
# fixture'ами, — после чего сравниваются:
#   * код возврата,
#   * stdout байт в байт (включая бинарный вывод -view),
#   * stderr байт в байт,
#   * список созданных файлов (имена).
#
# Режимы сравнения:
#   full      — всё перечисленное;
#   bytes     — плюс полное совпадение содержимого всех файлов;
#   codes     — только код возврата, stdout и stderr (для случаев, где содержимое
#               выхода эталона зависит от неинициализированной памяти; подробности —
#               docs/porting-notes.md, Q6/Q10, и scripts/truncation_sweep.py).
set -uo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
REF="$ROOT/ref-build/build/ltc"
PORT="$ROOT/port/build/ltc"
WORK=$(mktemp -d)
trap 'chmod -R u+w "$WORK" 2>/dev/null; rm -rf "$WORK"' EXIT

[[ -x "$REF" ]]  || { echo "нет эталона: $REF (ref-build/build.sh)"; exit 1; }
[[ -x "$PORT" ]] || { echo "нет порта: $PORT (cmake --build port/build)"; exit 1; }

pass=0; fail=0; skip=0
declare -a failures=()

#---------------------------------------------------------------------------
# fixture'ы: одинаковый набор в обоих каталогах
#---------------------------------------------------------------------------
make_fixtures() {
    local d=$1
    python3 - "$d" <<'PY'
import os, sys, random
d = sys.argv[1]

def w(path, data):
    p = os.path.join(d, path)
    os.makedirs(os.path.dirname(p) or d, exist_ok=True)
    with open(p, 'wb') as f:
        f.write(data)

rng = random.Random(42)
w('raw.bin', bytes(range(256)) * 2 + rng.randbytes(1024))      # есть и NUL, и 0xFF
w('x.lta', (b'(object Root (name "x"))\n' * 40))
w('x.LTA', (b'(object Root (name "X"))\n' * 40))
w('empty.bin', b'')
w('one.bin', b'\x41')
w('two.bin', b'AB')
w('empty.lta', b'')
w('one.lta', b'\x41')
w('two.lta', b'AB')
w('abc.lta', b'abc')
w('noext', b'n' * 100)
w('.hidden', b'h' * 10)
w('x.lta.bak', b'b' * 10)
w('dir.d/file', b'f' * 10)
w('my', b'm' * 10)                    # имя файла "my file.lta" распадается на "my"
os.makedirs(os.path.join(d, 'emptydir.x'), exist_ok=True)
os.makedirs(os.path.join(d, 'sub'), exist_ok=True)
os.makedirs(os.path.join(d, 'sub2'), exist_ok=True)
# каталог только для чтения: производное имя выхода проявится в сообщении об ошибке
os.makedirs(os.path.join(d, 'ro'), exist_ok=True)
w('ro/x.LTA', b'(object R (name "ro"))\n' * 10)
PY
    # .ltc-входы: валидный поток делает эталон (порт его пока читать не умеет)
    "$REF" "$d/x.lta" -out "$d/valid.ltc" >/dev/null 2>&1
    : > "$d/zero.ltc"
    printf '\x01\x00\x00\x00' > "$d/badver.ltc"
    printf '\x00\x00\x00\x00junkjunkjunk' > "$d/junk.ltc"
    chmod 555 "$d/ro"
}

#---------------------------------------------------------------------------
# один сценарий
#---------------------------------------------------------------------------
# run_case <режим> <имя> <рабочий подкаталог> <argv...>
run_case() {
    local mode=$1 name=$2 subdir=$3
    shift 3

    local a="$WORK/$name/ref" b="$WORK/$name/port"
    mkdir -p "$a" "$b"
    make_fixtures "$a"
    make_fixtures "$b"

    ( cd "$a/$subdir" && "$REF"  "$@" > "$WORK/$name/ref.out"  2> "$WORK/$name/ref.err" ); local rc_ref=$?
    ( cd "$b/$subdir" && "$PORT" "$@" > "$WORK/$name/port.out" 2> "$WORK/$name/port.err" ); local rc_port=$?

    local problems=()

    {
        [[ $rc_ref -eq $rc_port ]] || problems+=("код возврата: эталон $rc_ref, порт $rc_port")
        cmp -s "$WORK/$name/ref.out" "$WORK/$name/port.out" || problems+=("stdout расходится")
        cmp -s "$WORK/$name/ref.err" "$WORK/$name/port.err" || problems+=("stderr расходится")

        chmod -R u+w "$a" "$b" 2>/dev/null
        if [[ $mode != codes ]]; then
            ( cd "$a" && find . -type f | sort > "$WORK/$name/list.ref" )
            ( cd "$b" && find . -type f | sort > "$WORK/$name/list.port" )
            cmp -s "$WORK/$name/list.ref" "$WORK/$name/list.port" || {
                problems+=("набор файлов расходится: $(diff "$WORK/$name/list.ref" "$WORK/$name/list.port" | head -4 | tr '\n' ' ')")
            }
        fi

        if [[ $mode == bytes ]]; then
            diff -r -q "$a" "$b" > "$WORK/$name/diff.txt" 2>&1 || {
                problems+=("содержимое файлов расходится: $(head -2 "$WORK/$name/diff.txt" | tr '\n' ' ')")
            }
        fi
    }

    if [[ ${#problems[@]} -eq 0 ]]; then
        if [[ $mode == codes ]]; then
            printf '  [ OK ] %-24s код %s (содержимое не сравнивается: зона Q6/Q10)\n' "$name" "$rc_ref"
        else
            printf '  [ OK ] %-24s код %s\n' "$name" "$rc_ref"
        fi
        (( pass++ ))
    else
        printf '  [FAIL] %-24s %s\n' "$name" "${problems[0]}"
        local i
        for ((i = 1; i < ${#problems[@]}; ++i)); do printf '         %s\n' "${problems[$i]}"; done
        [[ -s "$WORK/$name/ref.out" ]] && printf '         эталон stdout: %s\n' "$(head -c 120 "$WORK/$name/ref.out" | tr '\n' '|')"
        [[ -s "$WORK/$name/port.out" ]] && printf '         порт   stdout: %s\n' "$(head -c 120 "$WORK/$name/port.out" | tr '\n' '|')"
        failures+=("$name")
        (( fail++ ))
    fi
}

echo "=== CLI parity: эталон vs порт"

# --- справка и разбор аргументов -------------------------------------------
run_case full   help                . -help
run_case full   noargs              .
run_case full   unknown             . -bogus
run_case full   unknown_with_file   . x.lta -bogus
run_case full   double_dash         . x.lta --out y.txt
run_case full   lone_dash           . -
run_case full   help_wins           . -help -bogus
run_case full   help_after_file     . x.lta -help
run_case full   out_alone           . -out
run_case full   out_no_value        . x.lta -out
run_case full   view_and_out        . x.lta -view -out y.txt
run_case full   many_globals        . x.lta y.lta
run_case full   out_dup             . x.lta -out a.txt -out b.txt
run_case full   uppercase_flag      . -OUT sub2/y.ltc x.lta
run_case full   upper_view          . -VIEW raw.bin

# --- ошибки входа ----------------------------------------------------------
run_case full   missing_file        . nofile.lta
run_case full   no_extension        . noext -view
run_case full   dot_in_dir          . dir.d/file -view
run_case full   hidden_file         . .hidden -view
run_case full   bad_ext_bak         . x.lta.bak -view
run_case full   name_with_space     . "my file.lta" -view

# --- просмотр (сырой вход) -------------------------------------------------
run_case full   view_raw            . raw.bin -view
run_case full   view_empty          . empty.bin -view
run_case full   view_extra_globals  . raw.bin x.lta -view

# --- ошибки расширения (без -view; -view до проверки расширения не доходит) --
run_case full   ext_noext           . noext
run_case full   ext_dot_in_dir      . dir.d/file
run_case full   ext_hidden          . .hidden
run_case full   ext_bak             . x.lta.bak
run_case full   ext_bin             . raw.bin

# --- сжатие: имена и (где эталон тоже пишет литералы) байты ----------------
run_case bytes  compress_empty      . empty.lta
run_case bytes  compress_one        . one.lta
run_case bytes  compress_two        . two.lta
run_case full   compress_abc        . abc.lta
run_case full   compress_raw        . x.lta -out raw_out.ltc
run_case full   compress_lta        . x.lta
run_case full   compress_to_lta_ext . x.lta -out out.lta
run_case full   compress_upper_lta  . x.LTA
run_case full   compress_verbose    . x.lta -verbose
run_case full   compress_out_sub    . x.lta -out sub/y.ltc
run_case bytes  compress_dir_input  . emptydir.x -out out1.bin
run_case full   compress_out_baddir . x.lta -out nodir/y.ltc

# --- производное имя в сообщении об ошибке (каталог только для чтения) -----
run_case full   derived_name_upper  ro x.LTA

# --- ошибки формата .ltc ---------------------------------------------------
run_case full   corrupt_version     . badver.ltc
run_case full   corrupt_version_out . badver.ltc -out o.lta

# --- декодирование (.ltc на входе) -----------------------------------------
run_case bytes  decompress           . valid.ltc
run_case bytes  decompress_out       . valid.ltc -out o.lta
run_case bytes  view_compressed      . valid.ltc -view
run_case bytes  empty_ltc            . zero.ltc
# мусор после валидного заголовка: и эталон, и порт выдают байты, но эталон берёт их
# из неинициализированного окна (Q6), а поток рвётся посреди токена (Q10) — побайтовое
# совпадение здесь невозможно в принципе, поэтому сравниваются только коды и потоки
run_case codes  junk_ltc             . junk.ltc
run_case bytes  decompress_derived   . valid.ltc
run_case full   decompress_badver    . badver.ltc -out o2.lta

echo
printf '=== CLI parity: %d OK, %d FAIL, %d SKIP\n' "$pass" "$fail" "$skip"
if (( fail > 0 )); then
    printf 'провалились: %s\n' "${failures[*]}"
    exit 1
fi
exit 0
