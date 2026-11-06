#!/usr/bin/env python3
"""Independent decoder written from the format reading (LSB-first bits,
32-bit version field, then tokens: 0=span(12b offset,4b len-2) / 1=literal(8b))."""
import sys

NUM_OFFSET_BITS = 12
NUM_LENGTH_BITS = 4
BREAK_EVEN      = (1 + NUM_OFFSET_BITS + NUM_LENGTH_BITS) // 9   # == 1
WINDOW          = 1 << NUM_OFFSET_BITS

class Bits:
    def __init__(self, data): self.d, self.i, self.mask = data, 0, 0
    def bit(self):
        if self.mask == 0:
            if self.i >= len(self.d): return None
            self.cur = self.d[self.i]; self.i += 1; self.mask = 1
        v = 1 if (getattr(self, 'cur', 0) & self.mask) else 0
        self.mask = (self.mask << 1) & 0xFF
        return v
    def bits_msb(self, n):
        v = 0
        for _ in range(n):
            b = self.bit()
            if b is None: return None
            v = (v << 1) | b
        return v

def decode(data):
    b = Bits(data)
    ver = b.bits_msb(32)
    if ver != 0: raise SystemExit('bad version %r' % ver)
    wnd = bytearray(WINDOW); pos = 1; out = bytearray()
    while True:
        t = b.bit()
        if t is None: break
        if t == 1:
            v = b.bits_msb(8)
            if v is None: break
            wnd[pos] = v; pos = (pos + 1) % WINDOW
            out.append(v)
        else:
            off = b.bits_msb(NUM_OFFSET_BITS)
            ln  = b.bits_msb(NUM_LENGTH_BITS)
            if off is None or ln is None: break
            if off == 0: break                      # end of stream
            ln += BREAK_EVEN + 1
            src = off
            for _ in range(ln):
                v = wnd[src]
                wnd[pos] = v; src = (src + 1) % WINDOW; pos = (pos + 1) % WINDOW
                out.append(v)
    return bytes(out)

if __name__ == '__main__':
    print(decode(open(sys.argv[1], 'rb').read()).__len__(), 'bytes', file=sys.stderr)
    sys.stdout.buffer.write(decode(open(sys.argv[1], 'rb').read()))
