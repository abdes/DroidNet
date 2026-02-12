#!/usr/bin/env python3
"""
Validate blue-noise texture data stored in a C++ header.

Checks:
- Declared dimensions/data size consistency.
- Parsed byte count consistency.
- Basic distribution sanity (range, mean/std, uniqueness).
- Structural artifact signals (adjacent equality, repeated rows/columns).

Exit code:
- 0: pass
- 1: fail
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

import numpy as np


@dataclass
class ParsedHeader:
    size: int
    slices: int
    data_size: int
    data: np.ndarray  # uint8 flat


def parse_header(path: Path) -> ParsedHeader:
    text = path.read_text(encoding="utf-8")

    def find_int(name: str) -> int:
        m = re.search(rf"{name}\s*=\s*(\d+)\s*;", text)
        if not m:
            raise ValueError(f"Could not find '{name}' in {path}")
        return int(m.group(1))

    size = find_int("kBlueNoiseSize")
    slices = find_int("kBlueNoiseSlices")
    data_size = find_int("kBlueNoiseDataSize")

    arr_match = re.search(
        r"TextureData_BlueNoise\[\]\s*=\s*\{(.*?)\};",
        text,
        flags=re.S,
    )
    if not arr_match:
        raise ValueError("Could not find TextureData_BlueNoise initializer")

    arr_text = arr_match.group(1)
    tokens = re.findall(r"0x[0-9a-fA-F]+|\b\d+\b", arr_text)
    data = np.array([int(t, 16) if t.lower().startswith("0x") else int(t) for t in tokens], dtype=np.uint8)

    return ParsedHeader(size=size, slices=slices, data_size=data_size, data=data)


def adjacent_equality_ratio(vol: np.ndarray, axis: int) -> float:
    if axis == 2:  # x
        return float(np.mean(vol[:, :, 1:] == vol[:, :, :-1]))
    if axis == 1:  # y
        return float(np.mean(vol[:, 1:, :] == vol[:, :-1, :]))
    if axis == 0:  # z
        return float(np.mean(vol[1:, :, :] == vol[:-1, :, :]))
    raise ValueError("axis must be 0/1/2")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--header",
        type=Path,
        default=Path("src/Oxygen/Renderer/Internal/BlueNoiseData.h"),
        help="Path to BlueNoiseData.h",
    )
    args = ap.parse_args()

    p = parse_header(args.header)
    expected_count = p.size * p.size * p.slices

    print(f"header={args.header}")
    print(f"declared: size={p.size}, slices={p.slices}, data_size={p.data_size}")
    print(f"expected_count={expected_count}, parsed_count={p.data.size}")

    failures: list[str] = []

    if p.data_size != expected_count:
        failures.append(
            f"kBlueNoiseDataSize mismatch: declared={p.data_size}, expected={expected_count}"
        )
    if p.data.size != expected_count:
        failures.append(
            f"TextureData_BlueNoise count mismatch: parsed={p.data.size}, expected={expected_count}"
        )

    if failures:
        for f in failures:
            print(f"FAIL: {f}")
        return 1

    vol = p.data.reshape((p.slices, p.size, p.size))
    hist = np.bincount(p.data.astype(np.int32), minlength=256)
    dominant_fraction = float(hist.max() / p.data.size)

    global_unique = int(np.unique(p.data).size)
    mean = float(np.mean(p.data))
    std = float(np.std(p.data))
    min_v = int(np.min(p.data))
    max_v = int(np.max(p.data))

    per_slice_unique = np.array([np.unique(vol[z]).size for z in range(p.slices)])
    per_slice_row_unique = np.array(
        [np.unique(vol[z], axis=0).shape[0] for z in range(p.slices)]
    )
    per_slice_col_unique = np.array(
        [np.unique(vol[z].T, axis=0).shape[0] for z in range(p.slices)]
    )

    eq_x = adjacent_equality_ratio(vol, axis=2)
    eq_y = adjacent_equality_ratio(vol, axis=1)
    eq_z = adjacent_equality_ratio(vol, axis=0)

    print(f"value range=[{min_v}, {max_v}] unique={global_unique}/256")
    print(f"mean={mean:.3f} std={std:.3f} dominant_fraction={dominant_fraction:.6f}")
    print(
        "per-slice unique: "
        f"min={int(per_slice_unique.min())} "
        f"max={int(per_slice_unique.max())} "
        f"avg={float(per_slice_unique.mean()):.2f}"
    )
    print(
        "per-slice unique rows: "
        f"min={int(per_slice_row_unique.min())} "
        f"max={int(per_slice_row_unique.max())} "
        f"avg={float(per_slice_row_unique.mean()):.2f}"
    )
    print(
        "per-slice unique cols: "
        f"min={int(per_slice_col_unique.min())} "
        f"max={int(per_slice_col_unique.max())} "
        f"avg={float(per_slice_col_unique.mean()):.2f}"
    )
    print(f"adjacent equality ratios: x={eq_x:.6f} y={eq_y:.6f} z={eq_z:.6f}")

    # Conservative sanity gates.
    if min_v < 0 or max_v > 255:
        failures.append("values out of uint8 range")
    if global_unique < 200:
        failures.append(f"too few distinct values globally ({global_unique})")
    if not (110.0 <= mean <= 145.0):
        failures.append(f"mean out of expected range ({mean:.3f})")
    if not (45.0 <= std <= 85.0):
        failures.append(f"std out of expected range ({std:.3f})")
    if dominant_fraction > 0.02:
        failures.append(
            f"dominant value frequency too high ({dominant_fraction:.6f})"
        )
    if int(per_slice_unique.min()) < 180:
        failures.append(
            f"too few unique values in at least one slice (min={int(per_slice_unique.min())})"
        )
    if int(per_slice_row_unique.min()) < int(p.size * 0.8):
        failures.append(
            "repeated rows detected (some slices have too few unique rows)"
        )
    if int(per_slice_col_unique.min()) < int(p.size * 0.8):
        failures.append(
            "repeated columns detected (some slices have too few unique columns)"
        )
    if eq_x > 0.02 or eq_y > 0.02:
        failures.append(
            f"adjacent spatial equality too high (x={eq_x:.6f}, y={eq_y:.6f})"
        )

    if failures:
        for f in failures:
            print(f"FAIL: {f}")
        return 1

    print("PASS: blue-noise header data looks structurally sane.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
