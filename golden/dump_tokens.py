#!/usr/bin/env python3
"""Дампер токенов .ltc и проверка инвариантов формата.

Независимая реализация разбора (как docs/decode.py), но цель другая: не восстановить
байты, а увидеть структуру потока — сколько литералов, сколько span'ов, где EOS.

    dump_tokens.py FILE [FILE...]          печать сводки по каждому файлу
    dump_tokens.py --assert FILE...        только проверки, код 1 при нарушении
    dump_tokens.py --tokens FILE           полный список токенов

Чтение за концом файла трактуется как нулевые биты (Q10) — именно так декодер
эталона и порт понимают обрезанный хвост. Из-за потери неполного байта (Q2) EOS
часто записан не целиком: его старшие биты в файл не попали и «дорисовываются»
нулями при чтении. Поэтому в сводке печатается `pad` — сколько бит пришлось
дорисовать.

Инварианты (--assert):
  * версия == 0;
  * последний токен — EOS (span с offset 0 и длиной 0), других offset 0 нет;
  * суммарная длина литералов и span'ов == длине восстановленных данных;
  * дорисованных бит меньше 17 (иначе токен EOS вообще отсутствовал бы).
"""
import sys

OFF_BITS = 12
LEN_BITS = 4
BREAK_EVEN = (1 + OFF_BITS + LEN_BITS) // 9      # 1
WINDOW = 1 << OFF_BITS                           # 4096


class Bits:
    """Чтение бит с дорисовкой нулей за концом данных (Q10)."""

    def __init__(self, data):
        self.data = data
        self.pos = 0
        self.mask = 0
        self.pad = 0          # сколько бит дочитано за концом файла

    def bit(self):
        if self.mask == 0:
            if self.pos >= len(self.data):
                self.pad += 1
                return 0
            self.curr = self.data[self.pos]
            self.pos += 1
            self.mask = 1
        value = 1 if (self.curr & self.mask) else 0
        self.mask = (self.mask << 1) & 0xFF
        return value

    def field(self, n):
        value = 0
        for _ in range(n):
            value = (value << 1) | self.bit()
        return value


def parse(data):
    """-> dict(version, tokens=[(kind, ...)], literals, spans, length, eos_index, trailing_bits)"""
    bits = Bits(data)
    version = bits.field(32)
    tokens = []
    literals = spans = 0
    length = 0
    eos_index = None
    while True:
        flag = bits.bit()
        if flag == 1:
            value = bits.field(8)
            tokens.append(("literal", value))
            literals += 1
            length += 1
        else:
            offset = bits.field(OFF_BITS)
            raw_len = bits.field(LEN_BITS)
            if offset == 0:
                tokens.append(("eos", raw_len))
                eos_index = len(tokens) - 1
                break
            span_len = raw_len + BREAK_EVEN + 1
            tokens.append(("span", offset, span_len))
            spans += 1
            length += span_len
    return {
        "version": version,
        "tokens": tokens,
        "literals": literals,
        "spans": spans,
        "length": length,
        "eos_index": eos_index,
        "bytes": len(data),
        "pad": bits.pad,
    }


def check(path):
    data = open(path, "rb").read()
    info = parse(data)
    problems = []
    if info["version"] != 0:
        problems.append("версия %#x" % info["version"])
    if info["eos_index"] is None:
        problems.append("нет EOS")
    elif info["eos_index"] != len(info["tokens"]) - 1:
        problems.append("EOS не последний токен (%d из %d)" % (info["eos_index"], len(info["tokens"])))
    else:
        eos = info["tokens"][-1]
        if eos[1] != 0:
            problems.append("EOS с ненулевой длиной: %r" % (eos,))
    if info["pad"] >= 17:
        problems.append("EOS не найден: дорисовано %d бит" % info["pad"])
    # сумма длин токенов должна совпадать с восстановленной длиной
    total = sum(1 for t in info["tokens"] if t[0] == "literal") + sum(
        t[2] for t in info["tokens"] if t[0] == "span")
    if total != info["length"]:
        problems.append("арифметика длин: %d != %d" % (total, info["length"]))
    return info, problems


def main(argv):
    mode = "summary"
    files = []
    for arg in argv[1:]:
        if arg in ("--assert", "--tokens", "--summary"):
            mode = arg[2:]
        else:
            files.append(arg)

    bad = 0
    for path in files:
        info, problems = check(path)
        if mode == "tokens":
            print("%s: version=%d bytes=%d" % (path, info["version"], info["bytes"]))
            for i, tok in enumerate(info["tokens"]):
                print("  %4d %s" % (i, tok))
        elif mode == "summary":
            print("%-44s bytes=%6d version=%d lit=%5d span=%5d out=%7d pad=%d %s"
                  % (path, info["bytes"], info["version"], info["literals"], info["spans"],
                     info["length"], info["pad"],
                     "OK" if not problems else "PROBLEMS: " + "; ".join(problems)))
        if problems:
            bad += 1
            if mode == "assert":
                print("FAIL %s: %s" % (path, "; ".join(problems)))
    if mode == "assert":
        print("assert: %d файлов, нарушений: %d" % (len(files), bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
