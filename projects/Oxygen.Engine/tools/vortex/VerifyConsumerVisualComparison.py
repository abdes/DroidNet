"""Check the small human checkpoint: visible effects and exact clear return."""

from argparse import ArgumentParser
import json
from pathlib import Path

import numpy as np
from PIL import Image


# Interior material regions in the fixed daylight cameras, in normalized XY.
# Keep them below the horizon and away from silhouettes/overlapping objects.
MATERIAL_REGIONS = (
    {'red_cone': (0, (.36, .57, .395, .61)),
     'green_sphere': (1, (.446, .51, .47, .545)),
     'blue_cylinder': (2, (.49, .53, .52, .59))},
    {'red_cone': (0, (.305, .68, .36, .75)),
     'green_sphere': (1, (.29, .54, .35, .61)),
     'blue_cylinder': (2, (.535, .55, .575, .60))},
)


def validate_material_regions(rgb, view):
    height, width = rgb.shape[:2]
    result = {}
    for name, (channel, bounds) in MATERIAL_REGIONS[view].items():
        x0, y0, x1, y1 = bounds
        rect = [int(x0 * width), int(y0 * height), int(x1 * width), int(y1 * height)]
        region = rgb[rect[1]:rect[3], rect[0]:rect[2]].astype(np.int16)
        colored = int(np.count_nonzero((region[..., channel] > region[..., (channel + 1) % 3] + 8)
                      & (region[..., channel] > region[..., (channel + 2) % 3] + 8)))
        count = region.shape[0] * region.shape[1]
        if colored < 100 or colored < .5 * count:
            raise RuntimeError(f'Material color missing in {name}, view {view}: {colored}/{count}')
        result[name] = {'rectangle': rect, 'material_pixels': colored, 'region_pixels': count}
    return result


def validate_cycle(reports):
    expected = {'clear': ('Clear', 1, 32), 'volume': ('Volume', 32, 96),
                'local': ('Local', 96, 128), 'returned': ('Clear', 128, 2**64)}
    hashes, paths, frames = set(), set(), []
    for name, (mode, first, end) in expected.items():
        report = reports[name]
        if report.get('verdict') != 'pass' or report.get('mode') != mode or len(report.get('views', [])) != 2:
            raise RuntimeError(f'Wrong or incomplete cycle phase: {name}')
        identity = report.get('capture_identity', {})
        digest, path = identity.get('sha256'), identity.get('path')
        if not digest or not path or digest in hashes or path in paths:
            raise RuntimeError('Cycle phases must use distinct captured executions')
        hashes.add(digest)
        paths.add(path)
        phase_frames = []
        for index, view in enumerate(report['views']):
            sequence = view.get('frame_sequence', 0)
            if view.get('view_index') != index or not first <= sequence < end:
                raise RuntimeError(f'Wrong view pairing or scripted frame for {name}')
            baseline = reports['clear']['views'][index]
            if (view['width'], view['height']) != (baseline['width'], baseline['height']):
                raise RuntimeError('Cycle view dimensions changed')
            phase_frames.append(sequence)
        if phase_frames[0] != phase_frames[1]:
            raise RuntimeError('The two views are from different logical frames')
        frames.append(phase_frames[0])
    if any(a >= b for a, b in zip(frames, frames[1:])):
        raise RuntimeError('Cycle captures are out of order')


def main():
    parser = ArgumentParser(description=__doc__)
    for name in ('clear', 'volume', 'local', 'returned', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    paths = {name: getattr(args, name) for name in ('clear', 'volume', 'local', 'returned')}
    reports = {name: json.loads(path.read_text()) for name, path in paths.items()}
    validate_cycle(reports)
    results = []
    for view in range(2):
        reference = reports['clear']['views'][view]
        shape = (reference['height'], reference['width'], 4)
        images, pixels = {}, {}
        for name, report in reports.items():
            item = report['views'][view]
            if (item['width'], item['height'], item['format'], item['gain'], item['target_gain']) != (
                    reference['width'], reference['height'], 'RGBA32F', 2**-15, 2**-15):
                raise RuntimeError('The comparison changed dimensions, format or exposure')
            data = np.fromfile(item['radiance'], dtype='<f4')
            if data.size != np.prod(shape) or not np.isfinite(data).all():
                raise RuntimeError('Incomplete or nonfinite HDR image')
            pixels[name] = data.reshape(shape)
            exposure_path = paths[name].with_name(paths[name].name.replace('.visual.json', '.exposure.json'))
            exposure = json.loads(exposure_path.read_text())
            images[name] = np.asarray(Image.open(exposure['views'][view]['image']).convert('RGB'), dtype=np.int16)
            if images[name].shape != shape[:2] + (3,):
                raise RuntimeError('Mismatched displayed image')
        if not np.array_equal(pixels['clear'], pixels['returned']):
            raise RuntimeError(f'Clear restoration changed HDR pixels in view {view}')
        if not np.array_equal(images['clear'], images['returned']):
            raise RuntimeError(f'Clear restoration changed displayed pixels in view {view}')
        row = {'view': view, 'exact_clear_restoration': True, 'effects': {}}
        for name in ('volume', 'local'):
            delta = np.max(np.abs(images[name] - images['clear']), axis=2)
            visible = int(np.count_nonzero(delta >= 3))
            if visible < 100:
                raise RuntimeError(f'{name} has no visible three-code contribution in view {view}')
            hdr_changed = int(np.count_nonzero(np.any(pixels[name][..., :3] != pixels['clear'][..., :3], axis=2)))
            if name == 'local' and hdr_changed > reference['width'] * reference['height'] // 2:
                raise RuntimeError('The local patch affects more than half the image')
            row['effects'][name] = {'visible_changed_pixels': visible, 'hdr_changed_pixels': hdr_changed,
                                    'maximum_rgb_code_change': int(delta.max())}
        for name in ('clear', 'volume', 'local'):
            rgb = images[name]
            row[name + '_materials'] = validate_material_regions(rgb, view)
        results.append(row)
    args.output.write_text(json.dumps({'verdict': 'pass', 'views': results,
        'scope': 'Same scene/exposure, visible fog, localized contribution and exact scripted return; mouse interaction remains the human check'}, indent=2)+'\n')
    print('PASS: visible fog/local effects, material colors and exact clear restoration in both views')


if __name__ == '__main__':
    main()
