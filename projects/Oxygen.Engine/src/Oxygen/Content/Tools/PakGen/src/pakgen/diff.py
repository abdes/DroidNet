"""Diff utilities for PakGen (deepened for Task 13).

Phase 1 (initial) provided only name-level, human string lists. Task 13 adds
structured, field-level diffing for:

* Spec vs PAK: material parameter changes, geometry LOD / submesh structural
    discrepancies (removed submesh, changed counts).
* PAK vs PAK (initial support): directory / descriptor size & hash changes.

Public structured diff entry points now return JSON-serializable dictionaries
with a stable shape so the CLI and external tools can render or further process
results.

Lightweight binary parsing logic is duplicated (rather than coupled tightly to
the writer) to keep the diff side-effects free. Descriptor layouts mirror the
packing functions (see ``packing.packers``) and any evolution must update the
offset math here accordingly.
"""

from __future__ import annotations
from typing import Any, Dict, Iterable, List, Set, Tuple
from pathlib import Path
import struct
import zlib

try:
    from .spec.models import PakSpec
except Exception:  # pragma: no cover
    PakSpec = Any  # type: ignore

__all__ = [
    "diff_specs",
    "diff_paks",
    "diff_spec_to_pak",
    # New structured diff APIs (Task 13)
    "diff_spec_vs_pak_deep",
    "diff_paks_deep",
]


def _names(seq: Iterable[Any], key: str = "name") -> Set[str]:
    out: Set[str] = set()
    for item in seq:
        if isinstance(item, dict):
            n = item.get(key)
            if isinstance(n, str):
                out.add(n)
        else:
            n = getattr(item, key, None)
            if isinstance(n, str):
                out.add(n)
    return out


def _as_spec_dict(spec: Any) -> Dict[str, Any]:
    if isinstance(spec, dict):
        return spec
    if hasattr(spec, "__dict__"):
        # PakSpec dataclass → gather known fields if present
        fields = {}
        for k in ["buffers", "textures", "audios", "materials", "geometries"]:
            if hasattr(spec, k):
                fields[k] = getattr(spec, k)
        fields["version"] = getattr(spec, "version", 0)
        fields["content_version"] = getattr(spec, "content_version", 0)
        return fields  # type: ignore
    return {}


def diff_specs(a: Any, b: Any) -> List[str]:
    A = _as_spec_dict(a)
    B = _as_spec_dict(b)
    diffs: List[str] = []
    for group in ["buffers", "textures", "audios", "materials", "geometries"]:
        an = _names(A.get(group, []) or [])
        bn = _names(B.get(group, []) or [])
        added = sorted(bn - an)
        removed = sorted(an - bn)
        if added:
            diffs.append(f"{group}: added {added}")
        if removed:
            diffs.append(f"{group}: removed {removed}")
    if A.get("version") != B.get("version"):
        diffs.append(f"version: {A.get('version')} -> {B.get('version')}")
    if A.get("content_version") != B.get("content_version"):
        diffs.append(
            f"content_version: {A.get('content_version')} -> {B.get('content_version')}"
        )
    return diffs


def diff_paks(info_a: Dict[str, Any], info_b: Dict[str, Any]) -> List[str]:
    diffs: List[str] = []
    for field in ["version", "content_version", "asset_count"]:
        if info_a.get(field) != info_b.get(field):
            diffs.append(f"{field}: {info_a.get(field)} -> {info_b.get(field)}")
    # Resource counts
    for r in ["textures", "buffers", "audios"]:
        a_count = len(info_a.get(r, []) or [])
        b_count = len(info_b.get(r, []) or [])
        if a_count != b_count:
            diffs.append(f"{r}_count: {a_count} -> {b_count}")
    return diffs


def diff_spec_to_pak(spec: Any, pak_info: Dict[str, Any]) -> List[str]:
    S = _as_spec_dict(spec)
    diffs: List[str] = []
    for group in ["textures", "buffers", "audios", "materials", "geometries"]:
        spec_names = _names(S.get(group, []) or [])
        pak_items = pak_info.get(group, []) or []
        pak_names = _names(pak_items)
        missing = sorted(spec_names - pak_names)
        extra = sorted(pak_names - spec_names)
        if missing:
            diffs.append(f"{group}: missing in pak {missing}")
        if extra:
            diffs.append(f"{group}: extra in pak {extra}")
    return diffs


# === Task 13 Deep Diff Implementation -------------------------------------#


def _read_file_bytes(path: str | Path) -> bytes:
    return Path(path).read_bytes()


def _parse_asset_header(desc: bytes) -> Dict[str, Any]:
    # AssetHeader layout (writer.header_builder):
    # type(1) + name(64) + version(1) + streaming_priority(1) + content_hash(8)
    # + variant_flags(4) + reserved(16) = 95 bytes
    if len(desc) < 95:
        return {"valid": False}
    asset_type = desc[0]
    raw_name = desc[1:65]
    # stop at first null
    name = raw_name.split(b"\x00", 1)[0].decode("utf-8", "ignore")
    version = desc[65]
    streaming_priority = desc[66]
    content_hash = struct.unpack_from("<Q", desc, 67)[0]
    variant_flags = struct.unpack_from("<I", desc, 75)[0]
    return {
        "valid": True,
        "asset_type": asset_type,
        "name": name,
        "version": version,
        "streaming_priority": streaming_priority,
        "content_hash": content_hash,
        "variant_flags": variant_flags,
    }


def _parse_material_descriptor(desc: bytes) -> Dict[str, Any]:
    # See pack_material_asset_descriptor for layout. Require 256 bytes.
    if len(desc) < 95 + 35:  # up to start of texture indices
        return {"valid": False}
    header = _parse_asset_header(desc)
    if not header.get("valid"):
        return {"valid": False}
    # Offsets relative to start (after 95 header bytes)
    base = 95
    material_domain = desc[base]
    flags = struct.unpack_from("<I", desc, base + 1)[0]
    shader_stages = struct.unpack_from("<I", desc, base + 5)[0]
    base_color = list(struct.unpack_from("<4f", desc, base + 9))
    normal_scale = struct.unpack_from("<f", desc, base + 25)[0]

    # PBR scalars are stored as UNorm16 in PakFormat.h.
    metalness_u16 = struct.unpack_from("<H", desc, base + 29)[0]
    roughness_u16 = struct.unpack_from("<H", desc, base + 31)[0]
    ambient_occlusion_u16 = struct.unpack_from("<H", desc, base + 33)[0]
    metalness = metalness_u16 / 65535.0
    roughness = roughness_u16 / 65535.0
    ambient_occlusion = ambient_occlusion_u16 / 65535.0
    return {
        **header,
        "material_domain": material_domain,
        "flags": flags,
        "shader_stages": shader_stages,
        "base_color": base_color,
        "normal_scale": normal_scale,
        "metalness": metalness,
        "roughness": roughness,
        "ambient_occlusion": ambient_occlusion,
        "valid": True,
    }


def _parse_geometry_descriptor(desc: bytes) -> Dict[str, Any]:
    if len(desc) < 95 + 4:
        return {"valid": False}
    header = _parse_asset_header(desc)
    if not header.get("valid"):
        return {"valid": False}
    lod_count = struct.unpack_from("<I", desc, 95)[0]
    return {**header, "lod_count": lod_count, "valid": True}


def _parse_geometry_variable_blob(
    data: bytes, start: int, lod_count: int
) -> Dict[str, Any]:
    # Sequentially parse LOD -> mesh desc (105) -> submesh descs (108 * count)
    # -> mesh view descs (16 * count). We only need submesh counts and names.
    # Mesh descriptor layout bits required: after name(64)+type(1), submesh_count(u32), mesh_view_count(u32)
    offset = start
    lods: List[Dict[str, Any]] = []
    try:
        for _ in range(lod_count):
            if offset + 105 > len(data):
                break
            mesh_desc = data[offset : offset + 105]
            offset += 105
            mesh_name = (
                mesh_desc[:64].split(b"\x00", 1)[0].decode("utf-8", "ignore")
            )
            mesh_type = mesh_desc[64]
            submesh_count = struct.unpack_from("<I", mesh_desc, 65)[0]
            mesh_view_total = struct.unpack_from("<I", mesh_desc, 69)[0]
            submeshes: List[Dict[str, Any]] = []
            for _sm in range(submesh_count):
                if offset + 108 > len(data):
                    break
                sm_desc = data[offset : offset + 108]
                offset += 108
                sm_name = (
                    sm_desc[:64].split(b"\x00", 1)[0].decode("utf-8", "ignore")
                )
                sm_mv_count = struct.unpack_from("<I", sm_desc, 80)[0]
                # Skip mesh view descriptors
                mv_size = 16 * sm_mv_count
                offset += mv_size
                submeshes.append(
                    {"name": sm_name, "mesh_view_count": sm_mv_count}
                )
            lods.append(
                {
                    "name": mesh_name,
                    "mesh_type": mesh_type,
                    "submesh_count": len(submeshes),
                    "mesh_view_total": mesh_view_total,
                    "submeshes": submeshes,
                }
            )
    except Exception:  # pragma: no cover - defensive parsing
        pass
    return {"lods": lods}


def _gather_pak_assets(
    path: str | Path,
) -> Dict[Tuple[int, str], Dict[str, Any]]:
    """Parse a pak file and extract descriptor + minimal variable data.

    Returns mapping keyed by (asset_type, name).
    """
    data = _read_file_bytes(path)
    # Footer is last 256 bytes: directory_offset at first 8 bytes
    if len(data) < 256:
        return {}
    footer = data[-256:]
    directory_offset = struct.unpack_from("<Q", footer, 0)[0]
    directory_size = struct.unpack_from("<Q", footer, 8)[0]
    asset_count = struct.unpack_from("<Q", footer, 16)[0]
    entries: List[Dict[str, Any]] = []
    if (
        directory_offset
        and directory_size
        and directory_offset + directory_size <= len(data)
    ):
        for i in range(directory_size // 64):
            e_off = directory_offset + i * 64
            entry = data[e_off : e_off + 64]
            asset_type = entry[16]
            entry_offset = struct.unpack_from("<Q", entry, 17)[0]
            desc_offset = struct.unpack_from("<Q", entry, 25)[0]
            desc_size = struct.unpack_from("<I", entry, 33)[0]
            entries.append(
                {
                    "asset_type": asset_type,
                    "entry_offset": entry_offset,
                    "desc_offset": desc_offset,
                    "desc_size": desc_size,
                }
            )
    assets: Dict[Tuple[int, str], Dict[str, Any]] = {}
    for e in entries:
        desc_slice = data[e["desc_offset"] : e["desc_offset"] + e["desc_size"]]
        header = _parse_asset_header(desc_slice)
        if not header.get("valid"):
            continue
        key = (header["asset_type"], header["name"])
        record: Dict[str, Any] = {
            "desc_size": e["desc_size"],
            "descriptor_crc32": zlib.crc32(desc_slice) & 0xFFFFFFFF,
        }
        if header["asset_type"] == 1:  # material
            record.update(_parse_material_descriptor(desc_slice))
        elif header["asset_type"] == 2:  # geometry
            geo = _parse_geometry_descriptor(desc_slice)
            record.update(geo)
            # Directory entry desc_size includes the full descriptor payload.
            # For geometry, the variable blob begins immediately after the
            # fixed-size 256-byte GeometryAssetDesc.
            var_start = e["desc_offset"] + 256
            if geo.get("lod_count"):
                record.update(
                    _parse_geometry_variable_blob(
                        data, var_start, geo["lod_count"]
                    )
                )
        else:
            record.update(header)
        assets[key] = record
    return assets


def diff_spec_vs_pak_deep(spec: Any, pak_path: str | Path) -> Dict[str, Any]:
    """Deep diff a spec (dict or PakSpec) against a pak file.

    Returns structure with sections:
    {
      'materials': [ { 'name': ..., 'field': 'base_color', 'expected': [...], 'actual': [...]} ],
      'geometries': [ { 'name': 'Geo', 'issue': 'lod_count', 'expected': 2, 'actual': 1}, ... ],
      'summary': { 'material_differences': N, 'geometry_differences': M }
    }
    """
    spec_dict = _as_spec_dict(spec)
    pak_assets = _gather_pak_assets(pak_path)
    material_diffs: List[Dict[str, Any]] = []
    geometry_diffs: List[Dict[str, Any]] = []
    # Support unified assets list only (tests updated); derive materials/geometries views
    unified_assets = spec_dict.get("assets") or []
    mat_view = [
        a
        for a in unified_assets
        if isinstance(a, dict) and a.get("type") == "material"
    ]
    geo_view = [
        a
        for a in unified_assets
        if isinstance(a, dict) and a.get("type") == "geometry"
    ]
    for m in mat_view:
        name = (
            getattr(m, "name", None)
            if not isinstance(m, dict)
            else m.get("name")
        )
        if not isinstance(name, str):
            continue
        pak_mat = pak_assets.get((1, name))
        if not pak_mat:
            material_diffs.append({"name": name, "issue": "missing_in_pak"})
            continue
        expected_bc = (
            getattr(m, "base_color", None)
            if not isinstance(m, dict)
            else m.get("base_color")
        ) or [1.0, 1.0, 1.0, 1.0]
        actual_bc = pak_mat.get("base_color")
        if actual_bc and list(map(float, expected_bc)) != list(
            map(float, actual_bc)
        ):
            material_diffs.append(
                {
                    "name": name,
                    "field": "base_color",
                    "expected": list(map(float, expected_bc)),
                    "actual": list(map(float, actual_bc)),
                }
            )
    # Geometries
    for g in geo_view:
        gname = (
            getattr(g, "name", None)
            if not isinstance(g, dict)
            else g.get("name")
        )
        if not isinstance(gname, str):
            continue
        pak_geo = pak_assets.get((2, gname))
        if not pak_geo:
            geometry_diffs.append({"name": gname, "issue": "missing_in_pak"})
            continue
        spec_lods = (
            getattr(g, "lods", None)
            if not isinstance(g, dict)
            else g.get("lods")
        ) or []
        spec_lod_count = len(spec_lods)
        if pak_geo.get("lod_count") != spec_lod_count:
            geometry_diffs.append(
                {
                    "name": gname,
                    "issue": "lod_count",
                    "expected": spec_lod_count,
                    "actual": pak_geo.get("lod_count"),
                }
            )
        # Submesh removal detection (per lod index)
        pak_lods = pak_geo.get("lods", [])
        for li, spec_lod in enumerate(spec_lods):
            spec_submeshes = (
                getattr(spec_lod, "submeshes", None)
                if not isinstance(spec_lod, dict)
                else spec_lod.get("submeshes")
            ) or []
            # Collect only valid string names for deterministic diffing
            tmp_names = []
            for sm in spec_submeshes:
                if isinstance(sm, dict):
                    nm = sm.get("name")
                else:
                    nm = getattr(sm, "name", None)
                if isinstance(nm, str):
                    tmp_names.append(nm)
            spec_submesh_names = set(tmp_names)
            pak_lod = pak_lods[li] if li < len(pak_lods) else {}
            pak_submesh_names = {
                sm.get("name")
                for sm in pak_lod.get("submeshes", [])
                if isinstance(sm.get("name"), str)
            }
            # We treat submeshes present in PAK but absent in expected spec as removed.
            removed = sorted(
                name for name in (pak_submesh_names - spec_submesh_names)
            )
            if removed:  # names that existed in pak but not expected spec
                geometry_diffs.append(
                    {
                        "name": gname,
                        "issue": "removed_submesh",
                        "lod_index": li,
                        "removed": removed,
                    }
                )
    return {
        "materials": material_diffs,
        "geometries": geometry_diffs,
        "summary": {
            "material_differences": len(material_diffs),
            "geometry_differences": len(geometry_diffs),
        },
    }


def diff_paks_deep(left: str | Path, right: str | Path) -> Dict[str, Any]:
    """Deep diff two pak files (initial asset descriptor size/hash focus)."""
    a = _gather_pak_assets(left)
    b = _gather_pak_assets(right)
    changes: List[Dict[str, Any]] = []
    keys = set(a.keys()) | set(b.keys())
    for k in sorted(keys):
        av = a.get(k)
        bv = b.get(k)
        if av is not None and bv is None:
            changes.append(
                {"asset_type": k[0], "name": k[1], "status": "removed"}
            )
            continue
        if bv is not None and av is None:
            changes.append(
                {"asset_type": k[0], "name": k[1], "status": "added"}
            )
            continue
        if av is None or bv is None:  # both None shouldn't happen
            continue
        # Present in both – compare descriptor size & hash
        if av.get("desc_size") != bv.get("desc_size"):
            changes.append(
                {
                    "asset_type": k[0],
                    "name": k[1],
                    "field": "desc_size",
                    "left": av.get("desc_size"),
                    "right": bv.get("desc_size"),
                }
            )
        if av.get("descriptor_crc32") != bv.get("descriptor_crc32"):
            changes.append(
                {
                    "asset_type": k[0],
                    "name": k[1],
                    "field": "descriptor_crc32",
                    "left": av.get("descriptor_crc32"),
                    "right": bv.get("descriptor_crc32"),
                }
            )
    return {"changes": changes, "summary": {"count": len(changes)}}
