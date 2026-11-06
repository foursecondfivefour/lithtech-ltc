#!/usr/bin/env bash
# Все гейты по порядку: M0 → M4. Каждый следующий включает в себя предыдущие,
# поэтому этот скрипт — полная приёмка репозитория «с нуля».
set -uo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
rc=0
for gate in m0 m1 m2 m3 m4; do
    printf '\n########## %s ##########\n' "$gate"
    if "$ROOT/scripts/${gate}_gate.sh"; then
        printf '########## %s: PASS\n' "$gate"
    else
        printf '########## %s: FAIL\n' "$gate"
        rc=1
    fi
done
printf '\n=== ПРИЁМКА: %s\n' "$([[ $rc -eq 0 ]] && echo "все гейты PASS" || echo "есть падения")"
exit $rc
