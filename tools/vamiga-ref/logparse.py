#!/usr/bin/env python3
"""Read a ref --log file: `label ADDR hex...` lines (peek) and `label 3-digit words` (colors).

    import logparse; L = logparse.load("run.log"); L["rec0"].u16(0x66); L["rec0"].s32(0x14)
Several lines with the same label are kept in order (L.all("rec0")); L["rec0"] is the last one.
"""
import sys, struct, collections


class Blob:
    def __init__(self, label, addr, data):
        self.label, self.addr, self.data = label, addr, data

    def u8(self, o): return self.data[o]
    def u16(self, o): return struct.unpack_from(">H", self.data, o)[0]
    def s16(self, o): return struct.unpack_from(">h", self.data, o)[0]
    def u32(self, o): return struct.unpack_from(">I", self.data, o)[0]
    def s32(self, o): return struct.unpack_from(">i", self.data, o)[0]


class Log(collections.OrderedDict):
    def all(self, label): return self._all.get(label, [])


def load(path):
    out = Log()
    out._all = collections.defaultdict(list)
    for ln in open(path):
        p = ln.split()
        if not p or p[0] in ("#", "seg", "seglist") or ln.startswith("#"):
            continue
        if len(p) > 1 and p[1][:2] in ("F:", "R:"):
            b = Blob(p[0], p[1], bytes.fromhex("".join(p[2:])))
        elif len(p) == 33 and all(len(w) == 3 and all(c in "0123456789ABCDEF" for c in w) for w in p[1:]):
            b = Blob(p[0], "colors", b"".join(struct.pack(">H", int(w, 16)) for w in p[1:]))
        else:
            continue
        out[p[0]] = b
        out._all[p[0]].append(b)
    return out


def segs(path):
    r = {}
    for ln in open(path):
        p = ln.split()
        if p and p[0] == "seg":
            r[int(p[1])] = (int(p[3], 16), int(p[5], 16), int(p[7]))
    return r


if __name__ == "__main__":
    for k, b in load(sys.argv[1]).items():
        print(k, b.addr, len(b.data))
