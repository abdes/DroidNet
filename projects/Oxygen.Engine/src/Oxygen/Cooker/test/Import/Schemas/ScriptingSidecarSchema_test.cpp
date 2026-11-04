//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Schemas/oxygen.scripting-sidecar.schema.json

#include <string>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/test/Support/AuthoredFiles.h>
#include <Oxygen/Cooker/test/Support/JsonSchema.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using nlohmann::json;
using oxygen::cooker::test::LoadSchema;
using oxygen::cooker::test::SchemaCase;
using oxygen::cooker::test::ScriptSidecar;

auto Schema() -> const json&
{
  static const auto schema
    = LoadSchema("Import/Schemas/oxygen.scripting-sidecar.schema.json");
  return schema;
}

constexpr auto kSlot
  = std::string_view { "00000000-0000-4000-8000-00000000000a" };
constexpr auto kNode
  = std::string_view { "00000000-0000-4000-8000-0000000000c1" };
constexpr auto kScript = std::string_view { "/Content/Scripts/spin.oscript" };

//! A binding row with the required fields, and `extra` merged over them.
auto Binding(const json& extra = json::object()) -> json
{
  auto row = json {
    { "node_index", 0 },
    { "slot_id", kSlot },
    { "script_virtual_path", kScript },
  };
  row.update(extra);
  return row;
}

//! An instance binding row with the required fields, and `extra` merged over
//! them.
auto Instance(const json& extra = json::object()) -> json
{
  auto row = json {
    { "node_index", 1 },
    { "node_path", json::array({ kNode }) },
    { "slot_id", kSlot },
  };
  row.update(extra);
  return row;
}

//! A document whose only binding row holds `param`.
auto WithParam(json param) -> std::string
{
  return json {
    {
      "bindings",
      json::array(
        { Binding({ { "params", json::array({ std::move(param) }) } }) }),
    },
  }
    .dump();
}

auto Param(const std::string_view type, json value) -> json
{
  return json {
    { "key", "p" },
    { "type", type },
    { "value", std::move(value) },
  };
}

//! A document whose only instance row holds `row`.
auto WithInstance(json row) -> std::string
{
  return json {
    { "bindings", json::array() },
    { "instance_bindings", json::array({ std::move(row) }) },
  }
    .dump();
}

//! A document whose only binding row is `row`.
auto WithBinding(json row) -> std::string
{
  return json { { "bindings", json::array({ std::move(row) }) } }.dump();
}

class ScriptingSidecarSchemaCaseTest
  : public ::testing::TestWithParam<SchemaCase> { };

NOLINT_TEST_P(ScriptingSidecarSchemaCaseTest, ValidatesDocument)
{
  oxygen::cooker::test::ExpectSchemaCase(Schema(), GetParam());
}

INSTANTIATE_TEST_SUITE_P(Cases, ScriptingSidecarSchemaCaseTest,
  ::testing::Values(
    SchemaCase {
      "ScriptingSidecarSchemaAcceptsAuthoredDocument",
      ScriptSidecar { "/Content/Scenes/Test.script-sidecar" }
        .Set("/$schema",
          "./src/Oxygen/Cooker/Import/Schemas/"
          "oxygen.scripting-sidecar.schema.json")
        .Bind(0, "/Content/Scripts/spin.oscript",
          ScriptSidecar::AssetParam { "mesh", "/Content/Geometry/Cube.ogeo" })
        .Bind(3, "/Content/Scripts/spin.oscript")
        .Text(),
      true,
      "",
    },
    SchemaCase {
      "ScriptingSidecarSchemaAcceptsEveryParamType",
      json {
        { "bindings",
          json::array({
            Binding({
              { "execution_order", -2 },
              { "params",
                json::array({
                  Param("bool", true),
                  Param("int32", -2147483648LL),
                  Param("int32", 2147483647),
                  Param("float", 2),
                  Param("float", 2.5),
                  Param("string", "hero"),
                  Param("vec2", json::array({ 1, 0.5 })),
                  Param("vec3", json::array({ 1, 0, 0 })),
                  Param("vec4", json::array({ 1, 0, 0, 1 })),
                  Param("asset", "../Materials/Brick.omat"),
                  Param("node", json { { "node", 2 } }),
                  Param("node",
                    json { { "node", 4294967294LL },
                      { "path", json::array({ kNode }) } }),
                }) },
            }),
          }) },
        { "instance_bindings",
          json::array({
            Instance({ { "params", json::array({ Param("bool", false) }) } }),
          }) },
      }
        .dump(),
      true,
      "",
    },
    SchemaCase {
      "ScriptingSidecarSchemaAcceptsEmptyBindings",
      R"({ "bindings": [] })",
      true,
      "",
    },
    SchemaCase {
      "ScriptingSidecarSchemaRejectsMissingBindings",
      R"({ "instance_bindings": [] })",
      false,
      "'bindings'",
    },
    SchemaCase {
      "ScriptingSidecarSchemaRejectsBindingsOfAnotherType",
      R"({ "bindings": {} })",
      false,
      "/bindings: ",
    },
    SchemaCase {
      "ScriptingSidecarSchemaRejectsInstanceBindingsOfAnotherType",
      R"({ "bindings": [], "instance_bindings": 1 })",
      false,
      "/instance_bindings: ",
    },
    SchemaCase {
      "ScriptingSidecarSchemaRejectsRootOfAnotherType",
      "5",
      false,
      "<root>: ",
    },
    SchemaCase {
      "ScriptingSidecarSchemaRejectsRowOfAnotherType",
      R"({ "bindings": [ 3 ] })",
      false,
      "/bindings/0: ",
    },
    SchemaCase {
      "ScriptingSidecarSchemaRejectsMissingNodeIndex",
      json { { "bindings",
               json::array({ json { { "slot_id", kSlot },
                 { "script_virtual_path", kScript } } }) } }
        .dump(),
      false,
      "'node_index'",
    },
    SchemaCase {
      "ScriptingSidecarSchemaRejectsNegativeNodeIndex",
      WithBinding(Binding({ { "node_index", -1 } })),
      false,
      "/bindings/0/node_index: ",
    },
    SchemaCase {
      "ScriptingSidecarSchemaRejectsNodeIndexAboveUint32",
      WithBinding(Binding({ { "node_index", 4294967296LL } })),
      false,
      "/bindings/0/node_index: ",
    },
    SchemaCase {
      "ScriptingSidecarSchemaRejectsMissingSlotId",
      json { { "bindings",
               json::array({ json { { "node_index", 0 },
                 { "script_virtual_path", kScript } } }) } }
        .dump(),
      false,
      "'slot_id'",
    },
    SchemaCase {
      "ScriptingSidecarSchemaRejectsSlotIdOfAnotherType",
      WithBinding(Binding({ { "slot_id", 7 } })),
      false,
      "/bindings/0/slot_id: ",
    },
    SchemaCase {
      "ScriptingSidecarSchemaRejectsMissingScriptPath",
      json { { "bindings",
               json::array(
                 { json { { "node_index", 0 }, { "slot_id", kSlot } } }) } }
        .dump(),
      false,
      "'script_virtual_path'",
    },
    SchemaCase {
      "ScriptingSidecarSchemaRejectsMalformedScriptPath",
      WithBinding(Binding({ { "script_virtual_path", "no_extension" } })),
      false,
      "/bindings/0/script_virtual_path: ",
    },
    SchemaCase {
      "ScriptingSidecarSchemaRejectsFractionalExecutionOrder",
      WithBinding(Binding({ { "execution_order", 1.5 } })),
      false,
      "/bindings/0/execution_order: ",
    },
    SchemaCase {
      "ScriptingSidecarSchemaRejectsExecutionOrderAboveInt32",
      WithBinding(Binding({ { "execution_order", 2147483648LL } })),
      false,
      "/bindings/0/execution_order: ",
    },
    SchemaCase {
      "ScriptingSidecarSchemaRejectsExecutionOrderBelowInt32",
      WithBinding(Binding({ { "execution_order", -2147483649LL } })),
      false,
      "/bindings/0/execution_order: ",
    },
    SchemaCase {
      "ScriptingSidecarSchemaRejectsParamsOfAnotherType",
      WithBinding(Binding({ { "params", 1 } })),
      false,
      "/bindings/0/params: ",
    },
    SchemaCase {
      "ScriptingSidecarSchemaRejectsParamMissingKey",
      WithBinding(Binding({ { "params",
        json::array({ json { { "type", "bool" }, { "value", true } } }) } })),
      false,
      "'key'",
    },
    SchemaCase {
      "ScriptingSidecarSchemaRejectsEmptyParamKey",
      WithBinding(Binding({ { "params",
        json::array({ json {
          { "key", "" }, { "type", "bool" }, { "value", true } } }) } })),
      false,
      "/bindings/0/params/0/key: ",
    },
    SchemaCase {
      "ScriptingSidecarSchemaRejectsParamKeyWithNul",
      WithBinding(Binding({ { "params",
        json::array({ json { { "key", std::string { "a\0b", 3 } },
          { "type", "bool" }, { "value", true } } }) } })),
      false,
      "/bindings/0/params/0/key: ",
    },
    SchemaCase {
      "ScriptingSidecarSchemaRejectsParamMissingType",
      WithBinding(Binding({ { "params",
        json::array({ json { { "key", "p" }, { "value", true } } }) } })),
      false,
      "'type'",
    },
    SchemaCase {
      "ScriptingSidecarSchemaRejectsParamMissingValue",
      WithBinding(Binding({ { "params",
        json::array({ json { { "key", "p" }, { "type", "bool" } } }) } })),
      false,
      "'value'",
    },
    SchemaCase {
      "ScriptingSidecarSchemaRejectsUnknownParamType",
      WithParam(Param("quaternion", json::array({ 0, 0, 0, 1 }))),
      false,
      "/bindings/0/params/0/type: ",
    },
    SchemaCase {
      "ScriptingSidecarSchemaRejectsBoolParamOfAnotherType",
      WithParam(Param("bool", 1)),
      false,
      "/bindings/0/params/0/value: ",
    },
    SchemaCase {
      "ScriptingSidecarSchemaRejectsFractionalInt32Param",
      WithParam(Param("int32", 1.5)),
      false,
      "/bindings/0/params/0/value: ",
    },
    SchemaCase {
      "ScriptingSidecarSchemaRejectsInt32ParamAboveRange",
      WithParam(Param("int32", 2147483648LL)),
      false,
      "/bindings/0/params/0/value: ",
    },
    SchemaCase {
      "ScriptingSidecarSchemaRejectsFloatParamOfAnotherType",
      WithParam(Param("float", "1.0")),
      false,
      "/bindings/0/params/0/value: ",
    },
    SchemaCase {
      "ScriptingSidecarSchemaRejectsStringParamOfAnotherType",
      WithParam(Param("string", 1)),
      false,
      "/bindings/0/params/0/value: ",
    },
    SchemaCase {
      "ScriptingSidecarSchemaRejectsStringParamWithNul",
      WithParam(Param("string", std::string { "a\0b", 3 })),
      false,
      "/bindings/0/params/0/value: ",
    },
    SchemaCase {
      "ScriptingSidecarSchemaRejectsVec2ParamOfWrongSize",
      WithParam(Param("vec2", json::array({ 1, 2, 3 }))),
      false,
      "/bindings/0/params/0/value: ",
    },
    SchemaCase {
      "ScriptingSidecarSchemaRejectsVec3ParamOfWrongSize",
      WithParam(Param("vec3", json::array({ 1, 2 }))),
      false,
      "/bindings/0/params/0/value: ",
    },
    SchemaCase {
      "ScriptingSidecarSchemaRejectsVec4ParamOfWrongSize",
      WithParam(Param("vec4", json::array({ 1, 2, 3 }))),
      false,
      "/bindings/0/params/0/value: ",
    },
    SchemaCase {
      "ScriptingSidecarSchemaRejectsVectorParamOfNonNumbers",
      WithParam(Param("vec3", json::array({ 1, "y", 3 }))),
      false,
      "/bindings/0/params/0/value/1: ",
    },
    SchemaCase {
      "ScriptingSidecarSchemaRejectsMalformedAssetParam",
      WithParam(Param("asset", "")),
      false,
      "/bindings/0/params/0/value: ",
    },
    SchemaCase {
      "ScriptingSidecarSchemaRejectsNodeParamOfAnotherType",
      WithParam(Param("node", 2)),
      false,
      "/bindings/0/params/0/value: ",
    },
    SchemaCase {
      "ScriptingSidecarSchemaRejectsNodeParamMissingNode",
      WithParam(Param("node", json::object())),
      false,
      "'node'",
    },
    SchemaCase {
      "ScriptingSidecarSchemaRejectsUnsetNodeParam",
      WithParam(Param("node", json { { "node", 4294967295LL } })),
      false,
      "/bindings/0/params/0/value/node: ",
    },
    SchemaCase {
      "ScriptingSidecarSchemaRejectsEmptyNodeParamPath",
      WithParam(
        Param("node", json { { "node", 1 }, { "path", json::array() } })),
      false,
      "/bindings/0/params/0/value/path: ",
    },
    SchemaCase {
      "ScriptingSidecarSchemaRejectsMissingInstanceNodeIndex",
      WithInstance(
        json { { "node_path", json::array({ kNode }) }, { "slot_id", kSlot } }),
      false,
      "'node_index'",
    },
    SchemaCase {
      "ScriptingSidecarSchemaRejectsMissingInstanceNodePath",
      WithInstance(json { { "node_index", 1 }, { "slot_id", kSlot } }),
      false,
      "'node_path'",
    },
    SchemaCase {
      "ScriptingSidecarSchemaRejectsEmptyInstanceNodePath",
      WithInstance(Instance({ { "node_path", json::array() } })),
      false,
      "/instance_bindings/0/node_path: ",
    },
    SchemaCase {
      "ScriptingSidecarSchemaRejectsInstanceNodePathOfAnotherType",
      WithInstance(Instance({ { "node_path", json::array({ 3 }) } })),
      false,
      "/instance_bindings/0/node_path/0: ",
    },
    SchemaCase {
      "ScriptingSidecarSchemaRejectsMissingInstanceSlotId",
      WithInstance(
        json { { "node_index", 1 }, { "node_path", json::array({ kNode }) } }),
      false,
      "'slot_id'",
    },
    SchemaCase {
      "ScriptingSidecarSchemaRejectsInstanceParamOfAnotherType",
      WithInstance(
        Instance({ { "params", json::array({ Param("bool", "yes") }) } })),
      false,
      "/instance_bindings/0/params/0/value: ",
    }),
  oxygen::cooker::test::SchemaCaseName);

} // namespace
