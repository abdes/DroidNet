import struct

from pakgen.packing.packers import _pack_directional_light_record


def test_pack_directional_light_record_writes_is_sunlight_flag():
    # Arrange
    # DirectionalLightRecord layout (little-endian):
    #   u32 node_index
    #   LightCommonRecord (48 bytes)
    #   f32 angular_size
    #   u32 environment_contribution
    #   u32 is_sun_light   <-- asserted below
    #   u32 cascade_count
    #   f32[4] cascade_distances
    #   f32 distribution_exponent
    #   u8[8] reserved
    light = {
        "node_index": 0,
        "is_sunlight": True,
    }

    # Act
    packed = _pack_directional_light_record(light, node_count=1)

    # Assert
    assert len(packed) == 96

    # Offset to is_sun_light = 4 (node_index) + 48 (common) + 4 (angular) + 4 (env)
    is_sun_light_offset = 60
    (is_sun_light,) = struct.unpack_from("<I", packed, is_sun_light_offset)
    assert is_sun_light == 1
