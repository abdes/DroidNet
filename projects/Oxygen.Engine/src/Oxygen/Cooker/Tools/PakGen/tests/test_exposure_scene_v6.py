"""Exposure wire layout and invalid inputs in the current scene format."""

import math
import struct
from pathlib import Path

import pytest

from pakgen.packing.errors import PakError
from pakgen.packing.inspector import inspect_pak
from pakgen.spec.validator import run_validation_pipeline
from pakgen.packing.packers import (
    _pack_orthographic_camera_record,
    _pack_perspective_camera_record,
    _pack_post_process_volume_environment_record,
)


@pytest.mark.parametrize(
    "pack,expected_size,physical_offset",
    [(_pack_perspective_camera_record, 32, 20),
     (_pack_orthographic_camera_record, 40, 28)],
)
def test_camera_physical_fields_follow_projection(pack, expected_size, physical_offset):
    record = pack({"node_index": 0, "aperture_f": 2.8, "shutter_rate": 250, "iso": 400}, node_count=1)
    assert len(record) == expected_size
    assert struct.unpack_from("<3f", record, physical_offset) == pytest.approx((2.8, 250, 400))
    defaults = pack({"node_index": 0}, node_count=1)
    assert struct.unpack_from("<3f", defaults, physical_offset) == (11, 125, 100)


@pytest.mark.parametrize("field", ["aperture_f", "shutter_rate", "iso"])
@pytest.mark.parametrize("value", [0, -1, math.nan, math.inf, 1e100, 1e-100, True])
def test_camera_rejects_nonpositive_or_unrepresentable_values(field, value):
    with pytest.raises(PakError):
        _pack_perspective_camera_record({"node_index": 0, field: value}, node_count=1)


def test_exposure_prefix_mask_reserved_words_and_curve_tail():
    record = _pack_post_process_volume_environment_record({
        "auto_exposure_black_influence": 0.25,
        "auto_exposure_transition_distance_ev": 2.5,
        "auto_exposure_metering_mask": "Mask",
        "auto_exposure_compensation_curve": [
            {"metered_ev": -6, "compensation_ev": -1},
            {"metered_ev": 16, "compensation_ev": 1},
        ],
    }, {"Mask": 9})
    assert len(record) == 160
    assert struct.unpack_from("<II", record) == (5, 160)
    assert struct.unpack_from("<I2fI", record, 104) == (1, 0.25, 2.5, 9)
    assert record[120:132] == bytes(12)
    assert struct.unpack_from("<I", record, 132) == (2,)
    assert record[136:144] == bytes(8)
    assert struct.unpack_from("<4f", record, 144) == (-6, -1, 16, 1)


@pytest.mark.parametrize("patch", [
    {"tone_mapper": 4}, {"exposure_mode": True},
    {"auto_exposure_metering_mode": -1}, {"exposure_key": 0},
    {"auto_exposure_min_ev": 20, "auto_exposure_max_ev": 10},
    {"auto_exposure_low_percentile": 0.5, "auto_exposure_high_percentile": 0.5},
    {"auto_exposure_log_luminance_range": 0},
    {"auto_exposure_min_log_luminance": 30, "auto_exposure_log_luminance_range": 4},
    {"auto_exposure_speed_up": -1}, {"auto_exposure_black_influence": 1.1},
    {"auto_exposure_transition_distance_ev": 0}, {"display_gamma": math.nan},
    {"exposure_mode": 0, "manual_exposure_ev": -100},
    {"auto_exposure_metering_mask": "Missing"},
])
def test_invalid_exposure_settings_are_rejected(patch):
    with pytest.raises(PakError):
        _pack_post_process_volume_environment_record(patch)


def test_zero_target_is_valid_and_empty_curve_has_no_tail():
    record = _pack_post_process_volume_environment_record({"auto_exposure_target_luminance": 0})
    assert len(record) == 144
    assert struct.unpack_from("<f", record, 92) == (0,)
    assert struct.unpack_from("<I", record, 132) == (0,)


def test_curve_rejects_duplicate_encoded_ev_and_excess_keys():
    with pytest.raises(PakError, match="strictly increasing"):
        _pack_post_process_volume_environment_record({
            "auto_exposure_compensation_curve": [
                {"metered_ev": 1, "compensation_ev": 0},
                {"metered_ev": 1 + 1e-9, "compensation_ev": 1},
            ],
        })
    with pytest.raises(PakError, match="at most 64"):
        _pack_post_process_volume_environment_record({
            "auto_exposure_compensation_curve": [
                {"metered_ev": i, "compensation_ev": 0} for i in range(65)
            ],
        })


@pytest.mark.parametrize("version", [5, 6, 8, True, "7"])
def test_pakgen_rejects_noncurrent_source_versions(version):
    issues = run_validation_pipeline({
        "source_identity": "01a0a760-498c-7342-9a13-e3303795adb9",
        "version": version, "assets": [],
    })
    assert any(issue.code == "E_VERSION" for issue in issues)


def test_inspector_rejects_obsolete_scene_descriptor(tmp_path):
    golden = Path(__file__).parent / "_golden" / "scene_basic.pak"
    info = inspect_pak(golden)
    scene = next(entry for entry in info["directory_entries"] if entry["asset_type"] == 3)
    data = bytearray(golden.read_bytes())
    data[scene["desc_offset"] + 65] = 5
    obsolete = tmp_path / "obsolete-scene.pak"
    obsolete.write_bytes(data)
    with pytest.raises(ValueError, match="requires descriptor version 7"):
        inspect_pak(obsolete)


def test_file_build_rejects_retired_spec_version(tmp_path):
    import json
    from pakgen.api import BuildOptions, build_pak

    source = tmp_path / "old.json"
    source.write_text(json.dumps({
        "version": 6,
        "source_identity": "01a0a760-498c-7342-9a13-e3303795adb9",
        "assets": [],
    }), encoding="utf-8")
    with pytest.raises(ValueError, match="E_VERSION"):
        build_pak(BuildOptions(input_spec=source, output_path=tmp_path / "old.pak"))
    assert not (tmp_path / "old.pak").exists()


@pytest.mark.parametrize("asset_type, version", [("material", 2), ("geometry", 1), ("scene", 7)])
def test_asset_headers_emit_only_current_descriptor_versions(asset_type, version):
    from pakgen.packing.packers import pack_asset_header

    assert pack_asset_header({"type": asset_type})[65] == version
    with pytest.raises(PakError, match="E_VERSION"):
        pack_asset_header({"type": asset_type, "version": version - 1})
