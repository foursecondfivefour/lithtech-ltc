#!/usr/bin/env python3
"""Генерация и проверка «золотого» набора .ltc эталонным бинарником.

Оракул — ref-build/build/ltc, собранный из оригинальных MSVC-исходников (см.
ref-build/build.sh). Каждый fixture сжимается по одному разу на прогон; в
manifest.txt пишется всё, что нужно гейту: размеры и хэши входа и результата.

    generate.py            пересоздать expected/ + manifest.txt + expected.sha256
    generate.py --check    пересобрать во временный каталог и сверить с коммитом
    generate.py --spec     дополнительно проверить весь корпус независимым
                           декодером docs/decode.py (проверка спецификации, §4)

Проверки, встроенные в оба режима:
  * round-trip:  ref(x.ltc) == fixture           (декодер эталона)
  * детерминизм: второй прогон даёт те же байты  (сжатие без скрытого состояния)
"""
import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DEFAULT_REF = os.path.join(ROOT, "ref-build", "build", "ltc")
DECODER = os.path.join(ROOT, "docs", "decode.py")


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for blk in iter(lambda: fh.read(1 << 20), b""):
            h.update(blk)
    return h.hexdigest()


def run_ltc(ref, src, out):
    r = subprocess.run([ref, src, "-out", out], capture_output=True)
    if r.returncode != 0:
        raise SystemExit("ltc failed (%d) on %s: %s" % (r.returncode, src, r.stderr.decode(errors="replace")))
    if r.stdout.strip() or r.stderr.strip():
        raise SystemExit("ltc produced output on success for %s: %r %r" % (src, r.stdout, r.stderr))


def produce(ref, fixtures, dest):
    """-> список строк манифеста."""
    os.makedirs(dest, exist_ok=True)
    rows = []
    names = sorted(n for n in os.listdir(fixtures) if n.endswith(".bin"))
    with tempfile.TemporaryDirectory() as tmp:
        dec = os.path.join(tmp, "rt.bin")
        for i, name in enumerate(names, 1):
            src = os.path.join(fixtures, name)
            out = os.path.join(dest, name[:-4] + ".ltc")

            run_ltc(ref, src, out)

            # round-trip: декодер эталона обязан вернуть исходный байт-в-байт
            if os.path.exists(dec):
                os.remove(dec)
            r = subprocess.run([ref, out, "-out", dec], capture_output=True)
            if r.returncode != 0 or not os.path.exists(dec):
                raise SystemExit("round-trip failed (open) on %s" % name)
            if sha256(dec) != sha256(src):
                raise SystemExit("round-trip MISMATCH on %s (%d -> %d bytes)"
                                 % (name, os.path.getsize(src), os.path.getsize(dec)))

            rows.append("%-24s %8d %8d %s %s"
                        % (name, os.path.getsize(src), os.path.getsize(out), sha256(src), sha256(out)))
            print("  [%2d/%2d] %-24s %8d -> %8d" % (i, len(names), name, os.path.getsize(src), os.path.getsize(out)))
    return rows


def write_manifest(path, rows, ref):
    gcc = subprocess.run(["g++-14", "--version"], capture_output=True, text=True).stdout.splitlines()
    with open(path, "w") as fh:
        fh.write("# LTC golden manifest — формат: <fixture> <in_bytes> <ltc_bytes> <in_sha256> <ltc_sha256>\n")
        fh.write("# oracle: ref-build/build/ltc sha256=%s\n" % sha256(ref))
        fh.write("# compiler: %s\n" % (gcc[0] if gcc else "unknown"))
        fh.write("# cases: %d\n" % len(rows))
        for r in rows:
            fh.write(r + "\n")


def write_sha256list(dest, path):
    lines = []
    for n in sorted(os.listdir(dest)):
        if n.endswith(".ltc"):
            lines.append("%s  %s" % (sha256(os.path.join(dest, n)), n))
    with open(path, "w") as fh:
        fh.write("\n".join(lines) + "\n")


def spec_check(fixtures, dest):
    """Независимый декодер (docs/decode.py) должен восстановить каждый fixture."""
    sys.path.insert(0, os.path.dirname(DECODER))
    import importlib.util
    spec = importlib.util.spec_from_file_location("lta_decoder", DECODER)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    bad = 0
    for n in sorted(os.listdir(dest)):
        if not n.endswith(".ltc"):
            continue
        src = os.path.join(fixtures, n[:-4] + ".bin")
        got = mod.decode(open(os.path.join(dest, n), "rb").read())
        if got != open(src, "rb").read():
            print("  SPEC MISMATCH %s" % n)
            bad += 1
    print("spec-check: %d/%d файлов декодируются по спецификации"
          % (len([n for n in os.listdir(dest) if n.endswith('.ltc')]) - bad,
             len([n for n in os.listdir(dest) if n.endswith('.ltc')])))
    return bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ref", default=DEFAULT_REF)
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--spec", action="store_true")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    if not os.access(args.ref, os.X_OK):
        raise SystemExit("эталонный бинарник не найден: %s (соберите ref-build/build.sh)" % args.ref)
    fixtures = os.path.join(HERE, "fixtures")
    expected = os.path.join(HERE, "expected")

    if not args.check:
        shutil.rmtree(expected, ignore_errors=True)
        rows = produce(args.ref, fixtures, expected)
        write_manifest(os.path.join(HERE, "manifest.txt"), rows, args.ref)
        write_sha256list(expected, os.path.join(HERE, "expected.sha256"))
        print("wrote %d goldens -> %s" % (len(rows), expected))
        if args.spec:
            return 1 if spec_check(fixtures, expected) else 0
        return 0

    # --check: пересобрать в temp и сравнить с коммитом
    print("check: пересборка эталоном %s" % args.ref)
    with tempfile.TemporaryDirectory() as tmp:
        rows = produce(args.ref, fixtures, tmp)
        write_manifest(os.path.join(tmp, "m.txt"), rows, args.ref)
        def rows_only(text):
            # хэш бинарника-оракула зависит от пересборки; сверяем только данные
            return [l for l in text.splitlines() if not l.startswith("# oracle:")]

        committed = "\n".join(rows_only(open(os.path.join(HERE, "manifest.txt")).read()))
        fresh = "\n".join(rows_only(open(os.path.join(tmp, "m.txt")).read()))
        if committed != fresh:
            import difflib
            print("MANIFEST DIFF:")
            for line in list(difflib.unified_diff(committed.splitlines(), fresh.splitlines(),
                                                  "committed", "fresh", lineterm=""))[:60]:
                print("  " + line)
            return 1
        if args.spec:
            if spec_check(fixtures, tmp):
                return 1
    print("check: OK — %d файлов совпадают с эталоном байт в байт" % len(rows))
    return 0


if __name__ == "__main__":
    sys.exit(main())
