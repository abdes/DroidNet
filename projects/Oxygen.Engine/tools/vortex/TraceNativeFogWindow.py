"""Sample only a named native window for intermittent visible fog changes.

This observes ordinary presentation without RenderDoc. Samples are wall-clock
observations, not a guarantee that every presented frame was captured.
"""

from argparse import ArgumentParser
import ctypes
from ctypes import wintypes
import json
from pathlib import Path
import time

import numpy as np
from PIL import ImageGrab


def main():
    parser = ArgumentParser(description=__doc__)
    parser.add_argument('--window', type=int, required=True)
    parser.add_argument('--seconds', type=float, default=45)
    parser.add_argument('--hz', type=float, default=30)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    ctypes.windll.user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
    is_window = ctypes.windll.user32.IsWindow
    is_window.argtypes = [ctypes.c_void_p]
    foreground = ctypes.windll.user32.GetForegroundWindow
    foreground.restype = wintypes.HWND
    get_rect = ctypes.windll.user32.GetClientRect
    get_rect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
    to_screen = ctypes.windll.user32.ClientToScreen
    to_screen.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.POINT)]
    regions = {'main_fog': (.30, .43, .54, .73),
               'ground_control': (.05, .65, .18, .8),
               'sky_control': (.05, .20, .20, .30)}
    records, previous, snapshots = [], {}, []
    begin = time.perf_counter()
    next_sample = begin
    while time.perf_counter() - begin < args.seconds:
        if not is_window(args.window):
            raise RuntimeError('The target window closed during observation')
        if foreground() != args.window:
            time.sleep(.03)
            next_sample = time.perf_counter()
            continue
        rect, origin = wintypes.RECT(), wintypes.POINT()
        if not get_rect(args.window, ctypes.byref(rect)) or not to_screen(args.window, ctypes.byref(origin)):
            raise RuntimeError('Cannot locate target client area')
        width, height = rect.right, rect.bottom
        if width < 640 or height < 480:
            raise RuntimeError('The target window is minimized or too small')
        row = {'seconds': time.perf_counter() - begin, 'regions': {}}
        changed = False
        for name, (x0, y0, x1, y1) in regions.items():
            # Small visible-screen regions avoid PrintWindow/GPU replay and
            # avoid repeatedly copying the complete rendered image.
            crop = np.asarray(ImageGrab.grab(bbox=(origin.x + int(x0 * width), origin.y + int(y0 * height),
                                                  origin.x + int(x1 * width), origin.y + int(y1 * height)),
                                             all_screens=True).convert('RGB'), dtype=np.int16)
            sample = crop[::max(1, crop.shape[0] // 32), ::max(1, crop.shape[1] // 32)]
            delta = np.abs(sample - previous[name]) if name in previous else np.zeros_like(sample)
            row['regions'][name] = {'mean_rgb': sample.mean(axis=(0, 1)).tolist(),
                                    'maximum_code_delta': int(delta.max()),
                                    'mean_absolute_code_delta': float(delta.mean())}
            previous[name] = sample.copy()
            if name == 'main_fog' and row['seconds'] > 7 and delta.max() >= 4:
                changed = True
        if not records or (changed and len(snapshots) < 32):
            screenshot = ImageGrab.grab(bbox=(origin.x, origin.y, origin.x + width, origin.y + height), all_screens=True)
            path = args.output.with_name(f'{args.output.stem}-{len(records):04d}.png')
            screenshot.save(path)
            snapshots.append(str(path))
            row['image'] = str(path)
        records.append(row)
        next_sample += 1 / args.hz
        time.sleep(max(0, next_sample - time.perf_counter()))
    if not records:
        raise RuntimeError('No observations: target window was not foreground')
    result = {'window': args.window, 'elapsed_seconds': time.perf_counter() - begin,
              'samples': len(records), 'window_extent': [width, height],
              'scope': 'Ordinary window presentation, fixed image regions; no RenderDoc; no automatic flicker acceptance verdict',
              'snapshots': snapshots, 'records': records}
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(f'Native window trace: {len(records)} samples over {result["elapsed_seconds"]:.2f} seconds')


if __name__ == '__main__':
    main()
