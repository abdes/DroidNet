"""Negative controls against incomplete or mismatched native history pairs."""

from argparse import ArgumentParser
import json
from pathlib import Path

from AnalyzeRenderDocHardwareHistory import compare_volume_pair


def main():
    parser = ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    valid = {'extent': (1, 1, 32), 'values': [(0.5,) * 4] * 32, 'bounds': (0.0,) * 4}
    assert compare_volume_pair(valid, valid) == (128, 0.0)
    cases = {
        'shortened_data': dict(valid, values=valid['values'][:16]),
        'shorter_volume': dict(valid, extent=(1, 1, 16), values=valid['values'][:16]),
        'equal_count_reshaped_volume': dict(valid, extent=(2, 1, 16)),
        'empty_volume': dict(valid, extent=(1, 1, 0), values=[]),
        'missing_channel': dict(valid, values=[(0.5,) * 3] * 32),
        'underbounded_channel': dict(valid, values=[(1.0,) * 4] * 32),
    }
    for name, invalid in cases.items():
        for observed, reference in ((valid, invalid), (invalid, valid)):
            try:
                compare_volume_pair(observed, reference)
            except RuntimeError:
                continue
            raise AssertionError(f'Checker accepted {name}')
    result = {'verdict': 'pass', 'complete_pair_channel_checks': 128,
              'rejected_controls': list(cases), 'negative_checks': len(cases) * 2}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result))


if __name__ == '__main__':
    main()
