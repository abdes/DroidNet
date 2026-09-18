"""Measure matched static fog sequences; human shimmer acceptance is separate."""

from argparse import ArgumentParser
import json
from pathlib import Path

import numpy as np
from PIL import Image


def measure_sequence(report):
    results = []
    for view in range(2):
        frames = report['frames'][view::2]
        pairs = []
        for before, after in zip(frames, frames[1:]):
            rgb = [np.asarray(Image.open(x['image']).convert('RGB'), dtype=np.float64)
                   for x in (before, after)]
            delta = np.abs(rgb[1] - rgb[0])
            volume = [np.fromfile(x['volume'], dtype='<f4').reshape(-1, 4).astype(np.float64)
                      for x in (before, after)]
            if not all(np.isfinite(x).all() for x in volume):
                raise RuntimeError('Nonfinite volume')
            alpha_delta = np.abs(volume[1][:, 3] - volume[0][:, 3])
            pairs.append({'frames': [before['frame'], after['frame']],
                          'rgb_code_mean_absolute': float(delta.mean()),
                          'rgb_code_maximum': float(delta.max()),
                          'pixels_changed': int(np.count_nonzero(np.max(delta, axis=2))),
                          'pixels_changed_at_least_three_codes': int(np.count_nonzero(np.max(delta, axis=2) >= 3)),
                          'mean_brightness_change_codes': float((rgb[1] - rgb[0]).mean()),
                          'transmittance_mean_absolute': float(alpha_delta.mean()),
                          'transmittance_maximum_absolute': float(alpha_delta.max())})
        results.append({'view': view, 'pairs': pairs})
    return results


def main():
    parser = ArgumentParser(description=__doc__)
    for name in ('before', 'after', 'control', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    reports = {name: json.loads(getattr(args, name).read_text())
               for name in ('before', 'after', 'control')}
    captures = {str(Path(r['capture']).resolve()) for r in reports.values()}
    if len(captures) != 3:
        raise RuntimeError('Requires three distinct native captures')
    reference = reports['before']['frames']
    for name, report in reports.items():
        if report.get('verdict') != 'pass' or len(report['frames']) != 6:
            raise RuntimeError('Incomplete capture inputs')
        for field in ('image', 'scene', 'volume'):
            if len({x[field] for x in report['frames']}) != 6:
                raise RuntimeError(f'Export paths alias different frames: {field}')
        for expected, actual in zip(reference, report['frames']):
            for field in ('view', 'frame', 'extent', 'volume_extent', 'camera_sha256',
                          'pre_exposure', 'gain', 'target_gain', 'history_weight'):
                if actual[field] != expected[field]:
                    raise RuntimeError(f'Comparison changed {field}')
            if name == 'control':
                if actual['jitter'] != [.5, .5, .5]:
                    raise RuntimeError('Control must disable jitter')
            elif actual['jitter'] != expected['jitter']:
                raise RuntimeError('Jitter sequence changed')
    result = {'inputs_verified': True, 'measurements': {
        name: measure_sequence(report) for name, report in reports.items()},
        'scope': 'Matched three-frame native sequences at fixed exposure and camera; numerical changes are measurements, not a human flicker verdict'}
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
