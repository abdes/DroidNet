"""One-way migration preserves all effective authored values."""

import copy

import pytest

from MigrateSceneV8 import migrate


@pytest.mark.parametrize("old_mode", [False, True])
def test_both_modes_migrate_to_identical_automatic_source(old_mode):
    document = {"$schema": "oxygen.scene-descriptor.v7", "version": 7, "name": "Preserved", "nodes": [{"name": "Root"}],
                "environment": {"sky_light": {"source": 1, "enabled": False,
                    "real_time_capture_enabled": old_mode, "intensity": 3.25,
                    "tint_rgb": [0.2, 0.4, 0.8], "diffuse_intensity": 0.5,
                    "specular_intensity": 1.5, "lower_hemisphere_color": [0.1, 0.2, 0.3],
                    "source_cubemap_angle_radians": 0.75, "affect_reflections": False},
                    "fog": {"visible_in_real_time_sky_captures": False}}}
    original = copy.deepcopy(document)
    expected = copy.deepcopy(document)
    expected["version"] = 8
    expected["$schema"] = "oxygen.scene-descriptor.v8"
    del expected["environment"]["sky_light"]["real_time_capture_enabled"]
    assert migrate(document) == expected
    assert document == original


@pytest.mark.parametrize("version", [None, True, 6, 8, 7.0, "7"])
def test_migration_requires_exact_previous_source_version(version):
    with pytest.raises(ValueError, match="integer version 7"):
        migrate({"version": version})
