"""The vertex-synthesis tables of docs/re/03-3d-pipeline.md ("Vertex synthesis", "Fixed-formula synthesis without an
operand", "Ops 34, 35, 36, 52, 53") transcribed as text and evaluated by a small independent word evaluator, against
extract.models._apply_synth on random slot arrays (values over the full word range, so the 16-bit wrap is exercised)."""
import random

import pytest

from extract import models as M

SIGN = lambda x: ((x + 0x8000) & 0xFFFF) - 0x8000


class Wd:
    """A 16-bit word: every operation wraps."""
    def __init__(self, v): self.v = SIGN(v)
    def __add__(self, o): return Wd(self.v + o.v)
    def __sub__(self, o): return Wd(self.v - o.v)
    def __rmul__(self, n): return Wd(n * self.v)
    __mul__ = __rmul__


def h(x): return Wd(x.v >> 1)
def q(x): return Wd(x.v >> 2)


# new slot -> formula text, in the note's notation (Vk: the slot base+k; slots written earlier in the same op or by an earlier op are read back)
T41 = {9: "V3+V2-V1", 10: "V3-D", 11: "V5-V6+V7", 12: "V11+V8-V6", 13: "V5+(V4-V6)", 14: "V8+(V4-V6)",
       15: "V7+(V4-V6)", 16: "V11+(V4-V6)", 17: "V12+(V4-V6)"}
T42 = {17: "V3+V4-V8", 18: "V2-V1+V6", 19: "h(V6-V1)+V7", 20: "h(V6-V1)+V16", 21: "h(V6-V1)+h(V3+V4)+2*(V6-V1)",
       22: "h(V21+V0)", 23: "V4+V5-V8", 24: "h(V8+V11)", 25: "V4+V5-V3", 26: "V6+V9-V1", 27: "2*V6-V1",
       28: "V2-2*V1+2*V6", 29: "h(V6-V1)+V0", 100: "h(h(V13+V14)+V10)"}
T43 = {10: "V3+V4-V6", 11: "h(V3+V4)+q(V5-V3)", 12: "h(V11+V0)", 13: "V7+q(V9-V7)+h(q(V9-V7))",
       14: "V4+V5-V3", 15: "V3+V4-V7"}
T44 = {5: "V0+h(V1-V0)", 6: "V0+h(h(V1-V0))"}
T59 = {21: "V6-V7+V10", 22: "h(V3+V4)", 23: "2*h(V3+V4)-V9", 24: "2*h(V3+V4)-V8", 25: "V4+V5-V3",
       26: "h(h(V4-V3+2*V5)+V5)", 27: "h(h(V4-V3+2*V5)+V4-V3+V5)", 28: "h(4*h(V3+V4)-2*V3+2*V5-V8-V9)",
       29: "h(V1+V2)", 30: "V6-V7+V12", 31: "V6-V7+V14", 32: "h(V6-V7)+V1+V6-V7", 33: "h(V6-V7)+V2+V6-V7",
       34: "h(V6-V7)+V11+2*(V6-V7)", 35: "h(V6-V7)+V13+2*(V6-V7)", 36: "h(V6-V7)+V1+2*(V6-V7)",
       37: "h(V6-V7)+V2+2*(V6-V7)", 38: "2*h(V3+V4)-V3+V5-V9", 39: "2*h(V3+V4)-V3+V5-V8", 110: "h(h(V18+V19)+V16)"}
T34 = {4: "V0-V1+V2", 5: "V0-V1+V3", 6: "V3+V2-V1", 7: "V0-2*V1+V2+V3", 8: "V3-D", 9: "V3-D-V1+V2"}
T35 = {k: T34[k] for k in (4, 5, 6, 7)}
T36 = {6: "V0-V1+V2", 7: "V0-V1+V3", 8: "V0-V1+V4", 9: "V0-V1+V5", 10: "V2+V5-V1", 11: "V0-2*V1+V2+V5",
       12: "V4-D", 13: "V4-D-V2+V3"}
M_ = "h(V1+V2)"
T38 = {11: "V0+V1-V2", 12: "V0+V1-V4", 13: "V0+V1-V3", 14: "V0-V1+V6", 15: "V2+V6-V1", 16: "V0-V2+V6",
       17: "V3-V4+V9", 18: "V0+V1-2*V4+V9", 19: "V0+V1-V3-V4+V9", 20: "3*V5-2*V7"}
T45 = {5: "M", 6: "2*M-V3", 7: "M+q(V0-M)", 8: "V1+V3+V4-2*M", 9: "V2+V3+V4-2*M", 10: "2*V3+V4-2*M",
       11: "3*V7-2*V0"}
T56 = {15: "2*V4-V7", 16: "2*V4-V8", 17: "2*V4-V9", 18: "2*V4-V10", 19: "2*V3-V11", 20: "2*V3-V12",
       21: "2*V3-V13", 22: "2*V3-V14"}
T63 = {16: "V0-V1+V2", 17: "V3-V4+V5", 18: "V7-V8+V9", 19: "V10-V11+V12", 20: "V13-V14+V15"}
T64 = {23: "V1+V2-V0", 24: "M", 25: "V0-h(M-V0)", 26: "V0-h(M-V0)-h(M-V1)", 27: "V1-h(M-V1)", 28: "V1-h(M-V0)-h(M-V1)"}
T65 = {23: "V1+V2-V0", 24: "V2+V5-V0"}

NSLOTS = 128


def evaluate(table, arr, comp, shift=0):
    """Apply a table to arr (slot -> [x, y, z] ints), one component, writing as it goes."""
    for slot, text in table.items():
        env = {"h": h, "q": q}
        for k in range(NSLOTS):
            env[f"V{k}"] = Wd(arr[k][comp])
        env["M"] = h(env["V1"] + env["V2"])
        if "D" in text:
            a, b = (env["V2"], env["V3"]) if table in (T34, T35, T41) else (env["V3"], env["V4"])
            d = a - b
            env["D"] = Wd(d.v << shift) if shift >= 0 else Wd(d.v >> -shift)
        val = eval(text, {"__builtins__": {}}, env)
        arr[slot][comp] = val.v


def fresh(rng, nblock):
    arr = {}
    for k in range(nblock):
        arr[k] = [rng.randint(-32768, 32767) for _ in range(3)] + [M.V_BLOCK]
    return arr


def reference(arr, table, shift=0):
    ref = {k: list(v) for k, v in arr.items()}
    for k in range(NSLOTS):
        ref.setdefault(k, [0, 0, 0, 0])
    for c in range(3):
        evaluate(table, ref, c, shift)
    return ref


def check(op, table, nblock, shift=0, rec=None, seeds=60):
    for seed in range(seeds):
        rng = random.Random(seed)
        arr = fresh(rng, nblock)
        before = {k: list(v) for k, v in arr.items()}
        ref = reference(arr, table, shift)
        M._apply_synth(arr, rec or (op, 0, shift))
        for slot in table:
            assert arr[slot][:3] == ref[slot][:3], (op, slot, seed)
            assert arr[slot][3] == M.V_SYNTH
        for k in before:       # nothing else changes
            if k not in table:
                assert arr[k] == before[k]


@pytest.mark.parametrize("s", [-3, -1, 0, 1, 2])
def test_op_34_35_36_41_with_shifts(s):
    check(34, T34, 4, s)
    check(35, T35, 4, s)
    check(36, T36, 6, s)
    check(41, T41, 9, s)


def test_ops_43_44_42_in_sequence_read_back_the_slots_the_earlier_ops_wrote():
    for seed in range(60):
        rng = random.Random(seed)
        arr = fresh(rng, 17)
        ref = {k: list(v) for k, v in arr.items()}
        for k in range(NSLOTS):
            ref.setdefault(k, [0, 0, 0, 0])
        for op, table in ((43, T43), (44, T44), (42, T42)):
            M._apply_synth(arr, (op, 0, 0))
            for c in range(3):
                evaluate(table, ref, c)
        for slot in list(T43) + list(T44) + list(T42):
            assert arr[slot][:3] == ref[slot][:3], (slot, seed)


def test_op_59_and_the_fixed_formula_ops():
    check(59, T59, 21)
    for op, table, nb in ((38, T38, 10), (45, T45, 6), (56, T56, 15), (63, T63, 16), (64, T64, 3), (65, T65, 6)):
        check(op, table, nb)


def test_ops_46_and_47_are_ops_38_and_45():
    assert M.FIXED_SYNTH[46] == 38 and M.FIXED_SYNTH[47] == 45


def test_ops_52_and_53_word_arithmetic():
    rng = random.Random(7)
    arr = fresh(rng, 40)
    ref = {k: list(v) for k, v in arr.items()}
    M._apply_synth(arr, (52, 1, 2, 10))      # a = slot 1, b = slot 2, t = slot 10
    for c in range(3):
        d = Wd(ref[2][c]) - Wd(ref[1][c])
        assert arr[13][c] == (Wd(ref[10][c]) + d).v
        assert arr[14][c] == (Wd(ref[11][c]) + h(d)).v
        assert arr[15][c] == (Wd(ref[12][c]) + h(d)).v
    M._apply_synth(arr, (53, 3, 20))
    for j in range(3):
        for k in range(1, 6):
            for c in range(3):
                want = (Wd(ref[3 + k][c]) + Wd(ref[20 + j][c]) - Wd(ref[3][c])).v
                assert arr[3 + 30 + 5 * j + k - 1][c] == want
