"""Spec loading utilities (JSON/YAML) for PakGen."""

from __future__ import annotations
from pathlib import Path
from typing import Any
import json

from .models import PakSpec

try:  # Optional YAML support
    import yaml  # type: ignore
except ImportError:  # pragma: no cover
    yaml = None  # type: ignore


def load_spec(path: str | Path) -> PakSpec:
    p = Path(path)
    if not p.exists():
        raise FileNotFoundError(p)
    text = p.read_text(encoding="utf-8")
    if p.suffix.lower() in {".yaml", ".yml"}:
        if yaml is None:
            raise RuntimeError("YAML spec provided but PyYAML not installed")
        data: Any = yaml.safe_load(text)
    else:
        data = json.loads(text)
    if not isinstance(data, dict):  # basic validation
        raise ValueError("Root of specification must be an object")
    return _parse_spec_dict(data)


def _parse_spec_dict(data: dict[str, Any]) -> PakSpec:
    # Minimal direct mapping; detailed validation to be implemented later.
    spec = PakSpec(
        version=int(data.get("version", 1)),
        content_version=int(data.get("content_version", 0)),
    )
    # Transitional: direct shallow assignment of raw dict lists so planning can proceed.
    # Proper model instantiation will replace this.
    spec.buffers = data.get("buffers", [])  # type: ignore[assignment]
    spec.textures = data.get("textures", [])  # type: ignore[assignment]
    spec.audios = data.get("audios", [])  # type: ignore[assignment]
    # Authoritative assets list
    if isinstance(data.get("assets"), list):
        spec.assets = data.get("assets")  # type: ignore[assignment]
    return spec


__all__ = ["load_spec"]
