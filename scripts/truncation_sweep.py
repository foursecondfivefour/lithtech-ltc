#!/usr/bin/env python3
"""Свип по обрезкам: где поведение эталона детерминировано, а где нет.

Для каждого префикса сжатого файла сравниваются выходы эталона и порта, а сам
префикс разбирается на токены со строгим EOF. Гипотеза: расхождения возможны
ТОЛЬКО когда поток кончается посреди токена — там эталон читает
неинициализированную память (`GetBit` не трогает выходной параметр при конце файла),
и воспроизвести его нельзя в принципе. Если расхождение нашлось вне этой зоны,
это настоящий баг порта.

    truncation_sweep.py INPUT [--step N]
"""
import argparse
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REF = os.path.join(ROOT, 'ref-build', 'build', 'ltc')
PORT = os.path.join(ROOT, 'port', 'build', 'ltc')


def parse_tokens(data):
    """-> (список (start_bit, end_bit) для каждого токена).

    Разбор префикса со строгим EOF: незавершённый последний токен тоже попадает
    в список (его end_bit за концом данных) — именно он и есть зона
    неопределённости эталона.
    """
    total = len(data) * 8
    pos = 32 * 8      # пропускаем заголовок версии
    spans = []

    class Eof(Exception):
        pass

    def bit():
        nonlocal pos
        if pos >= total:
            raise Eof
        value = (data[pos >> 3] >> (pos & 7)) & 1
        pos += 1
        return value

    def field(n):
        value = 0
        for _ in range(n):
            value = (value << 1) | bit()
        return value

    while True:
        start = pos
        try:
            if bit() == 1:                      # флаг прочитан
                field(8)
            else:
                offset = field(12)
                if offset == 0:
                    spans.append((start, pos))
                    return spans                # EOS прочитан целиком
                field(4)
            spans.append((start, pos))
        except Eof:
            spans.append((start, total + 1))    # обрыв посреди токена
            return spans


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('input')
    ap.add_argument('--step', type=int, default=1)
    args = ap.parse_args()

    for path in (REF, PORT):
        if not os.access(path, os.X_OK):
            print('нет бинарника: %s' % path, file=sys.stderr)
            return 2

    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, 'in.lta')
        packed_path = os.path.join(tmp, 'in.ltc')
        subprocess.run([REF, args.input, '-out', packed_path], check=True, capture_output=True)
        packed = open(packed_path, 'rb').read()

        total = os.path.getsize(args.input)
        print('%s: %d байт -> %d байт .ltc, шаг %d' % (args.input, total, len(packed), args.step))

        probe = os.path.join(tmp, 'cut.ltc')
        ref_out = os.path.join(tmp, 'ref.bin')
        port_out = os.path.join(tmp, 'port.bin')

        checked = mid_token = mismatches = outside = 0
        examples = []

        for cut in range(1, len(packed), args.step):
            with open(probe, 'wb') as fh:
                fh.write(packed[:cut])
            subprocess.run([REF, probe, '-out', ref_out], capture_output=True)
            subprocess.run([PORT, probe, '-out', port_out], capture_output=True)

            ref_bytes = open(ref_out, 'rb').read()
            port_bytes = open(port_out, 'rb').read()
            prefix = packed[:cut]
            spans = parse_tokens(prefix)
            bits_available = cut * 8
            # детерминированно только когда обрезка попала ровно на границу токена:
            # тогда эталон упирается в конец файла при чтении флага и просто
            # останавливается. Внутри токена он читает неинициализированную память.
            at_boundary = any(end == bits_available for _, end in spans[:-1]) or \
                (spans and spans[-1][1] == bits_available)

            checked += 1
            if not at_boundary:
                mid_token += 1
            if ref_bytes != port_bytes:
                mismatches += 1
                if at_boundary:
                    outside += 1
                    if len(examples) < 10:
                        examples.append((cut, len(ref_bytes), len(port_bytes)))

        print('точек обрезки: %d, из них на границе токена: %d' % (checked, checked - mid_token))
        print('расхождений эталон/порт: %d; вне зоны неопределённости: %d' % (mismatches, outside))
        for cut, ref_len, port_len in examples:
            print('   ! cut=%d ref=%d port=%d' % (cut, ref_len, port_len))

        if outside:
            print('ВНИМАНИЕ: расхождения вне зоны неопределённости — это баг порта')
            return 1
        return 0


if __name__ == '__main__':
    sys.exit(main())
