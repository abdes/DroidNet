# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

"""Tests of the schema checks, the C++ rendering and the command line."""

# pyright: strict

from __future__ import annotations

import copy
import json
from pathlib import Path
from typing import Any

import pytest
import yaml

from scene_schema_codegen import cli, cpp, model

_SCHEMA_DIR = Path(__file__).resolve().parents[3] / "Schema"
_SCHEMA = _SCHEMA_DIR / "SceneComponents.yaml"
_JSON_SCHEMA = _SCHEMA_DIR / "SceneComponents.schema.json"
_HEADER = _SCHEMA_DIR / "Generated.SceneComponents.h"


def _rules() -> object:
    return json.loads(_JSON_SCHEMA.read_text(encoding="utf-8"))


def _minimal() -> dict[str, Any]:
    return {
        "version": 1,
        "field_sets": [
            {
                "name": "Light",
                "fields": [
                    {"name": "ColorR", "id": 0},
                    {"name": "ColorG", "id": 1},
                    {"name": "ColorB", "id": 2},
                    {"name": "Range", "id": 5},
                ],
            }
        ],
        "components": [
            {
                "name": "PointLight",
                "kind": "point-light",
                "wire": {"id": 3, "field_set": "Light"},
                "properties": [
                    {
                        "name": "Color",
                        "key": "/color",
                        "type": "vec3",
                        "fields": ["ColorR", "ColorG", "ColorB"],
                    },
                    {
                        "name": "Range",
                        "key": "/range",
                        "type": "float",
                        "fields": ["Range"],
                    },
                ],
            },
            {
                "name": "Geometry",
                "kind": "geometry",
                "properties": [
                    {"name": "GeometryUri", "key": "/geometry_uri", "type": "asset"}
                ],
            },
        ],
    }


def _error_of(document: object) -> str:
    with pytest.raises(model.SchemaError) as raised:
        model.parse(document, _rules())
    return str(raised.value)


def test_committed_header_matches_the_schema() -> None:
    schema = model.load(_SCHEMA, _JSON_SCHEMA)
    expected = cpp.render(schema, _SCHEMA.name)
    assert _HEADER.read_bytes() == expected.encode("utf-8")


def test_schema_keeps_the_published_component_ids() -> None:
    schema = model.load(_SCHEMA, _JSON_SCHEMA)
    ids = {component.name: wire.id for component, wire in schema.wired()}
    assert ids == {
        "Transform": 1,
        "PerspectiveCamera": 2,
        "DirectionalLight": 3,
        "OrthographicCamera": 4,
        "PointLight": 5,
        "SpotLight": 6,
        "Node": 7,
        "LocalFogVolume": 8,
    }


def test_field_count_is_one_past_the_largest_id() -> None:
    schema = model.parse(_minimal(), _rules())
    assert schema.field_sets[0].count == 6


def test_render_orders_components_and_fields_by_id() -> None:
    document = _minimal()
    document["field_sets"][0]["fields"].reverse()
    text = cpp.render(model.parse(document, _rules()), "test.yaml")
    assert "enum class ComponentId : std::uint16_t {\n  kInvalid = 0,\n" in text
    assert "  kPointLight = 3,\n};" in text
    assert (
        "enum class LightField : std::uint16_t {\n"
        "  kColorR = 0,\n  kColorG = 1,\n  kColorB = 2,\n  kRange = 5,\n"
        "  kCount = 6,\n};"
    ) in text
    assert "Geometry" not in text


def test_component_without_properties_is_valid_and_not_rendered() -> None:
    document = _minimal()
    document["components"].append(
        {"name": "Scripting", "kind": "scripting", "properties": []}
    )
    text = cpp.render(model.parse(document, _rules()), "test.yaml")
    assert "Scripting" not in text


def test_json_schema_violation_is_reported_with_its_path() -> None:
    document = _minimal()
    document["components"][0]["properties"][1]["type"] = "double"
    assert "/components/0/properties/1/type" in _error_of(document)


def test_enum_property_requires_its_enumeration() -> None:
    document = _minimal()
    document["components"][0]["properties"][1]["type"] = "enum"
    assert "/components/0/properties/1" in _error_of(document)


def test_duplicate_field_id_is_refused() -> None:
    document = _minimal()
    document["field_sets"][0]["fields"][3]["id"] = 2
    assert "duplicate field id in Light: 2" in _error_of(document)


def test_duplicate_component_id_is_refused() -> None:
    document = _minimal()
    twin = copy.deepcopy(document["components"][0])
    twin["name"] = "SpotLight"
    twin["kind"] = "spot-light"
    document["components"].append(twin)
    assert "duplicate component id: 3" in _error_of(document)


def test_unknown_field_is_refused() -> None:
    document = _minimal()
    document["components"][0]["properties"][1]["fields"] = ["Radius"]
    assert "PointLight.Range: not in Light: Radius" in _error_of(document)


def test_vector_goes_to_three_fields() -> None:
    document = _minimal()
    document["components"][0]["properties"][0]["fields"] = ["ColorR"]
    assert "PointLight.Color: vec3 values go to 3 fields, not 1" in _error_of(document)


def test_field_used_twice_in_a_component_is_refused() -> None:
    document = _minimal()
    document["components"][0]["properties"][1]["fields"] = ["ColorR"]
    assert "duplicate field use in PointLight: ColorR" in _error_of(document)


def test_fields_need_a_wire() -> None:
    document = _minimal()
    document["components"][1]["properties"][0]["type"] = "float"
    document["components"][1]["properties"][0]["fields"] = ["Range"]
    assert "Geometry.GeometryUri: fields without a wire" in _error_of(document)


def test_wired_asset_property_is_refused() -> None:
    document = _minimal()
    document["components"][0]["properties"][1] = {
        "name": "Mesh",
        "key": "/mesh",
        "type": "asset",
    }
    assert "PointLight.Mesh: asset values have no fields" in _error_of(document)


def test_unknown_field_set_is_refused() -> None:
    document = _minimal()
    document["components"][0]["wire"]["field_set"] = "Lamp"
    assert "PointLight: unknown field set Lamp" in _error_of(document)


def _with_surface(document: dict[str, Any]) -> dict[str, Any]:
    document["components"][1]["parts"] = [
        {
            "name": "Surface",
            "kind": "surface",
            "properties": [
                {
                    "name": "Material",
                    "key": "/material",
                    "type": "asset",
                    "sources": ["asset"],
                }
            ],
        }
    ]
    return document


def test_schema_declares_the_surface_material() -> None:
    schema = model.load(_SCHEMA, _JSON_SCHEMA)
    geometry = next(entry for entry in schema.components if entry.kind == "geometry")
    surface = geometry.parts[0]
    assert (surface.name, surface.kind) == ("Surface", "surface")
    assert [(entry.key, entry.type, entry.sources) for entry in surface.properties] == [
        ("/material", "asset", ("asset",))
    ]


def test_parts_are_not_rendered() -> None:
    text = cpp.render(model.parse(_with_surface(_minimal()), _rules()), "test.yaml")
    assert "Surface" not in text
    assert "Material" not in text


def test_part_property_needs_a_source() -> None:
    document = _with_surface(_minimal())
    del document["components"][1]["parts"][0]["properties"][0]["sources"]
    assert "/components/1/parts/0/properties/0" in _error_of(document)


def test_part_property_has_no_fields() -> None:
    document = _with_surface(_minimal())
    document["components"][1]["parts"][0]["properties"][0]["fields"] = ["Range"]
    assert "/components/1/parts/0/properties/0" in _error_of(document)


def test_unknown_source_is_refused() -> None:
    document = _minimal()
    document["components"][1]["properties"][0]["sources"] = ["prefab"]
    assert "/components/1/properties/0/sources/0" in _error_of(document)


def test_part_named_like_a_property_is_refused() -> None:
    document = _with_surface(_minimal())
    document["components"][1]["parts"][0]["name"] = "GeometryUri"
    assert "duplicate part or property in Geometry: GeometryUri" in _error_of(document)


def test_duplicate_part_kind_is_refused() -> None:
    document = _with_surface(_minimal())
    twin = copy.deepcopy(document["components"][1]["parts"][0])
    twin["name"] = "Section"
    document["components"][1]["parts"].append(twin)
    assert "duplicate part kind in Geometry: surface" in _error_of(document)


def test_duplicate_key_in_a_part_is_refused() -> None:
    document = _with_surface(_minimal())
    twin = copy.deepcopy(document["components"][1]["parts"][0]["properties"][0])
    twin["name"] = "Shade"
    document["components"][1]["parts"][0]["properties"].append(twin)
    assert "duplicate key in Geometry.Surface: /material" in _error_of(document)


def test_cli_writes_once_and_refuses_an_invalid_schema(tmp_path: Path) -> None:
    out = tmp_path / "Generated.h"
    arguments = ["--json-schema", str(_JSON_SCHEMA), "--cpp-out", str(out)]
    assert cli.main(["--schema", str(_SCHEMA), *arguments]) == 0
    written = out.stat().st_mtime_ns
    assert cli.main(["--schema", str(_SCHEMA), *arguments]) == 0
    assert out.stat().st_mtime_ns == written

    invalid = _minimal()
    invalid["version"] = 2
    bad = tmp_path / "bad.yaml"
    bad.write_text(yaml.safe_dump(invalid), encoding="utf-8")
    assert cli.main(["--schema", str(bad), *arguments]) == 1
    assert out.stat().st_mtime_ns == written
