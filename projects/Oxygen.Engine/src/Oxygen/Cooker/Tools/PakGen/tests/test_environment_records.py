"""Golden scene-v6 environment record layouts, independent of the writer helpers."""

import struct

import pytest

from pakgen.packing.packers import _pack_scene_environment_block
from pakgen.packing.constants import SCENE_ASSET_VERSION_CURRENT


@pytest.mark.parametrize("tone", range(4))
@pytest.mark.parametrize("exposure", range(3))
@pytest.mark.parametrize("metering", range(3))
def test_complete_environment_record_layout(tone, exposure, metering):
    post = {
        "tone_mapper": 1,
        "exposure_mode": 2,
        "exposure_enabled": False,
        "exposure_compensation_ev": 1.25,
        "exposure_key": 8.75,
        "manual_exposure_ev": 11.5,
        "auto_exposure_min_ev": -3.25,
        "auto_exposure_max_ev": 12.75,
        "auto_exposure_speed_up": 5.5,
        "auto_exposure_speed_down": 1.75,
        "auto_exposure_metering_mode": 0,
        "auto_exposure_low_percentile": 0.2,
        "auto_exposure_high_percentile": 0.85,
        "auto_exposure_min_log_luminance": -10.5,
        "auto_exposure_log_luminance_range": 21.25,
        "auto_exposure_target_luminance": 0.27,
        "auto_exposure_spot_meter_radius": 0.35,
        "bloom_intensity": 0.6,
        "bloom_threshold": 2.25,
        "saturation": 0.8,
        "contrast": 1.4,
        "vignette_intensity": 0.3,
        "display_gamma": 2.4,
        "enabled": True,
    }
    post.update(
        tone_mapper=tone, exposure_mode=exposure, auto_exposure_metering_mode=metering
    )
    data = _pack_scene_environment_block(
        {
            "environment": {
                "post_process_volume": post,
                "background": {"enabled": True, "color_rgb": [0.05, 0.25, 0.75]},
            }
        }
    )
    assert SCENE_ASSET_VERSION_CURRENT == 6
    assert struct.unpack_from("<II", data) == (176, 2)
    assert struct.unpack_from("<III", data, 8) == (5, 144, 1)
    assert struct.unpack_from("<II", data, 20) == (tone, exposure)
    assert struct.unpack_from("<f", data, 28)[0] == pytest.approx(1.25)
    assert struct.unpack_from("<f", data, 72)[0] == pytest.approx(8.75)
    assert struct.unpack_from("<f", data, 76)[0] == pytest.approx(11.5)
    assert struct.unpack_from("<f", data, 32)[0] == pytest.approx(-3.25)
    assert struct.unpack_from("<f", data, 36)[0] == pytest.approx(12.75)
    assert struct.unpack_from("<f", data, 40)[0] == pytest.approx(5.5)
    assert struct.unpack_from("<f", data, 44)[0] == pytest.approx(1.75)
    assert struct.unpack_from("<f", data, 84)[0] == pytest.approx(0.2)
    assert struct.unpack_from("<f", data, 88)[0] == pytest.approx(0.85)
    assert struct.unpack_from("<f", data, 92)[0] == pytest.approx(-10.5)
    assert struct.unpack_from("<f", data, 96)[0] == pytest.approx(21.25)
    assert struct.unpack_from("<f", data, 100)[0] == pytest.approx(0.27)
    assert struct.unpack_from("<f", data, 104)[0] == pytest.approx(0.35)
    assert struct.unpack_from("<f", data, 48)[0] == pytest.approx(0.6)
    assert struct.unpack_from("<f", data, 52)[0] == pytest.approx(2.25)
    assert struct.unpack_from("<f", data, 56)[0] == pytest.approx(0.8)
    assert struct.unpack_from("<f", data, 60)[0] == pytest.approx(1.4)
    assert struct.unpack_from("<f", data, 64)[0] == pytest.approx(0.3)
    assert struct.unpack_from("<f", data, 108)[0] == pytest.approx(2.4)
    assert struct.unpack_from("<I", data, 68)[0] == 0
    assert struct.unpack_from("<I", data, 80)[0] == metering
    assert struct.unpack_from("<III", data, 152) == (6, 24, 1)
    assert struct.unpack_from("<3f", data, 164) == pytest.approx((0.05, 0.25, 0.75))
