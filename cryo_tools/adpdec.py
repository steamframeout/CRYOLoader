#!/usr/bin/env python3
"""Decodes an audsrv .adp (APCM header + SPU2 ADPCM blocks) the way the SPU2 plays it:
stops at the first block whose flags have the end bit (1) without repeat (2)."""
import struct, sys
import numpy as np

F = [(0, 0), (60, 0), (115, -52), (98, -55), (122, -60)]


def decode(path):
    d = open(path, 'rb').read()
    magic, ver, ch, loop, _, pitch, samples = struct.unpack('<4sBBBBII', d[:16])
    assert magic == b'APCM', magic
    out = []
    s1 = s2 = 0
    end_flags = None
    for off in range(16, len(d), 16):
        b = d[off:off + 16]
        shift, filt, flags = b[0] & 15, b[0] >> 4, b[1]
        f0, f1 = F[filt]
        for byte in b[2:]:
            for nib in (byte & 15, byte >> 4):
                v = nib - 16 if nib >= 8 else nib
                v = (v << 12) >> shift
                v += (s1 * f0 + s2 * f1 + 32) >> 6
                v = max(-32768, min(32767, v))
                out.append(v)
                s2, s1 = s1, v
        if flags & 1:
            end_flags = flags
            break
    return dict(pitch=pitch, rate=pitch * 48000 / 4096, samples=samples, loop=loop, channels=ch,
                end_flags=end_flags, pcm=np.array(out, dtype=np.float64))


if __name__ == '__main__':
    for p in sys.argv[1:]:
        r = decode(p)
        print(p, 'pitch', r['pitch'], 'rate %.0f' % r['rate'], 'hdr samples', r['samples'], 'decoded', len(r['pcm']),
              'end flags', r['end_flags'], 'dur %.3f s' % (len(r['pcm']) / r['rate']))
