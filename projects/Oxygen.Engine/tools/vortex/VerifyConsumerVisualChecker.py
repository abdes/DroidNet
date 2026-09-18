"""Negative controls for capture identity and scripted visual-cycle phases."""

from argparse import ArgumentParser
from copy import deepcopy
import json
from pathlib import Path

import numpy as np

from VerifyConsumerVisualComparison import MATERIAL_REGIONS, validate_cycle, validate_material_regions


def main():
    parser = ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    valid = {}
    for name, mode, frame in (('clear', 'Clear', 31), ('volume', 'Volume', 95),
                             ('local', 'Local', 127), ('returned', 'Clear', 159)):
        valid[name] = {'verdict': 'pass', 'mode': mode,
                       'capture_identity': {'path': name + '.rdc', 'sha256': name},
                       'views': [{'view_index': i, 'frame_sequence': frame,
                                  'width': 100 - 50*i, 'height': 60 - 30*i} for i in range(2)]}
    validate_cycle(valid)
    cases = {}
    cases['reused_initial_clear'] = deepcopy(valid)
    cases['reused_initial_clear']['returned'] = deepcopy(valid['clear'])
    cases['local_as_volume'] = deepcopy(valid)
    cases['local_as_volume']['volume'] = deepcopy(valid['local'])
    cases['out_of_order_frames'] = deepcopy(valid)
    for view in cases['out_of_order_frames']['local']['views']:
        view['frame_sequence'] = 40
    cases['renamed_duplicate_capture'] = deepcopy(valid)
    cases['renamed_duplicate_capture']['returned']['capture_identity']['sha256'] = 'clear'
    cases['wrong_view_pairing'] = deepcopy(valid)
    cases['wrong_view_pairing']['returned']['views'][1]['view_index'] = 0
    cases['different_view_frames'] = deepcopy(valid)
    cases['different_view_frames']['returned']['views'][1]['frame_sequence'] = 160
    for name, case in cases.items():
        try:
            validate_cycle(case)
        except RuntimeError:
            continue
        raise AssertionError(f'Checker accepted {name}')
    material_controls = []
    for view in range(2):
        image = np.zeros((768, 1024, 3), dtype=np.int16)
        image[:256, :, 2] = 180  # Preserve a large blue sky in every control.
        for channel, (x0, y0, x1, y1) in MATERIAL_REGIONS[view].values():
            image[int(y0 * 768):int(y1 * 768), int(x0 * 1024):int(x1 * 1024), channel] = 180
        validate_material_regions(image, view)
        for name, (_, (x0, y0, x1, y1)) in MATERIAL_REGIONS[view].items():
            missing = image.copy()
            missing[int(y0 * 768):int(y1 * 768), int(x0 * 1024):int(x1 * 1024)] = 0
            try:
                validate_material_regions(missing, view)
            except RuntimeError:
                material_controls.append(f'view_{view}_missing_{name}_sky_preserved')
                continue
            raise AssertionError(f'Checker accepted missing {name} with sky intact')
    result = {'verdict': 'pass', 'valid_cycle': True, 'rejected_controls': list(cases)}
    result['rejected_material_controls'] = material_controls
    args.output.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print('PASS: valid cycle, six identity/phase controls and six sky-preserving material controls')


if __name__ == '__main__':
    main()
