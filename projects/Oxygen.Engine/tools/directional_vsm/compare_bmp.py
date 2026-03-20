#!/usr/bin/env python3
from __future__ import annotations

import argparse
import struct
from pathlib import Path

import numpy as np


def load_bmp(path: Path) -> np.ndarray:
    data = path.read_bytes()
    if data[:2] != b"BM":
        raise ValueError(f"{path} is not a BMP file")

    pixel_offset = struct.unpack_from("<I", data, 10)[0]
    dib_size = struct.unpack_from("<I", data, 14)[0]
    if dib_size < 40:
        raise ValueError(f"{path} uses unsupported BMP header size {dib_size}")

    width = struct.unpack_from("<i", data, 18)[0]
    height = struct.unpack_from("<i", data, 22)[0]
    bpp = struct.unpack_from("<H", data, 28)[0]
    compression = struct.unpack_from("<I", data, 30)[0]
    if compression != 0:
        raise ValueError(f"{path} uses unsupported compressed BMP format")
    if bpp not in (24, 32):
        raise ValueError(f"{path} uses unsupported {bpp}-bit BMP format")

    width_abs = abs(width)
    height_abs = abs(height)
    channels = bpp // 8
    stride = ((width_abs * channels + 3) // 4) * 4
    raw = np.frombuffer(data, dtype=np.uint8, count=stride * height_abs, offset=pixel_offset)
    rows = raw.reshape(height_abs, stride)[:, : width_abs * channels]
    pixels = rows.reshape(height_abs, width_abs, channels)
    rgb = pixels[:, :, :3][:, :, ::-1]
    if height > 0:
        rgb = np.flipud(rgb)
    return rgb.copy()


def crop_image(image: np.ndarray, crop: tuple[int, int, int, int] | None) -> np.ndarray:
    if crop is None:
        return image
    x, y, w, h = crop
    return image[y : y + h, x : x + w]


def dominant_counts(image: np.ndarray, margin: int) -> dict[str, int]:
    r = image[:, :, 0].astype(np.int16)
    g = image[:, :, 1].astype(np.int16)
    b = image[:, :, 2].astype(np.int16)
    red = np.count_nonzero((r > g + margin) & (r > b + margin))
    green = np.count_nonzero((g > r + margin) & (g > b + margin))
    blue = np.count_nonzero((b > r + margin) & (b > g + margin))
    return {
        "red": int(red),
        "green": int(green),
        "blue": int(blue),
        "total": int(image.shape[0] * image.shape[1]),
    }


def parse_crop(value: str) -> tuple[int, int, int, int]:
    parts = [int(part) for part in value.split(",")]
    if len(parts) != 4:
        raise argparse.ArgumentTypeError("crop must be x,y,w,h")
    return parts[0], parts[1], parts[2], parts[3]


def main() -> int:
    parser = argparse.ArgumentParser(description="Compare BMP captures with numpy-only tooling")
    parser.add_argument("before", type=Path, help="Reference BMP path")
    parser.add_argument("after", type=Path, nargs="?", help="Candidate BMP path")
    parser.add_argument("--crop", type=parse_crop, help="Crop rectangle x,y,w,h")
    parser.add_argument("--margin", type=int, default=20, help="Dominant-color margin")
    args = parser.parse_args()

    before = crop_image(load_bmp(args.before), args.crop)
    before_counts = dominant_counts(before, args.margin)
    print(f"{args.before.name}: total={before_counts['total']} red={before_counts['red']} green={before_counts['green']} blue={before_counts['blue']}")

    if args.after is None:
        return 0

    after = crop_image(load_bmp(args.after), args.crop)
    if before.shape != after.shape:
        raise ValueError(
            f"image shape mismatch: {before.shape} vs {after.shape}"
        )

    after_counts = dominant_counts(after, args.margin)
    diff = np.abs(after.astype(np.int16) - before.astype(np.int16))
    changed = np.count_nonzero(np.any(diff != 0, axis=2))
    mean_abs_diff = float(diff.mean())
    print(f"{args.after.name}: total={after_counts['total']} red={after_counts['red']} green={after_counts['green']} blue={after_counts['blue']}")
    print(
        "delta:"
        f" red={after_counts['red'] - before_counts['red']}"
        f" green={after_counts['green'] - before_counts['green']}"
        f" blue={after_counts['blue'] - before_counts['blue']}"
        f" changed_pixels={changed}"
        f" mean_abs_rgb_diff={mean_abs_diff:.4f}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
