"""Compare actual deferred and alpha-one forward AP images; inspect mixed output separately."""

from argparse import ArgumentParser
import json
from pathlib import Path
import numpy as np
from PIL import Image


def main():
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("--deferred", type=Path, required=True)
    parser.add_argument("--forward", type=Path, required=True)
    parser.add_argument("--mixed", type=Path, required=True)
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
        readability = []
        for phase, record in enumerate(records):
            before = np.fromfile(record['before_ap'],dtype='<f4').reshape(-1,4)
            after = np.fromfile(record['after_ap'],dtype='<f4').reshape(-1,4)
            affected = np.any(np.abs(after[:,:3]-before[:,:3]) > 1e-5,axis=-1)
            ap_changed.append(int(np.count_nonzero(affected)))
            mapped = np.array(Image.open(mapped_reports[phase]['views'][index]['image']).convert('RGB')).reshape(-1,3)
            pixels = mapped[affected].astype(np.int16)
            if not len(pixels):
                raise AssertionError('No AP-affected geometry to assess')
            white_fraction = float(np.mean(np.all(pixels >= 250,axis=1)))
            colored_fraction = float(np.mean(np.ptp(pixels,axis=1) > 8))
            readability.append({'phase':phase,'white_fraction':white_fraction,'colored_fraction':colored_fraction})
        result = {"view":index,"pixels":old.shape[0]*old.shape[1],"maximum_rgb_error":float(error.max()),
                  "out_of_budget_pixels":violations,"maximum_alpha_error":alpha_error,
                  "mixed_changed_pixels":mixed_changed,"ap_changed_pixels":ap_changed,
                  "readability":readability}
        results.append(result)
    passed = all(r["out_of_budget_pixels"] == 0 and r["maximum_alpha_error"] <= 1e-5
                 and r["mixed_changed_pixels"] > 0 and min(r['ap_changed_pixels']) > 100
                 and all(p['white_fraction'] < .01 and p['colored_fraction'] > .05 for p in r['readability'])
                 for r in results)
    args.output.write_text(json.dumps({"relative_tolerance":.005,"absolute_tolerance":1e-5,
        "views":results,"verdict":"pass" if passed else "fail"},indent=2)+"\n")
    print(json.dumps(results,indent=2))
    if not passed:
        raise SystemExit("FAIL: AP forward/deferred image acceptance")


if __name__ == "__main__":
    main()
