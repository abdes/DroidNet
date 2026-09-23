"""Completeness checks for model-2 quality measurements."""

import copy
import json
import unittest

from AssertLightingPhysicalProbe import REQUIRED_PROBES, REQUIRED_QUALIFICATIONS, validate


class PhysicalProbeGateTest(unittest.TestCase):
    def setUp(self):
        cases = [{"name": name, "status": "RUN", "result": "COMPLETED",
                  "lighting_model_revision": "2", "lighting_measurement_schema": "2",
                  "probe_count": str(count), "reference_budget_exceedances": "0",
                  "reference_deviation_details": "[]", "maximum_reference_deviation_scale": "0.25"}
                 for name, count in REQUIRED_PROBES.items()]
        for name, (field, count, metrics) in REQUIRED_QUALIFICATIONS.items():
            cases.append({"name": name, "status": "RUN", "result": "COMPLETED",
                          "lighting_model_revision": "2", field: str(count),
                          **{metric: "0.25" for metric in metrics}})
        next(case for case in cases if case["name"].startswith("AnalyticEmitter"))["finite_emitter_measurements"] = json.dumps(
            [{"reference": 1.0, "measured": 1.1}] * 288)
        self.case = cases[0]
        self.document = {"failures": 0, "errors": 0, "disabled": 0,
                         "testsuites": [{"name": "LightingGpuAbiTest", "testsuite": cases}]}

    def test_complete_measurements_are_admitted(self):
        validate(self.document)

    def test_approximation_error_is_reported_without_automatic_rejection(self):
        self.case.update(reference_budget_exceedances="1", reference_deviation_details='[{"probe": 1}]',
                         maximum_reference_deviation_scale="100")
        validate(self.document)

    def test_missing_fields_fail_closed(self):
        for key in self.case:
            with self.subTest(key=key):
                document = copy.deepcopy(self.document)
                del document["testsuites"][0]["testsuite"][0][key]
                with self.assertRaises((ValueError, TypeError)):
                    validate(document)

    def test_invalid_measurements_are_rejected(self):
        for value in ("nan", "inf", "-0.01"):
            with self.subTest(value=value):
                self.case["maximum_reference_deviation_scale"] = value
                with self.assertRaises(ValueError):
                    validate(self.document)

    def test_failed_skipped_partial_stale_and_duplicate_results_are_rejected(self):
        for key, value in (("status", "NOTRUN"), ("result", "SKIPPED"), ("probe_count", "1"),
                           ("lighting_model_revision", "1"), ("lighting_measurement_schema", "1"),
                           ("reference_deviation_details", "[{}]"), ("failures", [{"failure": "bad"}])):
            with self.subTest(key=key):
                document = copy.deepcopy(self.document)
                document["testsuites"][0]["testsuite"][0][key] = value
                with self.assertRaises(ValueError):
                    validate(document)
        self.document["testsuites"][0]["testsuite"].append(self.case)
        with self.assertRaises(ValueError):
            validate(self.document)

    def test_every_required_case_is_checked(self):
        for index in range(len(REQUIRED_PROBES) + len(REQUIRED_QUALIFICATIONS)):
            document = copy.deepcopy(self.document)
            document["testsuites"][0]["testsuite"].pop(index)
            with self.assertRaisesRegex(ValueError, "exactly one"):
                validate(document)

    def test_failed_suite_is_rejected(self):
        for field in ("failures", "errors", "disabled"):
            document = copy.deepcopy(self.document)
            document[field] = 1
            with self.assertRaises(ValueError):
                validate(document)

    def test_incomplete_finite_emitter_comparison_is_rejected(self):
        case = next(case for case in self.document["testsuites"][0]["testsuite"]
                    if case["name"].startswith("AnalyticEmitter"))
        case["finite_emitter_measurements"] = "[]"
        with self.assertRaisesRegex(ValueError, "incomplete"):
            validate(self.document)


if __name__ == "__main__":
    unittest.main()
