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

from scene_schema_codegen import changes, cli, cpp, model

_SCHEMA_DIR = Path(__file__).resolve().parents[3] / "Schema"
_SCHEMA = _SCHEMA_DIR / "SceneComponents.yaml"
_JSON_SCHEMA = _SCHEMA_DIR / "SceneComponents.schema.json"
_HEADER = _SCHEMA_DIR / "Generated.SceneComponents.h"
_CHANGES = _SCHEMA_DIR / "Generated.SceneChanges.h"


def _rules() -> object:
    return json.loads(_JSON_SCHEMA.read_text(encoding="utf-8"))


def _minimal() -> dict[str, Any]:
    return {
        "version": 2,
        "units": [
            {"id": "metre", "abbreviation": "m", "name": "metres"},
            {"id": "degree", "abbreviation": "°", "name": "degrees"},
            {"id": "radian", "abbreviation": "rad", "name": "radians"},
        ],
        "enums": [
            {
                "name": "Mode",
                "members": [
                    {"name": "Auto", "value": 0},
                    {"name": "Fixed", "value": 1},
                ],
            }
        ],
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
                "cardinality": "optional",
                "wire": {"id": 3, "field_set": "Light"},
                "properties": [
                    {
                        "name": "Color",
                        "key": "/color",
                        "type": "vec3",
                        "fields": ["ColorR", "ColorG", "ColorB"],
                        "override_id": 5,
                        "default": [1, 1, 1],
                        "range": {"min": 0, "max": 1},
                        "commit": "clamp",
                    },
                    {
                        "name": "Range",
                        "key": "/range",
                        "type": "float",
                        "fields": ["Range"],
                        "override_id": 6,
                        "default": 20,
                        "range": {"min": 0, "min_exclusive": True},
                        "unit": "metre",
                    },
                ],
            },
            {
                "name": "Geometry",
                "kind": "geometry",
                "cardinality": "optional",
                "properties": [
                    {
                        "name": "GeometryUri",
                        "key": "/geometry_uri",
                        "type": "asset",
                        "asset_type": "geometry",
                    }
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


def test_committed_changes_header_matches_the_schema() -> None:
    schema = model.load(_SCHEMA, _JSON_SCHEMA)
    expected = changes.render(schema, _SCHEMA.name)
    assert _CHANGES.read_bytes() == expected.encode("utf-8")


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
        {
            "name": "Scripting",
            "kind": "scripting",
            "cardinality": "optional",
            "properties": [],
        }
    )
    text = cpp.render(model.parse(document, _rules()), "test.yaml")
    assert "Scripting" not in text


def test_json_schema_violation_is_reported_with_its_path() -> None:
    document = _minimal()
    document["components"][0]["properties"][1]["type"] = "double"
    assert "/components/0/properties/1" in _error_of(document)


def test_enum_property_requires_its_enumeration() -> None:
    document = _minimal()
    document["components"][0]["properties"][1].update(type="enum", default="Auto")
    del document["components"][0]["properties"][1]["range"]
    del document["components"][0]["properties"][1]["unit"]
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
    uri = document["components"][1]["properties"][0]
    del uri["asset_type"]
    uri.update(type="float", fields=["Range"], override_id=7, default=0)
    assert "Geometry.GeometryUri: fields without a wire" in _error_of(document)


def test_wired_asset_property_is_refused() -> None:
    document = _minimal()
    document["components"][0]["properties"][1] = {
        "name": "Mesh",
        "key": "/mesh",
        "type": "asset",
        "asset_type": "geometry",
    }
    assert "PointLight.Mesh: asset values have no fields" in _error_of(document)


def test_unknown_field_set_is_refused() -> None:
    document = _minimal()
    document["components"][0]["wire"]["field_set"] = "Lamp"
    assert "PointLight: unknown field set Lamp" in _error_of(document)


def test_field_types_follow_the_properties_they_receive() -> None:
    document = _minimal()
    document["field_sets"][0]["fields"].append({"name": "Mode", "id": 6})
    document["components"][0]["properties"].append(
        {
            "name": "Mode",
            "key": "/mode",
            "type": "enum",
            "enum": "Mode",
            "fields": ["Mode"],
            "override_id": 7,
            "default": "Auto",
        }
    )
    text = cpp.render(model.parse(document, _rules()), "test.yaml")
    assert "  case LightField::kColorG: return FieldType::kFloat;\n" in text
    assert "  case LightField::kRange: return FieldType::kFloat;\n" in text
    assert "  case LightField::kMode: return FieldType::kInt32;\n" in text
    assert (
        "  case ComponentId::kPointLight: "
        "return FindFieldType(static_cast<LightField>(field));\n"
    ) in text


def test_field_no_property_uses_is_refused() -> None:
    document = _minimal()
    document["field_sets"][0]["fields"].append({"name": "Radius", "id": 6})
    assert "Light.Radius: no property uses it" in _error_of(document)


def test_field_receiving_two_value_types_is_refused() -> None:
    document = _minimal()
    twin = copy.deepcopy(document["components"][0])
    twin.update(
        name="SpotLight", kind="spot-light", wire={"id": 4, "field_set": "Light"}
    )
    twin["properties"][0]["override_id"] = 7
    twin["properties"][1].update(type="int", default=20, override_id=8)
    document["components"].append(twin)
    assert "Light.Range: receives float and int32 values" in _error_of(document)


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
                    "asset_type": "material",
                    "sources": ["asset"],
                    "override_id": 1,
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
    assert [
        (
            entry.key,
            entry.type,
            entry.asset_type,
            entry.sources,
            entry.override_id,
            entry.default,
        )
        for entry in surface.properties
    ] == [
        ("/material", "asset", "material", ("asset",), 1, None),
        ("/visible", "bool", None, ("asset",), 2, True),
        ("/casts_shadows", "bool", None, ("asset",), 3, True),
    ]


def test_part_property_is_rendered_as_an_override() -> None:
    text = cpp.render(model.parse(_with_surface(_minimal()), _rules()), "test.yaml")
    assert "  kGeometrySurfaceMaterial = 1,\n" in text
    assert (
        "  OverrideProperty {\n"
        "    .id = OverrideId::kGeometrySurfaceMaterial,\n"
        '    .component_kind = "geometry",\n'
        "    .component = ComponentId::kInvalid,\n"
        "    .part_kind = PartKind::kSurface,\n"
        '    .key = "/material",\n'
        "    .type = ValueType::kAsset,\n"
        "    .asset_type = AssetType::kMaterial,\n"
        "    .encoding = Encoding::kNone,\n"
        "    .member = ValueMember::kAsset,\n"
        "    .asset_sourced = true,\n"
        "  },\n"
    ) in text
    assert "constexpr auto FindOverrideProperty(const OverrideId id)" in text
    assert "  const std::string_view key) noexcept -> const OverrideProperty*" in text
    assert "namespace geometry::surface" not in text


def test_asset_property_needs_its_asset_type() -> None:
    document = _with_surface(_minimal())
    del document["components"][1]["parts"][0]["properties"][0]["asset_type"]
    assert "/components/1/parts/0/properties/0" in _error_of(document)


def test_asset_type_on_a_value_property_is_refused() -> None:
    document = _with_surface(_minimal())
    document["components"][1]["parts"][0]["properties"].append(
        {
            "name": "Visibility",
            "key": "/visible",
            "type": "bool",
            "asset_type": "material",
            "sources": ["asset"],
            "override_id": 2,
            "default": True,
        }
    )
    assert "/components/1/parts/0/properties/1" in _error_of(document)


def test_part_property_needs_a_source() -> None:
    document = _with_surface(_minimal())
    del document["components"][1]["parts"][0]["properties"][0]["sources"]
    assert "/components/1/parts/0/properties/0" in _error_of(document)


def test_part_property_has_no_fields() -> None:
    document = _with_surface(_minimal())
    document["components"][1]["parts"][0]["properties"][0]["fields"] = ["Range"]
    assert "/components/1/parts/0/properties/0" in _error_of(document)


@pytest.mark.parametrize("source", ["prefab", "model"])
def test_unknown_source_is_refused(source: str) -> None:
    document = _minimal()
    document["components"][1]["properties"][0].update(sources=[source], override_id=1)
    assert "/components/1/properties/0/sources/0" in _error_of(document)


def test_part_named_like_a_property_is_refused() -> None:
    document = _with_surface(_minimal())
    document["components"][1]["parts"][0]["name"] = "GeometryUri"
    assert "duplicate part or property in Geometry: GeometryUri" in _error_of(document)


def test_duplicate_part_kind_is_refused() -> None:
    document = _with_surface(_minimal())
    twin = copy.deepcopy(document["components"][1]["parts"][0])
    twin["name"] = "Section"
    twin["properties"][0]["override_id"] = 2
    document["components"][1]["parts"].append(twin)
    assert "duplicate part kind in Geometry: surface" in _error_of(document)


def test_duplicate_key_in_a_part_is_refused() -> None:
    document = _with_surface(_minimal())
    twin = copy.deepcopy(document["components"][1]["parts"][0]["properties"][0])
    twin["name"] = "Shade"
    twin["override_id"] = 2
    document["components"][1]["parts"][0]["properties"].append(twin)
    assert "duplicate key in Geometry.Surface: /material" in _error_of(document)


def test_cli_writes_once_and_refuses_an_invalid_schema(tmp_path: Path) -> None:
    out = tmp_path / "Generated.h"
    arguments = [
        "--json-schema",
        str(_JSON_SCHEMA),
        "--cpp-out",
        str(out),
        "--changes-out",
        str(tmp_path / "Changes.h"),
    ]
    assert cli.main(["--schema", str(_SCHEMA), *arguments]) == 0
    written = out.stat().st_mtime_ns
    assert cli.main(["--schema", str(_SCHEMA), *arguments]) == 0
    assert out.stat().st_mtime_ns == written

    invalid = _minimal()
    invalid["version"] = 1
    bad = tmp_path / "bad.yaml"
    bad.write_text(yaml.safe_dump(invalid), encoding="utf-8")
    assert cli.main(["--schema", str(bad), *arguments]) == 1
    assert out.stat().st_mtime_ns == written


def _text(document: dict[str, Any]) -> str:
    return cpp.render(model.parse(document, _rules()), "test.yaml")


def test_schema_declares_the_override_ids() -> None:
    schema = model.load(_SCHEMA, _JSON_SCHEMA)
    assert [
        (entry.id, entry.component.kind, entry.part and entry.part.kind, entry.prop.key)
        for entry in schema.overrides()[:5]
    ] == [
        (1, "geometry", "surface", "/material"),
        (2, "geometry", "surface", "/visible"),
        (3, "geometry", "surface", "/casts_shadows"),
        (4, "geometry", None, "/lod_policy"),
        (5, "transform", None, "/local_position/0"),
    ]


def test_every_valued_property_declares_a_default() -> None:
    schema = model.load(_SCHEMA, _JSON_SCHEMA)
    for component in schema.components:
        properties = [*component.properties]
        for part in component.parts:
            properties.extend(part.properties)
        for entry in properties:
            assert (entry.default is None) == (entry.type in ("asset", "string"))


def test_required_components_are_the_node_core() -> None:
    schema = model.load(_SCHEMA, _JSON_SCHEMA)
    assert [
        component.name
        for component in schema.components
        if component.cardinality == "required"
    ] == ["Metadata", "Hierarchy", "Transform", "Node"]


def test_defaults_and_ranges_are_rendered_per_field() -> None:
    text = _text(_minimal())
    assert "namespace point_light {" in text
    assert "inline constexpr float kColorGDefault = 1.0F;" in text
    assert (
        "inline constexpr PropertyRange<float> kColorGRange "
        "{ .min = 0.0F, .max = 1.0F, .clamp = true };"
    ) in text
    assert "inline constexpr float kRangeDefault = 20.0F;" in text
    assert (
        "inline constexpr PropertyRange<float> kRangeRange "
        "{ .min = 0.0F, .min_exclusive = true };"
    ) in text


def test_unbounded_numeric_property_renders_an_unlimited_range() -> None:
    document = _minimal()
    del document["components"][0]["properties"][1]["range"]
    assert "inline constexpr PropertyRange<float> kRangeRange {};" in _text(document)


def test_encoding_converts_default_and_range_into_field_units() -> None:
    document = _minimal()
    document["components"][0]["properties"][1].update(
        encoding="degrees-to-radians",
        unit="degree",
        default=90,
        range={"min": 0, "max": 180},
    )
    text = _text(document)
    assert "inline constexpr float kRangeDefault = 1.5707964F;" in text
    assert "{ .min = 0.0F, .max = 3.1415927F }" in text


def test_encoding_needs_its_authored_unit() -> None:
    document = _minimal()
    document["components"][0]["properties"][1]["encoding"] = "degrees-to-radians"
    assert "PointLight.Range: degrees-to-radians needs unit degree" in _error_of(
        document
    )


def _as_int(document: dict[str, Any], **values: Any) -> dict[str, Any]:
    entry = document["components"][0]["properties"][1]
    entry.update(type="int", **values)
    del entry["unit"]
    return document


def test_int_values_render_as_int32() -> None:
    text = _text(_as_int(_minimal(), default=4, range={"min": 1, "max": 4}))
    assert "inline constexpr std::int32_t kRangeDefault = 4;" in text
    assert (
        "inline constexpr PropertyRange<std::int32_t> kRangeRange "
        "{ .min = 1, .max = 4 };"
    ) in text


def test_int_range_bounds_are_whole() -> None:
    document = _as_int(_minimal(), default=1, range={"min": 0.5})
    assert "PointLight.Range: int range with a fractional bound" in _error_of(document)


def test_default_outside_its_range_is_refused() -> None:
    document = _minimal()
    document["components"][0]["properties"][1]["default"] = 0
    assert "PointLight.Range: default 0 is outside its range" in _error_of(document)


def test_vector_default_is_checked_per_component() -> None:
    document = _minimal()
    document["components"][0]["properties"][0]["default"] = [1, 2, 1]
    assert "PointLight.Color: default 2 is outside its range" in _error_of(document)


def test_empty_range_is_refused() -> None:
    document = _minimal()
    document["components"][0]["properties"][1]["range"] = {
        "min": 1,
        "max": 1,
        "max_exclusive": True,
    }
    assert "PointLight.Range: empty range" in _error_of(document)


def test_missing_default_is_refused() -> None:
    document = _minimal()
    del document["components"][0]["properties"][1]["default"]
    assert "/components/0/properties/1" in _error_of(document)


def test_commit_needs_a_range() -> None:
    document = _minimal()
    del document["components"][0]["properties"][0]["range"]
    assert "/components/0/properties/0" in _error_of(document)


def test_clamping_range_renders_its_commit() -> None:
    text = _text(_minimal())
    assert (
        "inline constexpr PropertyRange<float> kColorRRange "
        "{ .min = 0.0F, .max = 1.0F, .clamp = true };"
    ) in text
    assert (
        "inline constexpr PropertyRange<float> kRangeRange "
        "{ .min = 0.0F, .min_exclusive = true };"
    ) in text


@pytest.mark.parametrize(
    "bounds",
    [
        {"min": 0, "min_exclusive": True},
        {"max": 1, "max_exclusive": True},
        {"min": 0, "nonzero": True},
    ],
)
def test_clamp_needs_a_bound_it_can_reach(bounds: dict[str, Any]) -> None:
    document = _minimal()
    document["components"][0]["properties"][0]["range"] = bounds
    assert "PointLight.Color: clamp needs a bound it can reach" in _error_of(document)


def test_field_value_checks_cover_each_wired_field_by_value_type() -> None:
    text = _text(_with_enum(_minimal()))
    assert (
        "    case LightField::kColorG: "
        "return point_light::kColorGRange.Contains(value);"
    ) in text
    assert (
        "    case LightField::kRange: "
        "return IsMember(point_light::kRangeValues, value);"
    ) in text
    assert "  case ComponentId::kPointLight: return false;" in text


def test_range_contains_honours_every_bound() -> None:
    bounds = model.Range(0.0, 1.0, True, False, False)
    assert not bounds.contains(0.0)
    assert bounds.contains(1.0)
    assert not bounds.contains(float("nan"))
    assert not model.Range(None, None, False, False, True).contains(0.0)
    assert model.UNLIMITED.contains(-1e30)
    assert not model.UNLIMITED.contains(float("inf"))


def _with_enum(document: dict[str, Any]) -> dict[str, Any]:
    document["components"][0]["properties"][1] = {
        "name": "Mode",
        "key": "/mode",
        "type": "enum",
        "enum": "Mode",
        "fields": ["Range"],
        "override_id": 6,
        "default": "Fixed",
    }
    return document


def test_enum_default_renders_its_wire_value_and_members() -> None:
    text = _text(_with_enum(_minimal()))
    assert "inline constexpr std::array<std::uint8_t, 2> kModeMembers { 0, 1 };" in text
    assert "enum class Mode : std::uint8_t {\n  kAuto = 0,\n  kFixed = 1,\n};" in text
    assert "inline constexpr Mode kRangeDefault = Mode::kFixed;" in text
    assert (
        "inline constexpr std::span<const std::uint8_t> kRangeValues { kModeMembers };"
    ) in text


def test_enum_member_keys_are_snake_case_in_member_order() -> None:
    document = _minimal()
    document["enums"][0]["members"][1]["name"] = "ManualDistances"
    text = _text(document)
    assert (
        "inline constexpr std::array<std::string_view, 2> "
        'kModeMemberKeys { "auto", "manual_distances" };'
    ) in text


def test_root_default_is_rendered() -> None:
    document = _with_enum(_minimal())
    document["components"][0]["properties"][1]["root_default"] = "Auto"
    text = _text(document)
    assert "inline constexpr Mode kRangeRootDefault = Mode::kAuto;" in text


def test_enum_default_must_be_a_member() -> None:
    document = _with_enum(_minimal())
    document["components"][0]["properties"][1]["default"] = "Manual"
    assert "PointLight.Mode: Manual is not a member of Mode" in _error_of(document)


def test_unknown_enumeration_is_refused() -> None:
    document = _with_enum(_minimal())
    document["components"][0]["properties"][1]["enum"] = "Style"
    assert "PointLight.Mode: unknown enumeration Style" in _error_of(document)


def test_duplicate_member_value_is_refused() -> None:
    document = _minimal()
    document["enums"][0]["members"][1]["value"] = 0
    assert "duplicate member value in Mode: 0" in _error_of(document)


def test_root_default_belongs_to_enum_properties() -> None:
    document = _minimal()
    document["components"][0]["properties"][1]["root_default"] = "Auto"
    assert "/components/0/properties/1" in _error_of(document)


def test_unknown_unit_is_refused() -> None:
    document = _minimal()
    document["components"][0]["properties"][1]["unit"] = "foot"
    assert "PointLight.Range: unknown unit foot" in _error_of(document)


def test_display_unit_converts_radians_to_degrees_only() -> None:
    document = _minimal()
    document["components"][0]["properties"][1]["display_unit"] = "degree"
    assert "PointLight.Range: metre values cannot be shown in degree" in _error_of(
        document
    )
    document["components"][0]["properties"][1]["unit"] = "radian"
    model.parse(document, _rules())


def test_units_are_not_rendered() -> None:
    assert "metre" not in _text(_minimal())


def test_sourced_property_needs_an_override_id() -> None:
    document = _with_surface(_minimal())
    del document["components"][1]["parts"][0]["properties"][0]["override_id"]
    assert "/components/1/parts/0/properties/0" in _error_of(document)


def _with_lod_policy(document: dict[str, Any], **policy: Any) -> dict[str, Any]:
    document["components"][1]["properties"].append(
        {
            "name": "LodPolicy",
            "key": "/lod_policy",
            "type": "lod-policy",
            "sources": ["asset"],
            "override_id": 4,
            "default": policy,
        }
    )
    return document


def test_duplicate_override_id_is_refused() -> None:
    document = _with_lod_policy(_with_surface(_minimal()), type="fixed", lod_index=0)
    document["components"][1]["properties"][1]["override_id"] = 1
    assert "duplicate override id: 1" in _error_of(document)


def test_fixed_lod_policy_default_is_rendered_as_a_record() -> None:
    text = _text(_with_lod_policy(_minimal(), type="fixed", lod_index=2))
    assert (
        "inline constexpr data::pak::geometry::LodPolicyRecord kLodPolicyDefault {\n"
        "  .type = data::pak::geometry::LodPolicyType::kFixed,\n"
        "  .lod_index = 2,\n"
        "};"
    ) in text
    assert "  kGeometryLodPolicy = 4,\n" in text
    assert "    .type = ValueType::kLodPolicy,\n" in text


def test_distance_lod_policy_default_renders_its_thresholds() -> None:
    text = _text(
        _with_lod_policy(
            _minimal(), type="distance", thresholds=[10, 20], hysteresis_ratio=0.1
        )
    )
    assert "  .boundary_count = 2,\n" in text
    assert "  .thresholds = { 10.0F, 20.0F },\n" in text
    assert "  .hysteresis_ratio = 0.1F,\n" in text


def test_lod_policy_with_too_many_thresholds_is_refused() -> None:
    document = _with_lod_policy(
        _minimal(), type="distance", thresholds=[1] * 8, hysteresis_ratio=0.1
    )
    assert "/thresholds: [1, 1, 1, 1, 1, 1, 1, 1] is too long" in _error_of(document)


def test_screen_space_error_arrays_must_match() -> None:
    document = _with_lod_policy(
        _minimal(),
        type="screen_space_error",
        enter_finer_sse=[1, 2],
        exit_coarser_sse=[1],
    )
    assert (
        "Geometry.LodPolicy: enter_finer_sse and exit_coarser_sse differ in size"
        in _error_of(document)
    )


def test_header_is_kept_out_of_clang_format() -> None:
    text = _text(_minimal())
    assert text.index("// clang-format off") < text.index("namespace oxygen")
    assert text.rstrip().endswith("// clang-format on")


def test_every_wired_property_is_changeable() -> None:
    schema = model.load(_SCHEMA, _JSON_SCHEMA)
    for component, _ in schema.wired():
        for entry in component.properties:
            assert entry.override_id is not None, f"{component.name}.{entry.name}"


def test_rotation_is_one_property_of_three_angles() -> None:
    schema = model.load(_SCHEMA, _JSON_SCHEMA)
    transform = next(entry for entry in schema.components if entry.kind == "transform")
    rotation = next(entry for entry in transform.properties if entry.name == "Rotation")
    assert (rotation.key, rotation.type, rotation.encoding) == (
        "/local_rotation_euler_degrees",
        "vec3",
        "euler-yxz-degrees-to-quat",
    )
    assert rotation.fields == (
        "RotationXDegrees",
        "RotationYDegrees",
        "RotationZDegrees",
    )


def test_wired_property_needs_an_override_id() -> None:
    document = _minimal()
    del document["components"][0]["properties"][1]["override_id"]
    assert "/components/0/properties/1" in _error_of(document)


def test_override_id_without_fields_or_sources_is_refused() -> None:
    document = _minimal()
    document["components"][1]["properties"][0]["override_id"] = 7
    assert "/components/1/properties/0" in _error_of(document)


def test_rotation_encoding_needs_three_angles() -> None:
    document = _minimal()
    document["components"][0]["properties"][1].update(
        encoding="euler-yxz-degrees-to-quat", unit="degree"
    )
    assert (
        "PointLight.Range: euler-yxz-degrees-to-quat encodes vec3 values only"
        in _error_of(document)
    )


def _with_rotation(document: dict[str, Any]) -> dict[str, Any]:
    entry = document["components"][0]["properties"][0]
    entry.update(
        name="Rotation",
        key="/rotation",
        encoding="euler-yxz-degrees-to-quat",
        unit="degree",
        default=[0, 0, 0],
    )
    del entry["range"]
    del entry["commit"]
    return document


def test_rotation_keeps_its_angles_in_its_fields_and_changes_whole() -> None:
    document = _with_rotation(_minimal())
    text = _text(document)
    assert "inline constexpr float kColorRDefault = 0.0F;" in text
    assert "    .encoding = Encoding::kEulerYxzDegreesToQuat,\n" in text
    assert "    .member = ValueMember::kQuat,\n" in text
    rendered = changes.render(model.parse(document, _rules()), "test.yaml")
    assert (
        "  case OverrideId::kPointLightRotation: return detail::EncodeRotation("
        "value, ComponentId::kPointLight, { static_cast<std::uint16_t>"
        "(LightField::kColorR), static_cast<std::uint16_t>(LightField::kColorG), "
        "static_cast<std::uint16_t>(LightField::kColorB) });"
    ) in rendered
    assert (
        "  case OverrideId::kPointLightRotation: pak::world::WriteRotation(record, "
        "{ value.as_quat[0], value.as_quat[1], value.as_quat[2], value.as_quat[3] "
        "}); return true;"
    ) in rendered


def test_changes_encode_and_apply_each_wired_property() -> None:
    document = _minimal()
    document["components"][0]["properties"][1].update(
        encoding="degrees-to-radians", unit="degree", range={"min": 0, "max": 180}
    )
    rendered = changes.render(model.parse(document, _rules()), "test.yaml")
    assert (
        "  case OverrideId::kPointLightRange: return detail::EncodeFloat(value, "
        "ComponentId::kPointLight, static_cast<std::uint16_t>(LightField::kRange), "
        "true);"
    ) in rendered
    assert (
        "  case OverrideId::kPointLightColor: return detail::WriteVec3(record, "
        "std::array { LightField::kColorR, LightField::kColorG, "
        "LightField::kColorB }, std::array { value.as_vec3[0], value.as_vec3[1], "
        "value.as_vec3[2] });"
    ) in rendered
    assert (
        "  case OverrideId::kPointLightRange: return pak::world::WriteField("
        "record, LightField::kRange, value.as_float);"
    ) in rendered
    assert "pak::world::PointLightRecord& record" in rendered


def test_changes_read_each_wired_property_in_schema_units() -> None:
    document = _with_rotation(_minimal())
    document["components"][0]["properties"][1].update(
        encoding="degrees-to-radians", unit="degree", range={"min": 0, "max": 180}
    )
    rendered = changes.render(model.parse(document, _rules()), "test.yaml")
    assert (
        "[[nodiscard]] inline auto ReadProperty(\n"
        "  const pak::world::PointLightRecord& record, const OverrideId id)\n"
        "  -> std::optional<SchemaValue>"
    ) in rendered
    assert (
        "  case OverrideId::kPointLightRange: return detail::ReadValue(record, "
        "LightField::kRange, true);"
    ) in rendered
    assert (
        "  case OverrideId::kPointLightRotation: return detail::ReadRotation(record);"
    ) in rendered


def test_asset_sourced_property_encodes_without_a_record() -> None:
    rendered = changes.render(
        model.parse(_with_surface(_minimal()), _rules()), "test.yaml"
    )
    assert (
        "  case OverrideId::kGeometrySurfaceMaterial: "
        "return detail::EncodeAsset(value);"
    ) in rendered
    assert "kGeometrySurfaceMaterial: return pak::world" not in rendered


def test_wired_component_without_a_record_is_refused() -> None:
    document = _minimal()
    document["components"][0]["kind"] = "lamp"
    with pytest.raises(model.SchemaError) as raised:
        changes.render(model.parse(document, _rules()), "test.yaml")
    assert "no cooked record for lamp" in str(raised.value)
