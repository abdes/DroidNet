import struct

from pakgen.packing.packers import _pack_directional_light_record


def test_pack_directional_light_record_writes_is_sunlight_flag():
    # Arrange
    # DirectionalLightRecord layout (little-endian):
    #   u32 node_index
    #   LightCommonRecord (35 bytes)
    #   f32 angular_size
    #   u32 environment_contribution
    #   u32 is_sun_light   <-- asserted below
    #   u32 cascade_count
    #   f32[4] cascade_distances
    #   f32 distribution_exponent
    #   u8 split_mode
    #   f32 max_shadow_distance
    #   f32 transition_fraction
    #   f32 distance_fadeout_fraction
    #   f32 intensity_lux
    light = {
        "node_index": 0,
        "is_sunlight": True,
    }

    # Act
    packed = _pack_directional_light_record(light, node_count=1)

    # Assert
    assert len(packed) == 92

    # Offset to is_sun_light = 4 (node_index) + 35 (common) + 4 (angular) + 4 (env)
    is_sun_light_offset = 47
    (is_sun_light,) = struct.unpack_from("<I", packed, is_sun_light_offset)
    assert is_sun_light == 1


def test_pack_directional_light_record_derives_manual_max_shadow_distance():
    packed = _pack_directional_light_record(
        {
            "node_index": 0,
            "cascade_distances": [10.0, 30.0, 80.0, 200.0],
        },
        node_count=1,
    )

    assert len(packed) == 92

    split_mode_offset = 75
    (split_mode,) = struct.unpack_from("<B", packed, split_mode_offset)
    assert split_mode == 1

    max_shadow_distance_offset = 76
    (max_shadow_distance,) = struct.unpack_from(
        "<f", packed, max_shadow_distance_offset
    )
    assert max_shadow_distance == 200.0
