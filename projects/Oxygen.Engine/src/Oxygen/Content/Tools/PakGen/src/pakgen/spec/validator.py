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
    if len(mats) + len(geos) > MAX_ASSETS_TOTAL:
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
            if isinstance(a, dict) and a.get("type") in ("material", "geometry")
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
