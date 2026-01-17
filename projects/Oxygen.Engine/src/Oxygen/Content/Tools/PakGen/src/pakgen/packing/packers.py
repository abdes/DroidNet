"""Pure binary packing functions for PakGen (refactored).

All functions are side-effect free and validate sizes.
"""

from __future__ import annotations

import struct
from typing import Any, Dict, Sequence, List, Callable, Tuple

from .constants import (
    MAGIC,
    FOOTER_MAGIC,
    ASSET_HEADER_SIZE,
    ASSET_KEY_SIZE,
    MATERIAL_DESC_SIZE,
    GEOMETRY_DESC_SIZE,
    SCENE_DESC_SIZE,
    MESH_DESC_SIZE,
    SUBMESH_DESC_SIZE,
    MESH_VIEW_DESC_SIZE,
    DIRECTORY_ENTRY_SIZE,
    FOOTER_SIZE,
    SHADER_REF_DESC_SIZE,
    SCENE_ASSET_VERSION_CURRENT,
)
from .errors import PakError

__all__ = [
    "pack_header",
    "pack_footer",
    "pack_directory_entry",
    "pack_material_asset_descriptor",
    "pack_buffer_resource_descriptor",
    "pack_texture_resource_descriptor",
    "pack_audio_resource_descriptor",
    "pack_geometry_asset_descriptor",
    "pack_scene_asset_descriptor_and_payload",
    "pack_mesh_descriptor",
    "pack_submesh_descriptor",
    "pack_mesh_view_descriptor",
]


_COMPONENT_TYPE_RENDERABLE = 0x4853454D  # 'MESH'
_COMPONENT_TYPE_PERSPECTIVE_CAMERA = 0x4D414350  # 'PCAM'
_COMPONENT_TYPE_ORTHOGRAPHIC_CAMERA = 0x4D41434F  # 'OCAM'
_COMPONENT_TYPE_DIRECTIONAL_LIGHT = 0x54494C44  # 'DLIT'
_COMPONENT_TYPE_POINT_LIGHT = 0x54494C50  # 'PLIT'
_COMPONENT_TYPE_SPOT_LIGHT = 0x54494C53  # 'SLIT'

_ENV_SYSTEM_SKY_ATMOSPHERE = 0
_ENV_SYSTEM_VOLUMETRIC_CLOUDS = 1
_ENV_SYSTEM_FOG = 2
_ENV_SYSTEM_SKY_LIGHT = 3
_ENV_SYSTEM_SKY_SPHERE = 4
_ENV_SYSTEM_POST_PROCESS_VOLUME = 5


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
        + b"\x00" * 3
    )
    if len(out) != 16:
        raise PakError(
            "E_SIZE", f"LightShadowSettingsRecord size mismatch: {len(out)}"
        )
    return out


def _pack_light_common_record(light: Dict[str, Any]) -> bytes:
    affects_world = _u32_bool(light.get("affects_world"), 1)
    color = light.get("color_rgb", light.get("color", [1.0, 1.0, 1.0]))
    cr, cg, cb = _vec3(color, [1.0, 1.0, 1.0])
    intensity = _f(light.get("intensity"), 1.0)
    mobility = _u8(light.get("mobility"), 0)
    casts_shadows = _u8(light.get("casts_shadows"), 0)
    shadow = _pack_light_shadow_settings_record(light.get("shadow"))
    exposure_comp = _f(light.get("exposure_compensation_ev"), 0.0)

    out = (
        struct.pack("<I3ff", int(affects_world), cr, cg, cb, intensity)
        + struct.pack("<BB", int(mobility), int(casts_shadows))
        + b"\x00" * 2
        + shadow
        + struct.pack("<f", exposure_comp)
        + b"\x00" * 4
    )
    if len(out) != 48:
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
    distribution = _f(light.get("distribution_exponent"), 1.0)

    out = (
        struct.pack("<I", int(node_index))
        + common
        + struct.pack("<f", angular_size)
        + struct.pack("<I", int(env_contrib))
        + struct.pack("<I", int(is_sun_light))
        + struct.pack("<I", int(cascade_count))
        + struct.pack("<4f", *cascade_distances)
        + struct.pack("<f", distribution)
        + b"\x00" * 8
    )
    if len(out) != 96:
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

    out = (
        struct.pack("<I", int(node_index))
        + common
        + struct.pack("<f", rng)
        + struct.pack("<B", int(attenuation_model))
        + b"\x00" * 3
        + struct.pack("<f", decay)
        + struct.pack("<f", source_radius)
        + b"\x00" * 12
    )
    if len(out) != 80:
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

    out = (
        struct.pack("<I", int(node_index))
        + common
        + struct.pack("<f", rng)
        + struct.pack("<B", int(attenuation_model))
        + b"\x00" * 3
        + struct.pack("<f", decay)
        + struct.pack("<f", inner)
        + struct.pack("<f", outer)
        + struct.pack("<f", source_radius)
        + b"\x00" * 12
    )
    if len(out) != 88:
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
    mie_scale = _f(spec.get("mie_scale_height_m"), 1200.0)
    mie_g = _f(spec.get("mie_g"), 0.8)
    ab = spec.get("absorption_rgb", [0.0, 0.0, 0.0])
    absorption = _vec3(ab, [0.0, 0.0, 0.0])
    absorption_scale = _f(spec.get("absorption_scale_height_m"), 25000.0)
    multi_scattering = _f(spec.get("multi_scattering_factor"), 1.0)
    sun_disk_enabled = _u32_bool(spec.get("sun_disk_enabled"), 1)
    sun_disk_radius = _f(spec.get("sun_disk_angular_radius_radians"), 0.004675)
    aerial_scale = _f(spec.get("aerial_perspective_distance_scale"), 1.0)

    record_size = 116
    out = (
        _pack_env_record_header(_ENV_SYSTEM_SKY_ATMOSPHERE, record_size)
        + struct.pack("<I", int(enabled))
        + struct.pack("<ff", planet_radius_m, atmosphere_height_m)
        + struct.pack("<3f", *ground_albedo)
        + struct.pack("<3f", *rayleigh)
        + struct.pack("<f", rayleigh_scale)
        + struct.pack("<3f", *mie)
        + struct.pack("<f", mie_scale)
        + struct.pack("<f", mie_g)
        + struct.pack("<3f", *absorption)
        + struct.pack("<f", absorption_scale)
        + struct.pack("<f", multi_scattering)
        + struct.pack("<I", int(sun_disk_enabled))
        + struct.pack("<f", sun_disk_radius)
        + struct.pack("<f", aerial_scale)
        + b"\x00" * 16
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
    density = _f(spec.get("density"), 0.5)
    albedo = _vec3(spec.get("albedo_rgb", [0.9, 0.9, 0.9]), [0.9, 0.9, 0.9])
    extinction = _f(spec.get("extinction_scale"), 1.0)
    phase_g = _f(spec.get("phase_g"), 0.6)
    wind_dir = _vec3(spec.get("wind_dir_ws", [1.0, 0.0, 0.0]), [1.0, 0.0, 0.0])
    wind_speed = _f(spec.get("wind_speed_mps"), 10.0)
    shadow_strength = _f(spec.get("shadow_strength"), 0.8)

    record_size = 84
    out = (
        _pack_env_record_header(_ENV_SYSTEM_VOLUMETRIC_CLOUDS, record_size)
        + struct.pack("<I", int(enabled))
        + struct.pack("<ff", base_altitude, thickness)
        + struct.pack("<ff", coverage, density)
        + struct.pack("<3f", *albedo)
        + struct.pack("<f", extinction)
        + struct.pack("<f", phase_g)
        + struct.pack("<3f", *wind_dir)
        + struct.pack("<f", wind_speed)
        + struct.pack("<f", shadow_strength)
        + b"\x00" * 16
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

    record_size = 72
    out = (
        _pack_env_record_header(_ENV_SYSTEM_SKY_LIGHT, record_size)
        + struct.pack("<I", int(enabled))
        + struct.pack("<I", source)
        + cubemap
        + struct.pack("<f", intensity)
        + struct.pack("<3f", *tint)
        + struct.pack("<f", diffuse)
        + struct.pack("<f", specular)
        + b"\x00" * 16
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

    record_size = 80
    out = (
        _pack_env_record_header(_ENV_SYSTEM_SKY_SPHERE, record_size)
        + struct.pack("<I", int(enabled))
        + struct.pack("<I", source)
        + cubemap
        + struct.pack("<3f", *solid_color)
        + struct.pack("<f", intensity)
        + struct.pack("<f", rotation)
        + struct.pack("<3f", *tint)
        + b"\x00" * 16
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

    record_size = 76
    out = (
        _pack_env_record_header(_ENV_SYSTEM_POST_PROCESS_VOLUME, record_size)
        + struct.pack("<I", int(enabled))
        + struct.pack("<II", tone_mapper, exposure_mode)
        + struct.pack("<f", exposure_comp)
        + struct.pack("<ffff", ae_min, ae_max, ae_up, ae_down)
        + struct.pack("<ff", bloom_intensity, bloom_threshold)
        + struct.pack("<fff", saturation, contrast, vignette)
        + b"\x00" * 16
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
    density = _f(spec.get("density"), 0.01)
    height_falloff = _f(spec.get("height_falloff"), 0.2)
    height_offset_m = _f(spec.get("height_offset_m"), 0.0)
    start_distance_m = _f(spec.get("start_distance_m"), 0.0)
    max_opacity = _f(spec.get("max_opacity"), 1.0)
    albedo = _vec3(spec.get("albedo_rgb", [1.0, 1.0, 1.0]), [1.0, 1.0, 1.0])
    anisotropy_g = _f(spec.get("anisotropy_g"), 0.0)
    scattering_intensity = _f(spec.get("scattering_intensity"), 1.0)

    record_size = 72
    out = (
        _pack_env_record_header(_ENV_SYSTEM_FOG, record_size)
        + struct.pack("<I", int(enabled))
        + struct.pack("<I", model)
        + struct.pack("<f", density)
        + struct.pack("<f", height_falloff)
        + struct.pack("<f", height_offset_m)
        + struct.pack("<f", start_distance_m)
        + struct.pack("<f", max_opacity)
        + struct.pack("<3f", *albedo)
        + struct.pack("<f", anisotropy_g)
        + struct.pack("<f", scattering_intensity)
        + b"\x00" * 16
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
    byte_size = 16 + sum(len(r) for r in records)
    header = struct.pack(
        "<II8s", int(byte_size), int(systems_count), b"\x00" * 8
    )
    if len(header) != 16:
        raise PakError(
            "E_SIZE",
            f"SceneEnvironmentBlockHeader size mismatch: {len(header)}",
        )
    return header + b"".join(records)


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

    visible = renderable.get("visible", 1)
    visible_u32 = 1 if bool(visible) else 0
    reserved = b"\x00" * 12
    out = (
        struct.pack("<I", int(node_index))
        + geometry_key
        + struct.pack("<I", int(visible_u32))
        + reserved
    )
    if len(out) != 36:
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
    reserved = b"\x00" * 12

    out = (
        struct.pack("<I", int(node_index))
        + struct.pack("<4f", fov_y, aspect_ratio, near_plane, far_plane)
        + reserved
    )
    if len(out) != 32:
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
    reserved = b"\x00" * 12

    out = (
        struct.pack("<I", int(node_index))
        + struct.pack("<6f", left, right, bottom, top, near_plane, far_plane)
        + reserved
    )
    if len(out) != 40:
        raise PakError(
            "E_SIZE", f"OrthographicCameraRecord size mismatch: {len(out)}"
        )
    return out


def pack_scene_asset_descriptor_and_payload(
    scene: Dict[str, Any],
    *,
    header_builder,
    geometry_name_to_key: Dict[str, bytes],
) -> Tuple[bytes, bytes]:
    """Pack SceneAssetDesc (256 bytes) plus trailing payload.

    Payload layout (offsets are relative to descriptor start):
    - NodeRecord[]
    - scene string table (starts with NUL)
    - SceneComponentTableDesc[] directory (optional)
    - component table record data (optional)

    Supported component tables:
    - RenderableRecord table (component_type 'MESH')
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
    renderable_records = b"".join(
        _pack_renderable_record(r, geometry_name_to_key, node_count=node_count)
        for r in renderables
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
                36,
                renderable_records,
            )
        )
    if camera_records:
        component_tables.append(
            (
                _COMPONENT_TYPE_PERSPECTIVE_CAMERA,
                len(cameras),
                32,
                camera_records,
            )
        )
    if ortho_camera_records:
        component_tables.append(
            (
                _COMPONENT_TYPE_ORTHOGRAPHIC_CAMERA,
                len(ortho_cameras),
                40,
                ortho_camera_records,
            )
        )
    if directional_light_records:
        component_tables.append(
            (
                _COMPONENT_TYPE_DIRECTIONAL_LIGHT,
                len(directional_lights),
                96,
                directional_light_records,
            )
        )
    if point_light_records:
        component_tables.append(
            (
                _COMPONENT_TYPE_POINT_LIGHT,
                len(point_lights),
                80,
                point_light_records,
            )
        )
    if spot_light_records:
        component_tables.append(
            (
                _COMPONENT_TYPE_SPOT_LIGHT,
                len(spot_lights),
                88,
                spot_light_records,
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
    # Scene descriptor version. Mirrors pak::v3::kSceneAssetVersion.
    scene.setdefault("version", SCENE_ASSET_VERSION_CURRENT)
    header = header_builder(scene)
    nodes_table = struct.pack("<QII", nodes_offset, node_count, 68)
    scene_strings = struct.pack("<II", strings_offset, strings_size)
    dir_off = struct.pack("<Q", int(component_table_directory_offset))
    dir_count = struct.pack("<I", int(component_table_count))

    desc = (
        header
        + nodes_table
        + scene_strings
        + dir_off
        + dir_count
        + b"\x00" * 125
    )
    if len(desc) != SCENE_DESC_SIZE:
        raise PakError(
            "E_SIZE",
            f"Scene descriptor size mismatch: expected {SCENE_DESC_SIZE}, got {len(desc)}",
        )

    payload = (
        node_records
        + string_table
        + b"".join(component_entries)
        + b"".join(component_data)
    )

    payload += _pack_scene_environment_block(scene)
    return desc, payload


def pack_header(version: int, content_version: int, guid: bytes) -> bytes:
    if len(guid) != 16:
        raise PakError("E_SIZE", f"GUID size mismatch: {len(guid)}")
    if guid == b"\x00" * 16:
        raise PakError("E_GUID", "PAK header GUID must be non-zero")
    reserved = b"\x00" * 36
    data = struct.pack(
        "<8sHH16s36s", MAGIC, version, content_version, guid, reserved
    )
    if len(data) != 64:
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
    texture_table: Sequence[int],
    buffer_table: Sequence[int],
    audio_table: Sequence[int],
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

    reserved = b"\x00" * 108
    footer = (
        struct.pack("<QQQ", directory_offset, directory_size, asset_count)
        + pack_region(texture_region)
        + pack_region(buffer_region)
        + pack_region(audio_region)
        + pack_table(texture_table)
        + pack_table(buffer_table)
        + pack_table(audio_table)
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
    """Pack fixed 256-byte MaterialAssetDesc (no trailing shader refs).

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
    header = header_builder(asset)
    if len(header) != ASSET_HEADER_SIZE:
        raise PakError(
            "E_SIZE",
            f"AssetHeader size mismatch: expected {ASSET_HEADER_SIZE} got {len(header)}",
        )

    # Match oxygen::data::pak::MaterialAssetDesc exactly (see PakFormat.h).
    reserved = b"\x00" * 19
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
    desc += reserved
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
    - reserved0: 7 bytes
    - source_path: 120 bytes (UTF-8, null padded)
    - entry_point: 32 bytes (UTF-8, null padded)
    - defines: 256 bytes (UTF-8, null padded)
    - shader_hash: 8 bytes (uint64)
    Total = 424 bytes.
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
        reserved0 = b"\x00" * 7
        entry = (
            struct.pack("<B", stage)
            + reserved0
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
        + b"\x00" * 3
    )
    if len(desc) != 32:
        raise PakError(
            "E_SIZE", f"Buffer descriptor size mismatch: {len(desc)} != 32"
        )
    return desc


def pack_texture_resource_descriptor(
    resource_spec: Dict[str, Any], data_offset: int, data_size: int
) -> bytes:
    # Match legacy layout (see PackGen/packers.py + PakFormat.h TextureResourceDesc)
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
        + struct.pack("<I", width)
        + struct.pack("<I", height)
        + struct.pack("<H", depth)
        + struct.pack("<H", array_layers)
        + struct.pack("<H", mip_levels)
        + struct.pack("<B", format_val)
        + struct.pack("<H", alignment)
        + struct.pack("<Q", content_hash)
        + b"\x00"
    )
    if len(desc) != 40:
        raise PakError(
            "E_SIZE", f"Texture descriptor size mismatch: {len(desc)} != 40"
        )
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


def pack_name_string(name: str, size: int) -> bytes:
    # Truncate UTF-8 to fit size-1 then null pad to fixed size
    if not isinstance(name, str):
        name = str(name)
    raw = name.encode("utf-8")[: size - 1]
    return raw + b"\x00" * (size - len(raw))


def pack_mesh_descriptor(
    lod: Dict[str, Any],
    resource_index_map: Dict[str, Dict[str, int]],
    pack_name_fn: Callable[[str, int], bytes],
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
    mesh_bb_min = lod.get("bounding_box_min", [0.0, 0.0, 0.0])
    mesh_bb_max = lod.get("bounding_box_max", [0.0, 0.0, 0.0])
    procedural_params_size = lod.get("procedural_params_size", 0)
    # Mesh info block (32 bytes)
    if mesh_type == 2:  # procedural
        info = struct.pack("<I", procedural_params_size) + b"\x00" * (32 - 4)
    else:  # standard
        info = (
            struct.pack("<I", vertex_buffer_idx)
            + struct.pack("<I", index_buffer_idx)
            + struct.pack("<3f", *mesh_bb_min)
            + struct.pack("<3f", *mesh_bb_max)
        )
        info += b"\x00" * (32 - len(info))
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
    sm_bb_min = submesh.get("bounding_box_min", [0.0, 0.0, 0.0])
    sm_bb_max = submesh.get("bounding_box_max", [0.0, 0.0, 0.0])
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
    bb_min = asset.get("bounding_box_min", [0.0, 0.0, 0.0])
    bb_max = asset.get("bounding_box_max", [0.0, 0.0, 0.0])
    header = header_builder(asset)
    # header (95) + lod_count(4) + bb_min(12) + bb_max(12) = 123, need 256 -> 133 bytes reserved
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
