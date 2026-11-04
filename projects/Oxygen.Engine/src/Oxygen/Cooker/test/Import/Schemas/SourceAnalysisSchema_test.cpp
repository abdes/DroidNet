//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Schemas/oxygen.source-analysis.schema.json

#include <array>
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
    "Import/Schemas/oxygen.source-analysis.schema.json");
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

//! A schema constant, limit or list, read from the schema.
auto Owned(const std::string_view pointer) -> const json&
{
  return Schema().at(json::json_pointer { std::string { pointer } });
}

auto Definition(const std::string_view name) -> std::string
{
  return "/definitions/" + std::string { name };
}

auto Property(const std::string_view definition, const std::string_view name)
  -> std::string
{
  return Definition(definition) + "/properties/" + std::string { name };
}

auto Diagnostic() -> json
{
  return json {
    { "severity", "Warning" },
    { "code", "import.example" },
    { "message", "Something to report." },
    { "source_path", "Scene.json" },
    { "object_path", "Root/Body" },
    { "json_pointer", "/nodes/0" },
    { "object_name", "Body" },
  };
}

auto Logical() -> json
{
  return json {
    { "written", "Brick.otex" },
    { "virtual_path", "/Content/Textures/Brick.otex" },
    { "kind", "texture" },
    { "json_pointer", "/textures/0" },
    { "required", true },
  };
}

auto File() -> json
{
  return json {
    { "path", "Brick.png" },
    { "required", false },
  };
}

auto Read() -> json
{
  return json {
    { "offset", 0 },
    { "max_bytes", 4096 },
    { "sha256", std::string(64, 'a') },
  };
}

auto Metadata() -> json
{
  return json {
    { "size", 12 },
    { "is_directory", false },
    { "is_symlink", false },
    { "last_modified_seconds", 1700000000 },
    { "last_modified_nanoseconds", 500 },
  };
}

auto Observation() -> json
{
  return json {
    { "path", "Brick.png" },
    { "exists", true },
    { "metadata", Metadata() },
    { "reads", json::array({ Read() }) },
  };
}

auto ModelScene() -> json
{
  return json {
    { "index", 0 },
    { "name", "Main" },
    { "selector", "Main" },
    { "is_default", true },
    { "root_count", 1 },
    { "node_count", 3 },
  };
}

auto Model() -> json
{
  return json {
    { "format", "gltf" },
    { "format_version", "2.0" },
    { "unit_meters", 1.0 },
    { "right", "+X" },
    { "up", "+Y" },
    { "front", "+Z" },
    { "left_handed", false },
    { "node_count", 3 },
    { "mesh_count", 1 },
    { "scenes", json::array({ ModelScene() }) },
    { "materials", json::array({ "Paint" }) },
    { "textures", json::array({ "Brick" }) },
  };
}

auto Job() -> json
{
  return json {
    { "id", "model" },
    { "job_type", "gltf" },
    { "source_path", "Model.gltf" },
    { "complete", true },
    { "outputs", json::array({ Logical() }) },
    { "references", json::array({ Logical() }) },
    { "files", json::array({ File() }) },
    { "observations", json::array({ Observation() }) },
    { "diagnostics", json::array({ Diagnostic() }) },
    { "accessed_paths", json::array({ "Model.gltf" }) },
    { "model", Model() },
  };
}

auto Document(json jobs, json diagnostics) -> json
{
  return json {
    { "version", Owned("/properties/version/const") },
    { "producer_version", "1.0" },
    { "complete", true },
    { "jobs", std::move(jobs) },
    { "diagnostics", std::move(diagnostics) },
  };
}

auto Valid() -> json { return Document(json::array(), json::array()); }

auto Maximal() -> json
{
  return Document(json::array({ Job() }), json::array({ Diagnostic() }));
}

//! A test-name fragment: words split at `_` and `-`, a leading sign spelled
//! out.
auto CamelCase(std::string_view snake) -> std::string
{
  auto out = std::string {};
  if (snake.starts_with('+') || snake.starts_with('-')) {
    out = snake.front() == '+' ? "Plus" : "Minus";
    snake.remove_prefix(1);
  }
  auto upper = true;
  for (const auto c : snake) {
    if (c == '_' || c == '-') {
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

//! Keeps generated case names and messages alive for the whole run.
auto Keep(std::string text) -> std::string_view
{
  static auto storage = std::deque<std::string> {};
  return storage.emplace_back(std::move(text));
}

//! An object of the schema and where the maximal document holds one.
struct Object {
  std::string_view definition; //!< Empty for the document itself.
  std::string_view pointer;
};

constexpr auto kObjects = std::array {
  Object { .definition = "", .pointer = "" },
  Object { .definition = "job", .pointer = "/jobs/0" },
  Object { .definition = "logical", .pointer = "/jobs/0/outputs/0" },
  Object { .definition = "file", .pointer = "/jobs/0/files/0" },
  Object { .definition = "observation", .pointer = "/jobs/0/observations/0" },
  Object { .definition = "read", .pointer = "/jobs/0/observations/0/reads/0" },
  Object {
    .definition = "metadata",
    .pointer = "/jobs/0/observations/0/metadata",
  },
  Object { .definition = "diagnostic", .pointer = "/diagnostics/0" },
  Object { .definition = "model", .pointer = "/jobs/0/model" },
  Object { .definition = "model_scene", .pointer = "/jobs/0/model/scenes/0" },
};

//! A key every object of the schema requires, and none may add to.
auto ObjectCases(std::vector<SchemaCase>& cases) -> void
{
  for (const auto& object : kObjects) {
    const auto required = object.definition.empty()
      ? Owned("/required")
      : Owned(Definition(object.definition) + "/required");
    const auto label = CamelCase(object.definition);
    for (const auto& key : required) {
      const auto name = key.get<std::string>();
      cases.push_back(Rejects(Keep("RejectsMissing" + label + CamelCase(name)),
        Without(Maximal(), std::string { object.pointer } + "/" + name),
        Keep("'" + name + "'")));
    }
    cases.push_back(Rejects(Keep("RejectsUnknown" + label + "Field"),
      With(Maximal(), std::string { object.pointer } + "/unknown", true),
      "'unknown'"));
  }
}

//! Every member of an enumeration is accepted and a stranger is refused.
auto EnumCases(std::vector<SchemaCase>& cases, const std::string_view label,
  const std::string& enum_pointer, const std::string_view document_pointer)
  -> void
{
  for (const auto& member : Owned(enum_pointer)) {
    cases.push_back(Accepts(Keep("Accepts" + std::string { label }
                              + CamelCase(member.get<std::string>())),
      With(Maximal(), document_pointer, member)));
  }
  cases.push_back(Rejects(Keep("Rejects" + std::string { label } + "Stranger"),
    With(Maximal(), document_pointer, "bogus"),
    Keep(std::string { document_pointer } + ": ")));
}

auto Cases() -> std::vector<SchemaCase>
{
  auto cases = std::vector<SchemaCase> {
    Accepts("AcceptsMinimalDocument", Valid()),
    Accepts("AcceptsMaximalDocument", Maximal()),
    Accepts("AcceptsIncompleteDocument", With(Valid(), "/complete", false)),
    Accepts("AcceptsDiagnosticWithoutObjectName",
      Without(Maximal(), "/diagnostics/0/object_name")),
    Accepts("AcceptsJobWithoutModel", Without(Maximal(), "/jobs/0/model")),
    Accepts("AcceptsNullObservationMetadata",
      With(Maximal(), "/jobs/0/observations/0/metadata", nullptr)),
    Accepts("AcceptsUnknownSeverity",
      With(Maximal(), "/diagnostics/0/severity", "Unknown")),
    Accepts("AcceptsPreEpochModificationTime",
      With(Maximal(), "/jobs/0/observations/0/metadata/last_modified_seconds",
        -1)),
    Accepts("AcceptsModelWithUnstatedUnit",
      With(Maximal(), "/jobs/0/model/unit_meters", nullptr)),
    Accepts("AcceptsModelWithUnstatedVersion",
      With(Maximal(), "/jobs/0/model/format_version", nullptr)),
    Accepts("AcceptsModelWithInvalidBasis",
      With(Maximal(), "/jobs/0/model/left_handed", nullptr)),
    Accepts("AcceptsModelWithoutScenes",
      With(Maximal(), "/jobs/0/model/scenes", json::array())),
    Accepts("AcceptsMaximumNanoseconds",
      With(Maximal(),
        "/jobs/0/observations/0/metadata/last_modified_nanoseconds",
        Owned(Property("metadata", "last_modified_nanoseconds") + "/maximum"))),

    Rejects("RejectsOtherVersion",
      With(Maximal(), "/version",
        Owned("/properties/version/const").get<std::int64_t>() + 1),
      "/version: "),
    Rejects("RejectsNonStringProducerVersion",
      With(Maximal(), "/producer_version", 3), "/producer_version: "),
    Rejects("RejectsNonBooleanComplete", With(Maximal(), "/complete", "yes"),
      "/complete: "),
    Rejects("RejectsNonArrayJobs", With(Maximal(), "/jobs", "x"), "/jobs: "),
    Rejects("RejectsNonArrayDiagnostics", With(Maximal(), "/diagnostics", "x"),
      "/diagnostics: "),
    Rejects("RejectsNonStringJobId", With(Maximal(), "/jobs/0/id", 1),
      "/jobs/0/id: "),
    Rejects("RejectsNonStringJobType", With(Maximal(), "/jobs/0/job_type", 1),
      "/jobs/0/job_type: "),
    Rejects("RejectsNonStringSourcePath",
      With(Maximal(), "/jobs/0/source_path", 1), "/jobs/0/source_path: "),
    Rejects("RejectsNonBooleanJobComplete",
      With(Maximal(), "/jobs/0/complete", 1), "/jobs/0/complete: "),
    Rejects("RejectsNonArrayOutputs", With(Maximal(), "/jobs/0/outputs", "x"),
      "/jobs/0/outputs: "),
    Rejects("RejectsNonArrayReferences",
      With(Maximal(), "/jobs/0/references", "x"), "/jobs/0/references: "),
    Rejects("RejectsNonArrayFiles", With(Maximal(), "/jobs/0/files", "x"),
      "/jobs/0/files: "),
    Rejects("RejectsNonArrayObservations",
      With(Maximal(), "/jobs/0/observations", "x"), "/jobs/0/observations: "),
    Rejects("RejectsNonStringAccessedPath",
      With(Maximal(), "/jobs/0/accessed_paths", json::array({ 1 })),
      "/jobs/0/accessed_paths/0: "),
    Rejects("RejectsNonObjectModel", With(Maximal(), "/jobs/0/model", "x"),
      "/jobs/0/model: "),

    Rejects("RejectsNonStringWritten",
      With(Maximal(), "/jobs/0/outputs/0/written", 1),
      "/jobs/0/outputs/0/written: "),
    Rejects("RejectsNonStringLogicalVirtualPath",
      With(Maximal(), "/jobs/0/outputs/0/virtual_path", 1),
      "/jobs/0/outputs/0/virtual_path: "),
    Rejects("RejectsNonStringLogicalPointer",
      With(Maximal(), "/jobs/0/outputs/0/json_pointer", 1),
      "/jobs/0/outputs/0/json_pointer: "),
    Rejects("RejectsNonBooleanLogicalRequired",
      With(Maximal(), "/jobs/0/outputs/0/required", "yes"),
      "/jobs/0/outputs/0/required: "),
    Rejects("RejectsNonStringFilePath",
      With(Maximal(), "/jobs/0/files/0/path", 1), "/jobs/0/files/0/path: "),
    Rejects("RejectsNonBooleanFileRequired",
      With(Maximal(), "/jobs/0/files/0/required", "yes"),
      "/jobs/0/files/0/required: "),

    Rejects("RejectsNonBooleanObservationExists",
      With(Maximal(), "/jobs/0/observations/0/exists", "yes"),
      "/jobs/0/observations/0/exists: "),
    Rejects("RejectsNonObjectObservationMetadata",
      With(Maximal(), "/jobs/0/observations/0/metadata", 7),
      "/jobs/0/observations/0/metadata"),
    Rejects("RejectsNegativeMetadataSize",
      With(Maximal(), "/jobs/0/observations/0/metadata/size", -1),
      "/jobs/0/observations/0/metadata"),
    Rejects("RejectsNonBooleanIsDirectory",
      With(Maximal(), "/jobs/0/observations/0/metadata/is_directory", 0),
      "/jobs/0/observations/0/metadata"),
    Rejects("RejectsFractionalModificationSeconds",
      With(Maximal(), "/jobs/0/observations/0/metadata/last_modified_seconds",
        1.5),
      "/jobs/0/observations/0/metadata"),
    Rejects("RejectsNegativeModificationNanoseconds",
      With(Maximal(),
        "/jobs/0/observations/0/metadata/last_modified_nanoseconds", -1),
      "/jobs/0/observations/0/metadata"),
    Rejects("RejectsModificationNanosecondsAboveMaximum",
      With(Maximal(),
        "/jobs/0/observations/0/metadata/last_modified_nanoseconds",
        Owned(Property("metadata", "last_modified_nanoseconds") + "/maximum")
            .get<std::int64_t>()
          + 1),
      "/jobs/0/observations/0/metadata"),
    Rejects("RejectsNonArrayReads",
      With(Maximal(), "/jobs/0/observations/0/reads", "x"),
      "/jobs/0/observations/0/reads: "),
    Rejects("RejectsNegativeReadOffset",
      With(Maximal(), "/jobs/0/observations/0/reads/0/offset", -1),
      "/jobs/0/observations/0/reads/0/offset: "),
    Rejects("RejectsNegativeReadMaxBytes",
      With(Maximal(), "/jobs/0/observations/0/reads/0/max_bytes", -1),
      "/jobs/0/observations/0/reads/0/max_bytes: "),
    Rejects("RejectsShortReadDigest",
      With(Maximal(), "/jobs/0/observations/0/reads/0/sha256", "abc"),
      "/jobs/0/observations/0/reads/0/sha256: "),
    Rejects("RejectsUppercaseReadDigest",
      With(Maximal(), "/jobs/0/observations/0/reads/0/sha256",
        std::string(64, 'A')),
      "/jobs/0/observations/0/reads/0/sha256: "),

    Rejects("RejectsLowercaseSeverity",
      With(Maximal(), "/diagnostics/0/severity", "warning"),
      "/diagnostics/0/severity: "),
    Rejects("RejectsNonStringDiagnosticCode",
      With(Maximal(), "/diagnostics/0/code", 1), "/diagnostics/0/code: "),
    Rejects("RejectsNonStringDiagnosticMessage",
      With(Maximal(), "/diagnostics/0/message", 1), "/diagnostics/0/message: "),
    Rejects("RejectsNonStringDiagnosticSourcePath",
      With(Maximal(), "/diagnostics/0/source_path", 1),
      "/diagnostics/0/source_path: "),
    Rejects("RejectsNonStringObjectPath",
      With(Maximal(), "/diagnostics/0/object_path", 1),
      "/diagnostics/0/object_path: "),
    Rejects("RejectsNonStringDiagnosticPointer",
      With(Maximal(), "/diagnostics/0/json_pointer", 1),
      "/diagnostics/0/json_pointer: "),
    Rejects("RejectsNonStringObjectName",
      With(Maximal(), "/diagnostics/0/object_name", 1),
      "/diagnostics/0/object_name: "),

    Rejects("RejectsNonStringFormatVersion",
      With(Maximal(), "/jobs/0/model/format_version", 2),
      "/jobs/0/model/format_version: "),
    Rejects("RejectsZeroUnitMeters",
      With(Maximal(), "/jobs/0/model/unit_meters", 0),
      "/jobs/0/model/unit_meters"),
    Rejects("RejectsNegativeUnitMeters",
      With(Maximal(), "/jobs/0/model/unit_meters", -0.01),
      "/jobs/0/model/unit_meters"),
    Rejects("RejectsNonNumberUnitMeters",
      With(Maximal(), "/jobs/0/model/unit_meters", "1"),
      "/jobs/0/model/unit_meters"),
    Rejects("RejectsNonBooleanLeftHanded",
      With(Maximal(), "/jobs/0/model/left_handed", "no"),
      "/jobs/0/model/left_handed: "),
    Rejects("RejectsNegativeModelNodeCount",
      With(Maximal(), "/jobs/0/model/node_count", -1),
      "/jobs/0/model/node_count: "),
    Rejects("RejectsNegativeMeshCount",
      With(Maximal(), "/jobs/0/model/mesh_count", -1),
      "/jobs/0/model/mesh_count: "),
    Rejects("RejectsNonArrayScenes",
      With(Maximal(), "/jobs/0/model/scenes", "x"), "/jobs/0/model/scenes: "),
    Rejects("RejectsNonStringMaterial",
      With(Maximal(), "/jobs/0/model/materials", json::array({ 1 })),
      "/jobs/0/model/materials/0: "),
    Rejects("RejectsNonStringTexture",
      With(Maximal(), "/jobs/0/model/textures", json::array({ 1 })),
      "/jobs/0/model/textures/0: "),

    Rejects("RejectsNegativeSceneIndex",
      With(Maximal(), "/jobs/0/model/scenes/0/index", -1),
      "/jobs/0/model/scenes/0/index: "),
    Rejects("RejectsNonStringSceneName",
      With(Maximal(), "/jobs/0/model/scenes/0/name", 1),
      "/jobs/0/model/scenes/0/name: "),
    Rejects("RejectsEmptySceneSelector",
      With(Maximal(), "/jobs/0/model/scenes/0/selector", ""),
      "/jobs/0/model/scenes/0/selector: "),
    Rejects("RejectsNonBooleanSceneIsDefault",
      With(Maximal(), "/jobs/0/model/scenes/0/is_default", "yes"),
      "/jobs/0/model/scenes/0/is_default: "),
    Rejects("RejectsNegativeSceneRootCount",
      With(Maximal(), "/jobs/0/model/scenes/0/root_count", -1),
      "/jobs/0/model/scenes/0/root_count: "),
    Rejects("RejectsNegativeSceneNodeCount",
      With(Maximal(), "/jobs/0/model/scenes/0/node_count", -1),
      "/jobs/0/model/scenes/0/node_count: "),
  };

  ObjectCases(cases);
  EnumCases(cases, "LogicalKind", Property("logical", "kind") + "/enum",
    "/jobs/0/outputs/0/kind");
  EnumCases(cases, "ModelFormat", Property("model", "format") + "/enum",
    "/jobs/0/model/format");
  EnumCases(
    cases, "ModelRight", Definition("axis") + "/enum", "/jobs/0/model/right");
  EnumCases(cases, "ModelUp", Definition("axis") + "/enum", "/jobs/0/model/up");
  EnumCases(
    cases, "ModelFront", Definition("axis") + "/enum", "/jobs/0/model/front");
  return cases;
}

class SourceAnalysisSchemaTest : public ::testing::TestWithParam<SchemaCase> {
};

NOLINT_TEST_P(SourceAnalysisSchemaTest, ValidatesDocument)
{
  oxygen::cooker::test::ExpectSchemaCase(Schema(), GetParam());
}

INSTANTIATE_TEST_SUITE_P(Cases, SourceAnalysisSchemaTest,
  ::testing::ValuesIn(Cases()), oxygen::cooker::test::SchemaCaseName);

} // namespace
