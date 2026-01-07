"""Build planning: collect resources & assets into an intermediate plan.

First extraction pass from legacy generate_pak.py. This will later be
expanded to compute predicted layout sizes before writing.
"""

from __future__ import annotations
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, List, Tuple, Optional
import hashlib
import json
import time
import uuid

from ..logging import get_logger, section, step
from ..reporting import get_reporter
from .constants import (
    ASSET_NAME_MAX_LENGTH,
    MAX_RESOURCES_PER_TYPE,
    MAX_ASSETS_TOTAL,
    MAX_RESOURCE_SIZES,
)
from ..utils.io import read_data_from_spec


def _align_up(value: int, alignment: int) -> int:
    if alignment <= 0:
        return value
    return ((value + alignment - 1) // alignment) * alignment


def _maybe_pad_texture_payload(entry: Dict[str, Any], data: bytes) -> bytes:
    """Pad small uncompressed RGBA8 texture payloads for D3D12 row-pitch rules.

    The runtime upload path aligns row pitch to 256 bytes. When authors embed
    small textures (e.g., 1x1, 2x2) via data_hex, they typically provide only
    tightly packed texels (4 * w * h bytes). That will fail at runtime because
    the uploader expects each row padded to 256 bytes.

    We only auto-pad the common case we use in demos:
    - texture_type == 3 (2D)
    - compression_type == 0 (raw)
    - format == 30 (RGBA8_UNORM)
    - mip_levels == 1, array_layers == 1, depth == 1

    For very large inferred required sizes we fail early rather than allocate
    huge padding buffers silently.
    """

    if not data:
        return data

    try:
        texture_type = int(entry.get("texture_type", 0))
        compression_type = int(entry.get("compression_type", 0))
        fmt = int(entry.get("format", 0))
        width = int(entry.get("width", 0))
        height = int(entry.get("height", 0))
        depth = int(entry.get("depth", 1))
        array_layers = int(entry.get("array_layers", 1))
        mip_levels = int(entry.get("mip_levels", 1))
    except Exception:
        return data

    if (
        texture_type != 3
        or compression_type != 0
        or fmt != 30
        or mip_levels != 1
        or array_layers != 1
        or depth != 1
    ):
        return data

    if width <= 0 or height <= 0:
        return data

    bytes_per_pixel = 4
    row_pitch = _align_up(width * bytes_per_pixel, 256)
    required = row_pitch * height

    if len(data) >= required:
        return data

    max_autopad_bytes = 16 * 1024 * 1024  # 16 MiB safety cap
    if required > max_autopad_bytes:
        raise ValueError(
            "Embedded texture payload too small for inferred row-pitch layout "
            f"(name={entry.get('name')!r} format=RGBA8_UNORM width={width} height={height} "
            f"required_bytes={required} actual_bytes={len(data)}). "
            "Provide pre-padded rows or use an external file payload."
        )

    return data + b"\x00" * (required - len(data))


# ---------------------------------------------------------------------------
# Existing collection result dataclasses
# ---------------------------------------------------------------------------

# NOTE: For now we keep simple validation here; richer validation moves to spec/validator.py later.


@dataclass(slots=True)
class ResourceCollectionResult:
    data_blobs: Dict[str, List[bytes]]
    desc_fields: Dict[str, List[Dict[str, Any]]]
    index_map: Dict[str, Dict[str, int]]
    total_bytes: int


@dataclass(slots=True)
class AssetCollectionResult:
    material_assets: List[Dict[str, Any]]
    geometry_assets: List[Tuple[Dict[str, Any], bytes, int, int]]
    scene_assets: List[Tuple[Dict[str, Any], bytes, int, int]]
    total_assets: int = 0
    total_materials: int = 0
    total_geometries: int = 0
    total_scenes: int = 0


@dataclass(slots=True)
class BuildPlan:
    spec: Dict[str, Any]
    base_dir: Path
    resources: ResourceCollectionResult
    assets: AssetCollectionResult
    total_resource_bytes: int
    total_resource_counts: Dict[str, int]


# ---------------------------------------------------------------------------
# Planned Phase 5 immutable planning model (initial skeleton).
# These will be populated by a compute_pak_plan() function in a later step.
# Writer currently does its own layout; future refactor will consume PakPlan.
# ---------------------------------------------------------------------------


@dataclass(slots=True)
class RegionPlan:
    name: str
    offset: int
    size: int
    alignment: int
    padding_before: int
    padding_after: int


@dataclass(slots=True)
class TablePlan:
    name: str
    offset: int
    count: int
    entry_size: int
    padding_before: int


@dataclass(slots=True)
class AssetPlan:
    asset_type: str
    key_hex: str
    descriptor_offset: int
    descriptor_size: int
    alignment: int
    warnings: Tuple[str, ...] = ()
    variable_extra_size: int = (
        0  # bytes of trailing variable data (e.g., geometry LOD blob)
    )
    name: str = ""


@dataclass(slots=True)
class DirectoryPlan:
    offset: int
    size: int
    asset_count: int
    padding_before: int


@dataclass(slots=True)
class FooterPlan:
    offset: int
    size: int


@dataclass(slots=True)
class BrowseIndexPlan:
    offset: int
    size: int
    entry_count: int
    string_table_size: int
    padding_before: int


@dataclass(slots=True)
class PaddingStats:
    total: int
    by_section: Dict[str, int]


@dataclass(slots=True)
class PakPlan:
    regions: List[RegionPlan]
    tables: List[TablePlan]
    assets: List[AssetPlan]
    directory: DirectoryPlan
    browse_index: Optional[BrowseIndexPlan]
    footer: FooterPlan
    padding: PaddingStats
    file_size: int
    version: int
    content_version: int
    guid: bytes = b"\x00" * 16
    deterministic: bool = False
    # Future: spec hash inputs, ordering rationale, etc.


def to_plan_dict(plan: PakPlan) -> Dict[str, Any]:  # lightweight serializer
    def region(r: RegionPlan):
        return {
            "name": r.name,
            "offset": r.offset,
            "size": r.size,
            "alignment": r.alignment,
            "padding_before": r.padding_before,
            "padding_after": r.padding_after,
        }

    def table(t: TablePlan):
        return {
            "name": t.name,
            "offset": t.offset,
            "count": t.count,
            "entry_size": t.entry_size,
            "padding_before": t.padding_before,
        }

    def asset(a: AssetPlan):
        return {
            "type": a.asset_type,
            "key": a.key_hex,
            "descriptor_offset": a.descriptor_offset,
            "descriptor_size": a.descriptor_size,
            "alignment": a.alignment,
            "variable_extra_size": a.variable_extra_size,
            "name": a.name,
            "warnings": list(a.warnings),
        }

    out: Dict[str, Any] = {
        "version": plan.version,
        "content_version": plan.content_version,
        "deterministic": plan.deterministic,
        "file_size": plan.file_size,
        "regions": [region(r) for r in plan.regions],
        "tables": [table(t) for t in plan.tables],
        "assets": [asset(a) for a in plan.assets],
        "directory": {
            "offset": plan.directory.offset,
            "size": plan.directory.size,
            "asset_count": plan.directory.asset_count,
            "padding_before": plan.directory.padding_before,
        },
        "browse_index": (
            {
                "offset": plan.browse_index.offset,
                "size": plan.browse_index.size,
                "entry_count": plan.browse_index.entry_count,
                "string_table_size": plan.browse_index.string_table_size,
                "padding_before": plan.browse_index.padding_before,
            }
            if plan.browse_index
            else None
        ),
        "footer": {"offset": plan.footer.offset, "size": plan.footer.size},
        "padding": {
            "total": plan.padding.total,
            "by_section": dict(plan.padding.by_section),
        },
        "statistics": {
            "variable_extra_total": sum(
                a.variable_extra_size for a in plan.assets
            ),
            "asset_counts": {
                "materials": sum(
                    1 for a in plan.assets if a.asset_type == "material"
                ),
                "geometries": sum(
                    1 for a in plan.assets if a.asset_type == "geometry"
                ),
                "total": len(plan.assets),
            },
        },
    }
    scenes = sum(1 for a in plan.assets if a.asset_type == "scene")
    if scenes:
        out["statistics"]["asset_counts"]["scenes"] = scenes  # type: ignore[index]
    return out


def _validate_name_length(name: str) -> None:
    if len(name.encode("utf-8")) > ASSET_NAME_MAX_LENGTH:
        raise ValueError(f"Name too long: {name}")


def collect_resources(
    spec: Dict[str, Any],
    base_dir: Path,
    resource_types: List[str],
    *,
    simulate_delay: float | None = None,
) -> ResourceCollectionResult:
    logger = get_logger()
    data_blobs = {r: [] for r in resource_types}
    desc_fields = {r: [] for r in resource_types}
    index_map = {r: {} for r in resource_types}
    total_bytes = 0

    rep = get_reporter()
    with section("Collect resources"):
        for rtype in resource_types:
            items = spec.get(rtype + "s", [])
            if not items:
                continue
            # Determinism policy:
            # - buffers: preserve spec order (indices are semantic)
            # - textures: sort by name for deterministic indexing
            # - audio: preserve spec order (can be changed later if needed)
            if rtype == "texture":
                items = sorted(
                    items,
                    key=lambda e: (
                        (e.get("name") if isinstance(e, dict) else "") or ""
                    ),
                )
            rep.start_task(
                f"res.{rtype}", f"{rtype.title()} resources", total=len(items)
            )
            if len(items) > MAX_RESOURCES_PER_TYPE:
                rep.error(
                    f"Too many {rtype} resources: {len(items)}/{MAX_RESOURCES_PER_TYPE}"
                )
                raise ValueError(f"Too many {rtype} resources: {len(items)}")
            for idx, entry in enumerate(items):
                if not isinstance(entry, dict) or "name" not in entry:
                    rep.error(f"Invalid {rtype} entry at index {idx}")
                    raise ValueError(f"Invalid {rtype} entry at {idx}")
                name = entry["name"]
                _validate_name_length(name)
                if name in index_map[rtype]:
                    rep.error(f"Duplicate {rtype} name: {name}")
                    raise ValueError(f"Duplicate {rtype} name: {name}")
                try:
                    max_size = int(
                        MAX_RESOURCE_SIZES.get(rtype, 100 * 1024 * 1024)
                    )
                    data = read_data_from_spec(
                        entry, base_dir, max_size=max_size
                    )
                    if rtype == "texture":
                        padded = _maybe_pad_texture_payload(entry, data)
                        if len(padded) != len(data):
                            rep.warning(
                                "Padded texture payload for row-pitch alignment",
                                name=name,
                                original_bytes=len(data),
                                padded_bytes=len(padded),
                            )
                        data = padded
                except Exception as e:
                    rep.error(f"Failed to read {rtype} data for '{name}': {e}")
                    raise
                data_blobs[rtype].append(data)
                total_bytes += len(data)
                index_map[rtype][name] = idx
                desc_fields[rtype].append(entry)
                rep.advance(f"res.{rtype}", current_item=name)
                if simulate_delay and simulate_delay > 0:
                    # Sleep after updating progress so increment is visible immediately
                    time.sleep(simulate_delay)
            rep.end_task(f"res.{rtype}")

            # Texture resource indices: reserve index 0 for fallback / none.
            # Runtime loaders treat index 0 as kNoResourceIndex and will skip
            # collecting/loading dependencies for it. Therefore, we must never
            # assign a real, user-provided texture to index 0.
            if rtype == "texture":
                fallback_name = "__fallback_texture"
                if fallback_name not in index_map[rtype]:
                    if len(desc_fields[rtype]) >= MAX_RESOURCES_PER_TYPE:
                        rep.error(
                            f"Too many {rtype} resources (needs room for fallback): "
                            f"{len(desc_fields[rtype])}/{MAX_RESOURCES_PER_TYPE}"
                        )
                        raise ValueError(
                            f"Too many {rtype} resources (needs room for fallback): {len(desc_fields[rtype])}"
                        )

                    # 1x1 RGBA8_UNORM white texel. Values align with existing
                    # example spec constants (texture_type=3, format=30).
                    fallback_spec = {
                        "name": fallback_name,
                        "texture_type": 3,
                        "compression_type": 0,
                        "width": 1,
                        "height": 1,
                        "depth": 1,
                        "array_layers": 1,
                        "mip_levels": 1,
                        "format": 30,
                        "alignment": 256,
                        "data_hex": "ffffffff",
                    }
                    # NOTE: D3D12 upload paths often align row pitch to 256
                    # bytes. A 1x1 RGBA8 subresource therefore needs at least
                    # 256 bytes of data for a successful upload.
                    fallback_data = b"\xff\xff\xff\xff" + b"\x00" * (256 - 4)

                    # Insert at the front and shift all existing indices by +1.
                    data_blobs[rtype].insert(0, fallback_data)
                    desc_fields[rtype].insert(0, fallback_spec)
                    total_bytes += len(fallback_data)

                    old_map = index_map[rtype]
                    new_map: Dict[str, int] = {fallback_name: 0}
                    for k, v in old_map.items():
                        new_map[k] = int(v) + 1
                    index_map[rtype] = new_map
        # Summary after all resource types processed
        counts = {rt: len(desc_fields[rt]) for rt in resource_types}
        if any(counts.values()):
            rep.status(
                "Resources summary: "
                + " ".join(
                    f"{rt}={counts[rt]}" for rt in resource_types if counts[rt]
                )
                + f" total_bytes={total_bytes}"
            )
        # Threshold warnings (90% utilization of per-type cap)
        for rt in resource_types:
            c = counts[rt]
            if c and c > 0.9 * MAX_RESOURCES_PER_TYPE:
                rep.status(
                    f"Resource near limit: {rt} {c}/{MAX_RESOURCES_PER_TYPE}"
                )
    return ResourceCollectionResult(
        data_blobs, desc_fields, index_map, total_bytes
    )


def collect_assets(
    spec: Dict[str, Any],
    resource_index_map: Dict[str, Dict[str, int]],
    base_dir: Path,
    *,
    simulate_material_delay: float | None = None,  # kept for CLI/backwards use
    simulate_delay: float | None = None,  # generalized delay for all assets
) -> AssetCollectionResult:
    # Unified assets list; partition by 'type'
    assets_list = spec.get("assets", []) or []
    rep = get_reporter()
    materials: List[Dict[str, Any]] = []
    geometries: List[Tuple[Dict[str, Any], bytes, int, int]] = []
    scenes: List[Tuple[Dict[str, Any], bytes, int, int]] = []
    if not isinstance(assets_list, list):
        rep.error("Spec 'assets' must be a list")
        raise ValueError("Spec 'assets' must be a list")
    total_assets = len(assets_list)
    if total_assets > MAX_ASSETS_TOTAL:
        rep.error(f"Too many assets: {total_assets}/{MAX_ASSETS_TOTAL}")
        raise ValueError("Too many assets")
    if assets_list:
        rep.section("Collect assets")
    # Pre-count per type to size tasks
    material_entries = [a for a in assets_list if a.get("type") == "material"]
    geometry_entries = [a for a in assets_list if a.get("type") == "geometry"]
    scene_entries = [a for a in assets_list if a.get("type") == "scene"]
    if material_entries:
        rep.start_task(
            "assets.material", "Material assets", total=len(material_entries)
        )
    if geometry_entries:
        rep.start_task(
            "assets.geometry", "Geometry assets", total=len(geometry_entries)
        )
    if scene_entries:
        rep.start_task("assets.scene", "Scene assets", total=len(scene_entries))
    for entry in assets_list:
        if not isinstance(entry, dict) or entry.get("type") not in (
            "material",
            "geometry",
            "scene",
        ):
            # Skip unsupported types silently for now (future: error)
            continue
        if "name" not in entry:
            rep.error("Asset missing name field")
            raise ValueError("Asset missing name")
        _validate_name_length(entry["name"])
        raw_key = entry.get("asset_key")
        key_bytes = b"\x00" * 16
        if isinstance(raw_key, str):
            cleaned = raw_key.replace("-", "").strip()
            if len(cleaned) == 32:
                try:
                    key_bytes = bytes.fromhex(cleaned)
                except ValueError:
                    key_bytes = b"\x00" * 16
        if entry.get("type") == "material":
            materials.append(
                {
                    "name": entry["name"],
                    "asset_key": key_bytes,
                    "alignment": entry.get("alignment", 1),
                    "spec": entry,
                }
            )
            rep.advance("assets.material", current_item=entry["name"])
            # Apply generalized delay (material-specific value used as fallback)
            delay = simulate_delay
            if delay is None:
                delay = simulate_material_delay
            if delay and delay > 0:
                time.sleep(delay)
        elif entry.get("type") == "geometry":
            geometries.append((entry, key_bytes, 0, entry.get("alignment", 1)))
            rep.advance("assets.geometry", current_item=entry["name"])
            delay = simulate_delay
            if delay is None:
                delay = simulate_material_delay
            if delay and delay > 0:
                time.sleep(delay)
        elif entry.get("type") == "scene":
            scenes.append((entry, key_bytes, 0, entry.get("alignment", 1)))
            rep.advance("assets.scene", current_item=entry["name"])
            delay = simulate_delay
            if delay is None:
                delay = simulate_material_delay
            if delay and delay > 0:
                time.sleep(delay)
    if material_entries:
        rep.end_task("assets.material")
    if geometry_entries:
        rep.end_task("assets.geometry")
    if scene_entries:
        rep.end_task("assets.scene")
        # Assets summary
        total_assets = (
            len(material_entries) + len(geometry_entries) + len(scene_entries)
        )
        if total_assets:
            rep.status(
                "Assets summary: materials="
                + f"{len(material_entries)} geometries={len(geometry_entries)} scenes={len(scene_entries)} total={total_assets}"
            )
        max_assets_total = (
            MAX_RESOURCES_PER_TYPE * 2
        )  # heuristic overall soft cap
        if total_assets > 0.9 * max_assets_total:
            rep.status(
                f"Assets near heuristic limit: {total_assets}/{max_assets_total}"
            )
    return AssetCollectionResult(
        materials,
        geometries,
        scenes,
        total_assets=total_assets,
        total_materials=len(material_entries),
        total_geometries=len(geometry_entries),
        total_scenes=len(scene_entries),
    )


def build_plan(
    spec: Dict[str, Any],
    base_dir: Path,
    *,
    simulate_material_delay: float | None = None,
    simulate_delay: float | None = None,
) -> BuildPlan:
    resource_types = ["texture", "buffer", "audio"]
    res = collect_resources(
        spec,
        base_dir,
        resource_types,
        simulate_delay=(simulate_delay or simulate_material_delay),
    )
    assets = collect_assets(
        spec,
        res.index_map,
        base_dir,
        simulate_material_delay=simulate_material_delay,
        simulate_delay=simulate_delay,
    )
    counts = {rt: len(res.desc_fields[rt]) for rt in resource_types}
    return BuildPlan(
        spec=spec,
        base_dir=base_dir,
        resources=res,
        assets=assets,
        total_resource_bytes=res.total_bytes,
        total_resource_counts=counts,
    )


# ---------------------------------------------------------------------------
# Phase 5: pure planning (offset calculation) -- incremental introduction.
# This mirrors writer ordering but does not write bytes; it estimates sizes
# using existing packers for descriptor lengths where needed. For now we only
# compute coarse-grained section offsets; descriptor_offsets for assets use
# a simplified model (base descriptor only; variable LOD blob ignored until
# writer refactor integrates). This is sufficient for dry-run stability tests.
# ---------------------------------------------------------------------------


def compute_pak_plan(
    build_plan: BuildPlan, deterministic: bool = False
) -> PakPlan:
    from .constants import (
        HEADER_SIZE,
        FOOTER_SIZE,
        DATA_ALIGNMENT,
        TABLE_ALIGNMENT,
        RESOURCE_ENTRY_SIZES,
        MATERIAL_DESC_SIZE,
        GEOMETRY_DESC_SIZE,
        SCENE_DESC_SIZE,
        DIRECTORY_ENTRY_SIZE,
        MESH_DESC_SIZE,
        SUBMESH_DESC_SIZE,
        MESH_VIEW_DESC_SIZE,
        SHADER_REF_DESC_SIZE,
    )

    # Start after header.
    cursor = HEADER_SIZE
    padding: Dict[str, int] = {}

    def align(value: int, alignment: int, label: str) -> tuple[int, int]:
        pad = (alignment - (value % alignment)) % alignment
        if pad:
            padding[label] = padding.get(label, 0) + pad
        return value + pad, pad

    regions: List[RegionPlan] = []
    tables: List[TablePlan] = []
    assets: List[AssetPlan] = []

    # Normalize resource blobs: drop zero-length blobs so regions are only
    # created for actual byte content. Zero-length resources still get table
    # entries (descriptor) but point to offset 0. This avoids a mismatch where
    # writer finds blobs but planned region size is 0 (invariant violation).
    for _rt in ["texture", "buffer", "audio"]:
        blobs_list = build_plan.resources.data_blobs.get(_rt, [])
        if not blobs_list:
            continue
        all_empty = all(len(b) == 0 for b in blobs_list)
        if all_empty:
            # All blobs empty -> treat as no region; drop them entirely.
            build_plan.resources.data_blobs[_rt] = []  # type: ignore[index]
        else:
            # Keep zero-length blobs intermixed with non-empty; they do not
            # enlarge region size and will simply not advance inner cursor.
            # Writer tolerates zero-length emission naturally.
            pass

    def _is_zero_guid(key_bytes: bytes) -> bool:
        return key_bytes == b"\x00" * 16

    # Determine Pak GUID early so generated AssetKeys can be derived from it.
    if deterministic:
        spec_for_fingerprint = dict(build_plan.spec)
        spec_for_fingerprint.pop("name", None)
        spec_fingerprint = hashlib.sha256(
            json.dumps(
                spec_for_fingerprint,
                sort_keys=True,
                separators=(",", ":"),
                ensure_ascii=False,
            ).encode("utf-8")
        ).hexdigest()
        pak_guid = uuid.uuid5(
            uuid.NAMESPACE_DNS,
            f"pak:{spec_fingerprint}",
        ).bytes
    else:
        pak_guid = uuid.uuid4().bytes

    # Enforce uniqueness of AssetKey within a PAK.
    seen_keys: dict[bytes, list[str]] = {}
    for m in build_plan.assets.material_assets:
        if not isinstance(m, dict):
            continue
        spec = m.get("spec")
        spec = spec if isinstance(spec, dict) else {}
        name = spec.get("name", "")
        name = name if isinstance(name, str) else ""
        key = m.get("asset_key", b"\x00" * 16)
        if isinstance(key, (bytes, bytearray)):
            key_bytes = bytes(key)
            if not _is_zero_guid(key_bytes):
                seen_keys.setdefault(key_bytes, []).append(f"material:{name}")
    for geom_spec, key, _atype, _align in build_plan.assets.geometry_assets:
        name = (
            geom_spec.get("name")
            if isinstance(geom_spec.get("name"), str)
            else ""
        )
        if isinstance(key, (bytes, bytearray)):
            key_bytes = bytes(key)
            if not _is_zero_guid(key_bytes):
                seen_keys.setdefault(key_bytes, []).append(f"geometry:{name}")
    for scene_spec, key, _atype, _align in build_plan.assets.scene_assets:
        name = (
            scene_spec.get("name")
            if isinstance(scene_spec.get("name"), str)
            else ""
        )
        if isinstance(key, (bytes, bytearray)):
            key_bytes = bytes(key)
            if not _is_zero_guid(key_bytes):
                seen_keys.setdefault(key_bytes, []).append(f"scene:{name}")

    duplicates = {k: v for k, v in seen_keys.items() if len(v) > 1}
    if duplicates:
        details = ", ".join(
            f"{k.hex()} -> {v}"
            for k, v in sorted(duplicates.items(), key=lambda kv: kv[0].hex())
        )
        raise ValueError(f"Duplicate asset_key values in spec: {details}")

    # Resource regions
    for rtype in ["texture", "buffer", "audio"]:
        blobs = build_plan.resources.data_blobs.get(rtype, [])
        descs = build_plan.resources.desc_fields.get(rtype, [])
        # IMPORTANT: Do not reorder resource lists, even in deterministic mode.
        # Resource indices are semantic and referenced by packed asset
        # descriptors (e.g., material texture indices). Reordering resources
        # would desynchronize the index map computed during collection.
        if not blobs:
            regions.append(
                RegionPlan(
                    name=rtype,
                    offset=0,
                    size=0,
                    alignment=DATA_ALIGNMENT,
                    padding_before=0,
                    padding_after=0,
                )
            )
            continue
        aligned_cursor, pad_before = align(
            cursor, DATA_ALIGNMENT, f"region_{rtype}"
        )
        cursor = aligned_cursor
        region_offset = cursor
        # Each blob aligned individually inside region to DATA_ALIGNMENT
        inner_cursor = 0
        for blob in blobs:
            inner_pad = (
                DATA_ALIGNMENT - (inner_cursor % DATA_ALIGNMENT)
            ) % DATA_ALIGNMENT
            inner_cursor += inner_pad + len(blob)
        region_size = inner_cursor
        cursor += region_size
        regions.append(
            RegionPlan(
                name=rtype,
                offset=region_offset,
                size=region_size,
                alignment=DATA_ALIGNMENT,
                padding_before=pad_before,
                padding_after=0,
            )
        )

    # Resource tables
    for rtype in ["texture", "buffer", "audio"]:
        descs = build_plan.resources.desc_fields.get(rtype, [])
        if not descs:
            tables.append(
                TablePlan(
                    name=rtype,
                    offset=0,
                    count=0,
                    entry_size=0,
                    padding_before=0,
                )
            )
            continue
        aligned_cursor, pad_before = align(
            cursor, TABLE_ALIGNMENT, f"table_{rtype}"
        )
        cursor = aligned_cursor
        entry_size = RESOURCE_ENTRY_SIZES[rtype]
        table_size = len(descs) * entry_size
        table_offset = cursor
        cursor += table_size
        tables.append(
            TablePlan(
                name=rtype,
                offset=table_offset,
                count=len(descs),
                entry_size=entry_size,
                padding_before=pad_before,
            )
        )

    # Asset descriptors (material then geometry as writer ordering)
    # Align to DATA_ALIGNMENT once before descriptors region.
    materials = build_plan.assets.material_assets
    geometries = build_plan.assets.geometry_assets
    scenes = build_plan.assets.scene_assets
    if materials or geometries or scenes:
        cursor_aligned, pad_before_assets = align(
            cursor, DATA_ALIGNMENT, "assets_region"
        )
        cursor = cursor_aligned
    else:
        pad_before_assets = 0
    # Deterministic ordering: sort materials by name, geometries by name when flag set
    if deterministic:
        materials = sorted(
            materials,
            key=lambda m: (
                m.get("spec", {}).get("name") if isinstance(m, dict) else ""
            ),
        )

        def _geom_name(entry):
            spec = entry[0] if entry and isinstance(entry[0], dict) else {}
            name = spec.get("name")
            return name if isinstance(name, str) else ""

        geometries = sorted(geometries, key=_geom_name)

        def _scene_name(entry):
            spec = entry[0] if entry and isinstance(entry[0], dict) else {}
            name = spec.get("name")
            return name if isinstance(name, str) else ""

        scenes = sorted(scenes, key=_scene_name)

        # Keep build_plan asset lists in the same order the plan assumes.
        build_plan.assets.material_assets = materials
        build_plan.assets.geometry_assets = geometries
        build_plan.assets.scene_assets = scenes

    # Material descriptors
    for m in materials:
        alignment_req = 16  # provisional; material alignment typically 16
        cursor_aligned, pad_mat = align(cursor, alignment_req, "asset_material")
        cursor = cursor_aligned
        # Variable shader reference blob size = popcount(shader_stages) * SHADER_REF_DESC_SIZE
        shader_stages = 0
        if isinstance(m, dict):
            spec = m.get("spec", {})
            if isinstance(spec, dict):
                shader_stages = int(spec.get("shader_stages", 0))
        # Brian Kernighan popcount
        pop = 0
        ss = shader_stages
        while ss:
            ss &= ss - 1
            pop += 1
        shader_refs_extra = pop * SHADER_REF_DESC_SIZE
        assets.append(
            AssetPlan(
                asset_type="material",
                key_hex=(
                    m.get("asset_key", b"\x00" * 16).hex()
                    if isinstance(m, dict)
                    else ""
                ),
                descriptor_offset=cursor,
                descriptor_size=MATERIAL_DESC_SIZE,
                alignment=alignment_req,
                variable_extra_size=shader_refs_extra,
                name=(
                    m.get("spec", {}).get("name") if isinstance(m, dict) else ""
                ),
            )
        )
        cursor += MATERIAL_DESC_SIZE + shader_refs_extra
    # Geometry base descriptors (variable LOD data ignored for size now)
    for geom_spec, asset_key, _asset_type, alignment in geometries:
        cursor_aligned, pad_geo = align(
            cursor, alignment or 1, "asset_geometry"
        )
        cursor = cursor_aligned
        key_hex = (
            asset_key.hex() if isinstance(asset_key, (bytes, bytearray)) else ""
        )
        # Estimate variable LOD blob size: sum(mesh + submesh + mesh view descriptor sizes)
        variable_size = 0
        lods = geom_spec.get("lods", []) or []
        for lod in lods:
            if not isinstance(lod, dict):
                continue
            variable_size += MESH_DESC_SIZE
            submeshes = lod.get("submeshes", []) or []
            for sub in submeshes:
                if not isinstance(sub, dict):
                    continue
                variable_size += SUBMESH_DESC_SIZE
                for mv in sub.get("mesh_views", []) or []:
                    if isinstance(mv, dict):
                        variable_size += MESH_VIEW_DESC_SIZE
        gname = ""
        if isinstance(geom_spec, dict):
            nm = geom_spec.get("name")
            if isinstance(nm, str):
                gname = nm
        assets.append(
            AssetPlan(
                asset_type="geometry",
                key_hex=key_hex,
                descriptor_offset=cursor,
                descriptor_size=GEOMETRY_DESC_SIZE,
                alignment=alignment or 1,
                variable_extra_size=variable_size,
                name=gname,
            )
        )
        cursor += GEOMETRY_DESC_SIZE + variable_size

    # Scene descriptors + payload
    NODE_RECORD_SIZE = 68
    COMPONENT_TABLE_DESC_SIZE = 20
    RENDERABLE_RECORD_SIZE = 36
    PERSPECTIVE_CAMERA_RECORD_SIZE = 32
    ORTHOGRAPHIC_CAMERA_RECORD_SIZE = 40
    DIRECTIONAL_LIGHT_RECORD_SIZE = 96
    POINT_LIGHT_RECORD_SIZE = 80
    SPOT_LIGHT_RECORD_SIZE = 88

    ENV_BLOCK_HEADER_SIZE = 16
    ENV_SKY_ATMOSPHERE_RECORD_SIZE = 96
    ENV_VOLUMETRIC_CLOUDS_RECORD_SIZE = 64
    ENV_SKY_LIGHT_RECORD_SIZE = 64
    ENV_SKY_SPHERE_RECORD_SIZE = 72
    ENV_POST_PROCESS_VOLUME_RECORD_SIZE = 56

    def _scene_string_table_size(nodes_list: List[Dict[str, Any]]) -> int:
        offsets: dict[str, int] = {"": 0}
        buf = bytearray(b"\x00")
        for node in nodes_list:
            name = node.get("name") if isinstance(node, dict) else ""
            if not isinstance(name, str):
                name = ""
            if name not in offsets:
                offsets[name] = len(buf)
                buf.extend(name.encode("utf-8"))
                buf.append(0)
        return len(buf)

    for scene_spec, asset_key, _asset_type, alignment in scenes:
        cursor_aligned, _pad_scene = align(
            cursor, alignment or 1, "asset_scene"
        )
        cursor = cursor_aligned
        key_hex = (
            asset_key.hex() if isinstance(asset_key, (bytes, bytearray)) else ""
        )
        nodes_list = scene_spec.get("nodes", []) or []
        nodes_list = nodes_list if isinstance(nodes_list, list) else []
        nodes_dicts = [n for n in nodes_list if isinstance(n, dict)]
        node_count = len(nodes_dicts)
        strings_size = _scene_string_table_size(nodes_dicts)

        renderables_list = scene_spec.get("renderables", []) or []
        renderables_list = (
            renderables_list if isinstance(renderables_list, list) else []
        )
        renderable_count = sum(
            1 for r in renderables_list if isinstance(r, dict)
        )

        cameras_list = scene_spec.get("perspective_cameras", []) or []
        cameras_list = cameras_list if isinstance(cameras_list, list) else []
        camera_count = sum(1 for c in cameras_list if isinstance(c, dict))

        ortho_cameras_list = scene_spec.get("orthographic_cameras", []) or []
        ortho_cameras_list = (
            ortho_cameras_list if isinstance(ortho_cameras_list, list) else []
        )
        ortho_camera_count = sum(
            1 for c in ortho_cameras_list if isinstance(c, dict)
        )

        directional_lights_list = scene_spec.get("directional_lights", []) or []
        directional_lights_list = (
            directional_lights_list
            if isinstance(directional_lights_list, list)
            else []
        )
        directional_light_count = sum(
            1 for l in directional_lights_list if isinstance(l, dict)
        )

        point_lights_list = scene_spec.get("point_lights", []) or []
        point_lights_list = (
            point_lights_list if isinstance(point_lights_list, list) else []
        )
        point_light_count = sum(
            1 for l in point_lights_list if isinstance(l, dict)
        )

        spot_lights_list = scene_spec.get("spot_lights", []) or []
        spot_lights_list = (
            spot_lights_list if isinstance(spot_lights_list, list) else []
        )
        spot_light_count = sum(
            1 for l in spot_lights_list if isinstance(l, dict)
        )

        component_table_count = 0
        if renderable_count > 0:
            component_table_count += 1
        if camera_count > 0:
            component_table_count += 1
        if ortho_camera_count > 0:
            component_table_count += 1
        if directional_light_count > 0:
            component_table_count += 1
        if point_light_count > 0:
            component_table_count += 1
        if spot_light_count > 0:
            component_table_count += 1
        component_dir_bytes = component_table_count * COMPONENT_TABLE_DESC_SIZE
        component_data_bytes = (
            renderable_count * RENDERABLE_RECORD_SIZE
            + camera_count * PERSPECTIVE_CAMERA_RECORD_SIZE
            + ortho_camera_count * ORTHOGRAPHIC_CAMERA_RECORD_SIZE
            + directional_light_count * DIRECTIONAL_LIGHT_RECORD_SIZE
            + point_light_count * POINT_LIGHT_RECORD_SIZE
            + spot_light_count * SPOT_LIGHT_RECORD_SIZE
        )

        env_size = ENV_BLOCK_HEADER_SIZE
        env_spec = scene_spec.get("environment")
        if env_spec is not None:
            if not isinstance(env_spec, dict):
                raise ValueError("scene.environment must be an object")

            if isinstance(env_spec.get("sky_atmosphere"), dict):
                env_size += ENV_SKY_ATMOSPHERE_RECORD_SIZE
            if isinstance(env_spec.get("volumetric_clouds"), dict):
                env_size += ENV_VOLUMETRIC_CLOUDS_RECORD_SIZE
            if isinstance(env_spec.get("sky_light"), dict):
                env_size += ENV_SKY_LIGHT_RECORD_SIZE
            if isinstance(env_spec.get("sky_sphere"), dict):
                env_size += ENV_SKY_SPHERE_RECORD_SIZE
            if isinstance(env_spec.get("post_process_volume"), dict):
                env_size += ENV_POST_PROCESS_VOLUME_RECORD_SIZE

        variable_size = (
            node_count * NODE_RECORD_SIZE
            + strings_size
            + component_dir_bytes
            + component_data_bytes
            + env_size
        )

        sname = ""
        if isinstance(scene_spec, dict):
            nm = scene_spec.get("name")
            if isinstance(nm, str):
                sname = nm
        assets.append(
            AssetPlan(
                asset_type="scene",
                key_hex=key_hex,
                descriptor_offset=cursor,
                descriptor_size=SCENE_DESC_SIZE,
                alignment=alignment or 1,
                variable_extra_size=variable_size,
                name=sname,
            )
        )
        cursor += SCENE_DESC_SIZE + variable_size

    # Directory (only if assets present)
    if assets:
        cursor_aligned, pad_before_dir = align(
            cursor, TABLE_ALIGNMENT, "directory"
        )
        cursor = cursor_aligned
        directory_offset = cursor
        directory_size = len(assets) * DIRECTORY_ENTRY_SIZE
        cursor += directory_size
        directory_plan = DirectoryPlan(
            offset=directory_offset,
            size=directory_size,
            asset_count=len(assets),
            padding_before=pad_before_dir,
        )
    else:
        directory_plan = DirectoryPlan(
            offset=0, size=0, asset_count=0, padding_before=0
        )

    # Embedded browse index (optional; lives after directory and before footer)
    if assets:
        from .browse_index import (
            BrowseIndexEntrySpec,
            build_browse_index_payload,
            derive_virtual_path_from_asset_name,
        )

        entry_specs = [
            BrowseIndexEntrySpec(
                asset_key=bytes.fromhex(a.key_hex),
                virtual_path=derive_virtual_path_from_asset_name(a.name),
            )
            for a in assets
        ]
        payload, entry_count, string_table_size = build_browse_index_payload(
            entry_specs
        )

        cursor_aligned, pad_before_bix = align(
            cursor, TABLE_ALIGNMENT, "browse_index"
        )
        cursor = cursor_aligned
        browse_index_offset = cursor
        browse_index_size = len(payload)
        cursor += browse_index_size

        browse_index_plan = BrowseIndexPlan(
            offset=browse_index_offset,
            size=browse_index_size,
            entry_count=entry_count,
            string_table_size=string_table_size,
            padding_before=pad_before_bix,
        )
    else:
        browse_index_plan = None

    # Footer: lives at end; we simply append FOOTER_SIZE
    footer_offset = cursor
    cursor += FOOTER_SIZE
    footer_plan = FooterPlan(offset=footer_offset, size=FOOTER_SIZE)

    total_padding = sum(padding.values())
    padding_stats = PaddingStats(total=total_padding, by_section=padding)

    # pak_guid is computed up-front to allow deterministic AssetKey generation.

    return PakPlan(
        regions=regions,
        tables=tables,
        assets=assets,
        directory=directory_plan,
        browse_index=browse_index_plan,
        footer=footer_plan,
        padding=padding_stats,
        file_size=cursor,
        version=int(build_plan.spec.get("version", 1)),
        content_version=int(build_plan.spec.get("content_version", 0)),
        guid=pak_guid,
        deterministic=deterministic,
    )


__all__ = [
    "BuildPlan",
    "build_plan",
    "ResourceCollectionResult",
    "AssetCollectionResult",
    # Phase 5 planning exports
    "PakPlan",
    "RegionPlan",
    "TablePlan",
    "AssetPlan",
    "DirectoryPlan",
    "FooterPlan",
    "BrowseIndexPlan",
    "PaddingStats",
    "to_plan_dict",
    "compute_pak_plan",
]
