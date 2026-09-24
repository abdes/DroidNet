"""Scene-v7 light wire contract, independent of native serialization."""

import struct
import pytest

from pakgen.packing.errors import PakError
from pakgen.packing.packers import (
    _pack_directional_light_record, _pack_point_light_record,
    _pack_spot_light_record, pack_asset_header, pack_scene_asset_descriptor_and_payload,
)


def test_retained_directional_fields_are_packed_without_loss():
    record = _pack_directional_light_record({
        "node_index": 1, "color_rgb": [0.25, 0.5, 0.75],
        "affects_world": False, "casts_shadows": True,
        "shadow": {"bias": 0.125, "normal_bias": 0.25,
                   "contact_shadows": True, "resolution_hint": 3},
        "exposure_compensation_ev": -2,
        "angular_size_radians": 0.02, "atmosphere_light_slot": 2,
        "use_per_pixel_atmosphere_transmittance": True,
        "atmosphere_disk_luminance_scale_rgb": [0.5, 1, 2],
        "cascade_count": 3, "cascade_distances": [10, 30, 80, 200],
        "distribution_exponent": 4, "split_mode": 1,
        "max_shadow_distance": 90, "transition_fraction": 0.2,
        "distance_fadeout_fraction": 0.3, "intensity_lux": 12345,
    }, node_count=2)
    assert len(record) == 97
    assert struct.unpack_from("<II3fBffIBf", record) == pytest.approx(
        (1, 0, 0.25, 0.5, 0.75, 1, 0.125, 0.25, 1, 3, -2))
    assert struct.unpack_from("<fBB3fI4ffB4f", record, 38) == pytest.approx(
        (0.02, 2, 1, 0.5, 1, 2, 3, 10, 30, 80, 200, 4, 1, 90, 0.2, 0.3, 12345))


@pytest.mark.parametrize("pack,size", [(_pack_point_light_record, 50), (_pack_spot_light_record, 58)])
def test_local_range_radius_flux_preserved(pack, size):
    record = pack({"range": 4096, "source_radius": 0.25, "luminous_flux_lm": 321}, node_count=1)
    assert len(record) == size
    assert struct.unpack_from("<f", record, 38) == (4096,)
    assert struct.unpack_from("<2f", record, size - 8) == (0.25, 321)


@pytest.mark.parametrize("field", ["mobility", "attenuation_model", "decay_exponent", "environment_contribution", "is_sun_light", "is_sunlight"])
def test_removed_fields_are_rejected(field):
    with pytest.raises(PakError, match="Removed light properties"):
        _pack_directional_light_record({field: 0}, node_count=1)


@pytest.mark.parametrize("light", [
    {"color_rgb": [-1, 1, 1]}, {"intensity_lux": float("inf")},
    {"affects_world": 2}, {"shadow": {"contact_shadows": 2}},
    {"atmosphere_light_slot": 3}, {"exposure_compensation_ev": 200},
    {"cascade_count": 0}, {"cascade_distances": [10, 5, 30, 40]},
    {"atmosphere_disk_luminance_scale_rgb": [1, 1, -1]},
])
def test_invalid_candidate_is_rejected(light):
    with pytest.raises(PakError):
        _pack_directional_light_record(light, node_count=1)


def pack_scene(**lights):
    return pack_scene_asset_descriptor_and_payload(
        {"name": "Lights", "nodes": [{"name": "A"}, {"name": "B"}, {"name": "C"}], **lights},
        header_builder=pack_asset_header, geometry_name_to_key={})


def test_component_directory_matches_actual_light_record_sizes():
    descriptor, payload, _ = pack_scene(
        directional_lights=[{"node_index": 0}], point_lights=[{"node_index": 1}],
        spot_lights=[{"node_index": 2}])
    directory, count = struct.unpack_from("<QI", descriptor, len(descriptor) - 12)
    records = [struct.unpack_from("<IQII", descriptor + payload, directory + i * 20) for i in range(count)]
    assert sorted(record[3] for record in records) == [50, 58, 97]


@pytest.mark.parametrize("slot", [1, 2])
def test_stored_slot_owners_conflict_even_when_disabled(slot):
    with pytest.raises(PakError, match="already owned"):
        pack_scene(directional_lights=[
            {"node_index": 0, "atmosphere_light_slot": slot, "affects_world": False},
            {"node_index": 1, "atmosphere_light_slot": slot}])


def test_only_one_light_type_per_node():
    with pytest.raises(PakError, match="more than one light"):
        pack_scene(point_lights=[{"node_index": 0}], spot_lights=[{"node_index": 0}])
