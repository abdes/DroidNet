"""High-level API stubs for PakGen.

Concrete implementations will be added during migration.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Any, Optional

from .logging import get_logger
from .reporting import task, get_reporter
from .spec.loader import load_spec
from .spec.models import PakSpec
from .packing.inspector import (
    inspect_pak as _inspect_pak_impl,
    validate_pak as _validate_pak_impl,
)
from .packing.planner import (
    build_plan,
    compute_pak_plan,
    to_plan_dict,
    PakPlan,
)
from .packing.writer import write_pak as _write_pak
from .spec.validator import run_validation_pipeline, run_binary_validation
from .manifest import build_manifest
from .packing.inspector import compute_crc32
import hashlib, json

__all__ = [
    "BuildOptions",
    "BuildResult",
    "build_pak",
    "inspect_pak",
    "validate_spec",
    "validate_pak",
    "load_models",
    "PakSpec",
    "binary_validate",
    "plan_dry_run",
    "PakPlan",
]


@dataclass(slots=True)
class BuildOptions:
    input_spec: Path
    output_path: Path
    optimize: bool = False
    force: bool = False
    # Optional path; when provided a manifest JSON will be emitted alongside the pak
    manifest_path: Path | None = None
    # Deterministic build (stable ordering / offsets); passed to planner
    deterministic: bool = False
    # Testing / diagnostics: simulate per-material processing delay (seconds)
    simulate_material_delay: float | None = None


@dataclass(slots=True)
class BuildResult:
    output_file: Path
    bytes_written: int


def load_models(path: str | Path) -> PakSpec:  # replaced placeholder
    return load_spec(path)


def build_pak(options: BuildOptions) -> BuildResult:  # implemented stub
    logger = get_logger()
    rep = get_reporter()
    # Load spec (non-task status only)
    spec_model = load_models(options.input_spec)
    assets_list = list(spec_model.assets)
    spec_dict = {
        # PakGen emits PAK format v4 only.
        "version": 4,
        "content_version": getattr(spec_model, "content_version", 0),
        "buffers": spec_model.buffers,
        "textures": spec_model.textures,
        "audios": spec_model.audios,
        "assets": assets_list,
    }
    val_errors = run_validation_pipeline(spec_dict)
    if val_errors:
        raise ValueError(
            "Spec validation failed: "
            + "; ".join(f"{e.code}:{e.path}:{e.message}" for e in val_errors)
        )
    rep.status(
        "Spec summary: buffers="
        + f"{len(spec_model.buffers)} textures={len(spec_model.textures)} audios={len(spec_model.audios)} assets={len(assets_list)} validation=ok"
    )
    build = build_plan(
        spec_dict,
        options.input_spec.parent,  # type: ignore[arg-type]
        simulate_material_delay=options.simulate_material_delay,
        simulate_delay=options.simulate_material_delay,
    )
    with task("plan.layout", "Compute layout plan"):
        pak_plan = compute_pak_plan(build, deterministic=options.deterministic)
        # Plan summary reporter log
        if pak_plan:
            data_bytes = pak_plan.file_size - pak_plan.padding.total
            rep.status(
                "Plan summary: regions="
                + f"{len(pak_plan.regions)} tables={len(pak_plan.tables)} assets={len(assets_list)} "
                + f"file_size={pak_plan.file_size} data_bytes={data_bytes} padding={pak_plan.padding.total}"
            )
    spec_hash = hashlib.sha256(
        json.dumps(spec_dict, sort_keys=True, separators=(",", ":")).encode(
            "utf-8"
        )
    ).hexdigest()
    bytes_written = _write_pak(build, pak_plan, options.output_path)
    if options.manifest_path is not None:
        with task("manifest.emit", "Emit manifest"):
            file_bytes = options.output_path.read_bytes()
            pak_crc32 = compute_crc32(file_bytes)
            file_sha256 = hashlib.sha256(file_bytes).hexdigest()
            # Derive zero-length resource info & warnings (non-first zero length)
            zero_length: list[dict[str, Any]] = []
            warnings: list[str] = []
            for rtype in ["texture", "buffer", "audio"]:
                descs = build.resources.desc_fields.get(rtype, [])
                blobs = build.resources.data_blobs.get(rtype, [])
                # blobs length matches non-empty data blobs only; need to infer zero-length entries by descriptor fields
                for idx, spec in enumerate(descs):
                    size_field = (
                        spec.get("size")
                        or spec.get("data_size")
                        or spec.get("length")
                    )
                    declared_size = 0 if size_field in (None, 0) else size_field
                    # Heuristic: if declared_size==0 and either no blob at that index or blob len==0
                    blob_len = (
                        blobs[idx]
                        if idx < len(blobs)
                        and isinstance(blobs[idx], (bytes, bytearray))
                        else None
                    )
                    actual_len = (
                        len(blob_len)
                        if isinstance(blob_len, (bytes, bytearray))
                        else 0
                    )
                    if declared_size == 0 and actual_len == 0:
                        zero_length.append(
                            {
                                "type": rtype,
                                "name": spec.get("name"),
                                "index": idx,
                            }
                        )
                        if idx != 0:  # Non-default slot zero length -> warning
                            warnings.append(
                                f"Zero-length {rtype} resource '{spec.get('name')}' at index {idx}"
                            )
            # Build resource_index_map capturing final post-plan ordering
            resource_index_map: dict[str, list[dict[str, Any]]] = {}
            for rtype in ["texture", "buffer", "audio"]:
                descs = build.resources.desc_fields.get(rtype, [])
                if descs:
                    resource_index_map[rtype] = [
                        {"name": d.get("name"), "index": i}
                        for i, d in enumerate(descs)
                        if isinstance(d.get("name"), str)
                    ]
            build_manifest(
                pak_plan,
                options.manifest_path,
                pak_crc32=pak_crc32,
                spec_hash=spec_hash,
                file_sha256=file_sha256,
                zero_length_resources=zero_length or None,
                warnings=warnings or None,
                resource_index_map=resource_index_map or None,
            )
            logger.info(
                "Emitted manifest: %s (spec_hash=%s crc32=%s sha256=%s)",
                options.manifest_path.name,
                spec_hash[:12],
                f"{pak_crc32:08x}",
                file_sha256[:12],
            )
            rep.status(
                "Manifest summary: spec_hash="
                + f"{spec_hash[:12]} crc32={pak_crc32:08x} sha256={file_sha256[:12]}"
            )
    logger.info(
        "Built PAK: %s (%d bytes, assets=%d materials=%d geometries=%d)",
        options.output_path.name,
        bytes_written,
        build.assets.total_assets,
        build.assets.total_materials,
        build.assets.total_geometries,
    )
    rep.status(
        "Build summary: file="
        + f"{options.output_path.name} bytes={bytes_written} assets={build.assets.total_assets} materials={build.assets.total_materials} geometries={build.assets.total_geometries}"
    )
    return BuildResult(
        output_file=options.output_path, bytes_written=bytes_written
    )


def plan_dry_run(
    spec_path: str | Path,
    deterministic: bool = False,
    simulate_material_delay: float | None = None,
) -> tuple[PakPlan, dict]:
    """Compute a PakPlan for a spec file without writing output.

    Returns (PakPlan, plan_dict) where plan_dict is JSON-serialisable.
    """
    spec_model = load_models(spec_path)
    assets_list = list(spec_model.assets)
    spec_dict = {
        "name": Path(spec_path).stem,
        # PakGen emits PAK format v4 only.
        "version": 4,
        "content_version": getattr(spec_model, "content_version", 0),
        "buffers": spec_model.buffers,
        "textures": spec_model.textures,
        "audios": spec_model.audios,
        "assets": assets_list,
    }
    val_errors = run_validation_pipeline(spec_dict)
    if val_errors:
        raise ValueError(
            "Spec validation failed: "
            + "; ".join(f"{e.code}:{e.path}:{e.message}" for e in val_errors)
        )
    bp = build_plan(
        spec_dict,
        Path(spec_path).parent,  # type: ignore[arg-type]
        simulate_material_delay=simulate_material_delay,
        simulate_delay=simulate_material_delay,
    )
    pak_plan = compute_pak_plan(bp, deterministic=deterministic)
    return pak_plan, to_plan_dict(pak_plan)


def inspect_pak(path: str | Path) -> dict:  # implemented
    return _inspect_pak_impl(str(path))


def validate_spec(path: str | Path) -> None:  # placeholder minimal
    logger = get_logger()
    spec = load_models(path)
    logger.info("Loaded spec version %s", spec.version)


def validate_pak(path: str | Path) -> list[str]:
    info = _inspect_pak_impl(str(path))
    return _validate_pak_impl(info)


def binary_validate(spec_dict: dict, pak_info: dict):  # thin wrapper
    return run_binary_validation(spec_dict, pak_info)
