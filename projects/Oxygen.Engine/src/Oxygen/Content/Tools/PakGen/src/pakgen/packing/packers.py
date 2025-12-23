"""Pure binary packing functions for PakGen (refactored).

All functions are side-effect free and validate sizes.
"""

from __future__ import annotations

import struct
from typing import Any, Dict, Sequence, List, Callable

from .constants import (
    MAGIC,
    FOOTER_MAGIC,
    ASSET_KEY_SIZE,
    MATERIAL_DESC_SIZE,
    GEOMETRY_DESC_SIZE,
    MESH_DESC_SIZE,
    SUBMESH_DESC_SIZE,
    MESH_VIEW_DESC_SIZE,
    DIRECTORY_ENTRY_SIZE,
    FOOTER_SIZE,
    SHADER_REF_DESC_SIZE,
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
    "pack_mesh_descriptor",
    "pack_submesh_descriptor",
    "pack_mesh_view_descriptor",
]


def pack_header(version: int, content_version: int) -> bytes:
    reserved = b"\x00" * 52
    data = struct.pack("<8sHH52s", MAGIC, version, content_version, reserved)
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
    material_domain = asset.get("material_domain", 0)
    flags = asset.get("flags", 0)
    shader_stages = asset.get("shader_stages", 0)
    base_color = asset.get("base_color", [1.0, 1.0, 1.0, 1.0])
    normal_scale = asset.get("normal_scale", 1.0)
    metalness = asset.get("metalness", 0.0)
    roughness = asset.get("roughness", 1.0)
    ambient_occlusion = asset.get("ambient_occlusion", 1.0)
    texture_refs = asset.get("texture_refs", {})
    texture_map = resource_index_map.get("texture", {})

    def get_texture_index(field: str) -> int:
        ref = texture_refs.get(field)
        return texture_map.get(ref, 0) if ref else 0

    indices = [
        get_texture_index("base_color_texture"),
        get_texture_index("normal_texture"),
        get_texture_index("metallic_texture"),
        get_texture_index("roughness_texture"),
        get_texture_index("ambient_occlusion_texture"),
    ]
    reserved_textures = [0] * 8
    header = header_builder(asset)
    desc = (
        header
        + struct.pack("<B", material_domain)
        + struct.pack("<I", flags)
        + struct.pack("<I", shader_stages)
        + struct.pack("<4f", *base_color)
        + struct.pack("<f", normal_scale)
        + struct.pack("<f", metalness)
        + struct.pack("<f", roughness)
        + struct.pack("<f", ambient_occlusion)
        + b"".join(struct.pack("<I", i) for i in indices)
        + b"".join(struct.pack("<I", t) for t in reserved_textures)
        + b"\x00" * 68
    )
    if len(desc) < MATERIAL_DESC_SIZE:
        # Pad to fixed size (transitional placeholder padding)
        desc += b"\x00" * (MATERIAL_DESC_SIZE - len(desc))
    if len(desc) != MATERIAL_DESC_SIZE:
        raise PakError(
            "E_SIZE",
            f"Material descriptor size mismatch after padding: expected {MATERIAL_DESC_SIZE}, got {len(desc)}",
        )
    return desc


def pack_shader_reference_entries(shader_refs: List[Dict[str, Any]]) -> bytes:
    """Pack variable shader reference entries following a material descriptor.

    Each reference structure mirrors ShaderReferenceDesc in PakFormat.h:
    - shader_unique_id: 192 bytes (UTF-8, null padded)
    - shader_hash: 8 bytes (uint64)
    - reserved: 16 bytes
    Total = 216 bytes.
    """
    out = b""
    for ref in shader_refs:
        unique_id = ref.get("shader_unique_id") or ref.get("id") or ""
        if not isinstance(unique_id, str):
            unique_id = str(unique_id)
        raw = unique_id.encode("utf-8")[:191]
        unique_bytes = raw + b"\x00" * (192 - len(raw))
        shader_hash = int(ref.get("shader_hash", 0)) & 0xFFFFFFFFFFFFFFFF
        reserved = b"\x00" * 16
        entry = unique_bytes + struct.pack("<Q", shader_hash) + reserved
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
    desc = (
        struct.pack("<Q", data_offset)
        + struct.pack("<I", data_size)
        + struct.pack("<I", usage_flags)
        + struct.pack("<I", element_stride)
        + struct.pack("<B", element_format)
        + b"\x00" * 11
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
        + b"\x00" * 9
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
