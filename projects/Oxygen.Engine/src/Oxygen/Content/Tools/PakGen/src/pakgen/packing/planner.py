"""Build planning: collect resources & assets into an intermediate plan.

First extraction pass from legacy generate_pak.py. This will later be
expanded to compute predicted layout sizes before writing.
"""

from __future__ import annotations
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, List, Tuple, Optional
import time

from ..logging import get_logger, section, step
from ..reporting import get_reporter
from .constants import (
    ASSET_NAME_MAX_LENGTH,
    MAX_RESOURCES_PER_TYPE,
    MAX_ASSETS_TOTAL,
)
from ..utils.io import read_data_from_spec

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
    total_assets: int = 0
    total_materials: int = 0
    total_geometries: int = 0


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
class PaddingStats:
    total: int
    by_section: Dict[str, int]


@dataclass(slots=True)
class PakPlan:
    regions: List[RegionPlan]
    tables: List[TablePlan]
    assets: List[AssetPlan]
    directory: DirectoryPlan
    footer: FooterPlan
    padding: PaddingStats
    file_size: int
    version: int
    content_version: int
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

    return {
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
                    data = read_data_from_spec(entry, base_dir)
                except Exception:
                    data = b""
                data_blobs[rtype].append(data)
                total_bytes += len(data)
                index_map[rtype][name] = idx
                desc_fields[rtype].append(entry)
                rep.advance(f"res.{rtype}", current_item=name)
                if simulate_delay and simulate_delay > 0:
                    # Sleep after updating progress so increment is visible immediately
                    time.sleep(simulate_delay)
            rep.end_task(f"res.{rtype}")
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
    if material_entries:
        rep.start_task(
            "assets.material", "Material assets", total=len(material_entries)
        )
    if geometry_entries:
        rep.start_task(
            "assets.geometry", "Geometry assets", total=len(geometry_entries)
        )
    for entry in assets_list:
        if not isinstance(entry, dict) or entry.get("type") not in (
            "material",
            "geometry",
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
        else:  # geometry
            geometries.append((entry, key_bytes, 0, entry.get("alignment", 1)))
            rep.advance("assets.geometry", current_item=entry["name"])
            delay = simulate_delay
            if delay is None:
                delay = simulate_material_delay
            if delay and delay > 0:
                time.sleep(delay)
    if material_entries:
        rep.end_task("assets.material")
    if geometry_entries:
        rep.end_task("assets.geometry")
        # Assets summary
        total_assets = len(material_entries) + len(geometry_entries)
        if total_assets:
            rep.status(
                "Assets summary: materials="
                + f"{len(material_entries)} geometries={len(geometry_entries)} total={total_assets}"
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
        total_assets=total_assets,
        total_materials=len(material_entries),
        total_geometries=len(geometry_entries),
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

    # Resource regions (apply deterministic ordering inside each type by original spec index map key if flag set)
    for rtype in ["texture", "buffer", "audio"]:
        blobs = build_plan.resources.data_blobs.get(rtype, [])
        descs = build_plan.resources.desc_fields.get(rtype, [])
        if deterministic and descs:
            # Pair desc + blob then sort by name field for stability
            paired = list(zip(descs, blobs))
            paired.sort(key=lambda p: p[0].get("name", ""))
            if paired:
                descs_sorted, blobs_sorted = zip(*paired)
                build_plan.resources.desc_fields[rtype] = list(descs_sorted)  # type: ignore[index]
                build_plan.resources.data_blobs[rtype] = list(blobs_sorted)  # type: ignore[index]
                blobs = build_plan.resources.data_blobs.get(rtype, [])
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
    if materials or geometries:
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

    # Footer: lives at end; we simply append FOOTER_SIZE
    footer_offset = cursor
    cursor += FOOTER_SIZE
    footer_plan = FooterPlan(offset=footer_offset, size=FOOTER_SIZE)

    total_padding = sum(padding.values())
    padding_stats = PaddingStats(total=total_padding, by_section=padding)

    return PakPlan(
        regions=regions,
        tables=tables,
        assets=assets,
        directory=directory_plan,
        footer=footer_plan,
        padding=padding_stats,
        file_size=cursor,
        version=int(build_plan.spec.get("version", 1)),
        content_version=int(build_plan.spec.get("content_version", 0)),
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
    "PaddingStats",
    "to_plan_dict",
    "compute_pak_plan",
]
