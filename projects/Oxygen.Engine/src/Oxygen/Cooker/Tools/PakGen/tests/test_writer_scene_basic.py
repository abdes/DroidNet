import json
from pathlib import Path
import struct

from pakgen.api import BuildOptions, build_pak
from pakgen.spec.validator import run_validation_pipeline


def _extract_scene_descriptor(data: bytes) -> bytes:
    footer = data[-256:]
    directory_offset, directory_size, asset_count = struct.unpack_from(
        "<QQQ", footer, 0
    )
    entries = data[directory_offset : directory_offset + directory_size]
    for i in range(asset_count):
        e = entries[i * 64 : (i + 1) * 64]
        asset_type = e[16]
        if asset_type != 3:
            continue
        desc_offset = struct.unpack_from("<Q", e, 16 + 1 + 8)[0]
        desc_size = struct.unpack_from("<I", e, 16 + 1 + 8 + 8)[0]
        return data[desc_offset : desc_offset + desc_size]
    raise AssertionError("scene descriptor not found")


def _extract_scene_component_tables(scene_desc: bytes) -> list[dict[str, int]]:
    component_dir_offset = struct.unpack_from("<Q", scene_desc, 127)[0]
    component_count = struct.unpack_from("<I", scene_desc, 135)[0]
    tables = []
    for i in range(component_count):
        off = component_dir_offset + i * 20
        component_type = struct.unpack_from("<I", scene_desc, off)[0]
        table_offset, count, entry_size = struct.unpack_from("<QII", scene_desc, off + 4)
        tables.append(
            {
                "component_type": component_type,
                "table_offset": table_offset,
                "count": count,
                "entry_size": entry_size,
            }
        )
    return tables


def test_build_pak_with_scene_asset(tmp_path: Path):
    spec = {
        "version": 6,
        "content_version": 7,
        "buffers": [],
        "textures": [],
        "audios": [],
        "assets": [
            {
                "type": "material",
                "name": "MatA",
                "asset_key": "11" * 16,
                "alignment": 1,
            },
            {
                "type": "geometry",
                "name": "GeoA",
                "asset_key": "22" * 16,
                "lods": [
                    {
                        "vertex_count": 3,
                        "index_count": 3,
                        "mesh_type": 0,
                        "submeshes": [
                            {
                                "material": "MatA",
                                "bounding_box_min": [0.0, 0.0, 0.0],
                                "bounding_box_max": [1.0, 1.0, 1.0],
                                "mesh_views": [
                                    {
                                        "first_index": 0,
                                        "index_count": 3,
                                        "first_vertex": 0,
                                        "vertex_count": 3,
                                    }
                                ],
                            }
                        ],
                    }
                ],
            },
            {
                "type": "scene",
                "name": "SceneA",
                "asset_key": "33" * 16,
                "nodes": [
                    {"name": "Root", "parent": None},
                    {"name": "Child", "parent": 0},
                ],
                "renderables": [
                    {
                        "node_index": 1,
                        "geometry": "GeoA",
                        "material": "MatA",
                        "visible": True,
                    }
                ],
            },
        ],
    }

    spec_path = tmp_path / "spec.json"
    spec_path.write_text(json.dumps(spec), encoding="utf-8")
    out_path = tmp_path / "out.pak"

    res = build_pak(BuildOptions(input_spec=spec_path, output_path=out_path))
    data = out_path.read_bytes()
    assert res.bytes_written == len(data)

    # Footer: directory metadata at start
    footer = data[-256:]
    directory_offset, directory_size, asset_count = struct.unpack_from(
        "<QQQ", footer, 0
    )
    assert asset_count == 3
    assert directory_offset != 0
    assert directory_size == 3 * 64

    # Directory entry format: AssetKey(16) + asset_type(u8) + entry_off(u64)
    # + desc_off(u64) + desc_size(u32) + reserved(27)
    entries = data[directory_offset : directory_offset + directory_size]
    seen_scene = False
    for i in range(asset_count):
        e = entries[i * 64 : (i + 1) * 64]
        asset_type = e[16]
        desc_size = struct.unpack_from("<I", e, 16 + 1 + 8 + 8)[0]
        if asset_type == 3:
            seen_scene = True
            # Scene descriptor must be at least 256 bytes (fixed header) plus payload
            assert desc_size >= 256
    assert seen_scene

    scene_desc = _extract_scene_descriptor(data)
    assert scene_desc[65] == 3

    tables = _extract_scene_component_tables(scene_desc)
    assert len(tables) == 1
    renderable_table = tables[0]
    assert renderable_table["count"] == 1
    assert renderable_table["entry_size"] == 40
    record_offset = renderable_table["table_offset"]
    assert scene_desc[record_offset + 20 : record_offset + 36] == bytes.fromhex(
        "11" * 16
    )


def test_build_pak_with_v3_environment_and_local_fog_scene_asset(tmp_path: Path):
    spec = {
        "version": 6,
        "content_version": 7,
        "buffers": [],
        "textures": [],
        "audios": [],
        "assets": [
            {
                "type": "scene",
                "name": "SceneEnvironmentV3",
                "asset_key": "33" * 16,
                "version": 3,
                "nodes": [
                    {"name": "Root", "parent": None},
                    {"name": "FogNode", "parent": 0},
                ],
                "environment": {
                    "fog": {
                        "enabled": True,
                        "model": 1,
                        "extinction_sigma_t_per_m": 0.01,
                        "height_falloff_per_m": 0.2,
                        "height_offset_m": 0.0,
                        "start_distance_m": 1.0,
                        "max_opacity": 0.8,
                        "single_scattering_albedo_rgb": [0.9, 0.8, 0.7],
                        "anisotropy_g": 0.25,
                        "enable_height_fog": True,
                        "enable_volumetric_fog": True,
                        "second_fog_density": 0.03,
                        "second_fog_height_falloff": 0.15,
                        "second_fog_height_offset": 10.0,
                        "fog_inscattering_luminance": [1.0, 0.9, 0.8],
                        "sky_atmosphere_ambient_contribution_color_scale": [0.2, 0.3, 0.4],
                        "inscattering_color_cubemap_asset": "44" * 16,
                        "inscattering_color_cubemap_angle": 0.5,
                        "inscattering_texture_tint": [0.7, 0.6, 0.5],
                        "fully_directional_inscattering_color_distance": 100.0,
                        "non_directional_inscattering_color_distance": 50.0,
                        "directional_inscattering_luminance": [0.1, 0.2, 0.3],
                        "directional_inscattering_exponent": 4.0,
                        "directional_inscattering_start_distance": 15.0,
                        "end_distance_m": 2500.0,
                        "fog_cutoff_distance_m": 3000.0,
                        "volumetric_fog_scattering_distribution": 0.6,
                        "volumetric_fog_albedo": [0.5, 0.6, 0.7],
                        "volumetric_fog_emissive": [0.0, 0.1, 0.2],
                        "volumetric_fog_extinction_scale": 1.5,
                        "volumetric_fog_distance": 500.0,
                        "volumetric_fog_start_distance": 3.0,
                        "volumetric_fog_near_fade_in_distance": 4.0,
                        "volumetric_fog_static_lighting_scattering_intensity": 2.0,
                        "override_light_colors_with_fog_inscattering_colors": True,
                        "holdout": False,
                        "render_in_main_pass": True,
                        "visible_in_reflection_captures": True,
                        "visible_in_real_time_sky_captures": False,
                    },
                    "sky_light": {
                        "enabled": True,
                        "source": 1,
                        "cubemap_asset": "55" * 16,
                        "intensity": 1.5,
                        "tint_rgb": [0.8, 0.9, 1.0],
                        "diffuse_intensity": 1.1,
                        "specular_intensity": 1.2,
                        "real_time_capture_enabled": True,
                        "source_cubemap_angle_radians": 0.75,
                        "lower_hemisphere_color": [0.1, 0.2, 0.3],
                        "lower_hemisphere_is_solid_color": False,
                        "lower_hemisphere_blend_alpha": 0.35,
                        "volumetric_scattering_intensity": 0.4,
                        "affect_reflections": True,
                    },
                },
                "local_fog_volumes": [
                    {
                        "node_index": 1,
                        "enabled": True,
                        "radial_fog_extinction": 0.3,
                        "height_fog_extinction": 0.2,
                        "height_fog_falloff": 0.15,
                        "height_fog_offset": 1.25,
                        "fog_phase_g": 0.4,
                        "fog_albedo": [0.7, 0.8, 0.9],
                        "fog_emissive": [0.1, 0.2, 0.3],
                        "sort_priority": 2,
                    }
                ],
            }
        ],
    }

    spec_path = tmp_path / "spec_env.json"
    spec_path.write_text(json.dumps(spec), encoding="utf-8")
    out_path = tmp_path / "out_env.pak"

    build_pak(BuildOptions(input_spec=spec_path, output_path=out_path))
    scene_desc = _extract_scene_descriptor(out_path.read_bytes())

    assert scene_desc[65] == 3
    assert b"LFOG" in scene_desc


def test_scene_validation_rejects_non_v3_scene_asset_version():
    spec = {
        "version": 6,
        "content_version": 7,
        "buffers": [],
        "textures": [],
        "audios": [],
        "assets": [
            {
                "type": "scene",
                "name": "LegacyScene",
                "asset_key": "33" * 16,
                "version": 2,
                "nodes": [{"name": "Root", "parent": None}],
            }
        ],
    }

    errors = run_validation_pipeline(spec)
    assert any(error.code == "E_VERSION" for error in errors)
