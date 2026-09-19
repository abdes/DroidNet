"""Offline controls for the native exposure-transition capture oracle."""

import struct
import unittest

from AnalyzeRenderDocExposureTransitions import decode_log_rate, decode_state, response, state_at_slot
from ExposureCaptureReference import trimmed_histogram_log_luminance, target_gain


class ExposureTransitionOracleTest(unittest.TestCase):
    def setUp(self):
        self.requested = (5 << 32) | 7
        self.applied = (4 << 32) | 254
        raw = bytearray(80)
        struct.pack_into("<4f", raw, 0, .25, 1, .25, 1)
        struct.pack_into("<I", raw, 24, 3 | (2 << 10))
        struct.pack_into("<Q", raw, 40, self.requested)
        struct.pack_into("<Q", raw, 48, self.applied)
        self.previous = decode_state(raw)
        self.current = {"gain": .25, "latent_target": 1, "flags": 7 | (2 << 10)}
        self.args = dict(mode=2, policy=0, generation=0, fixed=.25, seed_log=-3,
                         dt=.25, up=1, down=1, distance=1.5)

    def test_trimmed_mass_integrates_partial_bins(self):
        bins = [0] * 256
        bins[0], bins[255] = 3, 1
        # Retaining the middle half leaves exactly two units in bin zero.
        self.assertEqual(trimmed_histogram_log_luminance(bins, .25, .75, -12, 25), -12)
        self.assertEqual(trimmed_histogram_log_luminance(bins, 0.0, 1.0, -12, 25), -5.75)
        self.assertIsNone(trimmed_histogram_log_luminance([0] * 256, 0.0, 1.0, -12, 25))

    def test_target_interpolates_in_stops_with_clamped_endpoints(self):
        keys = [(-6, 6), (16, -16)]
        self.assertEqual(target_gain(keys, -20), 64)
        self.assertEqual(target_gain(keys, 2), .25)
        self.assertEqual(target_gain(keys, 20), 2 ** -16)

    def test_generation_offsets_preserve_both_high_words(self):
        self.assertEqual(self.previous["requested"], self.requested)
        self.assertEqual(self.previous["applied"], self.applied)

    def test_retained_unapplied_request_is_pending(self):
        self.args.update(policy=3, generation=self.requested)
        self.assertEqual(response(self.previous, self.current, **self.args), (.125, "seed"))

    def test_applied_request_does_not_repeat_seed(self):
        self.previous["applied"] = self.requested
        self.args.update(policy=3, generation=self.requested)
        value, method = response(self.previous, self.current, **self.args)
        self.assertEqual(method, "hybrid")
        self.assertAlmostEqual(value, 2 ** -1.75)

    def test_fixed_mode_entries_hold_their_transferred_gain(self):
        for mode in (0, 1, 3):
            with self.subTest(mode=mode):
                previous = dict(self.previous, flags=3 | (mode << 10))
                current = dict(self.current)
                if mode == 3:
                    previous.update(gain=1, latent=1)
                    current["latent_target"] = .25
                self.assertEqual(response(previous, current, **self.args),
                                 (previous["latent"], "mode_entry"))

    def test_following_auto_frame_adapts(self):
        value, method = response(self.previous, self.current, **self.args)
        self.assertEqual(method, "hybrid")
        self.assertAlmostEqual(value, 2 ** -1.75)

    def test_explicit_seed_precedes_mode_entry_and_locked_target(self):
        self.previous["flags"] = 3
        self.args.update(policy=3, generation=self.requested)
        self.assertEqual(response(self.previous, self.current, **self.args,
                                  target_flags=1), (.125, "seed"))

    def test_zero_and_locked_targets_precede_mode_entry(self):
        self.previous["flags"] = 3
        self.assertEqual(response(self.previous, self.current, **self.args,
                                  target_flags=1), (1, "locked_or_restored"))
        self.assertEqual(response(self.previous, self.current, **self.args,
                                  target_flags=2), (0, "zero_target"))

    def test_zero_rate_sentinel_preserves_positive_subnormals(self):
        self.assertEqual(decode_log_rate(-256), 0)
        self.assertEqual(decode_log_rate(-149), 2 ** -149)

    def test_state_roles_use_descriptor_slots_not_enumeration_order(self):
        inputs = [{"binding_index": 0xffff, "array_element": 102, "state": {"gain": .125}},
                  {"binding_index": 0xffff, "array_element": 101, "state": {"gain": .25}}]
        self.assertEqual(state_at_slot(inputs, 101), {"gain": .25})
        self.assertEqual(state_at_slot(inputs, 102), {"gain": .125})
        self.assertIsNone(state_at_slot(inputs, 0xffffffff))
        with self.assertRaises(RuntimeError):
            state_at_slot(inputs, 103)
        with self.assertRaises(RuntimeError):
            state_at_slot(inputs + inputs, 101)

    def test_borrowing_uses_source_even_with_local_fixed_mode(self):
        self.args["mode"] = 0
        self.assertEqual(response(self.previous, self.current, **self.args,
                                  borrowed={"gain": .125}), (.125, "borrowed"))

    def test_source_loss_preserves_displayed_zero_then_honors_seed(self):
        borrowed = {"gain": 0, "latent": .25}
        self.assertEqual(response(self.previous, self.current, **self.args,
                                  borrowed=borrowed, source_loss=True), (0, "source_loss"))
        self.args.update(policy=3, generation=self.requested)
        self.assertEqual(response(self.previous, self.current, **self.args,
                                  borrowed=borrowed, source_loss=True), (.125, "seed"))

    def test_source_loss_honors_locked_target(self):
        self.assertEqual(response(self.previous, self.current, **self.args,
                                  borrowed={"gain": .125}, source_loss=True,
                                  target_flags=1), (1, "locked_or_restored"))


if __name__ == "__main__":
    unittest.main()
