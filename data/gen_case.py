from __future__ import annotations

import argparse
from pathlib import Path
import numpy as np


COL = 256


# ----------------------------
# basic io
# ----------------------------

def write_matrix_txt(path: Path, mat: np.ndarray) -> None:
    mat = np.asarray(mat, dtype=np.int32)
    with path.open("w", encoding="utf-8") as f:
        for row in mat:
            f.write(" ".join(str(int(v)) for v in row) + "\n")


def read_matrix_txt(path: Path, rows: int | None = None, cols: int | None = None) -> np.ndarray:
    data = []
    with path.open("r", encoding="utf-8") as f:
        for line in f:
            s = line.strip()
            if not s:
                continue
            vals = [int(x, 0) for x in s.split()]
            data.append(vals)

    arr = np.asarray(data, dtype=np.int32)
    if rows is not None and arr.shape[0] != rows:
        raise ValueError(f"{path} row count mismatch: got {arr.shape[0]}, expect {rows}")
    if cols is not None and arr.shape[1] != cols:
        raise ValueError(f"{path} col count mismatch: got {arr.shape[1]}, expect {cols}")
    return arr


def write_stream_txt(path: Path, vec: np.ndarray) -> None:
    vec = np.asarray(vec, dtype=np.int32).reshape(-1)
    with path.open("w", encoding="utf-8") as f:
        for v in vec:
            f.write(f"{int(v)}\n")


# ----------------------------
# input generation
# ----------------------------

def make_input_rows(kind: str, rows: int, cols: int, seed: int,
                    low: int, high: int, const_value: int) -> np.ndarray:
    rng = np.random.default_rng(seed)

    if kind == "zeros":
        x = np.zeros((rows, cols), dtype=np.int32)
    elif kind == "const":
        x = np.full((rows, cols), const_value, dtype=np.int32)
    elif kind == "ramp":
        base = np.arange(cols, dtype=np.int32)
        x = np.stack([base + np.int32(10 * r) for r in range(rows)], axis=0)
    elif kind == "checker":
        base = np.array([1 if i % 2 == 0 else -1 for i in range(cols)], dtype=np.int32)
        x = np.stack([(r + 1) * base for r in range(rows)], axis=0).astype(np.int32)
    elif kind == "impulse":
        x = np.zeros((rows, cols), dtype=np.int32)
        for r in range(rows):
            pos = (cols // 2 + r) % cols
            x[r, pos] = np.int32(5 + r)
    elif kind == "random":
        x = rng.integers(low, high + 1, size=(rows, cols), dtype=np.int32)
    else:
        raise ValueError(f"unsupported kind: {kind}")

    return x.astype(np.int32)


# ----------------------------
# window simulator
# ----------------------------

class Win:
    def __init__(self, arr: np.ndarray, oob_mode: str = "zero"):
        self.arr = np.asarray(arr, dtype=np.int32).reshape(-1)
        self.pos = 0
        self.oob_mode = oob_mode

    def _get(self, idx: int) -> np.int32:
        n = self.arr.shape[0]
        if 0 <= idx < n:
            return np.int32(self.arr[idx])

        if self.oob_mode == "zero":
            return np.int32(0)
        if self.oob_mode == "clamp":
            return np.int32(self.arr[0] if idx < 0 else self.arr[-1])
        if self.oob_mode == "periodic":
            return np.int32(self.arr[idx % n])

        raise ValueError(f"unsupported oob_mode: {self.oob_mode}")

    def _slice8(self, start: int) -> np.ndarray:
        return np.asarray([self._get(start + k) for k in range(8)], dtype=np.int32)

    def readincr_v8(self) -> np.ndarray:
        v = self._slice8(self.pos)
        self.pos += 8
        return v

    def read_v8(self) -> np.ndarray:
        return self._slice8(self.pos)

    def decr_v8(self, n: int) -> None:
        self.pos -= 8 * n


# ----------------------------
# vector helpers
# ----------------------------

def upd_w(buf16: np.ndarray, idx: int, v8: np.ndarray) -> np.ndarray:
    out = np.asarray(buf16, dtype=np.int32).copy()
    out[idx * 8:(idx + 1) * 8] = np.asarray(v8, dtype=np.int32)
    return out


def ext_w(buf16: np.ndarray, idx: int) -> np.ndarray:
    return np.asarray(buf16[idx * 8:(idx + 1) * 8], dtype=np.int32)


def concat(lower8: np.ndarray, upper8: np.ndarray) -> np.ndarray:
    return np.concatenate([
        np.asarray(lower8, dtype=np.int32),
        np.asarray(upper8, dtype=np.int32)
    ]).astype(np.int32)


def mul8(buf16: np.ndarray, off: int, vec8: np.ndarray) -> np.ndarray:
    a = np.asarray(buf16[off:off + 8], dtype=np.int64)
    b = np.asarray(vec8, dtype=np.int64)
    return a * b


def lmul8_sim(buf16: np.ndarray, off: int, vec8: np.ndarray) -> np.ndarray:
    return mul8(buf16, off, vec8)


def lmac8_sim(acc: np.ndarray, buf16: np.ndarray, off: int, vec8: np.ndarray) -> np.ndarray:
    return np.asarray(acc, dtype=np.int64) + mul8(buf16, off, vec8)


def lmsc8_sim(acc: np.ndarray, buf16: np.ndarray, off: int, vec8: np.ndarray) -> np.ndarray:
    return np.asarray(acc, dtype=np.int64) - mul8(buf16, off, vec8)


def to_i32(v: np.ndarray) -> np.ndarray:
    return np.asarray(v, dtype=np.int32)


# ----------------------------
# kernel-faithful simulators
# ----------------------------

def hdiff_lap_kernel(row0: np.ndarray, row1: np.ndarray, row2: np.ndarray,
                     row3: np.ndarray, row4: np.ndarray,
                     oob_mode: str = "zero") -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    coeffs = np.full(8, -4, dtype=np.int32)
    coeffs_rest = np.full(8, -1, dtype=np.int32)

    row0_win = Win(row0, oob_mode)
    row1_win = Win(row1, oob_mode)
    row2_win = Win(row2, oob_mode)
    row3_win = Win(row3, oob_mode)
    row4_win = Win(row4, oob_mode)

    data_buf1 = np.zeros(16, dtype=np.int32)
    data_buf2 = np.zeros(16, dtype=np.int32)

    data_buf1 = upd_w(data_buf1, 0, row3_win.readincr_v8())
    data_buf1 = upd_w(data_buf1, 1, row3_win.read_v8())

    data_buf2 = upd_w(data_buf2, 0, row1_win.readincr_v8())
    data_buf2 = upd_w(data_buf2, 1, row1_win.read_v8())

    out_flux1 = []
    out_flux2 = []
    out_flux3 = []
    out_flux4 = []

    for _ in range(COL // 8):
        acc_0 = lmul8_sim(data_buf2, 2, coeffs_rest)
        acc_1 = lmul8_sim(data_buf2, 1, coeffs_rest)

        acc_0 = lmac8_sim(acc_0, data_buf1, 2, coeffs_rest)
        acc_1 = lmac8_sim(acc_1, data_buf1, 1, coeffs_rest)

        data_buf2 = upd_w(data_buf2, 0, row2_win.readincr_v8())
        data_buf2 = upd_w(data_buf2, 1, row2_win.read_v8())

        acc_0 = lmac8_sim(acc_0, data_buf2, 1, coeffs_rest)
        acc_0 = lmsc8_sim(acc_0, data_buf2, 2, coeffs)
        acc_0 = lmac8_sim(acc_0, data_buf2, 3, coeffs_rest)
        lap_ij = to_i32(acc_0)

        acc_1 = lmac8_sim(acc_1, data_buf2, 0, coeffs_rest)
        acc_1 = lmsc8_sim(acc_1, data_buf2, 1, coeffs)
        acc_1 = lmac8_sim(acc_1, data_buf2, 2, coeffs_rest)
        lap_0 = to_i32(acc_1)

        flux1 = to_i32(lap_ij.astype(np.int64) - lap_0.astype(np.int64))
        out_flux1.append(flux1)

        acc_0 = lmul8_sim(data_buf1, 3, coeffs_rest)
        acc_0 = lmsc8_sim(acc_0, data_buf2, 3, coeffs)

        row1_win.decr_v8(1)
        data_buf1 = upd_w(data_buf1, 0, row1_win.readincr_v8())
        data_buf1 = upd_w(data_buf1, 1, row1_win.read_v8())

        acc_0 = lmac8_sim(acc_0, data_buf2, 2, coeffs_rest)
        acc_0 = lmac8_sim(acc_0, data_buf2, 4, coeffs_rest)
        acc_0 = lmac8_sim(acc_0, data_buf1, 3, coeffs_rest)
        lap_0 = to_i32(acc_0)

        flux2 = to_i32(lap_0.astype(np.int64) - lap_ij.astype(np.int64))
        out_flux2.append(flux2)

        acc_1 = lmul8_sim(data_buf2, 2, coeffs_rest)
        acc_0 = lmul8_sim(data_buf2, 2, coeffs_rest)

        data_buf2 = upd_w(data_buf2, 0, row0_win.readincr_v8())
        data_buf2 = upd_w(data_buf2, 1, row0_win.read_v8())

        acc_1 = lmsc8_sim(acc_1, data_buf1, 2, coeffs)
        acc_1 = lmac8_sim(acc_1, data_buf1, 1, coeffs_rest)
        acc_1 = lmac8_sim(acc_1, data_buf2, 2, coeffs_rest)

        data_buf2 = upd_w(data_buf2, 0, row4_win.readincr_v8())
        data_buf2 = upd_w(data_buf2, 1, row4_win.read_v8())

        acc_1 = lmac8_sim(acc_1, data_buf1, 3, coeffs_rest)
        acc_0 = lmac8_sim(acc_0, data_buf2, 2, coeffs_rest)

        lap_0 = to_i32(acc_1)
        flux3 = to_i32(lap_ij.astype(np.int64) - lap_0.astype(np.int64))
        out_flux3.append(flux3)

        row3_win.decr_v8(1)
        data_buf1 = upd_w(data_buf1, 0, row3_win.readincr_v8())
        data_buf1 = upd_w(data_buf1, 1, row3_win.read_v8())

        acc_0 = lmsc8_sim(acc_0, data_buf1, 2, coeffs)
        acc_0 = lmac8_sim(acc_0, data_buf1, 1, coeffs_rest)

        data_buf2 = upd_w(data_buf2, 0, row1_win.readincr_v8())
        data_buf2 = upd_w(data_buf2, 1, row1_win.read_v8())

        acc_0 = lmac8_sim(acc_0, data_buf1, 3, coeffs_rest)
        flux4 = to_i32(acc_0.astype(np.int64) - lap_ij.astype(np.int64))
        out_flux4.append(flux4)

        data_buf1 = upd_w(data_buf1, 0, row3_win.readincr_v8())
        data_buf1 = upd_w(data_buf1, 1, row3_win.read_v8())

    return (
        np.concatenate(out_flux1).astype(np.int32),
        np.concatenate(out_flux2).astype(np.int32),
        np.concatenate(out_flux3).astype(np.int32),
        np.concatenate(out_flux4).astype(np.int32),
    )


def hdiff_flux1_kernel(row1: np.ndarray, row2: np.ndarray, row3: np.ndarray,
                       flux_forward1: np.ndarray, flux_forward2: np.ndarray,
                       flux_forward3: np.ndarray, flux_forward4: np.ndarray,
                       oob_mode: str = "zero") -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    row1_win = Win(row1, oob_mode)
    row2_win = Win(row2, oob_mode)
    row3_win = Win(row3, oob_mode)

    f1_win = Win(flux_forward1, oob_mode)
    f2_win = Win(flux_forward2, oob_mode)
    f3_win = Win(flux_forward3, oob_mode)
    f4_win = Win(flux_forward4, oob_mode)

    data_buf1 = np.zeros(16, dtype=np.int32)
    data_buf2 = np.zeros(16, dtype=np.int32)

    data_buf1 = upd_w(data_buf1, 0, row1_win.readincr_v8())
    data_buf1 = upd_w(data_buf1, 1, row1_win.read_v8())

    data_buf2 = upd_w(data_buf2, 0, row2_win.readincr_v8())
    data_buf2 = upd_w(data_buf2, 1, row2_win.read_v8())

    inter1 = []
    inter2 = []
    inter3 = []
    inter4 = []
    inter5 = []

    for _ in range(COL // 8):
        flux_sub = f1_win.readincr_v8()
        acc = lmul8_sim(data_buf2, 2, flux_sub)
        acc = lmsc8_sim(acc, data_buf2, 1, flux_sub)
        inter1.append(flux_sub)
        inter1.append(to_i32(acc))

        flux_sub = f2_win.readincr_v8()
        acc = lmul8_sim(data_buf2, 3, flux_sub)
        acc = lmsc8_sim(acc, data_buf2, 2, flux_sub)
        inter2.append(flux_sub)
        inter2.append(to_i32(acc))

        flux_sub = f3_win.readincr_v8()
        acc = lmul8_sim(data_buf2, 2, flux_sub)
        acc = lmsc8_sim(acc, data_buf1, 2, flux_sub)
        inter3.append(flux_sub)
        inter3.append(to_i32(acc))

        data_buf1 = upd_w(data_buf1, 0, row3_win.readincr_v8())
        data_buf1 = upd_w(data_buf1, 1, row3_win.read_v8())

        flux_sub = f4_win.readincr_v8()
        acc = lmul8_sim(data_buf1, 2, flux_sub)
        acc = lmsc8_sim(acc, data_buf2, 2, flux_sub)
        inter4.append(flux_sub)
        inter4.append(to_i32(acc))

        data_buf1 = upd_w(data_buf1, 0, row1_win.readincr_v8())
        data_buf1 = upd_w(data_buf1, 1, row1_win.read_v8())

        inter5.append(ext_w(data_buf2, 1))
        inter5.append(ext_w(data_buf2, 0))

        data_buf2 = upd_w(data_buf2, 0, row2_win.readincr_v8())
        data_buf2 = upd_w(data_buf2, 1, row2_win.read_v8())

    return (
        np.concatenate(inter1).astype(np.int32),
        np.concatenate(inter2).astype(np.int32),
        np.concatenate(inter3).astype(np.int32),
        np.concatenate(inter4).astype(np.int32),
        np.concatenate(inter5).astype(np.int32),
    )


def hdiff_flux2_kernel(flux_inter1: np.ndarray, flux_inter2: np.ndarray,
                       flux_inter3: np.ndarray, flux_inter4: np.ndarray,
                       flux_inter5: np.ndarray,
                       oob_mode: str = "zero") -> np.ndarray:
    inter1_win = Win(flux_inter1, oob_mode)
    inter2_win = Win(flux_inter2, oob_mode)
    inter3_win = Win(flux_inter3, oob_mode)
    inter4_win = Win(flux_inter4, oob_mode)
    inter5_win = Win(flux_inter5, oob_mode)

    out = []

    for _ in range(COL // 8):
        flux_sub = inter1_win.readincr_v8()
        flux_prod = inter1_win.readincr_v8()
        out1 = np.where(flux_prod.astype(np.int64) > 0, flux_sub, 0).astype(np.int32)

        flux_sub = inter2_win.readincr_v8()
        flux_prod = inter2_win.readincr_v8()
        out2 = np.where(flux_prod.astype(np.int64) > 0, flux_sub, 0).astype(np.int32)

        flux_sub = inter3_win.readincr_v8()
        flux_prod = inter3_win.readincr_v8()
        out3 = np.where(flux_prod.astype(np.int64) > 0, flux_sub, 0).astype(np.int32)

        flux_sub = inter4_win.readincr_v8()
        flux_prod = inter4_win.readincr_v8()
        out4 = np.where(flux_prod.astype(np.int64) > 0, flux_sub, 0).astype(np.int32)

        flx_out4 = (
            out2.astype(np.int64)
            - out1.astype(np.int64)
            - out3.astype(np.int64)
            + out4.astype(np.int64)
        ).astype(np.int32)

        tmp1 = inter5_win.readincr_v8()
        tmp2 = inter5_win.readincr_v8()
        data_buf2 = concat(tmp2, tmp1)

        final_out = (-7 * flx_out4.astype(np.int64) + data_buf2[2:10].astype(np.int64)).astype(np.int32)
        out.append(final_out)

    return np.concatenate(out).astype(np.int32)


def hdiff_one_window_exact(rows5: np.ndarray, oob_mode: str = "zero") -> np.ndarray:
    rows5 = np.asarray(rows5, dtype=np.int32)
    if rows5.shape != (5, COL):
        raise ValueError(f"rows5 must be shape (5, {COL}), got {rows5.shape}")

    row0, row1, row2, row3, row4 = rows5

    flux1, flux2, flux3, flux4 = hdiff_lap_kernel(
        row0, row1, row2, row3, row4, oob_mode=oob_mode
    )

    inter1, inter2, inter3, inter4, inter5 = hdiff_flux1_kernel(
        row1, row2, row3,
        flux1, flux2, flux3, flux4,
        oob_mode=oob_mode
    )

    out = hdiff_flux2_kernel(
        inter1, inter2, inter3, inter4, inter5,
        oob_mode=oob_mode
    )

    return out.astype(np.int32)


def hdiff_multi_windows_exact(rows: np.ndarray, oob_mode: str = "zero") -> np.ndarray:
    rows = np.asarray(rows, dtype=np.int32)
    if rows.ndim != 2 or rows.shape[1] != COL:
        raise ValueError(f"rows must be shape (N, {COL}), got {rows.shape}")
    if rows.shape[0] < 5:
        raise ValueError("need at least 5 input rows")

    num_iter = rows.shape[0] - 4
    outs = []
    for i in range(num_iter):
        outs.append(hdiff_one_window_exact(rows[i:i + 5], oob_mode=oob_mode))
    return np.stack(outs, axis=0).astype(np.int32)


# ----------------------------
# AIE input packing
# ----------------------------

def dump_human_readable_inputs(rows: np.ndarray, out_dir: Path, graph_id: str = "hdiff") -> None:
    rows = np.asarray(rows, dtype=np.int32)
    num_iter = rows.shape[0] - 4
    for i in range(5):
        mat = rows[i:i + num_iter].astype(np.int32)
        write_matrix_txt(out_dir / f"{graph_id}_in{i}.txt", mat)


def dump_aie_stream_inputs(rows: np.ndarray, out_dir: Path, graph_id: str = "hdiff") -> None:
    rows = np.asarray(rows, dtype=np.int32)
    num_iter = rows.shape[0] - 4
    for i in range(5):
        stream = rows[i:i + num_iter].reshape(-1).astype(np.int32)
        write_stream_txt(out_dir / f"{graph_id}_in{i}_stream.txt", stream)


# ----------------------------
# main
# ----------------------------

def main() -> None:
    base_dir = Path(__file__).resolve().parent

    ap = argparse.ArgumentParser(
        description="Generate AIE-style golden/input streams for arbitrary iter count on current 256-wide lap/flux1/flux2 implementation."
    )
    ap.add_argument("--data-dir", type=Path, default=base_dir)
    ap.add_argument("--iter", type=int, default=32, dest="iter_cnt",
                    help="number of output windows / host iter count")
    ap.add_argument("--cols", type=int, default=COL)
    ap.add_argument("--kind", choices=["zeros", "const", "ramp", "checker", "impulse", "random"], default="random")
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--low", type=int, default=-20)
    ap.add_argument("--high", type=int, default=20)
    ap.add_argument("--const-value", type=int, default=7)
    ap.add_argument("--graph-id", default="hdiff")
    ap.add_argument("--oob-mode", choices=["zero", "clamp", "periodic"], default="zero")
    args = ap.parse_args()

    if args.iter_cnt <= 0:
        raise ValueError("--iter must be positive")
    if args.cols != COL:
        raise ValueError(f"this flow expects cols={COL}")

    data_dir = args.data_dir
    data_dir.mkdir(parents=True, exist_ok=True)

    total_rows = args.iter_cnt + 4
    rows = make_input_rows(
        kind=args.kind,
        rows=total_rows,
        cols=COL,
        seed=args.seed,
        low=args.low,
        high=args.high,
        const_value=args.const_value,
    )

    input_txt = data_dir / "input.txt"
    gold_txt = data_dir / "gold_out.txt"

    write_matrix_txt(input_txt, rows)
    rows_reload = read_matrix_txt(input_txt, rows=total_rows, cols=COL)

    gold = hdiff_multi_windows_exact(rows_reload, oob_mode=args.oob_mode)
    write_matrix_txt(gold_txt, gold)

    dump_human_readable_inputs(rows_reload, data_dir, graph_id=args.graph_id)
    dump_aie_stream_inputs(rows_reload, data_dir, graph_id=args.graph_id)

    print(f"generated {args.iter_cnt} windows")
    print(f"input rows   : {rows_reload.shape[0]} x {rows_reload.shape[1]}")
    print(f"gold output  : {gold.shape[0]} x {gold.shape[1]}")
    print(f"stream length: each is {args.iter_cnt * COL} scalars")


if __name__ == "__main__":
    main()
