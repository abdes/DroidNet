"""Specification & binary validation.

Phases:
 1. schema: structural & type checks
 2. semantic: cross-field logic, references, limits
 3. cross: (reserved) multi-asset/global invariants (future)
 4. binary: plan/file structural coherence (added)

Returns list of ValidationErrorRecord; empty list means success.
"""

from __future__ import annotations
from typing import Any, List, Dict

from ..packing.constants import (
    ASSET_NAME_MAX_LENGTH,
    MAX_RESOURCES_PER_TYPE,
    MAX_ASSETS_TOTAL,
    MAX_TEXTURE_DIMENSION,
    MAX_TEXTURE_LAYERS,
    MAX_TEXTURE_MIP_LEVELS,
    MAX_BUFFER_STRIDE,
    MAX_VERTEX_COUNT,
    MAX_INDEX_COUNT,
    MAX_LODS_PER_GEOMETRY,
    MAX_SUBMESHES_PER_LOD,
    MAX_MESH_VIEWS_PER_SUBMESH,
    VALID_MESH_TYPES,
    YAML_SCHEMA_VERSION_CURRENT,
    YAML_SCHEMA_VERSION_MIN,
)


class ValidationErrorRecord:
    def __init__(self, code: str, message: str, path: str = "") -> None:
        self.code = code
        self.message = message
        self.path = path

    def to_dict(self) -> dict:
        return {"code": self.code, "message": self.message, "path": self.path}

    def __repr__(self) -> str:  # convenience for tests
        return f"ValidationErrorRecord(code={self.code}, path={self.path}, message={self.message})"


def _err(
    errors: List[ValidationErrorRecord], code: str, message: str, path: str
):
    errors.append(ValidationErrorRecord(code, message, path))


def _schema_phase(spec: Dict[str, Any]) -> List[ValidationErrorRecord]:
    errors: List[ValidationErrorRecord] = []

    # Validate schema version first
    version = spec.get("version", 1)
    if not isinstance(version, int) or version < YAML_SCHEMA_VERSION_MIN:
        _err(
            errors,
            "E_VERSION",
            f"Schema version must be >= {YAML_SCHEMA_VERSION_MIN}",
            "version",
        )
    elif version > YAML_SCHEMA_VERSION_CURRENT:
        _err(
            errors,
            "E_VERSION_FUTURE",
            f"Schema version {version} not supported (max: {YAML_SCHEMA_VERSION_CURRENT})",
            "version",
        )

    # Check for 'generate' directive usage with version < 4
    assets_list = spec.get("assets", [])
    if isinstance(assets_list, list) and version < 4:
        has_generate = any(
            isinstance(node, dict) and node.get("generate") is not None
            for asset in assets_list
            if isinstance(asset, dict)
            for node in (asset.get("nodes") or [])
        )
        if has_generate:
            _err(
                errors,
                "W_VERSION_MISMATCH",
                "Using 'generate' directive requires version >= 4; update to 'version: 4'",
                "version",
            )

    # Top-level required keys (version allowed default)
    for key in ["buffers", "textures", "audios"]:
        if key in spec and not isinstance(spec[key], list):
            _err(errors, "E_TYPE", f"'{key}' must be a list", key)
    # Unified assets list required (even if empty)
    assets_list = spec.get("assets", [])
    if not isinstance(assets_list, list):
        _err(errors, "E_TYPE", "'assets' must be a list", "assets")
    # Resource entries basic checks
    for rtype in ["buffer", "texture", "audio"]:
        plural = rtype + "s"
        entries = spec.get(plural, []) or []
        if len(entries) > MAX_RESOURCES_PER_TYPE:
            _err(errors, "E_COUNT", f"Too many {rtype} resources", plural)
        for i, e in enumerate(entries):
            path = f"{plural}[{i}]"
            if not isinstance(e, dict):
                _err(errors, "E_TYPE", "Entry must be object", path)
                continue
            name = e.get("name")
            if not isinstance(name, str):
                _err(errors, "E_FIELD", "Missing or invalid name", path)
            elif len(name.encode("utf-8")) > ASSET_NAME_MAX_LENGTH:
                _err(errors, "E_NAME_LEN", "Name too long", path + ".name")
            if rtype == "texture":
                w = e.get("width", 0)
                h = e.get("height", 0)
                if w < 0 or w > MAX_TEXTURE_DIMENSION:
                    _err(
                        errors, "E_RANGE", "Width out of range", path + ".width"
                    )
                if h < 0 or h > MAX_TEXTURE_DIMENSION:
                    _err(
                        errors,
                        "E_RANGE",
                        "Height out of range",
                        path + ".height",
                    )
                layers = e.get("array_layers", 1)
                if layers > MAX_TEXTURE_LAYERS:
                    _err(
                        errors,
                        "E_RANGE",
                        "array_layers too large",
                        path + ".array_layers",
                    )
                mips = e.get("mip_levels", 1)
                if mips > MAX_TEXTURE_MIP_LEVELS:
                    _err(
                        errors,
                        "E_RANGE",
                        "mip_levels too large",
                        path + ".mip_levels",
                    )
            if rtype == "buffer":
                stride = e.get("stride", 0)
                if stride < 0 or stride > MAX_BUFFER_STRIDE:
                    _err(
                        errors,
                        "E_RANGE",
                        "stride out of range",
                        path + ".stride",
                    )
    # Assets partition & limits
    mats = [
        a
        for a in assets_list
        if isinstance(a, dict) and a.get("type") == "material"
    ]
    geos = [
        a
        for a in assets_list
        if isinstance(a, dict) and a.get("type") == "geometry"
    ]
    scenes = [
        a
        for a in assets_list
        if isinstance(a, dict) and a.get("type") == "scene"
    ]
    if len(mats) + len(geos) + len(scenes) > MAX_ASSETS_TOTAL:
        _err(errors, "E_COUNT", "Total assets exceed limit", "assets")
    for i, m in enumerate(mats):
        path = f"assets[{i}]"  # material
        if not isinstance(m, dict):
            _err(errors, "E_TYPE", "Asset must be object", path)
            continue
        name = m.get("name")
        if not isinstance(name, str):
            _err(errors, "E_FIELD", "Missing name", path)
        elif len(name.encode("utf-8")) > ASSET_NAME_MAX_LENGTH:
            _err(errors, "E_NAME_LEN", "Name too long", path + ".name")

        uv_scale = m.get("uv_scale")
        if uv_scale is not None:
            if not isinstance(uv_scale, list) or len(uv_scale) != 2:
                _err(
                    errors,
                    "E_TYPE",
                    "uv_scale must be a list of 2 floats",
                    path + ".uv_scale",
                )
        uv_offset = m.get("uv_offset")
        if uv_offset is not None:
            if not isinstance(uv_offset, list) or len(uv_offset) != 2:
                _err(
                    errors,
                    "E_TYPE",
                    "uv_offset must be a list of 2 floats",
                    path + ".uv_offset",
                )
        uv_rotation = m.get("uv_rotation_radians")
        if uv_rotation is not None and not isinstance(
            uv_rotation, (int, float)
        ):
            _err(
                errors,
                "E_TYPE",
                "uv_rotation_radians must be a number",
                path + ".uv_rotation_radians",
            )
        uv_set = m.get("uv_set")
        if uv_set is not None:
            if not isinstance(uv_set, int) or uv_set < 0 or uv_set > 255:
                _err(
                    errors,
                    "E_RANGE",
                    "uv_set must be in [0, 255]",
                    path + ".uv_set",
                )
    for i, g in enumerate(geos):
        path = f"assets[{i}]"  # geometry
        if not isinstance(g, dict):
            _err(errors, "E_TYPE", "Asset must be object", path)
            continue
        name = g.get("name")
        if not isinstance(name, str):
            _err(errors, "E_FIELD", "Missing name", path)
        elif len(name.encode("utf-8")) > ASSET_NAME_MAX_LENGTH:
            _err(errors, "E_NAME_LEN", "Name too long", path + ".name")
        lods = g.get("lods", []) or []
        if len(lods) > MAX_LODS_PER_GEOMETRY:
            _err(errors, "E_COUNT", "Too many LODs", path + ".lods")
        for li, lod in enumerate(lods):
            lpath = f"{path}.lods[{li}]"
            subs = lod.get("submeshes", []) if isinstance(lod, dict) else []
            if isinstance(lod, dict) and len(subs) > MAX_SUBMESHES_PER_LOD:
                _err(
                    errors,
                    "E_COUNT",
                    "Too many submeshes",
                    lpath + ".submeshes",
                )
            for si, sub in enumerate(subs):
                spath = f"{lpath}.submeshes[{si}]"
                if (
                    isinstance(sub, dict)
                    and len(sub.get("mesh_views", []) or [])
                    > MAX_MESH_VIEWS_PER_SUBMESH
                ):
                    _err(
                        errors,
                        "E_COUNT",
                        "Too many mesh views",
                        spath + ".mesh_views",
                    )

        for i, s in enumerate(scenes):
            path = f"assets[{i}]"  # scene
            if not isinstance(s, dict):
                _err(errors, "E_TYPE", "Asset must be object", path)
                continue
            name = s.get("name")
            if not isinstance(name, str):
                _err(errors, "E_FIELD", "Missing name", path)
            elif len(name.encode("utf-8")) > ASSET_NAME_MAX_LENGTH:
                _err(errors, "E_NAME_LEN", "Name too long", path + ".name")

            nodes = s.get("nodes")
            if nodes is None:
                _err(errors, "E_FIELD", "Scene missing nodes", path + ".nodes")
            elif not isinstance(nodes, list):
                _err(errors, "E_TYPE", "nodes must be a list", path + ".nodes")
            else:
                if len(nodes) == 0:
                    _err(
                        errors,
                        "E_COUNT",
                        "Scene must contain at least one node",
                        path + ".nodes",
                    )
                for ni, node in enumerate(nodes):
                    npath = f"{path}.nodes[{ni}]"
                    if not isinstance(node, dict):
                        _err(errors, "E_TYPE", "Node must be object", npath)
                        continue
                    nname = node.get("name")
                    if not isinstance(nname, str):
                        _err(
                            errors,
                            "E_FIELD",
                            "Node missing name",
                            npath + ".name",
                        )
                    elif len(nname.encode("utf-8")) > ASSET_NAME_MAX_LENGTH:
                        _err(
                            errors,
                            "E_NAME_LEN",
                            "Name too long",
                            npath + ".name",
                        )
                    parent = node.get("parent")
                    if parent is not None and not isinstance(parent, int):
                        _err(
                            errors,
                            "E_TYPE",
                            "parent must be int or null",
                            npath + ".parent",
                        )
                    node_id = node.get("node_id")
                    if node_id is not None and not isinstance(node_id, str):
                        _err(
                            errors,
                            "E_TYPE",
                            "node_id must be hex string",
                            npath + ".node_id",
                        )

            renderables = s.get("renderables")
            if renderables is not None and not isinstance(renderables, list):
                _err(
                    errors,
                    "E_TYPE",
                    "renderables must be a list",
                    path + ".renderables",
                )

            perspective_cameras = s.get("perspective_cameras")
            if perspective_cameras is not None and not isinstance(
                perspective_cameras, list
            ):
                _err(
                    errors,
                    "E_TYPE",
                    "perspective_cameras must be a list",
                    path + ".perspective_cameras",
                )

            orthographic_cameras = s.get("orthographic_cameras")
            if orthographic_cameras is not None and not isinstance(
                orthographic_cameras, list
            ):
                _err(
                    errors,
                    "E_TYPE",
                    "orthographic_cameras must be a list",
                    path + ".orthographic_cameras",
                )
    return errors


def _semantic_phase(spec: Dict[str, Any]) -> List[ValidationErrorRecord]:
    errors: List[ValidationErrorRecord] = []
    # Buffer stride/data size coherence (only when inline hex provided).
    # Enforce: if stride > 0 then total data bytes must be an exact multiple of stride.
    # This catches manual hex edits that drift from declared element layout.
    for i, b in enumerate(spec.get("buffers", []) or []):
        if not isinstance(b, dict):
            continue
        stride = b.get("stride", 0) or 0
        data_hex = b.get("data_hex")
        if stride and data_hex and isinstance(data_hex, str):
            # Normalize hex (same rules as io.read_data_from_spec but lighter): remove spaces/newlines.
            h = data_hex.replace(" ", "").replace("\n", "")
            if len(h) % 2 != 0:
                _err(
                    errors,
                    "E_HEX_LEN",
                    "Buffer data_hex must have even number of hex chars",
                    f"buffers[{i}].data_hex",
                )
            else:
                byte_len = len(h) // 2
                if stride > 0 and byte_len % stride != 0:
                    _err(
                        errors,
                        "E_STRIDE_MULT",
                        f"Buffer data size {byte_len} not multiple of stride {stride}",
                        f"buffers[{i}].data_hex",
                    )
    # Duplicate names per resource type
    for rtype in ["buffers", "textures", "audios"]:
        seen = set()
        for i, e in enumerate(spec.get(rtype, []) or []):
            name = e.get("name") if isinstance(e, dict) else None
            if name and name in seen:
                _err(
                    errors,
                    "E_DUP",
                    f"Duplicate {rtype[:-1]} name",
                    f"{rtype}[{i}].name",
                )
            seen.add(name)
    # Asset name uniqueness across all assets
    asset_seen = set()
    for i, a in enumerate(spec.get("assets", []) or []):
        if not isinstance(a, dict):
            continue
        n = a.get("name")
        if not isinstance(n, str):
            continue
        if n in asset_seen:
            _err(errors, "E_DUP", "Duplicate asset name", f"assets[{i}].name")
        asset_seen.add(n)
    # Material texture_refs must reference existing textures
    texture_names = {
        e.get("name")
        for e in (spec.get("textures", []) or [])
        if isinstance(e, dict)
    }
    for i, m in enumerate(
        [
            a
            for a in (spec.get("assets", []) or [])
            if isinstance(a, dict) and a.get("type") == "material"
        ]
    ):
        if not isinstance(m, dict):
            continue
        tref_map = m.get("texture_refs", {}) or {}
        if not isinstance(tref_map, dict):
            _err(
                errors,
                "E_TYPE",
                "texture_refs must be object",
                f"materials[{i}].texture_refs",
            )
            continue
        for field, ref in tref_map.items():
            if ref and ref not in texture_names:
                _err(
                    errors,
                    "E_REF",
                    f"Unknown texture '{ref}'",
                    f"materials[{i}].texture_refs.{field}",
                )
    # Geometry LOD buffer references must exist
    buffer_names = {
        e.get("name")
        for e in (spec.get("buffers", []) or [])
        if isinstance(e, dict)
    }
    for gi, g in enumerate(
        [
            a
            for a in (spec.get("assets", []) or [])
            if isinstance(a, dict) and a.get("type") == "geometry"
        ]
    ):
        if not isinstance(g, dict):
            continue
        for li, lod in enumerate(g.get("lods", []) or []):
            if not isinstance(lod, dict):
                continue
            mesh_type = lod.get("mesh_type", 0)
            if mesh_type not in VALID_MESH_TYPES:
                _err(
                    errors,
                    "E_RANGE",
                    f"Invalid mesh_type '{mesh_type}'",
                    f"geometries[{gi}].lods[{li}].mesh_type",
                )
            vb = lod.get("vertex_buffer")
            ib = lod.get("index_buffer")
            if vb and vb not in buffer_names:
                _err(
                    errors,
                    "E_REF",
                    f"Unknown vertex_buffer '{vb}'",
                    f"geometries[{gi}].lods[{li}].vertex_buffer",
                )
            if ib and ib not in buffer_names:
                _err(
                    errors,
                    "E_REF",
                    f"Unknown index_buffer '{ib}'",
                    f"geometries[{gi}].lods[{li}].index_buffer",
                )
            if mesh_type == 3:
                jib = lod.get("joint_index_buffer")
                jwb = lod.get("joint_weight_buffer")
                ibb = lod.get("inverse_bind_buffer")
                jrb = lod.get("joint_remap_buffer")
                if not jib:
                    _err(
                        errors,
                        "E_REQUIRED",
                        "joint_index_buffer required for skinned mesh",
                        f"geometries[{gi}].lods[{li}].joint_index_buffer",
                    )
                if not jwb:
                    _err(
                        errors,
                        "E_REQUIRED",
                        "joint_weight_buffer required for skinned mesh",
                        f"geometries[{gi}].lods[{li}].joint_weight_buffer",
                    )
                if not ibb:
                    _err(
                        errors,
                        "E_REQUIRED",
                        "inverse_bind_buffer required for skinned mesh",
                        f"geometries[{gi}].lods[{li}].inverse_bind_buffer",
                    )
                if not jrb:
                    _err(
                        errors,
                        "E_REQUIRED",
                        "joint_remap_buffer required for skinned mesh",
                        f"geometries[{gi}].lods[{li}].joint_remap_buffer",
                    )
                if jib and jib not in buffer_names:
                    _err(
                        errors,
                        "E_REF",
                        f"Unknown joint_index_buffer '{jib}'",
                        f"geometries[{gi}].lods[{li}].joint_index_buffer",
                    )
                if jwb and jwb not in buffer_names:
                    _err(
                        errors,
                        "E_REF",
                        f"Unknown joint_weight_buffer '{jwb}'",
                        f"geometries[{gi}].lods[{li}].joint_weight_buffer",
                    )
                if ibb and ibb not in buffer_names:
                    _err(
                        errors,
                        "E_REF",
                        f"Unknown inverse_bind_buffer '{ibb}'",
                        f"geometries[{gi}].lods[{li}].inverse_bind_buffer",
                    )
                if jrb and jrb not in buffer_names:
                    _err(
                        errors,
                        "E_REF",
                        f"Unknown joint_remap_buffer '{jrb}'",
                        f"geometries[{gi}].lods[{li}].joint_remap_buffer",
                    )
    # Mesh view index/vertex counts limits (if provided)
    for gi, g in enumerate(
        [
            a
            for a in (spec.get("assets", []) or [])
            if isinstance(a, dict) and a.get("type") == "geometry"
        ]
    ):
        if not isinstance(g, dict):
            continue
        for li, lod in enumerate(g.get("lods", []) or []):
            if not isinstance(lod, dict):
                continue
            for si, sub in enumerate(lod.get("submeshes", []) or []):
                if not isinstance(sub, dict):
                    continue
                for vi, mv in enumerate(sub.get("mesh_views", []) or []):
                    if not isinstance(mv, dict):
                        continue
                    ic = mv.get("index_count", 0)
                    vc = mv.get("vertex_count", 0)
                    if ic < 0 or ic > MAX_INDEX_COUNT:
                        _err(
                            errors,
                            "E_RANGE",
                            "index_count out of range",
                            f"geometries[{gi}].lods[{li}].submeshes[{si}].mesh_views[{vi}].index_count",
                        )
                    if vc < 0 or vc > MAX_VERTEX_COUNT:
                        _err(
                            errors,
                            "E_RANGE",
                            "vertex_count out of range",
                            f"geometries[{gi}].lods[{li}].submeshes[{si}].mesh_views[{vi}].vertex_count",
                        )
    # Additional material numeric validations
    for mi, m in enumerate(
        [
            a
            for a in (spec.get("assets", []) or [])
            if isinstance(a, dict) and a.get("type") == "material"
        ]
    ):
        if not isinstance(m, dict):
            continue
        path = f"materials[{mi}]"
        for field in [
            "normal_scale",
            "metalness",
            "roughness",
            "ambient_occlusion",
        ]:
            val = m.get(field)
            if val is not None and not isinstance(val, (int, float)):
                _err(
                    errors,
                    "E_TYPE",
                    f"{field} must be number",
                    f"{path}.{field}",
                )
            if isinstance(val, (int, float)) and not (
                0.0 <= val <= 10.0
            ):  # broad sanity window
                _err(
                    errors,
                    "E_RANGE",
                    f"{field} out of range",
                    f"{path}.{field}",
                )
    # Texture mip sanity: mips <= floor(log2(max(w,h)))+1 if dimensions provided
    import math

    for ti, t in enumerate(spec.get("textures", []) or []):
        if not isinstance(t, dict):
            continue
        w = t.get("width")
        h = t.get("height")
        mips = t.get("mip_levels")
        if isinstance(w, int) and isinstance(h, int) and isinstance(mips, int):
            if w > 0 and h > 0:
                max_mips = int(math.floor(math.log2(max(w, h)))) + 1
                if mips > max_mips:
                    _err(
                        errors,
                        "E_RANGE",
                        "mip_levels exceed dimension limit",
                        f"textures[{ti}].mip_levels",
                    )

    # Scene references: node parent indices + renderable geometry keys
    def _clean_key_hex(val: Any) -> str | None:
        if not isinstance(val, str):
            return None
        cleaned = val.replace("-", "").strip().lower()
        if len(cleaned) != 32:
            return None
        try:
            bytes.fromhex(cleaned)
        except ValueError:
            return None
        return cleaned

    geometry_name_to_key: Dict[str, str] = {}
    for a in spec.get("assets", []) or []:
        if not isinstance(a, dict) or a.get("type") != "geometry":
            continue
        gname = a.get("name")
        if not isinstance(gname, str):
            continue
        raw_key = a.get("asset_key")
        key_hex = _clean_key_hex(raw_key)
        if key_hex is None:
            key_hex = "00" * 16
        geometry_name_to_key[gname] = key_hex

    known_geometry_keys = set(geometry_name_to_key.values())

    for si, s in enumerate(
        [
            a
            for a in (spec.get("assets", []) or [])
            if isinstance(a, dict) and a.get("type") == "scene"
        ]
    ):
        if not isinstance(s, dict):
            continue
        nodes = s.get("nodes", []) or []
        if not isinstance(nodes, list):
            continue
        node_count = len(nodes)
        if node_count > 0:
            root = nodes[0] if isinstance(nodes[0], dict) else {}
            root_parent = root.get("parent") if isinstance(root, dict) else None
            if root_parent not in (None, 0):
                _err(
                    errors,
                    "E_SCENE_ROOT",
                    "Root node parent must be null or 0",
                    f"scenes[{si}].nodes[0].parent",
                )

        for ni, node in enumerate(nodes):
            if not isinstance(node, dict):
                continue
            parent = node.get("parent")
            if parent is None:
                if ni != 0:
                    _err(
                        errors,
                        "E_SCENE_PARENT",
                        "Only node 0 may have null parent",
                        f"scenes[{si}].nodes[{ni}].parent",
                    )
            elif isinstance(parent, int):
                if parent < 0 or parent >= node_count:
                    _err(
                        errors,
                        "E_SCENE_PARENT",
                        "parent out of range",
                        f"scenes[{si}].nodes[{ni}].parent",
                    )

        renderables = s.get("renderables", []) or []
        if not isinstance(renderables, list):
            continue
        for ri, r in enumerate(renderables):
            rpath = f"scenes[{si}].renderables[{ri}]"
            if not isinstance(r, dict):
                _err(errors, "E_TYPE", "Renderable must be object", rpath)
                continue
            node_index = r.get("node_index")
            if not isinstance(node_index, int):
                _err(
                    errors,
                    "E_FIELD",
                    "Renderable missing node_index",
                    rpath + ".node_index",
                )
            elif node_index < 0 or node_index >= node_count:
                _err(
                    errors,
                    "E_RANGE",
                    "node_index out of range",
                    rpath + ".node_index",
                )

            geom_key = _clean_key_hex(r.get("geometry_asset_key"))
            if geom_key is None:
                geom_name = r.get("geometry")
                if isinstance(geom_name, str):
                    geom_key = geometry_name_to_key.get(geom_name)
            if geom_key is None:
                _err(
                    errors,
                    "E_REF",
                    "Renderable must reference geometry_asset_key (hex) or geometry (name)",
                    rpath,
                )
            elif geom_key not in known_geometry_keys:
                _err(
                    errors,
                    "E_REF",
                    "Unknown geometry reference",
                    rpath + ".geometry_asset_key",
                )

        perspective_cameras = s.get("perspective_cameras", []) or []
        if not isinstance(perspective_cameras, list):
            continue
        for ci, c in enumerate(perspective_cameras):
            cpath = f"scenes[{si}].perspective_cameras[{ci}]"
            if not isinstance(c, dict):
                _err(errors, "E_TYPE", "Camera must be object", cpath)
                continue
            node_index = c.get("node_index")
            if not isinstance(node_index, int):
                _err(
                    errors,
                    "E_FIELD",
                    "Camera missing node_index",
                    cpath + ".node_index",
                )
            elif node_index < 0 or node_index >= node_count:
                _err(
                    errors,
                    "E_RANGE",
                    "node_index out of range",
                    cpath + ".node_index",
                )

        orthographic_cameras = s.get("orthographic_cameras", []) or []
        if not isinstance(orthographic_cameras, list):
            continue
        for ci, c in enumerate(orthographic_cameras):
            cpath = f"scenes[{si}].orthographic_cameras[{ci}]"
            if not isinstance(c, dict):
                _err(errors, "E_TYPE", "Camera must be object", cpath)
                continue
            node_index = c.get("node_index")
            if not isinstance(node_index, int):
                _err(
                    errors,
                    "E_FIELD",
                    "Camera missing node_index",
                    cpath + ".node_index",
                )
            elif node_index < 0 or node_index >= node_count:
                _err(
                    errors,
                    "E_RANGE",
                    "node_index out of range",
                    cpath + ".node_index",
                )
    return errors


# Binary validation ---------------------------------------------------------


def run_binary_validation(
    spec: Dict[str, Any], pak_info: Dict[str, Any]
) -> List[ValidationErrorRecord]:
    """Cross-check spec expectations against parsed pak_info.

    Checks:
    - Asset counts match
    - Directory entry descriptor sizes plausible
    - Region/table bounds within file size
    - No zero-sized directory when asset_count>0
    """
    errors: List[ValidationErrorRecord] = []
    footer = pak_info.get("footer", {})
    directory = footer.get("directory", {})
    file_size = pak_info.get("file_size", 0)
    asset_count = directory.get("asset_count", 0)
    spec_asset_count = len(
        [
            a
            for a in (spec.get("assets", []) or [])
            if isinstance(a, dict)
            and a.get("type") in ("material", "geometry", "scene")
        ]
    )
    if spec_asset_count != asset_count:
        _err(
            errors,
            "E_BIN_COUNT",
            "Asset count mismatch",
            "footer.directory.asset_count",
        )
    dir_off = directory.get("offset", 0)
    dir_size = directory.get("size", 0)
    if asset_count > 0 and (dir_off == 0 or dir_size == 0):
        _err(
            errors,
            "E_BIN_DIRECTORY",
            "Directory missing for non-zero asset count",
            "footer.directory",
        )
    if dir_off + dir_size > file_size:
        _err(
            errors,
            "E_BIN_BOUNDS",
            "Directory exceeds file size",
            "footer.directory",
        )
    for e in pak_info.get("directory_entries", []) or []:
        ds = e.get("desc_size", 0)
        if ds < 64:
            _err(
                errors,
                "E_BIN_DESC",
                "Descriptor size too small",
                "directory_entries",
            )
    # Region/table bounds already partially checked in inspector; add redundant safety
    for group_name in ["regions", "tables"]:
        group = footer.get(group_name, {}) or {}
        for name, meta in group.items():
            off = meta.get("offset", 0)
            size = (
                meta.get("size", 0)
                if group_name == "regions"
                else meta.get("count", 0) * meta.get("entry_size", 0)
            )
            if off and off + size > file_size:
                _err(
                    errors,
                    "E_BIN_BOUNDS",
                    f"{group_name[:-1].title()} {name} exceeds file size",
                    f"footer.{group_name}.{name}",
                )
    return errors


def run_validation_pipeline(
    spec: Dict[str, Any],
) -> List[ValidationErrorRecord]:
    errors: List[ValidationErrorRecord] = []
    errors.extend(_schema_phase(spec))
    if errors:
        return errors  # stop early if schema invalid
    errors.extend(_semantic_phase(spec))
    return errors


__all__ = [
    "run_validation_pipeline",
    "ValidationErrorRecord",
    "run_binary_validation",
]
