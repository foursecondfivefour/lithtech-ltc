#!/usr/bin/env python3
"""Генератор корпуса M0 для LTC-порта.

Все входные файлы создаются детерминированно (фиксированные seed'ы), поэтому
`fixtures/` можно пересоздать на любой машине и получить те же байты.
LTC не парсит LTA — на вход ему годится любой поток байт, — но «реалистичные»
тексты в корпусе всё же есть: они дают ту же статистику, что настоящие .lta.

Использование:
    make_corpus.py [--out DIR] [--check]
        --check   не писать файлы, а сравнить с тем, что лежит в DIR
"""
import argparse
import hashlib
import os
import random
import sys

CASES = []          # (имя, callable -> bytes) — порядок фиксирован


def case(fn):
    CASES.append((fn.__name__, fn))
    return fn


# ---------------------------------------------------------------- базовые
@case
def empty():
    return b""


@case
def one_zero():
    break_even_point_probe = b"\x00"
    return break_even_point_probe


@case
def one_ff():
    return b"\xff"


@case
def one_A():
    return b"A"


@case
def two_ab():
    return b"AB"


@case
def three_aaa():
    return b"AAA"


def _cycle(period, total, offset=0):
    pat = bytes((i * 37 + offset) & 0xFF for i in range(period))
    return (pat * (total // period + 2))[:total]


# длины вокруг границ look-ahead (LOOK_AHEAD_SIZE = 17, BREAK_EVEN_POINT = 1)
for _n in (15, 16, 17, 18, 19, 20, 33):
    def _mk(n=_n):
        def f():
            return (b"abc" * 100)[:n]
        return f
    f = _mk()
    f.__name__ = "len%02d" % _n
    case(f)


# ---------------------------------------------------------------- окно 4096
for _n in (4095, 4096, 4097, 4098):
    def _mk(n=_n):
        def f():
            return b"A" * n
        return f
    f = _mk()
    f.__name__ = "run%04d" % _n
    case(f)


for _n in (4094, 4096, 8192):
    def _mk(n=_n):
        def f():
            return _cycle(56, n)
        return f
    f = _mk()
    f.__name__ = "cyc56_%04d" % _n
    case(f)


# ---------------------------------------------------------------- периоды
for _p in (1, 2, 3, 4, 5, 7, 8, 16, 17, 18, 31, 255, 256, 257, 1000, 4095):
    def _mk(p=_p):
        def f():
            return _cycle(p, 12000, offset=p)
        return f
    f = _mk()
    f.__name__ = "period%04d" % _p
    case(f)


# ---------------------------------------------------------------- ГСЧ
for _seed, _n in ((1, 100), (2, 1000), (3, 5000), (4, 20000), (5, 50000)):
    def _mk(seed=_seed, n=_n):
        def f():
            return random.Random(seed).randbytes(n)
        return f
    f = _mk()
    f.__name__ = "rand%05d" % _n
    case(f)


# ---------------------------------------------------------------- низкая энтропия
for _alpha in (2, 4, 8, 16):
    def _mk(a=_alpha):
        def f():
            rng = random.Random(1000 + a)
            return bytes(rng.randrange(a) for _ in range(12000))
        return f
    f = _mk()
    f.__name__ = "alpha%02d" % _alpha
    case(f)


# ---------------------------------------------------------------- текст
def _lta_text():
    """Псевдо-LTA: те же конструкции, что в настоящих .lta из прошлого корпуса."""
    rng = random.Random(7)
    out = ["(object Object000 (name \"Cube000\")"]
    for i in range(400):
        out.append("  (object Object%03d (name \"Mesh%03d\")" % (i, i))
        out.append("    (rotation (%0.4f %0.4f %0.4f %0.4f))" % tuple(rng.random() for _ in range(4)))
        out.append("    (pos (%d %d %d))" % (i * 16, 0, i * 16))
        out.append("    (visible 1)")
        out.append("    (surface \"Materials\\\\Stone0%d.dtx\")" % (i % 3))
        out.append("  )")
    out.append(")")
    return ("\r\n".join(out) + "\r\n").encode("ascii")


@case
def text_lta_like():
    return _lta_text()


@case
def text_source():
    body = (
        "uint32 CLZSSWindow::AddNode(uint32 nNode)\n"
        "{\n"
        "    uint32 nCurrNode = m_nRoot;\n"
        "    while (nCurrNode != NULL_NODE) {\n"
        "        if (nCurrNode == nNode) { ReplaceNode(nNode); return nCurrNode; }\n"
        "        nCurrNode = m_Tree[nCurrNode];\n"
        "    }\n"
        "    return NULL_NODE;\n"
        "}\n\n"
    )
    rng = random.Random(11)
    chunks, i = [], 0
    while sum(map(len, chunks)) < 39000:
        if rng.random() < 0.08:
            chunks.append("    // variant %d: LOOK_AHEAD_SIZE = %d\n" % (i, rng.randrange(2, 19)))
        chunks.append(body)
        i += 1
    return "".join(chunks).encode("ascii")


@case
def text_english():
    para = (
        "The quick brown fox jumps over the lazy dog. Pack my box with five dozen "
        "liquor jugs. How vexingly quick daft zebras jump! Sphinx of black quartz, "
        "judge my vow. The five boxing wizards jump quickly.\n"
    )
    return (para * 250).encode("ascii")


@case
def text_utf8():
    line = "Литий-текст: сжатие LTA-файлов, окно 4096, look-ahead 17. Ёжик в тумане.\n"
    return (line * 800).encode("utf-8")


# ---------------------------------------------------------------- структура
@case
def zeros_64k():
    return b"\x00" * 65536


@case
def ff_8k():
    return b"\xff" * 8192


@case
def alt_5500():
    return (b"\x55\xaa" * 4500)[:9000]


@case
def count_256x64():
    return bytes(range(256)) * 64


@case
def ramp_desc():
    return bytes((255 - (i & 0xFF)) for i in range(20000))


@case
def sparse_noise():
    """Нули с редкими вкраплениями — типичная картина для .ltb/.lta с паддингами."""
    rng = random.Random(21)
    b = bytearray(100000)
    for _ in range(300):
        p = rng.randrange(0, len(b) - 40)
        b[p:p + 4] = rng.randbytes(4)
    return bytes(b)


@case
def mixed_256k():
    rng = random.Random(31)
    parts = [
        b"LITH" + b"\x00" * 28,
        _lta_text()[:20000],
        rng.randbytes(60000),
        b"\x00" * 40000,
        _cycle(17, 30000),
        rng.randbytes(50000),
        b"\xff" * 10000,
        (b"repeat-me " * 3000),
    ]
    return b"".join(parts)[:262144]


# ---------------------------------------------------------------- запуск
def render():
    """-> dict: имя -> bytes (в порядке CASES)."""
    out = {}
    for name, fn in CASES:
        data = fn()
        assert data is not None, name
        out[name] = data
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "fixtures"))
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()

    data = render()
    os.makedirs(args.out, exist_ok=True)

    bad = 0
    seen = set()
    for name, blob in data.items():
        path = os.path.join(args.out, name + ".bin")
        seen.add(name + ".bin")
        if args.check:
            if not os.path.exists(path):
                print("MISSING %s" % path)
                bad += 1
                continue
            if open(path, "rb").read() != blob:
                print("CHANGED %s" % path)
                bad += 1
        else:
            with open(path, "wb") as fh:
                fh.write(blob)

    if args.check:
        extra = sorted(set(os.listdir(args.out)) - seen)
        for e in extra:
            print("UNEXPECTED %s" % e)
            bad += 1
        print("corpus: %d fixtures, %d problems" % (len(data), bad))
        return 1 if bad else 0

    total = sum(len(v) for v in data.values())
    print("corpus: %d fixtures, %d bytes total -> %s" % (len(data), total, args.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
