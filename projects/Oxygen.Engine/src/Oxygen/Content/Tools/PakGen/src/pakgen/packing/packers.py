"""Pure binary packing functions for PakGen (refactored).

All functions are side-effect free and validate sizes.
"""

from __future__ import annotations

import struct
from typing import Any, Dict, Sequence, List, Callable, Tuple

from .constants import (
    MAGIC,
    FOOTER_MAGIC,
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

    Currently supported component tables:
    - RenderableRecord table (component_type 'MESH')
    - PerspectiveCameraRecord table (component_type 'PCAM')
    - OrthographicCameraRecord table (component_type 'OCAM')
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
