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
    # repository layout fallback: src/.../Bindless/Bindless.schema.json
    fallback = sdir.parents[1] / "Bindless" / "Bindless.schema.json"
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
    """Normalize visibility to canonical D3D12 tokens and order."""
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


def normalize_doc(doc: Dict[str, Any]) -> Dict[str, Any]:
    """One-pass normalization of doc in-place and return it.

    - domain.kind -> uppercase SRV/CBV/UAV/SAMPLER
    - root_signature.visibility -> canonical list in D3D12 order
    - normalize domain references on parameters/ranges to lists
    """
    # domains: best-effort kind normalization (don't raise here; schema handles constraints)
    allowed = {"SRV", "CBV", "UAV", "SAMPLER"}
    lower_map = {k.lower(): k for k in allowed}
    for d in doc.get("domains", []) or []:
        k = d.get("kind")
        if isinstance(k, str):
            k_low = k.lower()
            if k_low in lower_map:
                d["kind"] = lower_map[k_low]

    # root_signature
    rs = doc.get("root_signature", []) or []
    for idx, param in enumerate(rs):
        # visibility is required by schema but normalize defensively here
        if "visibility" in param:
            param["visibility"] = _normalize_visibility(param.get("visibility"))
        # normalize param-level domains property to list
        if "domains" in param:
            param["domains"] = _as_list(param.get("domains"))
        # normalize ranges[].domain to list
        if param.get("type") == "descriptor_table":
            ranges = param.get("ranges", []) or []
            for r in ranges:
                if "domain" in r:
                    r["domain"] = _as_list(r.get("domain"))

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
