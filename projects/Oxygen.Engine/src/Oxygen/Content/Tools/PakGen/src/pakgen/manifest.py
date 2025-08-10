"""Manifest generation utilities for PakGen (Phase 7 minimal implementation).

The manifest is an optional JSON artifact that summarises the built PAK.
It is only produced when explicitly requested by the caller / CLI flag.

Scope (minimal):
- High level file metadata (size)
- Region layout summary (name, offset, size)
- Counts for resource tables & assets
- Determinism flag from the PakPlan
- Placeholder fields for future hashing (pak_crc32, spec_hash)

Future expansions (not yet implemented):
- Detailed per-asset entries with offsets & sizes
- Compression / encoding metadata
- Extended statistics (padding breakdown, table sizes, etc.)
- Hashing of individual resources
"""

from __future__ import annotations

from pathlib import Path
import json
from typing import Any

from .packing.planner import PakPlan

__all__ = ["build_manifest", "manifest_dict"]


def manifest_dict(
    pak_plan: PakPlan,
    *,
    pak_crc32: int | None = None,
    spec_hash: str | None = None,
    file_sha256: str | None = None,
    zero_length_resources: list[dict[str, Any]] | None = None,
    warnings: list[str] | None = None,
) -> dict[str, Any]:
    regions = [
        {
            "name": r.name,
            "offset": r.offset,
            "size": r.size,
        }
        for r in pak_plan.regions
        if r.size  # omit empty regions for brevity
    ]
    # Derive counts from assets list (PakPlan.assets is a flat list of AssetPlan)
    materials = sum(1 for a in pak_plan.assets if a.asset_type == "material")
    geometries = sum(1 for a in pak_plan.assets if a.asset_type == "geometry")
    d: dict[str, Any] = {
        "version": 1,
        "file_size": pak_plan.file_size,
        "deterministic": pak_plan.deterministic,
        "regions": regions,
        "counts": {
            "regions": len(regions),
            "tables": len(pak_plan.tables),
            "assets_total": len(pak_plan.assets),
            "materials": materials,
            "geometries": geometries,
        },
        # Placeholders for future phases
        "pak_crc32": pak_crc32,
        "spec_hash": spec_hash,
        "sha256": file_sha256,
    }
    if zero_length_resources:
        d["zero_length_resources"] = zero_length_resources
    if warnings:
        d["warnings"] = warnings
    return d


def build_manifest(
    pak_plan: PakPlan,
    output_path: Path,
    *,
    pak_crc32: int | None = None,
    spec_hash: str | None = None,
    file_sha256: str | None = None,
    zero_length_resources: list[dict[str, Any]] | None = None,
    warnings: list[str] | None = None,
) -> Path:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    data = manifest_dict(
        pak_plan,
        pak_crc32=pak_crc32,
        spec_hash=spec_hash,
        file_sha256=file_sha256,
        zero_length_resources=zero_length_resources,
        warnings=warnings,
    )
    with output_path.open("w", encoding="utf-8") as f:
        json.dump(data, f, indent=2, sort_keys=True)
        f.write("\n")
    return output_path
