"""Fail-closed checks for the native physical-lighting admission gate."""

import copy
import unittest

from AssertLightingPhysicalProbe import PROBE_COUNT, PROBE_NAME, validate


class PhysicalProbeGateTest(unittest.TestCase):
    def setUp(self):
        self.case = {
            "name": PROBE_NAME,
            "status": "RUN",
            "result": "COMPLETED",
            "physical_probe_schema": "1",
            "probe_count": str(PROBE_COUNT),
            "physical_budget_failures": "0",
            "physical_failure_details": "[]",
            "maximum_physical_budget_fraction": "0.25",
        }
        self.document = {
            "failures": 0,
            "errors": 0,
            "disabled": 0,
            "testsuites": [{"name": "LightingGpuAbiTest", "testsuite": [self.case]}],
        }

    def test_complete_passing_probe_is_admitted(self):
        validate(self.document)

    def test_physical_failure_is_rejected_even_when_native_test_passes(self):
        self.case.update(
            physical_budget_failures="1",
            physical_failure_details='[{"probe": 1, "lane": 3}]',
            maximum_physical_budget_fraction="1.1",
        )
        with self.assertRaisesRegex(ValueError, "physical budget FAILED"):
            validate(self.document)

    def test_missing_measurements_fail_closed(self):
        for key in self.case:
            with self.subTest(key=key):
                document = copy.deepcopy(self.document)
                del document["testsuites"][0]["testsuite"][0][key]
                with self.assertRaises((ValueError, TypeError)):
                    validate(document)

    def test_invalid_error_measurements_fail_closed(self):
        for value in ("nan", "inf", "-0.01", "1.001"):
            with self.subTest(value=value):
                self.case["maximum_physical_budget_fraction"] = value
                with self.assertRaises(ValueError):
                    validate(self.document)

    def test_failed_skipped_partial_and_duplicate_results_are_rejected(self):
        for key, value in (
            ("status", "NOTRUN"),
            ("result", "SKIPPED"),
            ("probe_count", "1"),
            ("physical_probe_schema", "2"),
            ("physical_failure_details", "[{}]"),
            ("failures", [{"failure": "probe failed"}]),
        ):
            with self.subTest(key=key):
                document = copy.deepcopy(self.document)
                document["testsuites"][0]["testsuite"][0][key] = value
                with self.assertRaises(ValueError):
                    validate(document)
        self.document["testsuites"][0]["testsuite"].append(self.case)
        with self.assertRaises(ValueError):
            validate(self.document)

    def test_failed_suite_is_not_admitted(self):
        for field in ("failures", "errors", "disabled"):
            with self.subTest(field=field):
                document = copy.deepcopy(self.document)
                document[field] = 1
                with self.assertRaises(ValueError):
                    validate(document)


if __name__ == "__main__":
    unittest.main()
