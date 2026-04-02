# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

"""Schema resolution, one-pass normalization, and JSON Schema validation."""

from __future__ import annotations

from pathlib import Path
from typing import Any, Dict, List
import importlib
import json

# Try dynamic import to keep developer environments light-weight
try:
    jsonschema = importlib.import_module("jsonschema")
    ValidationError = getattr(jsonschema, "exceptions").ValidationError  # type: ignore[attr-defined]
except Exception:  # pragma: no cover
    jsonschema = None
    ValidationError = None  # type: ignore[assignment]


def find_schema(explicit_path: str | None, script_path: str) -> Path | None:
    """Resolve Bindless.schema.json according to priority:
    1. explicit_path (if provided)
    2. directory containing script_path
    3. repository layout fallback: src/.../Bindless/Bindless.schema.json

    Returns the Path or None if not found.
    """
    if explicit_path:
        p = Path(explicit_path)
        if p.exists():
            return p
    sdir = Path(script_path).resolve().parent
    candidate = sdir / "Bindless.schema.json"
    if candidate.exists():
        return candidate
    # repository layout fallbacks
    for parent in sdir.parents:
        fallback_candidates = (
            parent / "Meta" / "Bindless.schema.json",
            parent / "Bindless" / "Bindless.schema.json",
            parent / "src" / "Oxygen" / "Core" / "Meta" / "Bindless.schema.json",
            parent / "src" / "Oxygen" / "Core" / "Bindless" / "Bindless.schema.json",
        )
        for fallback in fallback_candidates:
            if fallback.exists():
                return fallback
    return None


def _as_list(val: Any) -> List[Any]:
    if val is None:
        return []
    if isinstance(val, list):
        return val
    return [val]


def _normalize_visibility(vis: Any) -> List[str]:
    """Normalize D3D12 visibility to canonical tokens and order."""
    vis_map = {
        "VS": "VERTEX",
        "HS": "HULL",
        "DS": "DOMAIN",
        "GS": "GEOMETRY",
        "PS": "PIXEL",
        "ALL": "ALL",
        "VERTEX": "VERTEX",
        "HULL": "HULL",
        "DOMAIN": "DOMAIN",
        "GEOMETRY": "GEOMETRY",
        "PIXEL": "PIXEL",
    }
    canonical_order = ["ALL", "VERTEX", "HULL", "DOMAIN", "GEOMETRY", "PIXEL"]
    items = _as_list(vis)
    norm = []
    for v in items:
        if not isinstance(v, str):
            raise ValueError(f"Invalid visibility entry: {v}")
        up = v.upper()
        if up not in vis_map:
            raise ValueError(f"Unsupported visibility '{v}'")
        norm.append(vis_map[up])
    # dedup in canonical order
    present = {v: True for v in norm}
    return [v for v in canonical_order if v in present]


def _normalize_vulkan_stages(stages: Any) -> List[str]:
    """Normalize Vulkan pipeline stage flags to a canonical order."""
    stage_map = {
        "ALL": "ALL",
        "VERTEX": "VERTEX",
        "TESSELLATION_CONTROL": "TESSELLATION_CONTROL",
        "TESSELLATION_EVALUATION": "TESSELLATION_EVALUATION",
        "GEOMETRY": "GEOMETRY",
        "FRAGMENT": "FRAGMENT",
        "COMPUTE": "COMPUTE",
    }
    canonical_order = [
        "ALL",
        "VERTEX",
        "TESSELLATION_CONTROL",
        "TESSELLATION_EVALUATION",
        "GEOMETRY",
        "FRAGMENT",
        "COMPUTE",
    ]
    items = _as_list(stages)
    norm = []
    for stage in items:
        if not isinstance(stage, str):
            raise ValueError(f"Invalid Vulkan stage entry: {stage}")
        up = stage.upper()
        if up not in stage_map:
            raise ValueError(f"Unsupported Vulkan stage '{stage}'")
        norm.append(stage_map[up])
    present = {v: True for v in norm}
    return [v for v in canonical_order if v in present]


def normalize_doc(doc: Dict[str, Any]) -> Dict[str, Any]:
    """One-pass normalization of doc in-place and return it.

    - d3d12 root signature visibility -> canonical list in D3D12 order
    - vulkan pipeline stages -> canonical list in Vulkan stage order
    - d3d12 strategy kinds -> uppercase tokens
    - abi shader access class -> lowercase token
    """
    abi = doc.get("abi") or {}
    for domain in abi.get("domains", []) or []:
        access_class = domain.get("shader_access_class")
        if isinstance(access_class, str):
            domain["shader_access_class"] = access_class.lower()

    backends = doc.get("backends") or {}
    d3d12 = backends.get("d3d12") or {}
    d3d12_strategy = d3d12.get("strategy") or {}

    for heap in d3d12_strategy.get("heaps", []) or []:
        heap_type = heap.get("type")
        if isinstance(heap_type, str):
            heap["type"] = heap_type.upper()

    for table in d3d12_strategy.get("tables", []) or []:
        descriptor_kind = table.get("descriptor_kind")
        if isinstance(descriptor_kind, str):
            table["descriptor_kind"] = descriptor_kind.upper()

    for param in d3d12.get("root_signature", []) or []:
        if "visibility" in param:
            param["visibility"] = _normalize_visibility(param.get("visibility"))

    vulkan = backends.get("vulkan") or {}
    for binding in ((vulkan.get("strategy") or {}).get("bindings", []) or []):
        descriptor_type = binding.get("descriptor_type")
        if isinstance(descriptor_type, str):
            binding["descriptor_type"] = descriptor_type.upper()

    for entry in vulkan.get("pipeline_layout", []) or []:
        if entry.get("type") == "push_constants" and "stages" in entry:
            entry["stages"] = _normalize_vulkan_stages(entry.get("stages"))

    return doc


def validate_against_schema(
    doc: Dict[str, Any], schema_path: str | Path | None
) -> bool:
    """Validate doc against JSON Schema when available.

    Returns True on success; raises ValueError (schema validation failures) or
    RuntimeError (missing dependency or corrupted schema) otherwise.
    """
    if schema_path is None:
        return True
    p = Path(schema_path)
    if not p.exists():
        return True
    if jsonschema is None:
        raise RuntimeError(
            "The 'jsonschema' package is required to validate Spec.yaml.\n"
            "Install it in your environment: pip install jsonschema"
        )
    try:
        with p.open("r", encoding="utf-8") as f:
            schema = json.load(f)
        jsonschema.validate(instance=doc, schema=schema)  # type: ignore[call-arg]
    except Exception as e:  # pragma: no cover - formatted below
        if ValidationError is not None and isinstance(e, ValidationError):  # type: ignore[arg-type]
            path_attr = getattr(e, "path", None)
            path = (
                "->".join([str(p) for p in path_attr])
                if path_attr
                else "(root)"
            )
            msg = getattr(e, "message", str(e))
            raise ValueError(
                f"Spec.yaml validation failed at {path}: {msg}"
            ) from e
        raise RuntimeError(f"Failed to load/validate schema at {p}: {e}") from e
    return True
