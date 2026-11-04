//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Schemas/oxygen.scene-source-inspection.schema.json

#include <cctype>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/test/Support/JsonSchema.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using nlohmann::json;
using oxygen::cooker::test::SchemaCase;

auto Schema() -> const json&
{
  static const auto schema = oxygen::cooker::test::LoadSchema(
    "Import/Schemas/oxygen.scene-source-inspection.schema.json");
  return schema;
}

auto Accepts(const std::string_view name, const json& document) -> SchemaCase
{
  return SchemaCase {
    .name = name,
    .json = document.dump(),
    .valid = true,
    .error_substr = "",
  };
}

auto Rejects(const std::string_view name, const json& document,
  const std::string_view error) -> SchemaCase
{
  return SchemaCase {
    .name = name,
    .json = document.dump(),
    .valid = false,
    .error_substr = error,
  };
}

auto With(json document, const std::string_view pointer, json value) -> json
{
  document[json::json_pointer { std::string { pointer } }] = std::move(value);
  return document;
}

auto Without(json document, const std::string_view pointer) -> json
{
  const auto at = json::json_pointer { std::string { pointer } };
  document[at.parent_pointer()].erase(at.back());
  return document;
}

//! A schema constant or list, read from the schema.
auto Owned(const std::string_view pointer) -> const json&
{
  return Schema().at(json::json_pointer { std::string { pointer } });
}

//! Keeps generated case names and messages alive for the whole run.
auto Keep(std::string text) -> std::string_view
{
  static auto storage = std::deque<std::string> {};
  return storage.emplace_back(std::move(text));
}

//! A test-name fragment: words split at `_`.
auto CamelCase(const std::string_view snake) -> std::string
{
  auto out = std::string {};
  auto upper = true;
  for (const auto c : snake) {
    if (c == '_') {
      upper = true;
      continue;
    }
    out += upper
      ? static_cast<char>(std::toupper(static_cast<unsigned char>(c)))
      : c;
    upper = false;
  }
  return out;
}

auto Diagnostic() -> json
{
  return json {
    { "severity", "Warning" },
    { "code", "import.example" },
    { "message", "Something to report." },
    { "source_path", "Model.gltf" },
    { "object_path", "Root/Body" },
  };
}

//! A parsed, supported source.
auto Supported() -> json
{
  return json {
    { "schema_version", Owned("/properties/schema_version/const") },
    { "parsed", true },
    { "supported", true },
    { "format", "gltf" },
    { "external_files", json::array({ "Model.bin", "Brick.png" }) },
    { "source_unit_meters", 1.0 },
    {
      "source_axes",
      json {
        { "right", "+X" },
        { "up", "+Y" },
        { "front", "+Z" },
      },
    },
    { "source_left_handed", false },
    { "target_axes", Owned("/properties/target_axes/const") },
    { "target_unit_meters", Owned("/properties/target_unit_meters/const") },
    { "reverses_winding", true },
    { "mesh_count", 2 },
    { "material_count", 1 },
    { "node_count", 3 },
    { "diagnostics", json::array({ Diagnostic() }) },
  };
}

//! A source nothing was read from.
auto Unsupported() -> json
{
  auto document = Supported();
  document["parsed"] = false;
  document["supported"] = false;
  document["format"] = "";
  document["external_files"] = json::array();
  document["source_unit_meters"] = nullptr;
  document["source_axes"] = json {
    { "right", "" },
    { "up", "" },
    { "front", "" },
  };
  document["source_left_handed"] = nullptr;
  document["reverses_winding"] = false;
  document["mesh_count"] = 0;
  document["material_count"] = 0;
  document["node_count"] = 0;
  return document;
}

auto Cases() -> std::vector<SchemaCase>
{
  auto cases = std::vector<SchemaCase> {
    Accepts("AcceptsSupportedSource", Supported()),
    Accepts("AcceptsUnsupportedSource", Unsupported()),
    Accepts("AcceptsFbxSource", With(Supported(), "/format", "fbx")),
    Accepts("AcceptsParsedButUnsupportedSource",
      With(Unsupported(), "/parsed", true)),
    Accepts("AcceptsUnsupportedSourceWithStatedUnit",
      With(Unsupported(), "/source_unit_meters", 0.01)),
    Accepts("AcceptsUnsupportedSourceWithUnknownAxis",
      With(Unsupported(), "/source_axes/up", "unknown")),
    Accepts("AcceptsUnsupportedSourceWithGltfFormat",
      With(Unsupported(), "/format", "gltf")),
    Accepts("AcceptsSupportedSourceWithoutDiagnostics",
      With(Supported(), "/diagnostics", json::array())),
    Accepts("AcceptsSupportedSourceWithoutExternalFiles",
      With(Supported(), "/external_files", json::array())),
    Accepts("AcceptsLeftHandedSource",
      With(Supported(), "/source_left_handed", true)),
    Accepts("AcceptsInfoDiagnostic",
      With(Supported(), "/diagnostics/0/severity", "Info")),
    Accepts("AcceptsErrorDiagnostic",
      With(Supported(), "/diagnostics/0/severity", "Error")),

    Rejects("RejectsMissingSourceAxesRight",
      Without(Supported(), "/source_axes/right"), "'right'"),
    Rejects("RejectsMissingSourceAxesUp",
      Without(Supported(), "/source_axes/up"), "'up'"),
    Rejects("RejectsMissingSourceAxesFront",
      Without(Supported(), "/source_axes/front"), "'front'"),
    Rejects("RejectsMissingDiagnosticSeverity",
      Without(Supported(), "/diagnostics/0/severity"), "'severity'"),
    Rejects("RejectsMissingDiagnosticCode",
      Without(Supported(), "/diagnostics/0/code"), "'code'"),
    Rejects("RejectsMissingDiagnosticMessage",
      Without(Supported(), "/diagnostics/0/message"), "'message'"),
    Rejects("RejectsMissingDiagnosticSourcePath",
      Without(Supported(), "/diagnostics/0/source_path"), "'source_path'"),
    Rejects("RejectsMissingDiagnosticObjectPath",
      Without(Supported(), "/diagnostics/0/object_path"), "'object_path'"),

    Rejects("RejectsUnknownTopLevelField", With(Supported(), "/unknown", true),
      "'unknown'"),
    Rejects("RejectsUnknownSourceAxesField",
      With(Supported(), "/source_axes/unknown", "+X"), "'unknown'"),
    Rejects("RejectsUnknownDiagnosticField",
      With(Supported(), "/diagnostics/0/unknown", "x"), "'unknown'"),

    Rejects("RejectsOtherVersion",
      With(Supported(), "/schema_version",
        Owned("/properties/schema_version/const").get<std::int64_t>() + 1),
      "/schema_version: "),
    Rejects("RejectsNonBooleanParsed", With(Supported(), "/parsed", "yes"),
      "/parsed: "),
    Rejects("RejectsNonBooleanSupported", With(Supported(), "/supported", 1),
      "/supported: "),
    Rejects(
      "RejectsUnknownFormat", With(Supported(), "/format", "obj"), "/format: "),
    Rejects(
      "RejectsNonStringFormat", With(Supported(), "/format", 1), "/format: "),
    Rejects("RejectsNonArrayExternalFiles",
      With(Supported(), "/external_files", "x"), "/external_files: "),
    Rejects("RejectsEmptyExternalFile",
      With(Supported(), "/external_files", json::array({ "" })),
      "/external_files/0: "),
    Rejects("RejectsNonStringExternalFile",
      With(Supported(), "/external_files", json::array({ 1 })),
      "/external_files/0: "),
    Rejects("RejectsDuplicateExternalFiles",
      With(Supported(), "/external_files",
        json::array({ "Model.bin", "Model.bin" })),
      "/external_files: "),
    Rejects("RejectsZeroUnit", With(Supported(), "/source_unit_meters", 0),
      "/source_unit_meters"),
    Rejects("RejectsNegativeUnit",
      With(Supported(), "/source_unit_meters", -1.0), "/source_unit_meters"),
    Rejects("RejectsNonNumberUnit",
      With(Supported(), "/source_unit_meters", "1"), "/source_unit_meters: "),
    Rejects("RejectsNonObjectSourceAxes",
      With(Supported(), "/source_axes", "x"), "/source_axes: "),
    Rejects("RejectsNonStringSourceAxis",
      With(Supported(), "/source_axes/right", 1), "/source_axes/right: "),
    Rejects("RejectsNonBooleanLeftHanded",
      With(Supported(), "/source_left_handed", "no"), "/source_left_handed: "),
    Rejects("RejectsNonBooleanReversesWinding",
      With(Supported(), "/reverses_winding", "no"), "/reverses_winding: "),
    Rejects("RejectsNegativeMeshCount", With(Supported(), "/mesh_count", -1),
      "/mesh_count: "),
    Rejects("RejectsNegativeMaterialCount",
      With(Supported(), "/material_count", -1), "/material_count: "),
    Rejects("RejectsNegativeNodeCount", With(Supported(), "/node_count", -1),
      "/node_count: "),
    Rejects("RejectsFractionalNodeCount", With(Supported(), "/node_count", 1.5),
      "/node_count: "),
    Rejects("RejectsNonArrayDiagnostics",
      With(Supported(), "/diagnostics", "x"), "/diagnostics: "),

    Rejects("RejectsOtherTargetRight",
      With(Supported(), "/target_axes/right", "-X"), "/target_axes"),
    Rejects("RejectsOtherTargetUp", With(Supported(), "/target_axes/up", "+Y"),
      "/target_axes"),
    Rejects("RejectsOtherTargetFront",
      With(Supported(), "/target_axes/front", "+Y"), "/target_axes"),
    Rejects("RejectsOtherTargetUnit",
      With(Supported(), "/target_unit_meters", 0.01), "/target_unit_meters"),

    Rejects("RejectsUnknownSeverity",
      With(Supported(), "/diagnostics/0/severity", "Unknown"),
      "/diagnostics/0/severity: "),
    Rejects("RejectsLowercaseSeverity",
      With(Supported(), "/diagnostics/0/severity", "warning"),
      "/diagnostics/0/severity: "),
    Rejects("RejectsNonStringDiagnosticCode",
      With(Supported(), "/diagnostics/0/code", 1), "/diagnostics/0/code: "),
    Rejects("RejectsNonStringDiagnosticMessage",
      With(Supported(), "/diagnostics/0/message", 1),
      "/diagnostics/0/message: "),
    Rejects("RejectsNonStringDiagnosticSourcePath",
      With(Supported(), "/diagnostics/0/source_path", 1),
      "/diagnostics/0/source_path: "),
    Rejects("RejectsNonStringDiagnosticObjectPath",
      With(Supported(), "/diagnostics/0/object_path", 1),
      "/diagnostics/0/object_path: "),

    // A supported source is a parsed one with a stated unit and valid axes.
    Rejects("RejectsSupportedSourceThatWasNotParsed",
      With(Supported(), "/parsed", false), "/parsed: "),
    Rejects("RejectsSupportedSourceWithoutFormat",
      With(Supported(), "/format", ""), "/format: "),
    Rejects("RejectsSupportedSourceWithoutUnit",
      With(Supported(), "/source_unit_meters", nullptr),
      "/source_unit_meters: "),
    Rejects("RejectsSupportedSourceWithoutHandedness",
      With(Supported(), "/source_left_handed", nullptr),
      "/source_left_handed: "),
    Rejects("RejectsSupportedSourceWithEmptyAxis",
      With(Supported(), "/source_axes/front", ""), "/source_axes/front: "),
    Rejects("RejectsSupportedSourceWithUnknownAxis",
      With(Supported(), "/source_axes/right", "unknown"),
      "/source_axes/right: "),
  };

  // Every key of the report is required.
  for (const auto& key : Owned("/required")) {
    const auto name = key.get<std::string>();
    cases.push_back(Rejects(Keep("RejectsMissing" + CamelCase(name)),
      Without(Supported(), "/" + name), Keep("'" + name + "'")));
  }
  return cases;
}

class SceneSourceInspectionSchemaTest
  : public ::testing::TestWithParam<SchemaCase> { };

NOLINT_TEST_P(SceneSourceInspectionSchemaTest, ValidatesDocument)
{
  oxygen::cooker::test::ExpectSchemaCase(Schema(), GetParam());
}

INSTANTIATE_TEST_SUITE_P(Cases, SceneSourceInspectionSchemaTest,
  ::testing::ValuesIn(Cases()), oxygen::cooker::test::SchemaCaseName);

} // namespace
