"""Compare actual deferred and alpha-one forward AP images; inspect mixed output separately."""

from argparse import ArgumentParser
import json
from pathlib import Path
import numpy as np
from PIL import Image


def readability(record, mapped_record):
    before = np.fromfile(record['before_ap'], dtype='<f4').reshape(-1, 4)
    after = np.fromfile(record['after_ap'], dtype='<f4').reshape(-1, 4)
    assert len(before) == len(after) == record['width'] * record['height']
    affected = np.any(np.abs(after[:, :3] - before[:, :3]) > 1e-5, axis=-1)
    mapped = np.array(Image.open(mapped_record['image']).convert('RGB')).reshape(-1, 3)
    assert len(mapped) == len(before)
    pixels = mapped[affected].astype(np.int16)
    if not len(pixels):
        raise AssertionError('No AP-affected geometry to assess')
    return dict(ap_changed_pixels=int(np.count_nonzero(affected)),
                white_fraction=float(np.mean(np.all(pixels >= 250, axis=1))),
                colored_fraction=float(np.mean(np.ptp(pixels, axis=1) > 8)))

def main():
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--deferred", type=Path, required=True)
    parser.add_argument("--forward", type=Path, required=True)
    parser.add_argument("--mixed", type=Path, required=True)
    parser.add_argument("--opaque-reference", type=Path)
    parser.add_argument("--opaque-forward", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    reports = [json.loads(p.read_text()) for p in (args.deferred,args.forward,args.mixed)]
    mapped_reports = [json.loads(p.with_name(p.name.replace('.ap.json','.exposure.json')).read_text())
                      for p in (args.deferred,args.forward,args.mixed)]
    assert [r["case"] for r in reports] == ["ApDeferred","ApForward","ApMixed"]
    assert all(len(r["views"]) == 2 for r in reports)
    results = []
    for index, records in enumerate(zip(*(r["views"] for r in reports))):
        assert len({(r["width"],r["height"],r["gain"],r["pre_exposure"]) for r in records}) == 1
        images = [np.fromfile(r["radiance"],dtype="<f4").reshape(r["height"],r["width"],4).astype(np.float64)
                  for r in records]
        assert all(np.isfinite(image).all() for image in images)
        old, new, mixed = images
        error = np.abs(new[...,:3] - old[...,:3])
        budget = .005 * np.abs(old[...,:3]) + 1e-5
        violations = int(np.count_nonzero(np.any(error > budget,axis=-1)))
        alpha_error = float(np.max(np.abs(new[...,3] - old[...,3])))
        mixed_changed = int(np.count_nonzero(np.any(np.abs(mixed-old) > 1e-5,axis=-1)))
        ap_changed = []
        readability_results = []
        for phase, record in enumerate(records):
            metrics = readability(record, mapped_reports[phase]['views'][index])
            ap_changed.append(metrics.pop('ap_changed_pixels'))
            readability_results.append(dict(phase=phase, **metrics))
        result = {"view":index,"pixels":old.shape[0]*old.shape[1],"maximum_rgb_error":float(error.max()),
                  "out_of_budget_pixels":violations,"maximum_alpha_error":alpha_error,
                  "mixed_changed_pixels":mixed_changed,"ap_changed_pixels":ap_changed,
                  "readability":readability_results}
        results.append(result)
    passed = all(r["out_of_budget_pixels"] == 0 and r["maximum_alpha_error"] <= 1e-5
                 and r["mixed_changed_pixels"] > 0 and min(r['ap_changed_pixels']) > 100
                 and all(p['white_fraction'] < .01 and p['colored_fraction'] > .05 for p in r['readability'])
                 for r in results)
    opaque_results = []
    if bool(args.opaque_reference) != bool(args.opaque_forward):
        raise AssertionError('Both opaque reports are required together')
    if args.opaque_reference:
        opaque = [json.loads(p.read_text()) for p in (args.opaque_reference,args.opaque_forward)]
        opaque_mapped = [json.loads(p.with_name(p.name.replace('.ap.json','.exposure.json')).read_text()) for p in (args.opaque_reference,args.opaque_forward)]
        assert [r['case'] for r in opaque] == ['ApOpaqueReference','ApOpaqueForward']
        assert all(len(r['views']) == 2 for r in opaque)
        for index,(reference,forward) in enumerate(zip(*(r['views'] for r in opaque))):
            assert (reference['width'],reference['height'],reference['gain'],reference['pre_exposure']) == (forward['width'],forward['height'],forward['gain'],forward['pre_exposure'])
            a=np.fromfile(reference['radiance'],dtype='<f4').reshape(-1,4).astype(np.float64)
            b=np.fromfile(forward['radiance'],dtype='<f4').reshape(-1,4).astype(np.float64)
            assert np.isfinite(a).all() and np.isfinite(b).all()
            error=np.abs(b[:,:3]-a[:,:3])
            violations=int(np.count_nonzero(np.any(error > .005*np.abs(a[:,:3])+1e-5,axis=1)))
            alpha_error=float(np.max(np.abs(b[:,3]-a[:,3])))
            opaque_results.append(dict(view=index,pixels=len(a),maximum_rgb_error=float(error.max()),
                out_of_budget_pixels=violations,maximum_alpha_error=alpha_error,
                inline_opaque_ap_draws=forward['inline_opaque_ap_draws'],
                readability=[readability(r,opaque_mapped[phase]['views'][index]) for phase,r in enumerate((reference,forward))]))
        passed &= all(r['out_of_budget_pixels']==0 and r['maximum_alpha_error']<=1e-5
                      and r['inline_opaque_ap_draws']==0
                      and all(p['ap_changed_pixels']>100 and p['white_fraction']<.01 and p['colored_fraction']>.05 for p in r['readability'])
                      for r in opaque_results)
    args.output.write_text(json.dumps({"relative_tolerance":.005,"absolute_tolerance":1e-5,
        "views":results,"opaque_views":opaque_results,"verdict":"pass" if passed else "fail"},indent=2)+"\n")
    print(json.dumps(results,indent=2))
    if opaque_results: print(json.dumps(opaque_results,indent=2))
    if not passed:
        raise SystemExit("FAIL: AP forward/deferred image acceptance")


if __name__ == "__main__":
    main()
