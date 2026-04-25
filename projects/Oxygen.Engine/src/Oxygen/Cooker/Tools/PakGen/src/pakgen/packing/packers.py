"""Pure binary packing functions for PakGen (refactored).

All functions are side-effect free and validate sizes.
"""

from __future__ import annotations

import hashlib
import struct
from pathlib import Path
from typing import Any, Dict, Sequence, List, Callable, Tuple

from .constants import (
    MAGIC,
    FOOTER_MAGIC,
    ASSET_HEADER_SIZE,
    ASSET_KEY_SIZE,
    ASSET_TYPE_MAP,
    MATERIAL_DESC_SIZE,
    GEOMETRY_DESC_SIZE,
    SCENE_DESC_SIZE,
    SCRIPT_DESC_SIZE,
    INPUT_ACTION_DESC_SIZE,
    INPUT_MAPPING_CONTEXT_DESC_SIZE,
    MESH_DESC_SIZE,
    SUBMESH_DESC_SIZE,
    MESH_VIEW_DESC_SIZE,
    DIRECTORY_ENTRY_SIZE,
    FOOTER_SIZE,
    SHADER_REF_DESC_SIZE,
    SCENE_ASSET_VERSION_CURRENT,
    PHYSICS_RESOURCE_DESC_SIZE,
    PHYSICS_MATERIAL_ASSET_DESC_SIZE,
    COLLISION_SHAPE_ASSET_DESC_SIZE,
    PHYSICS_SCENE_DESC_SIZE,
    RIGID_BODY_BINDING_RECORD_SIZE,
    COLLIDER_BINDING_RECORD_SIZE,
    CHARACTER_BINDING_RECORD_SIZE,
    SOFT_BODY_BINDING_RECORD_SIZE,
    JOINT_BINDING_RECORD_SIZE,
    VEHICLE_BINDING_RECORD_SIZE,
    VEHICLE_WHEEL_BINDING_RECORD_SIZE,
    AGGREGATE_BINDING_RECORD_SIZE,
    SCRIPT_SLOT_RECORD_SIZE,
)
from .errors import PakError
from ..utils.io import DataError, read_data_from_spec

__all__ = [
    "pack_header",
    "pack_footer",
    "pack_asset_header",
    "pack_script_param_record",
    "pack_directory_entry",
    "pack_material_asset_descriptor",
    "pack_buffer_resource_descriptor",
    "pack_texture_resource_descriptor",
    "pack_audio_resource_descriptor",
    "pack_script_resource_descriptor",
    "pack_script_asset_descriptor",
    "pack_input_action_asset_descriptor",
    "pack_input_mapping_context_asset_descriptor_and_payload",
    "pack_script_slot_record",
    "pack_geometry_asset_descriptor",
    "pack_scene_asset_descriptor_and_payload",
    "pack_mesh_view_descriptor",
    "pack_physics_resource_descriptor",
    "pack_physics_material_asset_descriptor",
    "pack_collision_shape_asset_descriptor",
    "pack_physics_scene_asset_descriptor_and_payload",
    "pack_rigid_body_binding_record",
    "pack_collider_binding_record",
    "pack_character_binding_record",
    "pack_soft_body_binding_record",
    "pack_joint_binding_record",
    "pack_vehicle_binding_record",
    "pack_aggregate_binding_record",
    "resolve_procedural_params_blob",
]


_COMPONENT_TYPE_RENDERABLE = 0x4853454D  # 'MESH'
_COMPONENT_TYPE_PERSPECTIVE_CAMERA = 0x4D414350  # 'PCAM'
_COMPONENT_TYPE_ORTHOGRAPHIC_CAMERA = 0x4D41434F  # 'OCAM'
_COMPONENT_TYPE_DIRECTIONAL_LIGHT = 0x54494C44  # 'DLIT'
_COMPONENT_TYPE_POINT_LIGHT = 0x54494C50  # 'PLIT'
_COMPONENT_TYPE_SPOT_LIGHT = 0x54494C53  # 'SLIT'
_COMPONENT_TYPE_LOCAL_FOG_VOLUME = 0x474F464C  # 'LFOG'
_COMPONENT_TYPE_SCRIPTING = 0x50524353  # 'SCRP'

_ENV_SYSTEM_SKY_ATMOSPHERE = 0
_ENV_SYSTEM_VOLUMETRIC_CLOUDS = 1
_ENV_SYSTEM_FOG = 2
_ENV_SYSTEM_SKY_LIGHT = 3
_ENV_SYSTEM_SKY_SPHERE = 4
_ENV_SYSTEM_POST_PROCESS_VOLUME = 5

_SCRIPT_PARAM_BOOL = 1
_SCRIPT_PARAM_INT32 = 2
_SCRIPT_PARAM_FLOAT = 3
_SCRIPT_PARAM_STRING = 4
_SCRIPT_PARAM_VEC2 = 5
_SCRIPT_PARAM_VEC3 = 6
_SCRIPT_PARAM_VEC4 = 7

_PHYSICS_BINDING_RIGID_BODY = 0x59485052  # 'RPHY'
_PHYSICS_BINDING_COLLIDER = 0x4C4F4350  # 'PCOL'
_PHYSICS_BINDING_CHARACTER = 0x52484350  # 'PCHR'
_PHYSICS_BINDING_SOFT_BODY = 0x42534650  # 'PFSB'
_PHYSICS_BINDING_JOINT = 0x544E4A50  # 'PJNT'
_PHYSICS_BINDING_VEHICLE = 0x4C485650  # 'PVHL'
_PHYSICS_BINDING_AGGREGATE = 0x47474150  # 'PAGG'


def _c_string_bytes(value: str, max_bytes_without_nul: int, field: str) -> bytes:
    encoded = value.encode("utf-8")
    if len(encoded) > max_bytes_without_nul:
        raise PakError(
            "E_RANGE",
            f"{field} exceeds {max_bytes_without_nul} UTF-8 bytes",
        )
    return encoded + b"\x00" + (b"\x00" * (max_bytes_without_nul - len(encoded)))


def _normalize_sha256_digest(value: Any, *, field: str) -> bytes:
    if isinstance(value, (bytes, bytearray)):
        digest = bytes(value)
        if len(digest) != 32:
            raise PakError("E_SIZE", f"{field} bytes must be 32 bytes")
        return digest
    if isinstance(value, str):
        cleaned = value.strip()
        if cleaned.startswith("0x") or cleaned.startswith("0X"):
            cleaned = cleaned[2:]
        if len(cleaned) != 64:
            raise PakError("E_SIZE", f"{field} hex must be 64 characters")
        try:
            return bytes.fromhex(cleaned)
        except ValueError as exc:
            raise PakError("E_TYPE", f"{field} hex is invalid") from exc
    if isinstance(value, int):
        # Integer form is interpreted as legacy low 64-bit prefix.
        return struct.pack("<Q", value & 0xFFFFFFFFFFFFFFFF) + (b"\x00" * 24)
    if value is None:
        return b"\x00" * 32
    raise PakError(
        "E_TYPE",
        f"{field} must be 32-byte bytes or 64-char hex string",
    )


def pack_asset_header(asset_dict: Dict[str, Any]) -> bytes:
    name = asset_dict.get("name", "")
    type_name = asset_dict.get("type")
    asset_type = ASSET_TYPE_MAP.get(type_name, 0)
    version = asset_dict.get("version", 1)
    streaming_priority = asset_dict.get("streaming_priority", 0)
    content_hash = _normalize_sha256_digest(
        asset_dict.get("content_hash", 0), field="AssetHeader.content_hash"
    )
    variant_flags = asset_dict.get("variant_flags", 0)
    name_bytes = pack_name_string(name, 64)
    header = (
        struct.pack("<B", asset_type)
        + name_bytes
        + struct.pack("<B", version)
        + struct.pack("<B", streaming_priority)
        + content_hash
        + struct.pack("<I", variant_flags)
    )
    if len(header) != ASSET_HEADER_SIZE:
        raise PakError(
            "E_SIZE",
            f"AssetHeader size mismatch: expected {ASSET_HEADER_SIZE} got {len(header)}",
        )
    return header


def _pack_script_param_payload(param_type: int, value: Any) -> bytes:
    if param_type == _SCRIPT_PARAM_BOOL:
        payload = struct.pack("<?", bool(value))
        return payload + (b"\x00" * (60 - len(payload)))
    if param_type == _SCRIPT_PARAM_INT32:
        payload = struct.pack("<i", int(value))
        return payload + (b"\x00" * (60 - len(payload)))
    if param_type == _SCRIPT_PARAM_FLOAT:
        payload = struct.pack("<f", float(value))
        return payload + (b"\x00" * (60 - len(payload)))
    if param_type == _SCRIPT_PARAM_STRING:
        if not isinstance(value, str):
            raise PakError("E_TYPE", "script param string value must be a string")
        return _c_string_bytes(value, 59, "script param string value")
    if param_type in (_SCRIPT_PARAM_VEC2, _SCRIPT_PARAM_VEC3, _SCRIPT_PARAM_VEC4):
        count = {
            _SCRIPT_PARAM_VEC2: 2,
            _SCRIPT_PARAM_VEC3: 3,
            _SCRIPT_PARAM_VEC4: 4,
        }[param_type]
        if not isinstance(value, (list, tuple)) or len(value) != count:
            raise PakError(
                "E_TYPE",
                f"script param vector must have {count} numeric elements",
            )
        payload = struct.pack(f"<{count}f", *[float(v) for v in value])
        return payload + (b"\x00" * (60 - len(payload)))
    raise PakError("E_RANGE", f"unsupported script param type: {param_type}")


def pack_script_param_record(param: Dict[str, Any]) -> bytes:
    """Pack ScriptParamRecord (128 bytes) using v5 fixed-size C union layout."""
    if not isinstance(param, dict):
        raise PakError("E_TYPE", "script param entry must be an object")

    key = param.get("key", param.get("name", ""))
    if not isinstance(key, str):
        raise PakError("E_TYPE", "script param key must be a string")
    key_bytes = _c_string_bytes(key, 63, "script param key")

    explicit_type = param.get("type")
    value = param.get("value")
    if isinstance(explicit_type, str):
        t = explicit_type.strip().lower()
        type_map = {
            "bool": _SCRIPT_PARAM_BOOL,
            "int": _SCRIPT_PARAM_INT32,
            "int32": _SCRIPT_PARAM_INT32,
            "float": _SCRIPT_PARAM_FLOAT,
            "string": _SCRIPT_PARAM_STRING,
            "vec2": _SCRIPT_PARAM_VEC2,
            "vec3": _SCRIPT_PARAM_VEC3,
            "vec4": _SCRIPT_PARAM_VEC4,
        }
        if t not in type_map:
            raise PakError("E_RANGE", f"unsupported script param type: {explicit_type}")
        param_type = type_map[t]
    else:
        if isinstance(value, bool):
            param_type = _SCRIPT_PARAM_BOOL
        elif isinstance(value, int):
            param_type = _SCRIPT_PARAM_INT32
        elif isinstance(value, float):
            param_type = _SCRIPT_PARAM_FLOAT
        elif isinstance(value, str):
            param_type = _SCRIPT_PARAM_STRING
        elif isinstance(value, (list, tuple)) and len(value) in (2, 3, 4):
            param_type = {
                2: _SCRIPT_PARAM_VEC2,
                3: _SCRIPT_PARAM_VEC3,
                4: _SCRIPT_PARAM_VEC4,
            }[len(value)]
        else:
            raise PakError(
                "E_TYPE",
                "cannot infer script param type from value; set explicit 'type'",
            )

    payload = _pack_script_param_payload(param_type, value)
    # Fixed layout in v7: key[64], type(u32), union payload[60].
    record = struct.pack("<64sI60s", key_bytes, int(param_type), payload)
    if len(record) != 128:
        raise PakError("E_SIZE", f"ScriptParamRecord size mismatch: {len(record)}")
    return record


def _pack_asset_key_bytes(value: Any, field: str) -> bytes:
    if isinstance(value, (bytes, bytearray)):
        data = bytes(value)
        if len(data) != 16:
            raise PakError("E_SIZE", f"{field} must be 16 bytes")
        return data
    if isinstance(value, str):
        cleaned = value.replace("-", "").strip()
        if len(cleaned) != 32:
            raise PakError("E_SIZE", f"{field} must be 32 hex chars")
        try:
            return bytes.fromhex(cleaned)
        except ValueError as exc:
            raise PakError("E_TYPE", f"{field} is not valid hex") from exc
    raise PakError("E_TYPE", f"{field} must be bytes or hex string")


def _derive_physics_resource_asset_key(name: str) -> bytes:
    if not isinstance(name, str) or not name:
        raise PakError("E_TYPE", "physics resource name must be a non-empty string")
    digest = hashlib.sha256(name.encode("utf-8")).digest()
    return digest[:ASSET_KEY_SIZE]


def pack_script_slot_record(
    *,
    script_asset_key: bytes,
    params_array_offset: int = 0,
    params_count: int = 0,
    execution_order: int = 0,
    flags: int = 0,
) -> bytes:
    if len(script_asset_key) != 16:
        raise PakError("E_SIZE", "script_asset_key must be 16 bytes")
    out = (
        script_asset_key
        + struct.pack("<QIiI", int(params_array_offset), int(params_count), int(execution_order), int(flags))
    )
    if len(out) != SCRIPT_SLOT_RECORD_SIZE:
        raise PakError("E_SIZE", f"ScriptSlotRecord size mismatch: {len(out)}")
    return out


def _vec3(vals: Any, default: list[float]) -> tuple[float, float, float]:
    if not isinstance(vals, list) or len(vals) != 3:
        return float(default[0]), float(default[1]), float(default[2])
    return float(vals[0]), float(vals[1]), float(vals[2])


def _u32_bool(value: Any, default: int = 0) -> int:
    if value is None:
        return int(default)
    return 1 if bool(value) else 0


def _u8(value: Any, default: int = 0) -> int:
    if value is None:
        return int(default) & 0xFF
    return int(value) & 0xFF


def _f(value: Any, default: float = 0.0) -> float:
    if value is None:
        return float(default)
    return float(value)


def _pack_light_shadow_settings_record(shadow: Dict[str, Any] | None) -> bytes:
    shadow = shadow or {}
    bias = _f(shadow.get("bias"), 0.0)
    normal_bias = _f(shadow.get("normal_bias"), 0.0)
    contact_shadows = _u32_bool(shadow.get("contact_shadows"), 0)
    resolution_hint = _u8(shadow.get("resolution_hint"), 1)
    out = (
        struct.pack("<ffI", bias, normal_bias, int(contact_shadows))
        + struct.pack("<B", int(resolution_hint))
    )
    if len(out) != 13:
        raise PakError(
            "E_SIZE", f"LightShadowSettingsRecord size mismatch: {len(out)}"
        )
    return out


def _pack_light_common_record(light: Dict[str, Any]) -> bytes:
    affects_world = _u32_bool(light.get("affects_world"), 1)
    color = light.get("color_rgb", light.get("color", [1.0, 1.0, 1.0]))
    cr, cg, cb = _vec3(color, [1.0, 1.0, 1.0])
    mobility = _u8(light.get("mobility"), 0)
    casts_shadows = _u8(light.get("casts_shadows"), 0)
    shadow = _pack_light_shadow_settings_record(light.get("shadow"))
    exposure_comp = _f(light.get("exposure_compensation_ev"), 0.0)

    out = (
        struct.pack("<I3f", int(affects_world), cr, cg, cb)
        + struct.pack("<BB", int(mobility), int(casts_shadows))
        + shadow
        + struct.pack("<f", exposure_comp)
    )
    if len(out) != 35:
        raise PakError("E_SIZE", f"LightCommonRecord size mismatch: {len(out)}")
    return out


def _pack_directional_light_record(
    light: Dict[str, Any], *, node_count: int
) -> bytes:
    node_index = light.get("node_index", 0)
    if (
        not isinstance(node_index, int)
        or node_index < 0
        or node_index >= node_count
    ):
        raise PakError(
            "E_REF", f"DirectionalLight node_index out of range: {node_index}"
        )

    common = _pack_light_common_record(light)
    angular_size = _f(light.get("angular_size_radians"), 0.0)
    env_contrib = _u32_bool(light.get("environment_contribution"), 0)
    is_sun_light = _u32_bool(
        light.get(
            "is_sun_light",
            light.get("is_sunlight", light.get("IsSunLight")),
        ),
        0,
    )
    intensity_lux = _f(light.get("intensity_lux"), 100000.0)
    cascade_count = int(light.get("cascade_count", 4) or 0)
    if cascade_count < 0 or cascade_count > 4:
        raise PakError(
            "E_RANGE",
            f"DirectionalLight cascade_count out of range: {cascade_count}",
        )
    distances = light.get("cascade_distances", [0.0, 0.0, 0.0, 0.0])
    if not isinstance(distances, list) or len(distances) != 4:
        distances = [0.0, 0.0, 0.0, 0.0]
    cascade_distances = [float(d) for d in distances]
    split_mode = int(light.get("split_mode", 1 if "cascade_distances" in light else 0) or 0)
    if split_mode < 0 or split_mode > 1:
        raise PakError(
            "E_RANGE",
            f"DirectionalLight split_mode out of range: {split_mode}",
        )
    max_shadow_distance_default = (
        float(cascade_distances[3])
        if "max_shadow_distance" not in light and split_mode == 1
        else 160.0
    )
    max_shadow_distance = _f(
        light.get("max_shadow_distance"), max_shadow_distance_default
    )
    distribution = _f(light.get("distribution_exponent"), 3.0)
    transition_fraction = _f(light.get("transition_fraction"), 0.1)
    distance_fadeout_fraction = _f(light.get("distance_fadeout_fraction"), 0.1)

    out = (
        struct.pack("<I", int(node_index))
        + common
        + struct.pack("<f", angular_size)
        + struct.pack("<I", int(env_contrib))
        + struct.pack("<I", int(is_sun_light))
        + struct.pack("<I", int(cascade_count))
        + struct.pack("<4f", *cascade_distances)
        + struct.pack("<f", distribution)
        + struct.pack("<B", int(split_mode))
        + struct.pack("<f", max_shadow_distance)
        + struct.pack("<f", transition_fraction)
        + struct.pack("<f", distance_fadeout_fraction)
        + struct.pack("<f", intensity_lux)
    )
    if len(out) != 92:
        raise PakError(
            "E_SIZE", f"DirectionalLightRecord size mismatch: {len(out)}"
        )
    return out


def _pack_point_light_record(
    light: Dict[str, Any], *, node_count: int
) -> bytes:
    node_index = light.get("node_index", 0)
    if (
        not isinstance(node_index, int)
        or node_index < 0
        or node_index >= node_count
    ):
        raise PakError(
            "E_REF", f"PointLight node_index out of range: {node_index}"
        )

    common = _pack_light_common_record(light)
    rng = _f(light.get("range"), 10.0)
    attenuation_model = _u8(light.get("attenuation_model"), 0)
    decay = _f(light.get("decay_exponent"), 2.0)
    source_radius = _f(light.get("source_radius"), 0.0)
    luminous_flux_lm = _f(light.get("luminous_flux_lm"), 800.0)

    out = (
        struct.pack("<I", int(node_index))
        + common
        + struct.pack("<f", rng)
        + struct.pack("<f", decay)
        + struct.pack("<f", source_radius)
        + struct.pack("<f", luminous_flux_lm)
        + struct.pack("<B", int(attenuation_model))
    )
    if len(out) != 56:
        raise PakError("E_SIZE", f"PointLightRecord size mismatch: {len(out)}")
    return out


def _pack_spot_light_record(light: Dict[str, Any], *, node_count: int) -> bytes:
    node_index = light.get("node_index", 0)
    if (
        not isinstance(node_index, int)
        or node_index < 0
        or node_index >= node_count
    ):
        raise PakError(
            "E_REF", f"SpotLight node_index out of range: {node_index}"
        )

    common = _pack_light_common_record(light)
    rng = _f(light.get("range"), 10.0)
    attenuation_model = _u8(light.get("attenuation_model"), 0)
    decay = _f(light.get("decay_exponent"), 2.0)
    inner = _f(light.get("inner_cone_angle_radians"), 0.4)
    outer = _f(light.get("outer_cone_angle_radians"), 0.6)
    source_radius = _f(light.get("source_radius"), 0.0)
    luminous_flux_lm = _f(light.get("luminous_flux_lm"), 800.0)

    out = (
        struct.pack("<I", int(node_index))
        + common
        + struct.pack("<f", rng)
        + struct.pack("<f", decay)
        + struct.pack("<f", inner)
        + struct.pack("<f", outer)
        + struct.pack("<f", source_radius)
        + struct.pack("<f", luminous_flux_lm)
        + struct.pack("<B", int(attenuation_model))
    )
    if len(out) != 64:
        raise PakError("E_SIZE", f"SpotLightRecord size mismatch: {len(out)}")
    return out


def _pack_env_record_header(system_type: int, record_size: int) -> bytes:
    return struct.pack("<II", int(system_type), int(record_size))


def _pack_sky_atmosphere_environment_record(spec: Dict[str, Any]) -> bytes:
    enabled = _u32_bool(spec.get("enabled"), 1)
    planet_radius_m = _f(spec.get("planet_radius_m"), 6360000.0)
    atmosphere_height_m = _f(spec.get("atmosphere_height_m"), 80000.0)
    ga = spec.get("ground_albedo_rgb", [0.1, 0.1, 0.1])
    ground_albedo = _vec3(ga, [0.1, 0.1, 0.1])
    rs = spec.get("rayleigh_scattering_rgb", [5.8e-6, 13.5e-6, 33.1e-6])
    rayleigh = _vec3(rs, [5.8e-6, 13.5e-6, 33.1e-6])
    rayleigh_scale = _f(spec.get("rayleigh_scale_height_m"), 8000.0)
    ms = spec.get("mie_scattering_rgb", [21.0e-6, 21.0e-6, 21.0e-6])
    mie = _vec3(ms, [21.0e-6, 21.0e-6, 21.0e-6])
    ma = spec.get("mie_absorption_rgb", [0.0, 0.0, 0.0])
    mie_absorption = _vec3(ma, [0.0, 0.0, 0.0])
    mie_scale = _f(spec.get("mie_scale_height_m"), 1200.0)
    mie_g = _f(spec.get("mie_anisotropy"), 0.8)
    ab = spec.get("ozone_absorption_rgb", [0.0, 0.0, 0.0])
    absorption = _vec3(ab, [0.0, 0.0, 0.0])
    ozone_density = _vec3(
        spec.get("ozone_density_profile", [25000.0, 0.0, 0.0]),
        [25000.0, 0.0, 0.0],
    )
    multi_scattering = _f(spec.get("multi_scattering_factor"), 1.0)
    sky_luminance = _vec3(
        spec.get("sky_luminance_factor_rgb", [1.0, 1.0, 1.0]),
        [1.0, 1.0, 1.0],
    )
    sky_aerial_luminance = _vec3(
        spec.get(
            "sky_and_aerial_perspective_luminance_factor_rgb",
            [1.0, 1.0, 1.0],
        ),
        [1.0, 1.0, 1.0],
    )
    aerial_scale = _f(spec.get("aerial_perspective_distance_scale"), 1.0)
    aerial_strength = _f(spec.get("aerial_scattering_strength"), 1.0)
    aerial_start_depth = _f(spec.get("aerial_perspective_start_depth_m"), 0.0)
    height_fog_contribution = _f(spec.get("height_fog_contribution"), 1.0)
    trace_sample_count_scale = _f(spec.get("trace_sample_count_scale"), 1.0)
    transmittance_min_light_elevation_deg = _f(
        spec.get("transmittance_min_light_elevation_deg"), -6.0
    )
    sun_disk_enabled = _u32_bool(spec.get("sun_disk_enabled"), 1)
    holdout = _u32_bool(spec.get("holdout"), 0)
    render_in_main_pass = _u32_bool(spec.get("render_in_main_pass"), 1)

    record_size = 168
    out = (
        _pack_env_record_header(_ENV_SYSTEM_SKY_ATMOSPHERE, record_size)
        + struct.pack("<I", int(enabled))
        + struct.pack("<ff", planet_radius_m, atmosphere_height_m)
        + struct.pack("<3f", *ground_albedo)
        + struct.pack("<3f", *rayleigh)
        + struct.pack("<f", rayleigh_scale)
        + struct.pack("<3f", *mie)
        + struct.pack("<3f", *mie_absorption)
        + struct.pack("<f", mie_scale)
        + struct.pack("<f", mie_g)
        + struct.pack("<3f", *absorption)
        + struct.pack("<3f", *ozone_density)
        + struct.pack("<f", multi_scattering)
        + struct.pack("<3f", *sky_luminance)
        + struct.pack("<3f", *sky_aerial_luminance)
        + struct.pack("<f", aerial_scale)
        + struct.pack("<f", aerial_strength)
        + struct.pack("<f", aerial_start_depth)
        + struct.pack("<f", height_fog_contribution)
        + struct.pack("<f", trace_sample_count_scale)
        + struct.pack("<f", transmittance_min_light_elevation_deg)
        + struct.pack("<I", int(sun_disk_enabled))
        + struct.pack("<I", int(holdout))
        + struct.pack("<I", int(render_in_main_pass))
    )
    if len(out) != record_size:
        raise PakError(
            "E_SIZE",
            f"SkyAtmosphereEnvironmentRecord size mismatch: {len(out)}",
        )
    return out


def _pack_volumetric_clouds_environment_record(spec: Dict[str, Any]) -> bytes:
    enabled = _u32_bool(spec.get("enabled"), 1)
    base_altitude = _f(spec.get("base_altitude_m"), 1500.0)
    thickness = _f(spec.get("layer_thickness_m"), 4000.0)
    coverage = _f(spec.get("coverage"), 0.5)
    extinction_sigma_t_per_m = _f(spec.get("extinction_sigma_t_per_m"), 1.0e-3)
    albedo = _vec3(
        spec.get("single_scattering_albedo_rgb", [0.9, 0.9, 0.9]),
        [0.9, 0.9, 0.9],
    )
    phase_g = _f(spec.get("phase_g"), 0.6)
    wind_dir = _vec3(spec.get("wind_dir_ws", [1.0, 0.0, 0.0]), [1.0, 0.0, 0.0])
    wind_speed = _f(spec.get("wind_speed_mps"), 10.0)
    shadow_strength = _f(spec.get("shadow_strength"), 0.8)

    record_size = 64
    out = (
        _pack_env_record_header(_ENV_SYSTEM_VOLUMETRIC_CLOUDS, record_size)
        + struct.pack("<I", int(enabled))
        + struct.pack("<ff", base_altitude, thickness)
        + struct.pack("<ff", coverage, extinction_sigma_t_per_m)
        + struct.pack("<3f", *albedo)
        + struct.pack("<f", phase_g)
        + struct.pack("<3f", *wind_dir)
        + struct.pack("<f", wind_speed)
        + struct.pack("<f", shadow_strength)
    )
    if len(out) != record_size:
        raise PakError(
            "E_SIZE",
            f"VolumetricCloudsEnvironmentRecord size mismatch: {len(out)}",
        )
    return out


def _pack_sky_light_environment_record(spec: Dict[str, Any]) -> bytes:
    enabled = _u32_bool(spec.get("enabled"), 1)
    source = int(spec.get("source", 0) or 0)
    cubemap = _asset_key_bytes(spec.get("cubemap_asset"))
    intensity = _f(spec.get("intensity"), 1.0)
    tint = _vec3(spec.get("tint_rgb", [1.0, 1.0, 1.0]), [1.0, 1.0, 1.0])
    diffuse = _f(spec.get("diffuse_intensity"), 1.0)
    specular = _f(spec.get("specular_intensity"), 1.0)
    real_time_capture_enabled = _u32_bool(
        spec.get("real_time_capture_enabled"), 0
    )
    lower_hemisphere_color = _vec3(
        spec.get("lower_hemisphere_color", [0.0, 0.0, 0.0]),
        [0.0, 0.0, 0.0],
    )
    volumetric_scattering_intensity = _f(
        spec.get("volumetric_scattering_intensity"), 1.0
    )
    affect_reflections = _u32_bool(spec.get("affect_reflections"), 1)
    affect_global_illumination = _u32_bool(
        spec.get("affect_global_illumination"), 1
    )

    record_size = 84
    out = (
        _pack_env_record_header(_ENV_SYSTEM_SKY_LIGHT, record_size)
        + struct.pack("<I", int(enabled))
        + struct.pack("<I", source)
        + cubemap
        + struct.pack("<f", intensity)
        + struct.pack("<3f", *tint)
        + struct.pack("<f", diffuse)
        + struct.pack("<f", specular)
        + struct.pack("<I", int(real_time_capture_enabled))
        + struct.pack("<3f", *lower_hemisphere_color)
        + struct.pack("<f", volumetric_scattering_intensity)
        + struct.pack("<I", int(affect_reflections))
        + struct.pack("<I", int(affect_global_illumination))
    )
    if len(out) != record_size:
        raise PakError(
            "E_SIZE", f"SkyLightEnvironmentRecord size mismatch: {len(out)}"
        )
    return out


def _pack_sky_sphere_environment_record(spec: Dict[str, Any]) -> bytes:
    enabled = _u32_bool(spec.get("enabled"), 1)
    source = int(spec.get("source", 0) or 0)
    cubemap = _asset_key_bytes(spec.get("cubemap_asset"))
    solid_color = _vec3(
        spec.get("solid_color_rgb", [0.0, 0.0, 0.0]), [0.0, 0.0, 0.0]
    )
    intensity = _f(spec.get("intensity"), 1.0)
    rotation = _f(spec.get("rotation_radians"), 0.0)
    tint = _vec3(spec.get("tint_rgb", [1.0, 1.0, 1.0]), [1.0, 1.0, 1.0])

    record_size = 64
    out = (
        _pack_env_record_header(_ENV_SYSTEM_SKY_SPHERE, record_size)
        + struct.pack("<I", int(enabled))
        + struct.pack("<I", source)
        + cubemap
        + struct.pack("<3f", *solid_color)
        + struct.pack("<f", intensity)
        + struct.pack("<f", rotation)
        + struct.pack("<3f", *tint)
    )
    if len(out) != record_size:
        raise PakError(
            "E_SIZE", f"SkySphereEnvironmentRecord size mismatch: {len(out)}"
        )
    return out


def _pack_post_process_volume_environment_record(spec: Dict[str, Any]) -> bytes:
    enabled = _u32_bool(spec.get("enabled"), 1)
    tone_mapper = int(spec.get("tone_mapper", 0) or 0)
    exposure_mode = int(spec.get("exposure_mode", 1) or 0)
    exposure_comp = _f(spec.get("exposure_compensation_ev"), 0.0)
    ae_min = _f(spec.get("auto_exposure_min_ev"), -6.0)
    ae_max = _f(spec.get("auto_exposure_max_ev"), 16.0)
    ae_up = _f(spec.get("auto_exposure_speed_up"), 3.0)
    ae_down = _f(spec.get("auto_exposure_speed_down"), 1.0)
    bloom_intensity = _f(spec.get("bloom_intensity"), 0.0)
    bloom_threshold = _f(spec.get("bloom_threshold"), 1.0)
    saturation = _f(spec.get("saturation"), 1.0)
    contrast = _f(spec.get("contrast"), 1.0)
    vignette = _f(spec.get("vignette_intensity"), 0.0)

    record_size = 60
    out = (
        _pack_env_record_header(_ENV_SYSTEM_POST_PROCESS_VOLUME, record_size)
        + struct.pack("<I", int(enabled))
        + struct.pack("<II", tone_mapper, exposure_mode)
        + struct.pack("<f", exposure_comp)
        + struct.pack("<ffff", ae_min, ae_max, ae_up, ae_down)
        + struct.pack("<ff", bloom_intensity, bloom_threshold)
        + struct.pack("<fff", saturation, contrast, vignette)
    )
    if len(out) != record_size:
        raise PakError(
            "E_SIZE",
            f"PostProcessVolumeEnvironmentRecord size mismatch: {len(out)}",
        )
    return out


def _pack_fog_environment_record(spec: Dict[str, Any]) -> bytes:
    enabled = _u32_bool(spec.get("enabled"), 1)
    model = int(spec.get("model", 0) or 0)
    extinction_sigma_t_per_m = _f(spec.get("extinction_sigma_t_per_m"), 0.01)
    height_falloff_per_m = _f(spec.get("height_falloff_per_m"), 0.2)
    height_offset_m = _f(spec.get("height_offset_m"), 0.0)
    start_distance_m = _f(spec.get("start_distance_m"), 0.0)
    max_opacity = _f(spec.get("max_opacity"), 1.0)
    albedo = _vec3(
        spec.get("single_scattering_albedo_rgb", [1.0, 1.0, 1.0]),
        [1.0, 1.0, 1.0],
    )
    anisotropy_g = _f(spec.get("anisotropy_g"), 0.0)
    enable_height_fog = _u32_bool(spec.get("enable_height_fog"), 1)
    enable_volumetric_fog = _u32_bool(spec.get("enable_volumetric_fog"), 0)
    second_fog_density = _f(spec.get("second_fog_density"), 0.0)
    second_fog_height_falloff = _f(spec.get("second_fog_height_falloff"), 0.0)
    second_fog_height_offset = _f(spec.get("second_fog_height_offset"), 0.0)
    fog_inscattering = _vec3(
        spec.get("fog_inscattering_luminance", [1.0, 1.0, 1.0]),
        [1.0, 1.0, 1.0],
    )
    ambient_scale = _vec3(
        spec.get(
            "sky_atmosphere_ambient_contribution_color_scale",
            [1.0, 1.0, 1.0],
        ),
        [1.0, 1.0, 1.0],
    )
    cubemap_asset = _asset_key_bytes(spec.get("inscattering_color_cubemap_asset"))
    cubemap_angle = _f(spec.get("inscattering_color_cubemap_angle"), 0.0)
    cubemap_tint = _vec3(
        spec.get("inscattering_texture_tint", [1.0, 1.0, 1.0]),
        [1.0, 1.0, 1.0],
    )
    fully_directional = _f(
        spec.get("fully_directional_inscattering_color_distance"), 0.0
    )
    non_directional = _f(
        spec.get("non_directional_inscattering_color_distance"), 0.0
    )
    directional_luminance = _vec3(
        spec.get("directional_inscattering_luminance", [1.0, 1.0, 1.0]),
        [1.0, 1.0, 1.0],
    )
    directional_exponent = _f(
        spec.get("directional_inscattering_exponent"), 0.0
    )
    directional_start = _f(
        spec.get("directional_inscattering_start_distance"), 0.0
    )
    end_distance_m = _f(spec.get("end_distance_m"), 0.0)
    fog_cutoff_distance_m = _f(spec.get("fog_cutoff_distance_m"), 0.0)
    volumetric_distribution = _f(
        spec.get("volumetric_fog_scattering_distribution"), 0.0
    )
    volumetric_albedo = _vec3(
        spec.get("volumetric_fog_albedo", [1.0, 1.0, 1.0]),
        [1.0, 1.0, 1.0],
    )
    volumetric_emissive = _vec3(
        spec.get("volumetric_fog_emissive", [0.0, 0.0, 0.0]),
        [0.0, 0.0, 0.0],
    )
    volumetric_extinction_scale = _f(
        spec.get("volumetric_fog_extinction_scale"), 1.0
    )
    volumetric_distance = _f(spec.get("volumetric_fog_distance"), 0.0)
    volumetric_start_distance = _f(
        spec.get("volumetric_fog_start_distance"), 0.0
    )
    volumetric_near_fade = _f(
        spec.get("volumetric_fog_near_fade_in_distance"), 0.0
    )
    volumetric_static_lighting = _f(
        spec.get("volumetric_fog_static_lighting_scattering_intensity"), 1.0
    )
    override_light_colors = _u32_bool(
        spec.get("override_light_colors_with_fog_inscattering_colors"), 0
    )
    holdout = _u32_bool(spec.get("holdout"), 0)
    render_in_main_pass = _u32_bool(spec.get("render_in_main_pass"), 1)
    visible_in_reflection_captures = _u32_bool(
        spec.get("visible_in_reflection_captures"), 1
    )
    visible_in_real_time_sky_captures = _u32_bool(
        spec.get("visible_in_real_time_sky_captures"), 1
    )

    record_size = 232
    out = (
        _pack_env_record_header(_ENV_SYSTEM_FOG, record_size)
        + struct.pack("<I", int(enabled))
        + struct.pack("<I", model)
        + struct.pack("<f", extinction_sigma_t_per_m)
        + struct.pack("<f", height_falloff_per_m)
        + struct.pack("<f", height_offset_m)
        + struct.pack("<f", start_distance_m)
        + struct.pack("<f", max_opacity)
        + struct.pack("<3f", *albedo)
        + struct.pack("<f", anisotropy_g)
        + struct.pack("<I", int(enable_height_fog))
        + struct.pack("<I", int(enable_volumetric_fog))
        + struct.pack("<f", second_fog_density)
        + struct.pack("<f", second_fog_height_falloff)
        + struct.pack("<f", second_fog_height_offset)
        + struct.pack("<3f", *fog_inscattering)
        + struct.pack("<3f", *ambient_scale)
        + cubemap_asset
        + struct.pack("<f", cubemap_angle)
        + struct.pack("<3f", *cubemap_tint)
        + struct.pack("<f", fully_directional)
        + struct.pack("<f", non_directional)
        + struct.pack("<3f", *directional_luminance)
        + struct.pack("<f", directional_exponent)
        + struct.pack("<f", directional_start)
        + struct.pack("<f", end_distance_m)
        + struct.pack("<f", fog_cutoff_distance_m)
        + struct.pack("<f", volumetric_distribution)
        + struct.pack("<3f", *volumetric_albedo)
        + struct.pack("<3f", *volumetric_emissive)
        + struct.pack("<f", volumetric_extinction_scale)
        + struct.pack("<f", volumetric_distance)
        + struct.pack("<f", volumetric_start_distance)
        + struct.pack("<f", volumetric_near_fade)
        + struct.pack("<f", volumetric_static_lighting)
        + struct.pack("<I", int(override_light_colors))
        + struct.pack("<I", int(holdout))
        + struct.pack("<I", int(render_in_main_pass))
        + struct.pack("<I", int(visible_in_reflection_captures))
        + struct.pack("<I", int(visible_in_real_time_sky_captures))
    )
    if len(out) != record_size:
        raise PakError(
            "E_SIZE",
            f"FogEnvironmentRecord size mismatch: {len(out)}",
        )
    return out


def _pack_scene_environment_block(scene: Dict[str, Any]) -> bytes:
    env = scene.get("environment")
    if env is None:
        env = {}
    if not isinstance(env, dict):
        raise PakError("E_TYPE", "scene.environment must be an object")

    records: list[bytes] = []
    sky_atmosphere = env.get("sky_atmosphere")
    if isinstance(sky_atmosphere, dict):
        records.append(_pack_sky_atmosphere_environment_record(sky_atmosphere))
    volumetric_clouds = env.get("volumetric_clouds")
    if isinstance(volumetric_clouds, dict):
        records.append(
            _pack_volumetric_clouds_environment_record(volumetric_clouds)
        )
    fog = env.get("fog")
    if isinstance(fog, dict):
        records.append(_pack_fog_environment_record(fog))
    sky_light = env.get("sky_light")
    if isinstance(sky_light, dict):
        records.append(_pack_sky_light_environment_record(sky_light))
    sky_sphere = env.get("sky_sphere")
    if isinstance(sky_sphere, dict):
        records.append(_pack_sky_sphere_environment_record(sky_sphere))
    post_process = env.get("post_process_volume")
    if isinstance(post_process, dict):
        records.append(
            _pack_post_process_volume_environment_record(post_process)
        )

    records.sort(key=lambda b: struct.unpack_from("<I", b, 0)[0])
    systems_count = len(records)
    byte_size = 8 + sum(len(r) for r in records)
    header = struct.pack("<II", int(byte_size), int(systems_count))
    if len(header) != 8:
        raise PakError(
            "E_SIZE",
            f"SceneEnvironmentBlockHeader size mismatch: {len(header)}",
        )
    return header + b"".join(records)


def _pack_local_fog_volume_record(
    volume: Dict[str, Any], *, node_count: int
) -> bytes:
    node_index = volume.get("node_index", 0)
    if (
        not isinstance(node_index, int)
        or node_index < 0
        or node_index >= node_count
    ):
        raise PakError(
            "E_REF", f"LocalFogVolume node_index out of range: {node_index}"
        )

    enabled = _u32_bool(volume.get("enabled"), 1)
    radial_fog_extinction = _f(volume.get("radial_fog_extinction"), 1.0)
    height_fog_extinction = _f(volume.get("height_fog_extinction"), 1.0)
    height_fog_falloff = _f(volume.get("height_fog_falloff"), 1000.0)
    height_fog_offset = _f(volume.get("height_fog_offset"), 0.0)
    fog_phase_g = _f(volume.get("fog_phase_g"), 0.2)
    fog_albedo = _vec3(volume.get("fog_albedo", [1.0, 1.0, 1.0]), [1.0, 1.0, 1.0])
    fog_emissive = _vec3(
        volume.get("fog_emissive", [0.0, 0.0, 0.0]), [0.0, 0.0, 0.0]
    )
    sort_priority = int(volume.get("sort_priority", 0) or 0)

    out = (
        struct.pack("<I", int(node_index))
        + struct.pack("<I", int(enabled))
        + struct.pack("<f", radial_fog_extinction)
        + struct.pack("<f", height_fog_extinction)
        + struct.pack("<f", height_fog_falloff)
        + struct.pack("<f", height_fog_offset)
        + struct.pack("<f", fog_phase_g)
        + struct.pack("<3f", *fog_albedo)
        + struct.pack("<3f", *fog_emissive)
        + struct.pack("<i", sort_priority)
    )
    if len(out) != 56:
        raise PakError(
            "E_SIZE", f"LocalFogVolumeRecord size mismatch: {len(out)}"
        )
    return out


def _asset_key_bytes(value: Any) -> bytes:
    if isinstance(value, (bytes, bytearray)) and len(value) == ASSET_KEY_SIZE:
        return bytes(value)
    if isinstance(value, str):
        cleaned = value.replace("-", "").strip()
        if len(cleaned) == 32:
            try:
                return bytes.fromhex(cleaned)
            except ValueError:
                return b"\x00" * ASSET_KEY_SIZE
    return b"\x00" * ASSET_KEY_SIZE


def resolve_procedural_params_blob(
    lod: Dict[str, Any], base_dir: Path
) -> bytes:
    """Resolve procedural mesh parameter blob bytes for one geometry LOD.

    Supported authoring forms:
    - `procedural_params: {data_hex|file|path|data|size}`
    - flat aliases on the LOD object:
      `procedural_params_data_hex`, `procedural_params_file`,
      `procedural_params_path`, `procedural_params_data`
    """
    params_spec = lod.get("procedural_params")

    if params_spec is None:
        flat_map = {
            "data_hex": "procedural_params_data_hex",
            "file": "procedural_params_file",
            "path": "procedural_params_path",
            "data": "procedural_params_data",
            "size": "procedural_params_size",
        }
        flat_spec: Dict[str, Any] = {}
        for target_key, source_key in flat_map.items():
            if source_key in lod:
                flat_spec[target_key] = lod.get(source_key)
        if flat_spec:
            params_spec = flat_spec

    if params_spec is None:
        return b""
    if not isinstance(params_spec, dict):
        raise PakError("E_TYPE", "lod.procedural_params must be an object")

    try:
        blob = read_data_from_spec(params_spec, base_dir)
    except DataError as exc:
        raise PakError(
            "E_SPEC", f"invalid procedural_params payload: {exc}"
        ) from exc

    declared_size_raw = lod.get("procedural_params_size")
    if declared_size_raw is not None:
        declared_size = int(declared_size_raw)
        if declared_size < 0:
            raise PakError("E_RANGE", "procedural_params_size must be >= 0")
        if declared_size != len(blob):
            raise PakError(
                "E_SIZE",
                "procedural_params_size does not match authored "
                f"blob length ({declared_size} != {len(blob)})",
            )

    return blob


def _pack_scene_string_table(nodes: List[Dict[str, Any]]):
    offsets: Dict[str, int] = {"": 0}
    buf = bytearray(b"\x00")
    for node in nodes:
        name = node.get("name", "")
        if not isinstance(name, str):
            name = ""
        if name not in offsets:
            offsets[name] = len(buf)
            buf.extend(name.encode("utf-8"))
            buf.append(0)
    return bytes(buf), offsets


def _pack_node_record(
    node: Dict[str, Any], *, index: int, name_offset: int, node_count: int
) -> bytes:
    node_id = _asset_key_bytes(node.get("node_id"))
    parent = node.get("parent")
    if parent is None:
        parent_index = index
    else:
        if not isinstance(parent, int) or parent < 0 or parent >= node_count:
            raise PakError("E_REF", f"Invalid node parent index: {parent}")
        parent_index = parent
    node_flags = int(node.get("flags", 0) or 0)
    t = node.get("translation", [0.0, 0.0, 0.0])
    r = node.get("rotation", [0.0, 0.0, 0.0, 1.0])
    s = node.get("scale", [1.0, 1.0, 1.0])

    def _vec(vals: Any, n: int, default: List[float]) -> List[float]:
        if not isinstance(vals, list) or len(vals) != n:
            return default
        out: List[float] = []
        for v in vals:
            out.append(float(v))
        return out

    t3 = _vec(t, 3, [0.0, 0.0, 0.0])
    r4 = _vec(r, 4, [0.0, 0.0, 0.0, 1.0])
    s3 = _vec(s, 3, [1.0, 1.0, 1.0])

    # NodeRecord (PakFormat.h): AssetKey(16) + name_offset(u32) + parent(u32)
    # + flags(u32) + translation(3f) + rotation(4f) + scale(3f) = 68 bytes.
    out = (
        node_id
        + struct.pack("<I", int(name_offset))
        + struct.pack("<I", int(parent_index))
        + struct.pack("<I", int(node_flags))
        + struct.pack("<3f", *t3)
        + struct.pack("<4f", *r4)
        + struct.pack("<3f", *s3)
    )
    if len(out) != 68:
        raise PakError("E_SIZE", f"NodeRecord size mismatch: {len(out)}")
    return out


def _pack_renderable_record(
    renderable: Dict[str, Any],
    geometry_name_to_key: Dict[str, bytes],
    material_name_to_key: Dict[str, bytes],
    *,
    node_count: int,
) -> bytes:
    node_index = renderable.get("node_index", 0)
    if (
        not isinstance(node_index, int)
        or node_index < 0
        or node_index >= node_count
    ):
        raise PakError(
            "E_REF", f"Renderable node_index out of range: {node_index}"
        )

    geom_key = renderable.get("geometry_asset_key")
    geometry_key = _asset_key_bytes(geom_key)
    if geometry_key == b"\x00" * ASSET_KEY_SIZE and geom_key is None:
        geom_name = renderable.get("geometry")
        if isinstance(geom_name, str) and geom_name in geometry_name_to_key:
            geometry_key = geometry_name_to_key[geom_name]
    if geometry_key == b"\x00" * ASSET_KEY_SIZE:
        raise PakError("E_REF", "Renderable missing geometry reference")

    material_key = _asset_key_bytes(
        renderable.get("material_asset_key", renderable.get("material_key"))
    )
    if material_key == b"\x00" * ASSET_KEY_SIZE:
        material_name = renderable.get(
            "material",
            renderable.get("material_override", renderable.get("material_asset")),
        )
        if isinstance(material_name, str) and material_name in material_name_to_key:
            material_key = material_name_to_key[material_name]

    visible = renderable.get("visible", 1)
    visible_u32 = 1 if bool(visible) else 0
    out = (
        struct.pack("<I", int(node_index))
        + geometry_key
        + material_key
        + struct.pack("<I", int(visible_u32))
    )
    if len(out) != 40:
        raise PakError("E_SIZE", f"RenderableRecord size mismatch: {len(out)}")
    return out


def _pack_perspective_camera_record(
    camera: Dict[str, Any],
    *,
    node_count: int,
) -> bytes:
    node_index = camera.get("node_index", 0)
    if (
        not isinstance(node_index, int)
        or node_index < 0
        or node_index >= node_count
    ):
        raise PakError("E_REF", f"Camera node_index out of range: {node_index}")

    fov_y = float(camera.get("fov_y", 0.785398))
    aspect_ratio = float(camera.get("aspect_ratio", 1.777778))
    near_plane = float(camera.get("near_plane", 0.1))
    far_plane = float(camera.get("far_plane", 1000.0))
    out = (
        struct.pack("<I", int(node_index))
        + struct.pack("<4f", fov_y, aspect_ratio, near_plane, far_plane)
    )
    if len(out) != 20:
        raise PakError(
            "E_SIZE", f"PerspectiveCameraRecord size mismatch: {len(out)}"
        )
    return out


def _pack_orthographic_camera_record(
    camera: Dict[str, Any],
    *,
    node_count: int,
) -> bytes:
    node_index = camera.get("node_index", 0)
    if (
        not isinstance(node_index, int)
        or node_index < 0
        or node_index >= node_count
    ):
        raise PakError("E_REF", f"Camera node_index out of range: {node_index}")

    left = float(camera.get("left", -10.0))
    right = float(camera.get("right", 10.0))
    bottom = float(camera.get("bottom", -10.0))
    top = float(camera.get("top", 10.0))
    near_plane = float(camera.get("near_plane", -100.0))
    far_plane = float(camera.get("far_plane", 100.0))
    out = (
        struct.pack("<I", int(node_index))
        + struct.pack("<6f", left, right, bottom, top, near_plane, far_plane)
    )
    if len(out) != 28:
        raise PakError(
            "E_SIZE", f"OrthographicCameraRecord size mismatch: {len(out)}"
        )
    return out


def pack_scene_asset_descriptor_and_payload(
    scene: Dict[str, Any],
    *,
    header_builder,
    geometry_name_to_key: Dict[str, bytes],
    material_name_to_key: Dict[str, bytes] | None = None,
    script_name_to_key: Dict[str, bytes] | None = None,
    scripting_slot_base_index: int = 0,
) -> Tuple[bytes, bytes, List[Dict[str, Any]]]:
    """Pack SceneAssetDesc plus trailing payload.

    Payload layout (offsets are relative to descriptor start):
    - NodeRecord[]
    - scene string table (starts with NUL)
    - SceneComponentTableDesc[] directory (optional)
    - component table record data (optional)

    Supported component tables:
    - RenderableRecord table (component_type 'MESH')
    - LocalFogVolumeRecord table (component_type 'LFOG')
    - PerspectiveCameraRecord table (component_type 'PCAM')
    - OrthographicCameraRecord table (component_type 'OCAM')
    - DirectionalLightRecord table (component_type 'DLIT')
    - PointLightRecord table (component_type 'PLIT')
    - SpotLightRecord table (component_type 'SLIT')

    The payload always includes a trailing SceneEnvironment block (empty allowed).
    """
    nodes = scene.get("nodes", []) or []
    if not isinstance(nodes, list):
        raise PakError("E_TYPE", "scene.nodes must be a list")
    nodes = [n for n in nodes if isinstance(n, dict)]
    if len(nodes) == 0:
        raise PakError("E_COUNT", "scene must have at least one node")

    scene_version = scene.get("version")
    if scene_version is not None and int(scene_version) != SCENE_ASSET_VERSION_CURRENT:
        raise PakError(
            "E_VERSION",
            "Scene asset version 3 is required; re-cook authored scene content",
        )

    string_table, name_to_offset = _pack_scene_string_table(nodes)
    node_count = len(nodes)
    node_records = b"".join(
        _pack_node_record(
            n,
            index=i,
            name_offset=name_to_offset.get(n.get("name", ""), 0),
            node_count=node_count,
        )
        for i, n in enumerate(nodes)
    )

    renderables = scene.get("renderables", []) or []
    if not isinstance(renderables, list):
        raise PakError("E_TYPE", "scene.renderables must be a list")
    renderables = [r for r in renderables if isinstance(r, dict)]
    renderables.sort(key=lambda r: int(r.get("node_index", 0) or 0))
    material_name_to_key = material_name_to_key or {}
    renderable_records = b"".join(
        _pack_renderable_record(
            r,
            geometry_name_to_key,
            material_name_to_key,
            node_count=node_count,
        )
        for r in renderables
    )

    local_fog_volumes = scene.get("local_fog_volumes", []) or []
    if not isinstance(local_fog_volumes, list):
        raise PakError("E_TYPE", "scene.local_fog_volumes must be a list")
    local_fog_volumes = [v for v in local_fog_volumes if isinstance(v, dict)]
    local_fog_volumes.sort(key=lambda v: int(v.get("node_index", 0) or 0))
    local_fog_volume_records = b"".join(
        _pack_local_fog_volume_record(v, node_count=node_count)
        for v in local_fog_volumes
    )

    cameras = scene.get("perspective_cameras", []) or []
    if not isinstance(cameras, list):
        raise PakError("E_TYPE", "scene.perspective_cameras must be a list")
    cameras = [c for c in cameras if isinstance(c, dict)]
    cameras.sort(key=lambda c: int(c.get("node_index", 0) or 0))
    camera_records = b"".join(
        _pack_perspective_camera_record(c, node_count=node_count)
        for c in cameras
    )

    ortho_cameras = scene.get("orthographic_cameras", []) or []
    if not isinstance(ortho_cameras, list):
        raise PakError("E_TYPE", "scene.orthographic_cameras must be a list")
    ortho_cameras = [c for c in ortho_cameras if isinstance(c, dict)]
    ortho_cameras.sort(key=lambda c: int(c.get("node_index", 0) or 0))
    ortho_camera_records = b"".join(
        _pack_orthographic_camera_record(c, node_count=node_count)
        for c in ortho_cameras
    )

    directional_lights = scene.get("directional_lights", []) or []
    if not isinstance(directional_lights, list):
        raise PakError("E_TYPE", "scene.directional_lights must be a list")
    directional_lights = [l for l in directional_lights if isinstance(l, dict)]
    directional_lights.sort(key=lambda l: int(l.get("node_index", 0) or 0))
    directional_light_records = b"".join(
        _pack_directional_light_record(l, node_count=node_count)
        for l in directional_lights
    )

    point_lights = scene.get("point_lights", []) or []
    if not isinstance(point_lights, list):
        raise PakError("E_TYPE", "scene.point_lights must be a list")
    point_lights = [l for l in point_lights if isinstance(l, dict)]
    point_lights.sort(key=lambda l: int(l.get("node_index", 0) or 0))
    point_light_records = b"".join(
        _pack_point_light_record(l, node_count=node_count) for l in point_lights
    )

    spot_lights = scene.get("spot_lights", []) or []
    if not isinstance(spot_lights, list):
        raise PakError("E_TYPE", "scene.spot_lights must be a list")
    spot_lights = [l for l in spot_lights if isinstance(l, dict)]
    spot_lights.sort(key=lambda l: int(l.get("node_index", 0) or 0))
    spot_light_records = b"".join(
        _pack_spot_light_record(l, node_count=node_count) for l in spot_lights
    )

    # Offsets (relative to descriptor start)
    nodes_offset = SCENE_DESC_SIZE
    nodes_bytes = len(node_records)
    strings_offset = nodes_offset + nodes_bytes
    strings_size = len(string_table)

    component_tables: List[Tuple[int, int, int, bytes]] = []
    if renderable_records:
        component_tables.append(
            (
                _COMPONENT_TYPE_RENDERABLE,
                len(renderables),
                40,
                renderable_records,
            )
        )
    if local_fog_volume_records:
        component_tables.append(
            (
                _COMPONENT_TYPE_LOCAL_FOG_VOLUME,
                len(local_fog_volumes),
                56,
                local_fog_volume_records,
            )
        )
    if camera_records:
        component_tables.append(
            (
                _COMPONENT_TYPE_PERSPECTIVE_CAMERA,
                len(cameras),
                20,
                camera_records,
            )
        )
    if ortho_camera_records:
        component_tables.append(
            (
                _COMPONENT_TYPE_ORTHOGRAPHIC_CAMERA,
                len(ortho_cameras),
                28,
                ortho_camera_records,
            )
        )
    if directional_light_records:
        component_tables.append(
            (
                _COMPONENT_TYPE_DIRECTIONAL_LIGHT,
                len(directional_lights),
                92,
                directional_light_records,
            )
        )
    if point_light_records:
        component_tables.append(
            (
                _COMPONENT_TYPE_POINT_LIGHT,
                len(point_lights),
                56,
                point_light_records,
            )
        )
    if spot_light_records:
        component_tables.append(
            (
                _COMPONENT_TYPE_SPOT_LIGHT,
                len(spot_lights),
                64,
                spot_light_records,
            )
        )

    script_name_to_key = script_name_to_key or {}
    scripting = scene.get("scripting", []) or []
    if not isinstance(scripting, list):
        raise PakError("E_TYPE", "scene.scripting must be a list")
    scripting = [c for c in scripting if isinstance(c, dict)]
    scripting.sort(key=lambda c: int(c.get("node_index", 0) or 0))

    slot_temps: List[Dict[str, Any]] = []
    scripting_records = bytearray()
    local_slot_cursor = 0
    for comp in scripting:
        node_index = int(comp.get("node_index", 0) or 0)
        if node_index < 0 or node_index >= node_count:
            raise PakError(
                "E_REF", f"Scripting component node_index out of range: {node_index}"
            )
        comp_flags = int(comp.get("flags", 0) or 0)
        slots = comp.get("slots", []) or []
        if not isinstance(slots, list):
            raise PakError("E_TYPE", "scripting.slots must be a list")
        slots = [s for s in slots if isinstance(s, dict)]
        slots.sort(key=lambda s: int(s.get("execution_order", 0) or 0))
        local_start = local_slot_cursor
        for slot in slots:
            key_value = slot.get("script_asset_key")
            if key_value is None:
                key_value = slot.get("script_asset")
            if key_value is None:
                key_value = slot.get("script")
            if isinstance(key_value, str) and key_value in script_name_to_key:
                key_bytes = script_name_to_key[key_value]
            else:
                key_bytes = _pack_asset_key_bytes(key_value, "script_asset_key")
            params = slot.get("params", []) or []
            if not isinstance(params, list):
                raise PakError("E_TYPE", "script slot params must be a list")
            params = [p for p in params if isinstance(p, dict)]
            params_blob = b"".join(pack_script_param_record(p) for p in params)
            slot_temps.append(
                {
                    "script_asset_key": key_bytes,
                    "params_blob": params_blob,
                    "params_count": len(params),
                    "execution_order": int(slot.get("execution_order", 0) or 0),
                    "flags": int(slot.get("flags", 0) or 0),
                }
            )
            local_slot_cursor += 1
        scripting_records.extend(
            struct.pack(
                "<IIII",
                int(node_index),
                int(comp_flags),
                int(scripting_slot_base_index + local_start),
                int(len(slots)),
            )
        )

    if scripting_records:
        component_tables.append(
            (
                _COMPONENT_TYPE_SCRIPTING,
                int(len(scripting_records) // 16),
                16,
                bytes(scripting_records),
            )
        )

    component_tables.sort(key=lambda t: t[0])

    component_entries: List[bytes] = []
    component_data: List[bytes] = []
    if component_tables:
        component_dir_offset = strings_offset + strings_size
        component_dir_size = 20 * len(component_tables)
        table_data_cursor = component_dir_offset + component_dir_size
        for component_type, count, entry_size, blob in component_tables:
            component_entries.append(
                struct.pack("<I", int(component_type))
                + struct.pack(
                    "<QII",
                    int(table_data_cursor),
                    int(count),
                    int(entry_size),
                )
            )
            component_data.append(blob)
            table_data_cursor += len(blob)
        component_table_directory_offset = component_dir_offset
        component_table_count = len(component_tables)
    else:
        component_table_directory_offset = 0
        component_table_count = 0

    # SceneAssetDesc
    scene.setdefault("type", "scene")
    # Scene descriptor version. Mirrors pak::kSceneAssetVersion.
    scene.setdefault("version", SCENE_ASSET_VERSION_CURRENT)
    header = header_builder(scene)
    nodes_table = struct.pack("<QII", nodes_offset, node_count, 68)
    scene_strings = struct.pack("<II", strings_offset, strings_size)
    dir_off = struct.pack("<Q", int(component_table_directory_offset))
    dir_count = struct.pack("<I", int(component_table_count))

    desc = header + nodes_table + scene_strings + dir_off + dir_count
    if len(desc) != SCENE_DESC_SIZE:
        raise PakError(
            "E_SIZE",
            f"Scene descriptor size mismatch: expected {SCENE_DESC_SIZE}, got {len(desc)}",
        )

    payload_core = (
        node_records
        + string_table
        + b"".join(component_entries)
        + b"".join(component_data)
    )

    payload_core += _pack_scene_environment_block(scene)

    params_payload = bytearray()
    params_base = SCENE_DESC_SIZE + len(payload_core)
    slot_infos: List[Dict[str, Any]] = []
    for temp in slot_temps:
        params_blob = temp["params_blob"]
        params_count = int(temp["params_count"])
        if params_count > 0:
            params_rel_off = int(params_base + len(params_payload))
            params_payload.extend(params_blob)
        else:
            params_rel_off = 0
        slot_infos.append(
            {
                "script_asset_key": temp["script_asset_key"],
                "params_relative_offset": params_rel_off,
                "params_count": params_count,
                "execution_order": int(temp["execution_order"]),
                "flags": int(temp["flags"]),
            }
        )

    payload = payload_core + bytes(params_payload)
    return desc, payload, slot_infos


def pack_header(version: int, content_version: int, guid: bytes) -> bytes:
    if len(guid) != 16:
        raise PakError("E_SIZE", f"GUID size mismatch: {len(guid)}")
    if guid == b"\x00" * 16:
        raise PakError("E_GUID", "PAK header GUID must be non-zero")
    reserved = b"\x00" * 228
    data = struct.pack(
        "<8sHH16s228s", MAGIC, version, content_version, guid, reserved
    )
    if len(data) != 256:
        raise PakError("E_SIZE", f"Header size mismatch: {len(data)}")
    return data


def pack_footer(
    *,
    directory_offset: int,
    directory_size: int,
    asset_count: int,
    texture_region: Sequence[int],
    buffer_region: Sequence[int],
    audio_region: Sequence[int],
    script_region: Sequence[int],
    physics_region: Sequence[int],
    texture_table: Sequence[int],
    buffer_table: Sequence[int],
    audio_table: Sequence[int],
    script_resource_table: Sequence[int],
    script_slot_table: Sequence[int],
    physics_resource_table: Sequence[int],
    browse_index_offset: int = 0,
    browse_index_size: int = 0,
    pak_crc32: int = 0,
) -> bytes:
    def pack_region(region: Sequence[int]) -> bytes:
        off, size = region
        return struct.pack("<QQ", off, size)

    def pack_table(table: Sequence[int]) -> bytes:
        off, count, entry_size = table
        return struct.pack("<QII", off, count, entry_size)

    reserved = b"\x00" * 28
    footer = (
        struct.pack("<QQQ", directory_offset, directory_size, asset_count)
        + pack_region(texture_region)
        + pack_region(buffer_region)
        + pack_region(audio_region)
        + pack_region(script_region)
        + pack_region(physics_region)
        + pack_table(texture_table)
        + pack_table(buffer_table)
        + pack_table(audio_table)
        + pack_table(script_resource_table)
        + pack_table(script_slot_table)
        + pack_table(physics_resource_table)
        + struct.pack("<QQ", browse_index_offset, browse_index_size)
        + reserved
        + struct.pack("<I", pak_crc32)
        + FOOTER_MAGIC
    )
    if len(footer) != 256:
        raise PakError("E_SIZE", f"Footer size mismatch: {len(footer)}")
    return footer


def pack_directory_entry(
    *,
    asset_key: bytes,
    asset_type: int,
    entry_offset: int,
    desc_offset: int,
    desc_size: int,
) -> bytes:
    if len(asset_key) != ASSET_KEY_SIZE:
        raise PakError(
            "E_KEY_SIZE",
            f"Asset key must be {ASSET_KEY_SIZE} bytes (got {len(asset_key)})",
        )
    reserved = b"\x00" * 27
    data = (
        asset_key
        + struct.pack("<BQQI", asset_type, entry_offset, desc_offset, desc_size)
        + reserved
    )
    if len(data) != 64:
        raise PakError("E_SIZE", f"Directory entry size mismatch: {len(data)}")
    return data


def pack_material_asset_descriptor(
    asset: Dict[str, Any],
    resource_index_map: Dict[str, Dict[str, int]],
    *,
    header_builder,
    shader_refs_builder=None,
) -> bytes:
    """Pack fixed MaterialAssetDesc (no trailing shader refs).

    Shader reference entries (ShaderReferenceDesc) are emitted separately as a
    variable-length blob immediately following the fixed descriptor. The
    planner accounts for their total size (`variable_extra_size`). This keeps
    the base material descriptor layout stable while allowing a flexible
    number of shader stages.
    """

    def to_unorm16(value: float) -> int:
        clamped = 0.0 if value < 0.0 else (1.0 if value > 1.0 else float(value))
        scaled = clamped * 65535.0 + 0.5
        if scaled <= 0.0:
            return 0
        if scaled >= 65535.0:
            return 65535
        return int(scaled)

    def pack_f16(value: float) -> bytes:
        # Half precision float. Python's struct supports 'e'.
        return struct.pack("<e", float(value))

    def pack_f16x3(values: List[float]) -> bytes:
        v = list(values) if values is not None else [0.0, 0.0, 0.0]
        while len(v) < 3:
            v.append(0.0)
        v = v[:3]
        return b"".join(pack_f16(x) for x in v)

    material_domain = int(asset.get("material_domain", 0))
    flags = int(asset.get("flags", 0))
    shader_stages = int(asset.get("shader_stages", 0))

    base_color = asset.get("base_color", [1.0, 1.0, 1.0, 1.0])
    if not isinstance(base_color, list) or len(base_color) != 4:
        raise PakError("E_SPEC", "base_color must be a list of 4 floats")

    normal_scale = float(asset.get("normal_scale", 1.0))
    metalness = float(asset.get("metalness", 0.0))
    roughness = float(asset.get("roughness", 1.0))
    ambient_occlusion = float(asset.get("ambient_occlusion", 1.0))

    texture_refs = asset.get("texture_refs", {})
    texture_map = resource_index_map.get("texture", {})

    def get_texture_index(field: str) -> int:
        ref = texture_refs.get(field)
        return texture_map.get(ref, 0) if ref else 0

    # Core PBR slots.
    base_color_texture = get_texture_index("base_color_texture")
    normal_texture = get_texture_index("normal_texture")
    metallic_texture = get_texture_index("metallic_texture")
    roughness_texture = get_texture_index("roughness_texture")
    ambient_occlusion_texture = get_texture_index("ambient_occlusion_texture")

    # Tier1/2 texture slots (optional).
    emissive_texture = get_texture_index("emissive_texture")
    specular_texture = get_texture_index("specular_texture")
    sheen_color_texture = get_texture_index("sheen_color_texture")
    clearcoat_texture = get_texture_index("clearcoat_texture")
    clearcoat_normal_texture = get_texture_index("clearcoat_normal_texture")
    transmission_texture = get_texture_index("transmission_texture")
    thickness_texture = get_texture_index("thickness_texture")

    # Tier1/2 scalar params (optional).
    emissive_factor = asset.get("emissive_factor", [0.0, 0.0, 0.0])
    alpha_cutoff = float(asset.get("alpha_cutoff", 0.5))
    ior = float(asset.get("ior", 1.5))
    specular_factor = float(asset.get("specular_factor", 1.0))
    sheen_color_factor = asset.get("sheen_color_factor", [0.0, 0.0, 0.0])
    clearcoat_factor = float(asset.get("clearcoat_factor", 0.0))
    clearcoat_roughness = float(asset.get("clearcoat_roughness", 0.0))
    transmission_factor = float(asset.get("transmission_factor", 0.0))
    thickness_factor = float(asset.get("thickness_factor", 0.0))
    attenuation_color = asset.get("attenuation_color", [1.0, 1.0, 1.0])
    attenuation_distance = float(asset.get("attenuation_distance", 0.0))
    uv_scale = asset.get("uv_scale", [1.0, 1.0])
    uv_offset = asset.get("uv_offset", [0.0, 0.0])
    uv_rotation_radians = float(asset.get("uv_rotation_radians", 0.0))
    uv_set = int(asset.get("uv_set", 0))
    grid_spacing = asset.get("grid_spacing", [1.0, 1.0])
    grid_major_every = int(asset.get("grid_major_every", 10))
    grid_line_thickness = float(asset.get("grid_line_thickness", 1.0))
    grid_major_thickness = float(asset.get("grid_major_thickness", 2.0))
    grid_axis_thickness = float(asset.get("grid_axis_thickness", 2.0))
    grid_fade_start = float(asset.get("grid_fade_start", 0.0))
    grid_fade_end = float(asset.get("grid_fade_end", 0.0))
    grid_minor_color = asset.get(
        "grid_minor_color", [0.35, 0.35, 0.35, 1.0]
    )
    grid_major_color = asset.get(
        "grid_major_color", [0.55, 0.55, 0.55, 1.0]
    )
    grid_axis_color_x = asset.get(
        "grid_axis_color_x", [0.9, 0.2, 0.2, 1.0]
    )
    grid_axis_color_y = asset.get(
        "grid_axis_color_y", [0.2, 0.6, 0.9, 1.0]
    )
    grid_origin_color = asset.get(
        "grid_origin_color", [1.0, 1.0, 1.0, 1.0]
    )
    header = header_builder(asset)
    if len(header) != ASSET_HEADER_SIZE:
        raise PakError(
            "E_SIZE",
            f"AssetHeader size mismatch: expected {ASSET_HEADER_SIZE} got {len(header)}",
        )

    # Match oxygen::data::pak::render::MaterialAssetDesc exactly (see PakFormat.h).
    desc = (
        header
        + struct.pack("<B", material_domain)
        + struct.pack("<I", flags)
        + struct.pack("<I", shader_stages)
        + struct.pack("<4f", *[float(x) for x in base_color])
        + struct.pack("<f", normal_scale)
        + struct.pack(
            "<HHH",
            to_unorm16(metalness),
            to_unorm16(roughness),
            to_unorm16(ambient_occlusion),
        )
        + struct.pack(
            "<IIIIIIIIIIII",
            base_color_texture,
            normal_texture,
            metallic_texture,
            roughness_texture,
            ambient_occlusion_texture,
            emissive_texture,
            specular_texture,
            sheen_color_texture,
            clearcoat_texture,
            clearcoat_normal_texture,
            transmission_texture,
            thickness_texture,
        )
        + pack_f16x3(emissive_factor)
        + struct.pack("<H", to_unorm16(alpha_cutoff))
        + struct.pack("<f", ior)
        + struct.pack("<H", to_unorm16(specular_factor))
        + pack_f16x3(sheen_color_factor)
        + struct.pack(
            "<HHHHH",
            to_unorm16(clearcoat_factor),
            to_unorm16(clearcoat_roughness),
            to_unorm16(transmission_factor),
            to_unorm16(thickness_factor),
            # attenuation_color is 3x HalfFloat, packed below
            0,
        )
    )

    # Replace the last placeholder with attenuation_color and attenuation_distance.
    # This keeps the packing explicit and readable.
    desc = (
        desc[:-2]
        + pack_f16x3(attenuation_color)
        + struct.pack("<f", attenuation_distance)
    )
    if not isinstance(uv_scale, list) or len(uv_scale) != 2:
        raise PakError("E_SPEC", "uv_scale must be a list of 2 floats")
    if not isinstance(uv_offset, list) or len(uv_offset) != 2:
        raise PakError("E_SPEC", "uv_offset must be a list of 2 floats")
    if uv_set < 0 or uv_set > 255:
        raise PakError("E_RANGE", "uv_set must be in [0, 255]")

    desc += struct.pack("<2f", float(uv_scale[0]), float(uv_scale[1]))
    desc += struct.pack("<2f", float(uv_offset[0]), float(uv_offset[1]))
    desc += struct.pack("<f", uv_rotation_radians)
    desc += struct.pack("<B", uv_set)

    if not isinstance(grid_spacing, list) or len(grid_spacing) != 2:
        raise PakError("E_SPEC", "grid_spacing must be a list of 2 floats")
    for field_name, field_val in [
        ("grid_minor_color", grid_minor_color),
        ("grid_major_color", grid_major_color),
        ("grid_axis_color_x", grid_axis_color_x),
        ("grid_axis_color_y", grid_axis_color_y),
        ("grid_origin_color", grid_origin_color),
    ]:
        if not isinstance(field_val, list) or len(field_val) != 4:
            raise PakError(
                "E_SPEC", f"{field_name} must be a list of 4 floats"
            )

    desc += struct.pack("<2f", float(grid_spacing[0]), float(grid_spacing[1]))
    desc += struct.pack(
        "<Ifffff",
        grid_major_every,
        grid_line_thickness,
        grid_major_thickness,
        grid_axis_thickness,
        grid_fade_start,
        grid_fade_end,
    )
    desc += struct.pack("<4f", *[float(x) for x in grid_minor_color])
    desc += struct.pack("<4f", *[float(x) for x in grid_major_color])
    desc += struct.pack("<4f", *[float(x) for x in grid_axis_color_x])
    desc += struct.pack("<4f", *[float(x) for x in grid_axis_color_y])
    desc += struct.pack("<4f", *[float(x) for x in grid_origin_color])
    if len(desc) != MATERIAL_DESC_SIZE:
        raise PakError(
            "E_SIZE",
            f"Material descriptor size mismatch after padding: expected {MATERIAL_DESC_SIZE}, got {len(desc)}",
        )
    return desc


def pack_shader_reference_entries(
    shader_stages: int, shader_refs: List[Dict[str, Any]]
) -> bytes:
    """Pack variable shader reference entries following a material descriptor.

    Each reference structure mirrors ShaderReferenceDesc in PakFormat.h (v2):
    - shader_type: 1 byte (ShaderType enum value)
    - source_path: 120 bytes (UTF-8, null padded)
    - entry_point: 32 bytes (UTF-8, null padded)
    - defines: 256 bytes (UTF-8, null padded)
    - shader_hash: 8 bytes (uint64)
    Total = 417 bytes.
    """
    # Assign stages from the shader_stages bitfield in ascending set-bit order.
    stages: List[int] = [i for i in range(32) if (shader_stages & (1 << i))]
    if len(stages) != len(shader_refs):
        raise PakError(
            "E_SHADER_REFS",
            f"shader_references count ({len(shader_refs)}) must match popcount(shader_stages) ({len(stages)})",
        )

    out = b""
    for stage, ref in zip(stages, shader_refs):
        source_path = ref.get("source_path")
        entry_point = ref.get("entry_point")
        defines = ref.get("defines", "")

        if not isinstance(source_path, str) or not source_path:
            raise PakError(
                "E_SHADER_REF", "source_path must be a non-empty string"
            )
        if not isinstance(entry_point, str) or not entry_point:
            raise PakError(
                "E_SHADER_REF", "entry_point must be a non-empty string"
            )
        if not isinstance(defines, str):
            defines = str(defines)

        src_raw = source_path.encode("utf-8")[:119]
        src_bytes = src_raw + b"\x00" * (120 - len(src_raw))
        ep_raw = entry_point.encode("utf-8")[:31]
        ep_bytes = ep_raw + b"\x00" * (32 - len(ep_raw))
        def_raw = defines.encode("utf-8")[:255]
        def_bytes = def_raw + b"\x00" * (256 - len(def_raw))

        shader_hash = int(ref.get("shader_hash", 0)) & 0xFFFFFFFFFFFFFFFF
        entry = (
            struct.pack("<B", stage)
            + src_bytes
            + ep_bytes
            + def_bytes
            + struct.pack("<Q", shader_hash)
        )
        if len(entry) != SHADER_REF_DESC_SIZE:
            raise PakError(
                "E_SIZE",
                f"Shader reference size mismatch: expected {SHADER_REF_DESC_SIZE} got {len(entry)}",
            )
        out += entry
    return out


def pack_buffer_resource_descriptor(
    resource_spec: Dict[str, Any], data_offset: int, data_size: int
) -> bytes:
    usage_flags = resource_spec.get("usage", 0)
    element_stride = resource_spec.get("stride", 0)
    element_format = resource_spec.get("format", 0)
    content_hash = (
        int(resource_spec.get("content_hash", 0)) & 0xFFFFFFFFFFFFFFFF
    )
    desc = (
        struct.pack("<Q", data_offset)
        + struct.pack("<I", data_size)
        + struct.pack("<I", usage_flags)
        + struct.pack("<I", element_stride)
        + struct.pack("<B", element_format)
        + struct.pack("<Q", content_hash)
    )
    if len(desc) != 29:
        raise PakError(
            "E_SIZE", f"Buffer descriptor size mismatch: {len(desc)} != 29"
        )
    return desc


def pack_texture_resource_descriptor(
    resource_spec: Dict[str, Any], data_offset: int, data_size: int
) -> bytes:
    # Match current PakFormat_core::TextureResourceDesc layout.
    texture_type = resource_spec.get("texture_type", 0)
    compression_type = resource_spec.get("compression_type", 0)
    width = resource_spec.get("width", 0)
    height = resource_spec.get("height", 0)
    depth = resource_spec.get("depth", 1)
    array_layers = resource_spec.get("array_layers", 1)
    mip_levels = resource_spec.get("mip_levels", 1)
    format_val = resource_spec.get("format", 0)
    alignment = resource_spec.get("alignment", 256)
    content_hash = (
        int(resource_spec.get("content_hash", 0)) & 0xFFFFFFFFFFFFFFFF
    )
    desc = (
        struct.pack("<Q", data_offset)
        + struct.pack("<I", data_size)
        + struct.pack("<B", texture_type)
        + struct.pack("<B", compression_type)
        + struct.pack("<IIH", width, height, depth)
        + struct.pack("<HHB", array_layers, mip_levels, format_val)
        + struct.pack("<H", alignment)
        + struct.pack("<Q", content_hash)
    )
    if len(desc) != 39:
        raise PakError(
            "E_SIZE", f"Texture descriptor size mismatch: {len(desc)} != 39"
        )
    return desc


def pack_physics_resource_descriptor(
    resource_spec: Dict[str, Any], data_offset: int, data_size: int
) -> bytes:
    """Pack PhysicsResourceDesc."""
    fmt = int(resource_spec.get("format", 0))
    resource_asset_key_value = resource_spec.get("resource_asset_key")
    if resource_asset_key_value is None:
        resource_asset_key_value = resource_spec.get("asset_key")
    if resource_asset_key_value is None:
        resource_name = resource_spec.get("name")
        if isinstance(resource_name, str) and resource_name:
            resource_asset_key = _derive_physics_resource_asset_key(resource_name)
        else:
            resource_asset_key = b"\x00" * ASSET_KEY_SIZE
    else:
        resource_asset_key = _pack_asset_key_bytes(
            resource_asset_key_value, "resource_asset_key"
        )
    content_hash_raw = resource_spec.get("content_hash", 0)
    if isinstance(content_hash_raw, (bytes, bytearray)):
        content_hash = bytes(content_hash_raw)
        if len(content_hash) != 32:
            raise PakError("E_SIZE", "Physics content_hash bytes must be 32 bytes")
    elif isinstance(content_hash_raw, str):
        cleaned = content_hash_raw.strip()
        if cleaned.startswith("0x") or cleaned.startswith("0X"):
            cleaned = cleaned[2:]
        if len(cleaned) != 64:
            raise PakError("E_SIZE", "Physics content_hash hex must be 64 characters")
        try:
            content_hash = bytes.fromhex(cleaned)
        except ValueError as exc:
            raise PakError("E_TYPE", "Physics content_hash hex is invalid") from exc
    else:
        hash_prefix = int(content_hash_raw) & 0xFFFFFFFFFFFFFFFF
        content_hash = struct.pack("<Q", hash_prefix) + (b"\x00" * 24)
    desc = (
        struct.pack("<Q", data_offset)
        + struct.pack("<I", data_size)
        + struct.pack("<B", fmt)
        + resource_asset_key
        + content_hash
    )
    if len(desc) != PHYSICS_RESOURCE_DESC_SIZE:
        raise PakError("E_SIZE", f"PhysicsResourceDesc size mismatch: {len(desc)}")
    return desc


def pack_physics_material_asset_descriptor(
    asset: Dict[str, Any],
    *,
    header_builder,
) -> bytes:
    """Pack PhysicsMaterialAssetDesc."""
    header = header_builder(asset)
    static_friction = float(asset.get("static_friction", 0.5))
    dynamic_friction = float(asset.get("dynamic_friction", 0.5))
    restitution = float(asset.get("restitution", 0.0))
    density = float(asset.get("density", 1000.0))
    combine_friction = int(asset.get("combine_mode_friction", 0))
    combine_restitution = int(asset.get("combine_mode_restitution", 0))
    desc = (
        header
        + struct.pack(
            "<ffffBB",
            static_friction,
            dynamic_friction,
            restitution,
            density,
            combine_friction,
            combine_restitution,
        )
    )
    if len(desc) != PHYSICS_MATERIAL_ASSET_DESC_SIZE:
        raise PakError("E_SIZE", f"PhysicsMaterialAssetDesc size mismatch: {len(desc)}")
    return desc


def pack_collision_shape_asset_descriptor(
    asset: Dict[str, Any],
    resource_asset_key: bytes,
    *,
    header_builder,
    physics_material_name_to_asset_key: Dict[str, bytes] | None = None,
) -> bytes:
    """Pack CollisionShapeAssetDesc."""
    shape_type_lookup = {
        "invalid": 0,
        "sphere": 1,
        "capsule": 2,
        "box": 3,
        "cylinder": 4,
        "cone": 5,
        "convex_hull": 6,
        "triangle_mesh": 7,
        "height_field": 8,
        "plane": 9,
        "world_boundary": 10,
        "compound": 11,
    }

    boundary_mode_lookup = {
        "invalid": 0,
        "aabb_clamp": 1,
        "plane_set": 2,
    }

    def _parse_shape_type(value: Any, *, field_name: str) -> int:
        if isinstance(value, str):
            return shape_type_lookup.get(value.strip().lower(), 0)
        try:
            return int(value)
        except (TypeError, ValueError):
            raise PakError("E_TYPE", f"{field_name} must be a shape type string/int")

    def _parse_boundary_mode(value: Any) -> int:
        if isinstance(value, str):
            return boundary_mode_lookup.get(value.strip().lower(), 0)
        try:
            return int(value)
        except (TypeError, ValueError):
            raise PakError("E_TYPE", "boundary_mode must be a string/int")

    def _pack_compound_child(child: Dict[str, Any]) -> bytes:
        if not isinstance(child, dict):
            raise PakError("E_TYPE", "compound child entry must be an object")
        child_shape_type = _parse_shape_type(
            child.get("shape_type", 0), field_name="children[].shape_type"
        )
        radius = float(child.get("radius", 0.0))
        half_height = float(child.get("half_height", 0.0))
        half_extents = _vec3(child.get("half_extents"), [0.0, 0.0, 0.0])
        normal = _vec3(child.get("normal"), [0.0, 0.0, 0.0])
        distance = float(child.get("distance", 0.0))
        boundary_mode = _parse_boundary_mode(child.get("boundary_mode", 0))
        limits_min = _vec3(child.get("limits_min"), [0.0, 0.0, 0.0])
        limits_max = _vec3(child.get("limits_max"), [0.0, 0.0, 0.0])
        local_position = _vec3(child.get("local_position"), [0.0, 0.0, 0.0])
        local_rotation = child.get("local_rotation", [0.0, 0.0, 0.0, 1.0])
        if not isinstance(local_rotation, (list, tuple)) or len(local_rotation) != 4:
            raise PakError(
                "E_TYPE", "children[].local_rotation must be a 4-element list"
            )
        local_rotation = [
            float(local_rotation[0]),
            float(local_rotation[1]),
            float(local_rotation[2]),
            float(local_rotation[3]),
        ]
        local_scale = _vec3(child.get("local_scale"), [1.0, 1.0, 1.0])
        payload_asset_key = b"\x00" * 16
        if "payload_asset_key" in child:
            payload_asset_key = _pack_asset_key_bytes(
                child.get("payload_asset_key"), "children[].payload_asset_key"
            )
        elif "payload_ref" in child:
            payload_asset_key = _pack_asset_key_bytes(
                child.get("payload_ref"), "children[].payload_ref"
            )

        return struct.pack(
            "<Iff3f3ffI3f3f3f4f3f16s4x",
            child_shape_type & 0xFFFFFFFF,
            radius,
            half_height,
            half_extents[0],
            half_extents[1],
            half_extents[2],
            normal[0],
            normal[1],
            normal[2],
            distance,
            boundary_mode & 0xFFFFFFFF,
            limits_min[0],
            limits_min[1],
            limits_min[2],
            limits_max[0],
            limits_max[1],
            limits_max[2],
            local_position[0],
            local_position[1],
            local_position[2],
            local_rotation[0],
            local_rotation[1],
            local_rotation[2],
            local_rotation[3],
            local_scale[0],
            local_scale[1],
            local_scale[2],
            payload_asset_key,
        )

    header = header_builder(asset)
    physics_material_name_to_asset_key = physics_material_name_to_asset_key or {}
    shape_type_raw = asset.get("shape_type", 0)
    shape_type = _parse_shape_type(shape_type_raw, field_name="shape_type")

    local_position = _vec3(asset.get("local_position"), [0.0, 0.0, 0.0])
    local_rotation_raw = asset.get("local_rotation", [0.0, 0.0, 0.0, 1.0])
    if not isinstance(local_rotation_raw, (list, tuple)) or len(local_rotation_raw) != 4:
        raise PakError("E_TYPE", "local_rotation must be a list of 4 floats")
    local_rotation = [float(local_rotation_raw[0]), float(local_rotation_raw[1]), float(local_rotation_raw[2]), float(local_rotation_raw[3])]
    local_scale = _vec3(asset.get("local_scale"), [1.0, 1.0, 1.0])
    is_sensor = 1 if bool(asset.get("is_sensor", False)) else 0
    collision_own_layer = int(asset.get("collision_own_layer", 1)) & 0xFFFFFFFFFFFFFFFF
    collision_target_layers = int(asset.get("collision_target_layers", 0xFFFFFFFFFFFFFFFF)) & 0xFFFFFFFFFFFFFFFF
    material_asset_key = b"\x00" * 16
    if "material_asset_key" in asset:
        material_asset_key = _pack_asset_key_bytes(
            asset.get("material_asset_key"), "material_asset_key"
        )
    else:
        material_ref = asset.get(
            "material_asset",
            asset.get("material_asset_name", asset.get("material_ref")),
        )
        if material_ref is not None:
            if isinstance(material_ref, str):
                if material_ref in physics_material_name_to_asset_key:
                    material_asset_key = bytes(
                        physics_material_name_to_asset_key[material_ref]
                    )
                else:
                    try:
                        material_asset_key = _pack_asset_key_bytes(
                            material_ref, "material_ref"
                        )
                    except PakError as exc:
                        raise PakError(
                            "E_REF",
                            f"material_ref '{material_ref}' not found in authored assets",
                        ) from exc
            elif isinstance(material_ref, (bytes, bytearray)):
                material_asset_key = _pack_asset_key_bytes(
                    material_ref, "material_ref"
                )
            else:
                raise PakError(
                    "E_TYPE",
                    "material_ref/material_asset must be asset key bytes/hex or authored asset name",
                )

    params = asset.get("shape_params")
    params = params if isinstance(params, dict) else {}
    shape_params_blob = bytearray(80)

    if shape_type == 1:  # sphere
        radius = float(params.get("radius", asset.get("radius", 0.0)))
        struct.pack_into("<f", shape_params_blob, 0, radius)
    elif shape_type in (2, 4, 5):  # capsule/cylinder/cone
        radius = float(params.get("radius", asset.get("radius", 0.0)))
        half_height = float(params.get("half_height", asset.get("half_height", 0.0)))
        struct.pack_into("<ff", shape_params_blob, 0, radius, half_height)
    elif shape_type == 3:  # box
        half_extents = params.get("half_extents", asset.get("half_extents"))
        half_extents = _vec3(half_extents, [0.0, 0.0, 0.0])
        struct.pack_into("<3f", shape_params_blob, 0, *half_extents)
    elif shape_type == 9:  # plane
        normal = _vec3(params.get("normal", asset.get("normal")), [0.0, 0.0, 0.0])
        distance = float(params.get("distance", asset.get("distance", 0.0)))
        struct.pack_into("<3ff", shape_params_blob, 0, normal[0], normal[1], normal[2], distance)
    elif shape_type == 10:  # world boundary
        boundary_mode_raw = params.get("boundary_mode", asset.get("boundary_mode", 0))
        boundary_mode = _parse_boundary_mode(boundary_mode_raw)
        limits_min = _vec3(params.get("limits_min", asset.get("limits_min")), [0.0, 0.0, 0.0])
        limits_max = _vec3(params.get("limits_max", asset.get("limits_max")), [0.0, 0.0, 0.0])
        struct.pack_into("<I3f3f", shape_params_blob, 0, boundary_mode, *limits_min, *limits_max)
    elif shape_type == 11:  # compound
        children = asset.get("children", [])
        if children is None:
            children = []
        if not isinstance(children, list):
            raise PakError("E_TYPE", "children must be a list for compound shapes")
        child_records = b"".join(_pack_compound_child(child) for child in children)
        child_count = len(children)
        child_offset = COLLISION_SHAPE_ASSET_DESC_SIZE if child_count > 0 else 0
        struct.pack_into(
            "<II",
            shape_params_blob,
            0,
            child_count & 0xFFFFFFFF,
            child_offset & 0xFFFFFFFF,
        )
    else:
        child_records = b""

    payload_backed_shape_types = {5, 6, 7, 8, 11}
    cooked_ref = asset.get("cooked_shape_ref")
    cooked_ref = cooked_ref if isinstance(cooked_ref, dict) else {}
    default_cooked_asset_key = (
        _pack_asset_key_bytes(resource_asset_key, "resource_asset_key")
        if shape_type in payload_backed_shape_types
        else b"\x00" * ASSET_KEY_SIZE
    )
    payload_asset_key = default_cooked_asset_key
    payload_asset_key_raw = cooked_ref.get("payload_asset_key")
    if payload_asset_key_raw is not None:
        payload_asset_key = _pack_asset_key_bytes(
            payload_asset_key_raw, "cooked_shape_ref.payload_asset_key"
        )
    payload_type_raw = cooked_ref.get("payload_type")
    if payload_type_raw is None:
        payload_type = {
            5: 1,   # cone -> convex
            6: 1,   # convex hull -> convex
            7: 2,   # triangle mesh -> mesh
            8: 3,   # height field
            11: 4,  # compound
        }.get(shape_type, 0)
    elif isinstance(payload_type_raw, str):
        payload_type = {
            "invalid": 0,
            "convex": 1,
            "mesh": 2,
            "height_field": 3,
        }.get(payload_type_raw.strip().lower(), 0)
    else:
        payload_type = int(payload_type_raw)

    fixed_desc = (
        header
        + struct.pack("<B", shape_type & 0xFF)
        + struct.pack("<3f", *local_position)
        + struct.pack("<4f", *local_rotation)
        + struct.pack("<3f", *local_scale)
        + struct.pack("<I", is_sensor)
        + struct.pack("<Q", collision_own_layer)
        + struct.pack("<Q", collision_target_layers)
        + material_asset_key
        + bytes(shape_params_blob)
        + payload_asset_key
        + struct.pack("<B", payload_type & 0xFF)
    )
    if len(fixed_desc) != COLLISION_SHAPE_ASSET_DESC_SIZE:
        raise PakError("E_SIZE", f"CollisionShapeAssetDesc size mismatch: {len(fixed_desc)}")
    if shape_type == 11:
        desc = fixed_desc + child_records
    else:
        desc = fixed_desc
    return desc


def pack_physics_scene_asset_descriptor_and_payload(
    asset: Dict[str, Any],
    *,
    header_builder,
    shape_name_to_asset_key: Dict[str, bytes] | None = None,
    physics_material_name_to_asset_key: Dict[str, bytes] | None = None,
    physics_resource_name_to_asset_key: Dict[str, bytes] | None = None,
) -> tuple[bytes, bytes]:
    """Pack PhysicsSceneAssetDesc and physics binding payload."""

    shape_name_to_asset_key = shape_name_to_asset_key or {}
    physics_material_name_to_asset_key = (
        physics_material_name_to_asset_key or {}
    )
    physics_resource_name_to_asset_key = physics_resource_name_to_asset_key or {}
    no_asset_key = b"\x00" * 16

    def _resolve_asset_key(
        binding: Dict[str, Any],
        *,
        key_fields: Sequence[str],
        name_fields: Sequence[str],
        name_to_key: Dict[str, bytes],
        field_name: str,
        default_key: bytes,
    ) -> bytes:
        for field in key_fields:
            if field in binding:
                return _pack_asset_key_bytes(binding.get(field), field)
        for field in name_fields:
            value = binding.get(field)
            if value is None:
                continue
            if isinstance(value, str):
                if value in name_to_key:
                    return bytes(name_to_key[value])
                try:
                    return _pack_asset_key_bytes(value, field)
                except PakError as exc:
                    raise PakError(
                        "E_REF", f"{field_name} '{value}' not found in authored assets"
                    ) from exc
            if isinstance(value, (bytes, bytearray)):
                return _pack_asset_key_bytes(value, field)
            raise PakError("E_TYPE", f"{field_name} name must be a string")
        return default_key

    target_node_count = int(asset.get("target_node_count", 0))
    if target_node_count < 0:
        raise PakError("E_RANGE", "target_node_count must be non-negative")

    tables: list[tuple[int, int, int, bytes]] = []

    rigid_body_bindings = asset.get("rigid_body_bindings", []) or []
    if not isinstance(rigid_body_bindings, list):
        raise PakError("E_TYPE", "physics_scene.rigid_body_bindings must be a list")
    rigid_body_records: list[bytes] = []
    for binding in rigid_body_bindings:
        if not isinstance(binding, dict):
            continue
        shape_key = _resolve_asset_key(
            binding,
            key_fields=("shape_asset_key",),
            name_fields=("shape_asset", "shape_asset_name", "shape"),
            name_to_key=shape_name_to_asset_key,
            field_name="rigid_body.shape_asset",
            default_key=no_asset_key,
        )
        material_key = _resolve_asset_key(
            binding,
            key_fields=("material_asset_key",),
            name_fields=(
                "material_asset",
                "material_asset_name",
                "material",
            ),
            name_to_key=physics_material_name_to_asset_key,
            field_name="rigid_body.material_asset",
            default_key=no_asset_key,
        )
        rigid_body_records.append(
            pack_rigid_body_binding_record(
                binding,
                shape_key,
                material_key,
                node_count=target_node_count,
            )
        )
    if rigid_body_records:
        blob = b"".join(rigid_body_records)
        tables.append(
            (
                _PHYSICS_BINDING_RIGID_BODY,
                len(rigid_body_records),
                RIGID_BODY_BINDING_RECORD_SIZE,
                blob,
            )
        )

    collider_bindings = asset.get("collider_bindings", []) or []
    if not isinstance(collider_bindings, list):
        raise PakError("E_TYPE", "physics_scene.collider_bindings must be a list")
    collider_records: list[bytes] = []
    for binding in collider_bindings:
        if not isinstance(binding, dict):
            continue
        shape_key = _resolve_asset_key(
            binding,
            key_fields=("shape_asset_key",),
            name_fields=("shape_asset", "shape_asset_name", "shape"),
            name_to_key=shape_name_to_asset_key,
            field_name="collider.shape_asset",
            default_key=no_asset_key,
        )
        material_key = _resolve_asset_key(
            binding,
            key_fields=("material_asset_key",),
            name_fields=(
                "material_asset",
                "material_asset_name",
                "material",
            ),
            name_to_key=physics_material_name_to_asset_key,
            field_name="collider.material_asset",
            default_key=no_asset_key,
        )
        collider_records.append(
            pack_collider_binding_record(
                binding,
                shape_key,
                material_key,
                node_count=target_node_count,
            )
        )
    if collider_records:
        blob = b"".join(collider_records)
        tables.append(
            (
                _PHYSICS_BINDING_COLLIDER,
                len(collider_records),
                COLLIDER_BINDING_RECORD_SIZE,
                blob,
            )
        )

    character_bindings = asset.get("character_bindings", []) or []
    if not isinstance(character_bindings, list):
        raise PakError("E_TYPE", "physics_scene.character_bindings must be a list")
    character_records: list[bytes] = []
    for binding in character_bindings:
        if not isinstance(binding, dict):
            continue
        shape_key = _resolve_asset_key(
            binding,
            key_fields=("shape_asset_key",),
            name_fields=("shape_asset", "shape_asset_name", "shape"),
            name_to_key=shape_name_to_asset_key,
            field_name="character.shape_asset",
            default_key=no_asset_key,
        )
        character_records.append(
            pack_character_binding_record(
                binding,
                shape_key,
                node_count=target_node_count,
            )
        )
    if character_records:
        blob = b"".join(character_records)
        tables.append(
            (
                _PHYSICS_BINDING_CHARACTER,
                len(character_records),
                CHARACTER_BINDING_RECORD_SIZE,
                blob,
            )
        )

    soft_body_bindings = asset.get("soft_body_bindings", []) or []
    if not isinstance(soft_body_bindings, list):
        raise PakError("E_TYPE", "physics_scene.soft_body_bindings must be a list")
    soft_body_records: list[bytes] = []
    for binding in soft_body_bindings:
        if not isinstance(binding, dict):
            continue
        topology_asset_key = _resolve_asset_key(
            binding,
            key_fields=("topology_asset_key",),
            name_fields=("topology_resource", "topology_resource_name"),
            name_to_key=physics_resource_name_to_asset_key,
            field_name="soft_body.topology_resource",
            default_key=no_asset_key,
        )
        soft_body_records.append(
            pack_soft_body_binding_record(
                binding,
                topology_asset_key=topology_asset_key,
                node_count=target_node_count,
            )
        )
    if soft_body_records:
        blob = b"".join(soft_body_records)
        tables.append(
            (
                _PHYSICS_BINDING_SOFT_BODY,
                len(soft_body_records),
                SOFT_BODY_BINDING_RECORD_SIZE,
                blob,
            )
        )

    joint_bindings = asset.get("joint_bindings", []) or []
    if not isinstance(joint_bindings, list):
        raise PakError("E_TYPE", "physics_scene.joint_bindings must be a list")
    joint_records: list[bytes] = []
    for binding in joint_bindings:
        if not isinstance(binding, dict):
            continue
        constraint_asset_key = _resolve_asset_key(
            binding,
            key_fields=("constraint_asset_key",),
            name_fields=(
                "constraint_resource",
                "constraint_resource_name",
            ),
            name_to_key=physics_resource_name_to_asset_key,
            field_name="joint.constraint_resource",
            default_key=no_asset_key,
        )
        joint_records.append(
            pack_joint_binding_record(
                binding,
                constraint_asset_key,
                node_count=target_node_count,
            )
        )
    if joint_records:
        blob = b"".join(joint_records)
        tables.append(
            (
                _PHYSICS_BINDING_JOINT,
                len(joint_records),
                JOINT_BINDING_RECORD_SIZE,
                blob,
            )
        )

    vehicle_bindings = asset.get("vehicle_bindings", []) or []
    if not isinstance(vehicle_bindings, list):
        raise PakError("E_TYPE", "physics_scene.vehicle_bindings must be a list")
    vehicle_records: list[bytes] = []
    for binding in vehicle_bindings:
        if not isinstance(binding, dict):
            continue
        constraint_asset_key = _resolve_asset_key(
            binding,
            key_fields=("constraint_asset_key",),
            name_fields=(
                "constraint_resource",
                "constraint_resource_name",
            ),
            name_to_key=physics_resource_name_to_asset_key,
            field_name="vehicle.constraint_resource",
            default_key=no_asset_key,
        )
        vehicle_records.append(
            pack_vehicle_binding_record(
                binding,
                constraint_asset_key,
                node_count=target_node_count,
            )
        )
    if vehicle_records:
        blob = b"".join(vehicle_records)
        tables.append(
            (
                _PHYSICS_BINDING_VEHICLE,
                len(vehicle_records),
                VEHICLE_BINDING_RECORD_SIZE,
                blob,
            )
        )

    aggregate_bindings = asset.get("aggregate_bindings", []) or []
    if not isinstance(aggregate_bindings, list):
        raise PakError("E_TYPE", "physics_scene.aggregate_bindings must be a list")
    aggregate_records = [
        pack_aggregate_binding_record(binding, node_count=target_node_count)
        for binding in aggregate_bindings
        if isinstance(binding, dict)
    ]
    if aggregate_records:
        blob = b"".join(aggregate_records)
        tables.append(
            (
                _PHYSICS_BINDING_AGGREGATE,
                len(aggregate_records),
                AGGREGATE_BINDING_RECORD_SIZE,
                blob,
            )
        )

    tables.sort(key=lambda item: item[0])
    table_count = len(tables)

    payload = b""
    directory_offset = 0
    if tables:
        directory_offset = PHYSICS_SCENE_DESC_SIZE
        table_cursor = directory_offset + (20 * table_count)
        directory_entries: list[bytes] = []
        table_blobs: list[bytes] = []
        for binding_type, count, entry_size, blob in tables:
            directory_entries.append(
                struct.pack("<I", int(binding_type))
                + struct.pack("<QII", int(table_cursor), int(count), int(entry_size))
            )
            table_blobs.append(blob)
            table_cursor += len(blob)
        payload = b"".join(directory_entries) + b"".join(table_blobs)

    header = header_builder(asset)
    target_scene_key = _pack_asset_key_bytes(
        asset.get("target_scene_key"), "target_scene_key"
    )
    target_scene_content_hash_raw = asset.get("target_scene_content_hash", 0)
    if isinstance(target_scene_content_hash_raw, (bytes, bytearray)):
        target_scene_content_hash = bytes(target_scene_content_hash_raw)
        if len(target_scene_content_hash) != 32:
            raise PakError(
                "E_SIZE", "target_scene_content_hash bytes must be 32 bytes"
            )
    elif isinstance(target_scene_content_hash_raw, str):
        cleaned = target_scene_content_hash_raw.strip()
        if cleaned.startswith("0x") or cleaned.startswith("0X"):
            cleaned = cleaned[2:]
        if len(cleaned) != 64:
            raise PakError(
                "E_SIZE", "target_scene_content_hash hex must be 64 characters"
            )
        try:
            target_scene_content_hash = bytes.fromhex(cleaned)
        except ValueError as exc:
            raise PakError(
                "E_TYPE", "target_scene_content_hash hex is invalid"
            ) from exc
    else:
        hash_prefix = int(target_scene_content_hash_raw) & 0xFFFFFFFFFFFFFFFF
        target_scene_content_hash = struct.pack("<Q", hash_prefix) + (b"\x00" * 24)
    desc = (
        header
        + target_scene_key
        + struct.pack(
            "<IIQ",
            target_node_count,
            table_count,
            directory_offset,
        )
        + target_scene_content_hash
    )
    if len(desc) != PHYSICS_SCENE_DESC_SIZE:
        raise PakError("E_SIZE", f"PhysicsSceneAssetDesc size mismatch: {len(desc)}")
    return desc, payload


def pack_rigid_body_binding_record(
    binding: Dict[str, Any],
    shape_asset_key: bytes,
    material_asset_key: bytes,
    *,
    node_count: int,
) -> bytes:
    """Pack RigidBodyBindingRecord."""
    node_index = int(binding.get("node_index", 0))
    if node_index < 0 or node_index >= node_count:
        raise PakError("E_REF", f"RigidBody node_index out of range: {node_index}")
    body_type = int(binding.get("body_type", 0))
    motion_quality = int(binding.get("motion_quality", 0))
    layer = int(binding.get("collision_layer", 0))
    mask = int(binding.get("collision_mask", 0xFFFFFFFF))
    mass = float(binding.get("mass", 0.0))
    linear_damping = float(binding.get("linear_damping", 0.05))
    angular_damping = float(binding.get("angular_damping", 0.05))
    gravity_factor = float(binding.get("gravity_factor", 1.0))
    activation = 1 if bool(binding.get("initial_activation", True)) else 0
    is_sensor = 1 if bool(binding.get("is_sensor", False)) else 0
    com_override = binding.get("center_of_mass_override", [0.0, 0.0, 0.0])
    inertia_override = binding.get("inertia_tensor_override", [0.0, 0.0, 0.0])
    if not isinstance(com_override, (list, tuple)) or len(com_override) != 3:
        raise PakError(
            "E_TYPE", "rigid_body.center_of_mass_override must be a 3-element list"
        )
    if not isinstance(inertia_override, (list, tuple)) or len(inertia_override) != 3:
        raise PakError(
            "E_TYPE", "rigid_body.inertia_tensor_override must be a 3-element list"
        )
    com_x, com_y, com_z = float(com_override[0]), float(com_override[1]), float(
        com_override[2]
    )
    inertia_x, inertia_y, inertia_z = (
        float(inertia_override[0]),
        float(inertia_override[1]),
        float(inertia_override[2]),
    )
    has_com_override = 1 if "center_of_mass_override" in binding else 0
    has_inertia_override = 1 if "inertia_tensor_override" in binding else 0
    max_linear_velocity = float(binding.get("max_linear_velocity", 0.0))
    max_angular_velocity = float(binding.get("max_angular_velocity", 0.0))
    allowed_dof_flags = 0
    allowed_dof = binding.get("allowed_dof", {})
    if isinstance(allowed_dof, dict):
        if bool(allowed_dof.get("translate_x", False)):
            allowed_dof_flags |= 1 << 0
        if bool(allowed_dof.get("translate_y", False)):
            allowed_dof_flags |= 1 << 1
        if bool(allowed_dof.get("translate_z", False)):
            allowed_dof_flags |= 1 << 2
        if bool(allowed_dof.get("rotate_x", False)):
            allowed_dof_flags |= 1 << 3
        if bool(allowed_dof.get("rotate_y", False)):
            allowed_dof_flags |= 1 << 4
        if bool(allowed_dof.get("rotate_z", False)):
            allowed_dof_flags |= 1 << 5
    backend_bytes = b"\x00" * 10
    backend = binding.get("backend", {})
    if isinstance(backend, dict):
        target = str(backend.get("target", "")).strip().lower()
        if target == "jolt":
            num_velocity_steps = int(backend.get("num_velocity_steps_override", 0)) & 0xFF
            num_position_steps = int(backend.get("num_position_steps_override", 0)) & 0xFF
            backend_bytes = struct.pack("<BB", num_velocity_steps, num_position_steps) + (b"\x00" * 8)
        elif target == "physx":
            min_velocity_iters = int(backend.get("min_velocity_iters", 0)) & 0xFF
            min_position_iters = int(backend.get("min_position_iters", 0)) & 0xFF
            max_contact_impulse = float(backend.get("max_contact_impulse", 0.0))
            contact_report_threshold = float(backend.get("contact_report_threshold", 0.0))
            backend_bytes = struct.pack(
                "<BBff",
                min_velocity_iters,
                min_position_iters,
                max_contact_impulse,
                contact_report_threshold,
            )
    shape_key_bytes = _pack_asset_key_bytes(shape_asset_key, "shape_asset_key")
    material_key_bytes = _pack_asset_key_bytes(
        material_asset_key, "material_asset_key"
    )
    desc = (
        struct.pack(
            "<IBBHIffffII",
            node_index,
            body_type,
            motion_quality,
            layer,
            mask,
            mass,
            linear_damping,
            angular_damping,
            gravity_factor,
            activation,
            is_sensor,
        )
        + shape_key_bytes
        + material_key_bytes
        + struct.pack(
            "<3fI3fIffI",
            com_x,
            com_y,
            com_z,
            has_com_override,
            inertia_x,
            inertia_y,
            inertia_z,
            has_inertia_override,
            max_linear_velocity,
            max_angular_velocity,
            allowed_dof_flags,
        )
        + backend_bytes
    )
    if len(desc) != RIGID_BODY_BINDING_RECORD_SIZE:
        raise PakError("E_SIZE", f"RigidBodyBindingRecord size mismatch: {len(desc)}")
    return desc


def pack_collider_binding_record(
    binding: Dict[str, Any],
    shape_asset_key: bytes,
    material_asset_key: bytes,
    *,
    node_count: int,
) -> bytes:
    """Pack ColliderBindingRecord."""
    node_index = int(binding.get("node_index", 0))
    if node_index < 0 or node_index >= node_count:
        raise PakError("E_REF", f"Collider node_index out of range: {node_index}")
    layer = int(binding.get("collision_layer", 0))
    mask = int(binding.get("collision_mask", 0xFFFFFFFF))
    is_sensor = 1 if bool(binding.get("is_sensor", False)) else 0
    shape_key_bytes = _pack_asset_key_bytes(shape_asset_key, "shape_asset_key")
    material_key_bytes = _pack_asset_key_bytes(
        material_asset_key, "material_asset_key"
    )
    desc = (
        struct.pack("<I", node_index)
        + shape_key_bytes
        + material_key_bytes
        + struct.pack("<HII", layer, mask, is_sensor)
    )
    if len(desc) != COLLIDER_BINDING_RECORD_SIZE:
        raise PakError("E_SIZE", f"ColliderBindingRecord size mismatch: {len(desc)}")
    return desc


def pack_character_binding_record(
    binding: Dict[str, Any],
    shape_asset_key: bytes,
    *,
    node_count: int,
) -> bytes:
    """Pack CharacterBindingRecord."""
    node_index = int(binding.get("node_index", 0))
    if node_index < 0 or node_index >= node_count:
        raise PakError("E_REF", f"Character node_index out of range: {node_index}")
    mass = float(binding.get("mass", 80.0))
    max_slope = float(binding.get("max_slope_angle", 0.7854))
    step_height = float(binding.get("step_height", 0.3))
    step_down = float(binding.get("step_down_distance", 0.0))
    max_strength = float(binding.get("max_strength", 100.0))
    skin_width = float(binding.get("skin_width", 0.0))
    predictive_contact_distance = float(
        binding.get("predictive_contact_distance", 0.0)
    )
    layer = int(binding.get("collision_layer", 0))
    mask = int(binding.get("collision_mask", 0xFFFFFFFF))
    inner_shape_asset_key = _pack_asset_key_bytes(
        binding.get("inner_shape_asset_key", b"\x00" * 16),
        "inner_shape_asset_key",
    )
    backend_bytes = b"\x00" * 12
    backend = binding.get("backend", {})
    if isinstance(backend, dict):
        target = str(backend.get("target", "")).strip().lower()
        if target == "jolt":
            penetration_recovery_speed = float(
                backend.get("penetration_recovery_speed", 0.0)
            )
            max_num_hits = int(backend.get("max_num_hits", 0))
            hit_reduction_cos_max_angle = float(
                backend.get("hit_reduction_cos_max_angle", 0.0)
            )
            backend_bytes = struct.pack(
                "<fIf",
                penetration_recovery_speed,
                max_num_hits,
                hit_reduction_cos_max_angle,
            )
        elif target == "physx":
            contact_offset = float(backend.get("contact_offset", 0.0))
            backend_bytes = struct.pack("<f", contact_offset) + (b"\x00" * 8)
    shape_key_bytes = _pack_asset_key_bytes(shape_asset_key, "shape_asset_key")
    desc = (
        struct.pack("<I", node_index)
        + shape_key_bytes
        + struct.pack(
            "<fffffffHI",
            mass,
            max_slope,
            step_height,
            step_down,
            max_strength,
            skin_width,
            predictive_contact_distance,
            layer,
            mask,
        )
        + inner_shape_asset_key
        + backend_bytes
    )
    if len(desc) != CHARACTER_BINDING_RECORD_SIZE:
        raise PakError("E_SIZE", f"CharacterBindingRecord size mismatch: {len(desc)}")
    return desc


def pack_soft_body_binding_record(
    binding: Dict[str, Any],
    topology_asset_key: bytes,
    *,
    node_count: int,
) -> bytes:
    """Pack SoftBodyBindingRecord."""
    node_index = int(binding.get("node_index", 0))
    if node_index < 0 or node_index >= node_count:
        raise PakError("E_REF", f"SoftBody node_index out of range: {node_index}")
    edge_comp = float(binding.get("edge_compliance", 0.0))
    shear_comp = float(binding.get("shear_compliance", 0.0))
    bend_comp = float(binding.get("bend_compliance", 1.0))
    volume_comp = float(binding.get("volume_compliance", 0.0))
    pressure_coeff = float(binding.get("pressure_coefficient", 0.0))
    global_damping = float(binding.get("global_damping", 0.0))
    restitution = float(binding.get("restitution", 0.0))
    friction = float(binding.get("friction", 0.2))
    vertex_radius = float(binding.get("vertex_radius", 0.0))
    tether_max = float(binding.get("tether_max_distance_multiplier", 1.0))
    solver_iteration_count = int(binding.get("solver_iteration_count", 0))
    collision_layer = int(binding.get("collision_layer", 0))
    collision_mask = int(binding.get("collision_mask", 0xFFFFFFFF))
    topology_asset_key_bytes = _pack_asset_key_bytes(
        topology_asset_key, "topology_asset_key"
    )
    pinned_vertices = binding.get("pinned_vertices", [])
    kinematic_vertices = binding.get("kinematic_vertices", [])
    pinned_vertex_count = len(pinned_vertices) if isinstance(pinned_vertices, list) else 0
    kinematic_vertex_count = (
        len(kinematic_vertices) if isinstance(kinematic_vertices, list) else 0
    )
    pinned_vertex_byte_offset = int(binding.get("pinned_vertex_byte_offset", 0))
    kinematic_vertex_byte_offset = int(binding.get("kinematic_vertex_byte_offset", 0))
    backend_bytes = b"\x00" * 12
    topology_format = int(binding.get("topology_format", 2)) & 0xFF
    backend = binding.get("backend", {})
    if isinstance(backend, dict):
        target = str(backend.get("target", "")).strip().lower()
        if target == "jolt":
            num_velocity_steps = int(backend.get("velocity_iteration_count", 0))
            num_position_steps = solver_iteration_count
            gravity_factor = float(backend.get("gravity_factor", 1.0))
            backend_bytes = struct.pack(
                "<IIf",
                num_velocity_steps,
                num_position_steps,
                gravity_factor,
            )
            topology_format = 2
        elif target == "physx":
            youngs_modulus = float(backend.get("youngs_modulus", 0.0))
            poissons = float(backend.get("poisson_ratio", 0.0))
            dynamic_friction = float(backend.get("dynamic_friction", 0.0))
            backend_bytes = struct.pack(
                "<fff",
                youngs_modulus,
                poissons,
                dynamic_friction,
            )
            topology_format = 4
    tether_mode_value = binding.get("tether_mode", 0)
    if isinstance(tether_mode_value, str):
        tether_mode_key = tether_mode_value.strip().lower()
        tether_mode = {"none": 0, "euclidean": 1, "geodesic": 2}.get(
            tether_mode_key, 0
        )
    else:
        tether_mode = int(tether_mode_value)
    self_collision = 1 if bool(binding.get("self_collision", False)) else 0
    desc = (
        struct.pack(
            "<Iffffffffff",
            node_index,
            edge_comp,
            shear_comp,
            bend_comp,
            volume_comp,
            pressure_coeff,
            global_damping,
            restitution,
            friction,
            vertex_radius,
            tether_max,
        )
        + struct.pack(
            "<IHI16sIIII",
            solver_iteration_count,
            collision_layer,
            collision_mask,
            topology_asset_key_bytes,
            pinned_vertex_count,
            pinned_vertex_byte_offset,
            kinematic_vertex_count,
            kinematic_vertex_byte_offset,
        )
        + backend_bytes
        + struct.pack("<BBB", tether_mode & 0xFF, topology_format, self_collision)
    )
    if len(desc) != SOFT_BODY_BINDING_RECORD_SIZE:
        raise PakError("E_SIZE", f"SoftBodyBindingRecord size mismatch: {len(desc)}")
    return desc


def pack_joint_binding_record(
    binding: Dict[str, Any],
    constraint_asset_key: bytes,
    *,
    node_count: int,
) -> bytes:
    """Pack JointBindingRecord."""
    node_a = int(binding.get("node_index_a", 0))
    node_b_raw = binding.get("node_index_b", 0)
    if node_b_raw is None or (
        isinstance(node_b_raw, str) and node_b_raw.strip().lower() == "world"
    ):
        node_b = 0xFFFFFFFF
    else:
        node_b = int(node_b_raw)
    if node_a < 0 or node_a >= node_count:
        raise PakError("E_REF", f"Joint node_index_a out of range: {node_a}")
    if node_b != 0xFFFFFFFF and (node_b < 0 or node_b >= node_count):
        raise PakError("E_REF", f"Joint node_index_b out of range: {node_b}")
    constraint_asset_key_bytes = _pack_asset_key_bytes(
        constraint_asset_key, "constraint_asset_key"
    )
    backend_bytes = b"\x00" * 16
    backend = binding.get("backend", {})
    if isinstance(backend, dict):
        target = str(backend.get("target", "")).strip().lower()
        if target == "jolt":
            num_velocity_steps = int(backend.get("num_velocity_steps_override", 0)) & 0xFF
            num_position_steps = int(backend.get("num_position_steps_override", 0)) & 0xFF
            backend_bytes = struct.pack("<BB", num_velocity_steps, num_position_steps) + (b"\x00" * 14)
        elif target == "physx":
            inv_mass_scale0 = float(backend.get("inv_mass_scale0", 0.0))
            inv_mass_scale1 = float(backend.get("inv_mass_scale1", 0.0))
            inv_inertia_scale0 = float(backend.get("inv_inertia_scale0", 0.0))
            inv_inertia_scale1 = float(backend.get("inv_inertia_scale1", 0.0))
            backend_bytes = struct.pack(
                "<ffff",
                inv_mass_scale0,
                inv_mass_scale1,
                inv_inertia_scale0,
                inv_inertia_scale1,
            )
    desc = (
        struct.pack("<II", node_a, node_b)
        + constraint_asset_key_bytes
        + backend_bytes
    )
    if len(desc) != JOINT_BINDING_RECORD_SIZE:
        raise PakError("E_SIZE", f"JointBindingRecord size mismatch: {len(desc)}")
    return desc


def pack_vehicle_binding_record(
    binding: Dict[str, Any],
    constraint_asset_key: bytes,
    *,
    node_count: int,
) -> bytes:
    """Pack VehicleBindingRecord."""
    node_index = int(binding.get("node_index", 0))
    if node_index < 0 or node_index >= node_count:
        raise PakError("E_REF", f"Vehicle node_index out of range: {node_index}")
    controller_type_raw = binding.get("controller_type", 0)
    if isinstance(controller_type_raw, str):
        controller_type = {
            "wheeled": 0,
            "tracked": 1,
        }.get(controller_type_raw.strip().lower(), 0)
    else:
        controller_type = int(controller_type_raw)
    wheel_slice_offset = int(binding.get("wheel_slice_offset", 0))
    wheel_slice_count = int(binding.get("wheel_slice_count", 0))
    constraint_asset_key_bytes = _pack_asset_key_bytes(
        constraint_asset_key, "constraint_asset_key"
    )
    desc = (
        struct.pack(
            "<I16sIII",
            node_index,
            constraint_asset_key_bytes,
            controller_type,
            wheel_slice_offset,
            wheel_slice_count,
        )
    )
    if len(desc) != VEHICLE_BINDING_RECORD_SIZE:
        raise PakError("E_SIZE", f"VehicleBindingRecord size mismatch: {len(desc)}")
    return desc


def pack_aggregate_binding_record(
    binding: Dict[str, Any],
    *,
    node_count: int,
) -> bytes:
    """Pack AggregateBindingRecord."""
    node_index = int(binding.get("node_index", 0))
    if node_index < 0 or node_index >= node_count:
        raise PakError("E_REF", f"Aggregate node_index out of range: {node_index}")
    max_bodies = int(binding.get("max_bodies", 0))
    filter_overlap = 1 if bool(binding.get("filter_overlap", True)) else 0
    authority = int(binding.get("authority", 0))
    desc = (
        struct.pack("<IIIB", node_index, max_bodies, filter_overlap, authority)
    )
    if len(desc) != AGGREGATE_BINDING_RECORD_SIZE:
        raise PakError("E_SIZE", f"AggregateBindingRecord size mismatch: {len(desc)}")
    return desc


def pack_audio_resource_descriptor(
    resource_spec: Dict[str, Any], data_offset: int, data_size: int
) -> bytes:
    desc = (
        struct.pack("<Q", data_offset)
        + struct.pack("<I", data_size)
        + b"\x00" * 20
    )
    if len(desc) != 32:
        raise PakError(
            "E_SIZE", f"Audio descriptor size mismatch: {len(desc)} != 32"
        )
    return desc


def pack_script_resource_descriptor(
    resource_spec: Dict[str, Any], data_offset: int, data_size: int
) -> bytes:
    def _enum_u8(value: Any, mapping: Dict[str, int], field_name: str) -> int:
        if isinstance(value, str):
            key = value.strip().lower()
            if key not in mapping:
                raise PakError("E_RANGE", f"unsupported {field_name}: {value}")
            return mapping[key]
        return int(value) & 0xFF

    language = _enum_u8(
        resource_spec.get("language", 0),
        {"luau": 0},
        "script language",
    )
    encoding = _enum_u8(
        resource_spec.get("encoding", 0),
        {"bytecode": 0, "source": 1},
        "script encoding",
    )
    compression = _enum_u8(
        resource_spec.get("compression", 0),
        {"none": 0, "zstd": 1},
        "script compression",
    )
    content_hash = int(resource_spec.get("content_hash", 0)) & 0xFFFFFFFFFFFFFFFF
    desc = (
        struct.pack("<Q", data_offset)
        + struct.pack("<I", data_size)
        + struct.pack("<B", language)
        + struct.pack("<B", encoding)
        + struct.pack("<B", compression)
        + struct.pack("<Q", content_hash)
    )
    if len(desc) != 23:
        raise PakError(
            "E_SIZE", f"Script descriptor size mismatch: {len(desc)} != 23"
        )
    return desc


def pack_script_asset_descriptor(
    asset: Dict[str, Any],
    resource_index_map: Dict[str, Dict[str, int]],
    *,
    header_builder,
) -> bytes:
    asset = asset if isinstance(asset, dict) else {}

    def _resolve_resource_index(field_name: str, fallback_name: str | None = None) -> int:
        value = asset.get(field_name)
        if value is None and fallback_name is not None:
            value = asset.get(fallback_name)
        if isinstance(value, str):
            return int(resource_index_map.get("script", {}).get(value, 0))
        if isinstance(value, int):
            return int(value)
        return 0

    bytecode_resource_index = _resolve_resource_index(
        "bytecode_resource", fallback_name="script_resource"
    )
    source_resource_index = _resolve_resource_index(
        "source_resource", fallback_name="script"
    )
    external_source_path = asset.get("external_source_path", "")
    if not isinstance(external_source_path, str):
        raise PakError("E_TYPE", "external_source_path must be a string")
    external_source_path_bytes = _c_string_bytes(
        external_source_path, 119, "external_source_path"
    )
    flags = int(asset.get("flags", 0))
    if external_source_path:
        flags |= 0x1  # ScriptAssetFlags::kAllowExternalSource
    asset.setdefault("type", "script")
    header = header_builder(asset)
    desc = (
        header
        + struct.pack(
            "<III", bytecode_resource_index, source_resource_index, flags
        )
        + external_source_path_bytes
    )
    if len(desc) != SCRIPT_DESC_SIZE:
        raise PakError("E_SIZE", f"Script asset descriptor size mismatch: {len(desc)}")
    return desc


def pack_input_action_asset_descriptor(
    asset: Dict[str, Any], *, header_builder
) -> bytes:
    asset = asset if isinstance(asset, dict) else {}
    asset.setdefault("type", "input_action")
    header = header_builder(asset)
    if len(header) != ASSET_HEADER_SIZE:
        raise PakError("E_SIZE", f"Asset header size mismatch: {len(header)}")

    value_type_raw = asset.get("value_type", 0)
    if isinstance(value_type_raw, str):
        value_type_key = value_type_raw.strip().lower()
        value_type = {
            "bool": 0,
            "axis1d": 1,
            "axis_1d": 1,
            "axis2d": 2,
            "axis_2d": 2,
        }.get(value_type_key)
        if value_type is None:
            raise PakError("E_RANGE", f"Unsupported input action value_type: {value_type_raw}")
    else:
        value_type = int(value_type_raw)
    if value_type < 0 or value_type > 2:
        raise PakError("E_RANGE", f"input_action value_type out of range: {value_type}")

    flags = int(asset.get("flags", 0) or 0)
    if bool(asset.get("consumes_input", False)):
        flags |= 0x1

    desc = (
        header
        + struct.pack("<B", int(value_type))
        + struct.pack("<I", flags & 0xFFFFFFFF)
    )
    if len(desc) != INPUT_ACTION_DESC_SIZE:
        raise PakError("E_SIZE", f"InputActionAssetDesc size mismatch: {len(desc)}")
    return desc


def _resolve_asset_key(
    value: Any,
    *,
    name_to_key: Dict[str, bytes] | None = None,
    field_name: str = "asset_key",
) -> bytes:
    if isinstance(value, str) and name_to_key and value in name_to_key:
        return name_to_key[value]
    try:
        return _pack_asset_key_bytes(value, field_name)
    except PakError:
        return b"\x00" * ASSET_KEY_SIZE


def _parse_trigger_type(value: Any) -> int:
    if isinstance(value, str):
        key = value.strip().lower()
        mapping = {
            "pressed": 0,
            "released": 1,
            "down": 2,
            "hold": 3,
            "holdandrelease": 4,
            "hold_and_release": 4,
            "pulse": 5,
            "tap": 6,
            "chord": 7,
            "actionchain": 8,
            "action_chain": 8,
            "combo": 9,
        }
        if key not in mapping:
            raise PakError("E_RANGE", f"Unsupported trigger type: {value}")
        return mapping[key]
    value_i = int(value)
    if value_i < 0 or value_i > 9:
        raise PakError("E_RANGE", f"Trigger type out of range: {value_i}")
    return value_i


def _parse_trigger_behavior(value: Any) -> int:
    if isinstance(value, str):
        key = value.strip().lower()
        mapping = {
            "explicit": 0,
            "implicit": 1,
            "blocker": 2,
        }
        if key not in mapping:
            raise PakError("E_RANGE", f"Unsupported trigger behavior: {value}")
        return mapping[key]
    value_i = int(value)
    if value_i < 0 or value_i > 2:
        raise PakError("E_RANGE", f"Trigger behavior out of range: {value_i}")
    return value_i


def pack_input_mapping_context_asset_descriptor_and_payload(
    asset: Dict[str, Any], *, header_builder, action_name_to_key: Dict[str, bytes]
) -> tuple[bytes, bytes]:
    asset = asset if isinstance(asset, dict) else {}
    asset.setdefault("type", "input_mapping_context")
    header = header_builder(asset)
    if len(header) != ASSET_HEADER_SIZE:
        raise PakError("E_SIZE", f"Asset header size mismatch: {len(header)}")

    strings = bytearray(b"\x00")
    string_offsets: Dict[str, int] = {"": 0}

    def intern_slot_name(name: str) -> int:
        if name in string_offsets:
            return string_offsets[name]
        off = len(strings)
        strings.extend(name.encode("utf-8"))
        strings.append(0)
        string_offsets[name] = off
        return off

    mappings_spec = asset.get("mappings", []) or []
    if not isinstance(mappings_spec, list):
        raise PakError("E_TYPE", "input_mapping_context.mappings must be a list")

    mapping_records: list[bytes] = []
    trigger_records: list[bytes] = []
    aux_records: list[bytes] = []

    for mapping in mappings_spec:
        if not isinstance(mapping, dict):
            continue
        action_value = mapping.get("action_asset_key")
        if action_value is None:
            action_value = mapping.get("action_asset")
        if action_value is None:
            action_value = mapping.get("action")
        action_key = _resolve_asset_key(
            action_value, name_to_key=action_name_to_key, field_name="action_asset_key"
        )

        slot_name = mapping.get("slot_name", mapping.get("slot", ""))
        if not isinstance(slot_name, str):
            raise PakError("E_TYPE", "input mapping slot name must be a string")
        slot_name_offset = intern_slot_name(slot_name)

        map_flags = int(mapping.get("flags", 0) or 0) & 0xFFFFFFFF
        scale = mapping.get("scale", [1.0, 1.0])
        bias = mapping.get("bias", [0.0, 0.0])
        if not isinstance(scale, list) or len(scale) != 2:
            raise PakError("E_TYPE", "input mapping scale must be a [x, y] list")
        if not isinstance(bias, list) or len(bias) != 2:
            raise PakError("E_TYPE", "input mapping bias must be a [x, y] list")

        triggers_spec = mapping.get("triggers", []) or []
        if not isinstance(triggers_spec, list):
            raise PakError("E_TYPE", "input mapping triggers must be a list")
        trigger_start_index = len(trigger_records)
        trigger_count = 0

        for trigger in triggers_spec:
            if not isinstance(trigger, dict):
                continue
            trigger_type = _parse_trigger_type(trigger.get("type", 0))
            trigger_behavior = _parse_trigger_behavior(trigger.get("behavior", 1))
            trigger_flags = int(trigger.get("flags", 0) or 0) & 0xFFFFFFFF
            threshold = float(trigger.get("actuation_threshold", 0.5))

            linked_action_value = trigger.get("linked_action_asset_key")
            if linked_action_value is None:
                linked_action_value = trigger.get("linked_action_asset")
            if linked_action_value is None:
                linked_action_value = trigger.get("linked_action")
            linked_action_key = _resolve_asset_key(
                linked_action_value,
                name_to_key=action_name_to_key,
                field_name="linked_action_asset_key",
            )

            fparams = trigger.get("fparams", []) or []
            uparams = trigger.get("uparams", []) or []
            if not isinstance(fparams, list) or not isinstance(uparams, list):
                raise PakError("E_TYPE", "trigger fparams/uparams must be lists")
            if len(fparams) > 5 or len(uparams) > 5:
                raise PakError("E_RANGE", "trigger fparams/uparams max length is 5")
            fparams5 = [0.0] * 5
            uparams5 = [0] * 5
            for i, val in enumerate(fparams):
                fparams5[i] = float(val)
            for i, val in enumerate(uparams):
                uparams5[i] = int(val) & 0xFFFFFFFF

            aux_spec = trigger.get("aux", trigger.get("aux_actions", [])) or []
            if not isinstance(aux_spec, list):
                raise PakError("E_TYPE", "trigger aux must be a list")
            aux_start_index = len(aux_records)
            aux_count = 0
            for aux in aux_spec:
                if isinstance(aux, dict):
                    aux_action_value = aux.get("action_asset_key")
                    if aux_action_value is None:
                        aux_action_value = aux.get("action_asset")
                    if aux_action_value is None:
                        aux_action_value = aux.get("action")
                    completion_states = int(aux.get("completion_states", 0) or 0)
                    time_ns = int(aux.get("time_to_complete_ns", 0) or 0)
                    aux_flags = int(aux.get("flags", 0) or 0)
                else:
                    aux_action_value = aux
                    completion_states = 0
                    time_ns = 0
                    aux_flags = 0

                aux_action_key = _resolve_asset_key(
                    aux_action_value,
                    name_to_key=action_name_to_key,
                    field_name="aux.action_asset_key",
                )
                aux_records.append(
                    aux_action_key
                    + struct.pack("<I", completion_states & 0xFFFFFFFF)
                    + struct.pack("<Q", time_ns & 0xFFFFFFFFFFFFFFFF)
                    + struct.pack("<I", aux_flags & 0xFFFFFFFF)
                )
                aux_count += 1

            trigger_records.append(
                struct.pack("<B", trigger_type)
                + struct.pack("<B", trigger_behavior)
                + struct.pack("<I", trigger_flags)
                + struct.pack("<f", threshold)
                + linked_action_key
                + struct.pack("<I", aux_start_index)
                + struct.pack("<I", aux_count)
                + struct.pack("<5f", *fparams5)
                + struct.pack("<5I", *uparams5)
            )
            trigger_count += 1

        mapping_records.append(
            action_key
            + struct.pack("<I", slot_name_offset)
            + struct.pack("<I", trigger_start_index)
            + struct.pack("<I", trigger_count)
            + struct.pack("<I", map_flags)
            + struct.pack("<2f", float(scale[0]), float(scale[1]))
            + struct.pack("<2f", float(bias[0]), float(bias[1]))
        )

    mappings_blob = b"".join(mapping_records)
    triggers_blob = b"".join(trigger_records)
    aux_blob = b"".join(aux_records)
    strings_blob = bytes(strings)

    for rec in mapping_records:
        if len(rec) != 48:
            raise PakError("E_SIZE", f"InputActionMappingRecord size mismatch: {len(rec)}")
    for rec in trigger_records:
        if len(rec) != 74:
            raise PakError("E_SIZE", f"InputTriggerRecord size mismatch: {len(rec)}")
    for rec in aux_records:
        if len(rec) != 32:
            raise PakError("E_SIZE", f"InputTriggerAuxRecord size mismatch: {len(rec)}")

    mappings_offset = INPUT_MAPPING_CONTEXT_DESC_SIZE
    triggers_offset = mappings_offset + len(mappings_blob)
    aux_offset = triggers_offset + len(triggers_blob)
    strings_offset = aux_offset + len(aux_blob)
    payload = mappings_blob + triggers_blob + aux_blob + strings_blob

    context_flags = int(asset.get("flags", 0) or 0) & 0xFFFFFFFF
    default_priority = int(asset.get("default_priority", 0) or 0)
    desc = (
        header
        + struct.pack("<I", context_flags)
        + struct.pack("<i", default_priority)
        + struct.pack("<QII", mappings_offset, len(mapping_records), 48)
        + struct.pack("<QII", triggers_offset, len(trigger_records), 74)
        + struct.pack("<QII", aux_offset, len(aux_records), 32)
        + struct.pack("<QII", strings_offset, len(strings_blob), 1)
    )
    if len(desc) != INPUT_MAPPING_CONTEXT_DESC_SIZE:
        raise PakError(
            "E_SIZE",
            f"InputMappingContextAssetDesc size mismatch: {len(desc)}",
        )
    return desc, payload

def pack_name_string(name: str, size: int) -> bytes:
    # Truncate UTF-8 to fit size-1 then null pad to fixed size
    if not isinstance(name, str):
        name = str(name)
    raw = name.encode("utf-8")[: size - 1]
    return raw + b"\x00" * (size - len(raw))


def _coerce_vec3(value: Any, default: list[float]) -> list[float]:
    if value is None:
        return list(default)
    if not isinstance(value, (list, tuple)) or len(value) != 3:
        raise PakError("E_BOUNDS", f"Invalid vec3 bounds: {value}")
    return [float(value[0]), float(value[1]), float(value[2])]


def _merge_bounds(
    bounds: list[tuple[list[float], list[float]]],
) -> tuple[list[float], list[float]]:
    if not bounds:
        return [0.0, 0.0, 0.0], [0.0, 0.0, 0.0]
    min_vals = [float("inf"), float("inf"), float("inf")]
    max_vals = [float("-inf"), float("-inf"), float("-inf")]
    for bb_min, bb_max in bounds:
        for i in range(3):
            min_vals[i] = min(min_vals[i], bb_min[i])
            max_vals[i] = max(max_vals[i], bb_max[i])
    return min_vals, max_vals


def _collect_submesh_bounds(
    submesh: Dict[str, Any],
) -> tuple[list[float], list[float]]:
    if "bounding_box_min" in submesh or "bounding_box_max" in submesh:
        sm_min = _coerce_vec3(submesh.get("bounding_box_min"), [0.0, 0.0, 0.0])
        sm_max = _coerce_vec3(submesh.get("bounding_box_max"), [0.0, 0.0, 0.0])
        return sm_min, sm_max

    mesh_views = submesh.get("mesh_views", []) or []
    view_bounds: list[tuple[list[float], list[float]]] = []
    for mv in mesh_views:
        if not isinstance(mv, dict):
            continue
        if "bounding_box_min" in mv or "bounding_box_max" in mv:
            mv_min = _coerce_vec3(mv.get("bounding_box_min"), [0.0, 0.0, 0.0])
            mv_max = _coerce_vec3(mv.get("bounding_box_max"), [0.0, 0.0, 0.0])
            view_bounds.append((mv_min, mv_max))
    return _merge_bounds(view_bounds)


def _collect_lod_bounds(lod: Dict[str, Any]) -> tuple[list[float], list[float]]:
    if "bounding_box_min" in lod or "bounding_box_max" in lod:
        lod_min = _coerce_vec3(lod.get("bounding_box_min"), [0.0, 0.0, 0.0])
        lod_max = _coerce_vec3(lod.get("bounding_box_max"), [0.0, 0.0, 0.0])
        return lod_min, lod_max

    submeshes = lod.get("submeshes", []) or []
    submesh_bounds: list[tuple[list[float], list[float]]] = []
    for submesh in submeshes:
        submesh_bounds.append(_collect_submesh_bounds(submesh))
    return _merge_bounds(submesh_bounds)


def pack_mesh_descriptor(
    lod: Dict[str, Any],
    resource_index_map: Dict[str, Dict[str, int]],
    pack_name_fn: Callable[[str, int], bytes],
    *,
    procedural_params_size_override: int | None = None,
) -> bytes:
    # Mirror legacy mesh packing (PackGen/packers.py)
    mesh_name = pack_name_fn(lod.get("name", ""), 64)
    mesh_type = lod.get("mesh_type", 0)
    submeshes = lod.get("submeshes", []) or []
    mesh_view_count = sum(len(sm.get("mesh_views", [])) for sm in submeshes)
    submesh_count = len(submeshes)
    vertex_buffer_idx = resource_index_map.get("buffer", {}).get(
        lod.get("vertex_buffer", ""), 0
    )
    index_buffer_idx = resource_index_map.get("buffer", {}).get(
        lod.get("index_buffer", ""), 0
    )
    if "bounding_box_min" in lod or "bounding_box_max" in lod:
        mesh_bb_min = _coerce_vec3(lod.get("bounding_box_min"), [0.0, 0.0, 0.0])
        mesh_bb_max = _coerce_vec3(lod.get("bounding_box_max"), [0.0, 0.0, 0.0])
    else:
        submesh_bounds: list[tuple[list[float], list[float]]] = []
        for submesh in submeshes:
            submesh_bounds.append(_collect_submesh_bounds(submesh))
        mesh_bb_min, mesh_bb_max = _merge_bounds(submesh_bounds)
    if procedural_params_size_override is None:
        procedural_params_size = int(lod.get("procedural_params_size", 0) or 0)
    else:
        procedural_params_size = int(procedural_params_size_override)
    joint_index_buffer_idx = resource_index_map.get("buffer", {}).get(
        lod.get("joint_index_buffer", ""), 0
    )
    joint_weight_buffer_idx = resource_index_map.get("buffer", {}).get(
        lod.get("joint_weight_buffer", ""), 0
    )
    inverse_bind_buffer_idx = resource_index_map.get("buffer", {}).get(
        lod.get("inverse_bind_buffer", ""), 0
    )
    joint_remap_buffer_idx = resource_index_map.get("buffer", {}).get(
        lod.get("joint_remap_buffer", ""), 0
    )
    skeleton_asset_key = _asset_key_bytes(lod.get("skeleton_asset_key"))
    joint_count = int(lod.get("joint_count", 0) or 0)
    influences_per_vertex = int(lod.get("influences_per_vertex", 0) or 0)
    skinned_flags = int(lod.get("skinned_flags", 0) or 0)
    # Mesh info block (72 bytes)
    if mesh_type == 2:  # procedural
        info = struct.pack("<I", procedural_params_size)
        info += b"\x00" * (72 - len(info))
    elif mesh_type == 3:  # skinned
        info = (
            struct.pack(
                "<IIIIII",
                vertex_buffer_idx,
                index_buffer_idx,
                joint_index_buffer_idx,
                joint_weight_buffer_idx,
                inverse_bind_buffer_idx,
                joint_remap_buffer_idx,
            )
            + skeleton_asset_key
            + struct.pack(
                "<HHI", joint_count, influences_per_vertex, skinned_flags
            )
            + struct.pack("<3f", *mesh_bb_min)
            + struct.pack("<3f", *mesh_bb_max)
        )
    else:  # standard
        info = (
            struct.pack("<I", vertex_buffer_idx)
            + struct.pack("<I", index_buffer_idx)
            + struct.pack("<3f", *mesh_bb_min)
            + struct.pack("<3f", *mesh_bb_max)
        )
        info += b"\x00" * (72 - len(info))
    desc = (
        mesh_name
        + struct.pack("<B", mesh_type)
        + struct.pack("<I", submesh_count)
        + struct.pack("<I", mesh_view_count)
        + info
    )
    if len(desc) != MESH_DESC_SIZE:
        raise PakError(
            "E_SIZE",
            f"Mesh descriptor size mismatch: expected {MESH_DESC_SIZE}, got {len(desc)}",
        )
    return desc


def pack_submesh_descriptor(
    submesh: Dict[str, Any],
    simple_assets: List[Dict[str, Any]],
    pack_name_fn: Callable[[str, int], bytes],
) -> bytes:
    # legacy layout: name(64) + material asset key(16) + mesh_view_count(u32) + bbox min/max (6*4) = 64+16+4+24+24 = 132? but spec size 108.
    # Spec actually: name(64) + AssetKey(16) + mesh_view_count(4) + bb_min(12) + bb_max(12) = 64+16+4+12+12 = 108.
    sm_name = pack_name_fn(submesh.get("name", ""), 64)
    mat_name = submesh.get("material")
    mat_key = None
    for a in simple_assets:
        if a.get("name") == mat_name:
            mat_key = a.get("key")
            break
    if mat_key is None:
        raise PakError(
            "E_REF",
            f"Material asset '{mat_name}' not found for submesh '{submesh.get('name','')}')",
        )
    if len(mat_key) != ASSET_KEY_SIZE:  # type: ignore[arg-type]
        raise PakError(
            "E_KEY_SIZE",
            f"Material asset key must be {ASSET_KEY_SIZE} bytes (got {len(mat_key)})",
        )
    mesh_views = submesh.get("mesh_views", []) or []
    sm_bb_min, sm_bb_max = _collect_submesh_bounds(submesh)
    desc = (
        sm_name
        + mat_key  # type: ignore[operator]
        + struct.pack("<I", len(mesh_views))
        + struct.pack("<3f", *sm_bb_min)
        + struct.pack("<3f", *sm_bb_max)
    )
    if len(desc) != SUBMESH_DESC_SIZE:
        raise PakError(
            "E_SIZE",
            f"Submesh descriptor size mismatch: expected {SUBMESH_DESC_SIZE}, got {len(desc)}",
        )
    return desc


def pack_mesh_view_descriptor(mesh_view: Dict[str, Any]) -> bytes:
    first_index = mesh_view.get("first_index", 0)
    index_count = mesh_view.get("index_count", 0)
    first_vertex = mesh_view.get("first_vertex", 0)
    vertex_count = mesh_view.get("vertex_count", 0)
    desc = struct.pack(
        "<4I", first_index, index_count, first_vertex, vertex_count
    )
    if len(desc) != MESH_VIEW_DESC_SIZE:
        raise PakError(
            "E_SIZE",
            f"Mesh view descriptor size mismatch: expected {MESH_VIEW_DESC_SIZE}, got {len(desc)}",
        )
    return desc


def pack_geometry_asset_descriptor(
    asset: Dict[str, Any], *, header_builder, lods_builder=None
) -> bytes:
    # Match legacy geometry descriptor layout: header + lod_count + bb_min + bb_max + reserved
    lods = asset.get("lods", [])
    if "bounding_box_min" in asset or "bounding_box_max" in asset:
        bb_min = _coerce_vec3(asset.get("bounding_box_min"), [0.0, 0.0, 0.0])
        bb_max = _coerce_vec3(asset.get("bounding_box_max"), [0.0, 0.0, 0.0])
    else:
        lod_bounds = [_collect_lod_bounds(lod) for lod in lods]
        bb_min, bb_max = _merge_bounds(lod_bounds)
    header = header_builder(asset)
    # header + lod_count + bb_min + bb_max; reserve any remaining descriptor bytes.
    reserved_len = GEOMETRY_DESC_SIZE - (len(header) + 4 + 12 + 12)
    if reserved_len < 0:
        raise PakError(
            "E_SIZE",
            f"Geometry header + fields exceed descriptor size (need {GEOMETRY_DESC_SIZE})",
        )
    desc = (
        header
        + struct.pack("<I", len(lods))
        + struct.pack("<3f", *bb_min)
        + struct.pack("<3f", *bb_max)
        + b"\x00" * reserved_len
    )
    if len(desc) != GEOMETRY_DESC_SIZE:
        raise PakError(
            "E_SIZE",
            f"Geometry descriptor size mismatch: expected {GEOMETRY_DESC_SIZE}, got {len(desc)}",
        )
    return desc
