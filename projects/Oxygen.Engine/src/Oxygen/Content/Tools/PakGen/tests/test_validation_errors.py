from __future__ import annotations

"""Validation error mapping tests (ported from legacy validation cases).

Covers representative negative scenarios:
- Missing version field (treated via defaults but still expect no E_FIELD for version)
- Duplicate resource name (buffers)
- Missing texture reference
- Missing geometry buffer reference

We adapt legacy error codes to new validator codes:
 legacy -> new:
  E_SPEC_MISSING_FIELD (version) -> no direct equivalent (skip)
  E_DUP_RESOURCE_NAME -> E_DUP
  E_INVALID_REFERENCE (texture/buffer) -> E_REF

"""
from pathlib import Path
import json
from pakgen.spec.validator import run_validation_pipeline


def _run(spec: dict):
    return run_validation_pipeline(spec)


def _codes(errors):
    return {e.code for e in errors}


def test_validation_duplicate_buffer_name():  # noqa: N802
    spec = {
        "version": 1,
        "buffers": [{"name": "bufA"}, {"name": "bufA"}],
        "textures": [],
        "audios": [],
        "assets": [],
    }
    errs = _run(spec)
    assert "E_DUP" in _codes(errs)


def test_validation_missing_texture_reference():  # noqa: N802
    spec = {
        "version": 1,
        "textures": [],
        "buffers": [],
        "audios": [],
        "assets": [
            {
                "type": "material",
                "name": "mat0",
                "texture_refs": {"base_color": "not_exist"},
            }
        ],
    }
    errs = _run(spec)
    assert "E_REF" in _codes(errs)


def test_validation_missing_geometry_buffer():  # noqa: N802
    spec = {
        "version": 1,
        "buffers": [],
        "textures": [],
        "audios": [],
        "assets": [
            {
                "type": "geometry",
                "name": "geo0",
                "lods": [
                    {
                        "name": "lod0",
                        "vertex_buffer": "vb_missing",
                        "submeshes": [],
                    }
                ],
            }
        ],
    }
    errs = _run(spec)
    assert "E_REF" in _codes(errs)
