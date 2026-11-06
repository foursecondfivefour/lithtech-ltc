#!/usr/bin/env python3
"""Фазз-сверка компрессора: эталон против порта, байт в байт.

Каждый случай детерминирован (seed), поэтому прогон воспроизводим: при расхождении
вход и оба результата складываются в каталог --keep для разбора.

    fuzz_parity.py [--seeds N] [--ref PATH] [--port PATH] [--keep DIR]
"""
import argparse
import os
import random
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def make_case(seed):
    """-> (режим, данные). Режимы подобраны так, чтобы задевать все ветки компрессора."""
    rng = random.Random(seed)
    mode = rng.choice(['rand', 'alpha', 'period', 'runs', 'mixed', 'zeros', 'incr',
                       'text', 'edge', 'ghost'])

    if mode == 'rand':                                  # несжимаемое
        n = rng.choice([1, 2, 3, 17, 18, 50, 1000, 4096, 4097, 8192, 40000])
        data = rng.randbytes(n)
    elif mode == 'alpha':                               # низкая энтропия
        alphabet = rng.choice([2, 3, 5, 16, 64])
        n = rng.choice([100, 5000, 9000, 20000])
        data = bytes(rng.randrange(alphabet) for _ in range(n))
    elif mode == 'period':                              # периодические, включая 4095/4096
        period = rng.choice([1, 2, 3, 4, 7, 8, 16, 17, 18, 19, 255, 256, 257, 1000, 4095, 4096])
        n = rng.choice([3000, 12000, 30000])
        pattern = bytes((i * 7 + seed) & 0xFF for i in range(period))
        data = (pattern * (n // period + 2))[:n]
    elif mode == 'runs':                                # серии разной длины
        limit = rng.choice([1000, 9000, 30000])
        out = bytearray()
        while len(out) < limit:
            out += bytes([rng.randrange(256)]) * rng.choice([1, 2, 3, 16, 17, 18, 64, 1000])
        data = bytes(out)
    elif mode == 'mixed':                               # склейка разнородных кусков
        parts = [rng.randbytes(rng.choice([10, 1000, 5000])),
                 b'\x00' * rng.choice([17, 4096, 5000]),
                 b'abcabc' * rng.choice([3, 1000]),
                 rng.randbytes(rng.choice([50, 300, 20000]))]
        rng.shuffle(parts)
        data = b''.join(parts)
    elif mode == 'zeros':
        data = b'\x00' * rng.choice([1, 16, 17, 18, 4096, 4097, 12000])
    elif mode == 'incr':                                # монотонные последовательности
        n = rng.choice([17, 4096, 4097, 9000])
        start = rng.randrange(256)
        data = bytes((start + i) & 0xFF for i in range(n))
    elif mode == 'text':
        alphabet = b'abcdefghijklmnopqrstuvwxyz .,\n'
        data = bytes(rng.choice(alphabet) for _ in range(rng.choice([1000, 18000, 40000])))
    elif mode == 'edge':                                # точные границы окна
        base = rng.randbytes(4096)
        extra = rng.choice([0, 1, 2, 15, 16, 17, 18])
        data = (base * 3)[:4096 + extra] + base[:extra]
    else:                                               # ghost-буфер на краю окна
        base = bytes((i * 13 + seed) & 0xFF for i in range(4096))
        data = base * rng.choice([1, 2, 3])
    return mode, data


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--seeds', type=int, default=300)
    ap.add_argument('--ref', default=os.path.join(ROOT, 'ref-build', 'build', 'ltc'))
    ap.add_argument('--port', default=os.path.join(ROOT, 'port', 'build', 'ltc'))
    ap.add_argument('--keep', default=None, help='каталог для расхождений')
    args = ap.parse_args()

    for path in (args.ref, args.port):
        if not (os.path.isfile(path) and os.access(path, os.X_OK)):
            print('нет бинарника: %s' % path, file=sys.stderr)
            return 2

    keep = args.keep or tempfile.mkdtemp(prefix='ltc_fuzz_keep_')
    os.makedirs(keep, exist_ok=True)

    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, 'in.lta')
        ref_out = os.path.join(tmp, 'ref.ltc')
        port_out = os.path.join(tmp, 'port.ltc')

        bad = 0
        for seed in range(args.seeds):
            mode, data = make_case(seed)
            with open(src, 'wb') as fh:
                fh.write(data)

            subprocess.run([args.ref, src, '-out', ref_out], check=True, capture_output=True)
            subprocess.run([args.port, src, '-out', port_out], check=True, capture_output=True)

            with open(ref_out, 'rb') as fh:
                ref_bytes = fh.read()
            with open(port_out, 'rb') as fh:
                port_bytes = fh.read()

            if ref_bytes != port_bytes:
                bad += 1
                stem = os.path.join(keep, 'seed%04d_%s' % (seed, mode))
                for suffix, blob in (('.in.bin', data), ('.ref.ltc', ref_bytes),
                                     ('.port.ltc', port_bytes)):
                    with open(stem + suffix, 'wb') as fh:
                        fh.write(blob)
                print('MISMATCH seed=%d mode=%s in=%d ref=%d port=%d'
                      % (seed, mode, len(data), len(ref_bytes), len(port_bytes)))

    print('fuzz: %d случаев, расхождений: %d' % (args.seeds, bad))
    if bad:
        print('расхождения сложены в %s' % keep)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
