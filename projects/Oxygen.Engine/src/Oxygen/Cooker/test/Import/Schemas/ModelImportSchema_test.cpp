//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Schemas/oxygen.model-import.schema.json

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
    "Import/Schemas/oxygen.model-import.schema.json");
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

//! A schema constant, read from the schema.
auto Owned(const std::string_view pointer) -> const json&
{
  return Schema().at(json::json_pointer { std::string { pointer } });
}

//! The version of the surface provenance this record embeds.
auto ProvenanceVersion() -> json
{
  static const auto schema = oxygen::cooker::test::LoadSchema(
    "Import/Schemas/oxygen.surface-provenance.schema.json");
  return schema.at(json::json_pointer { "/properties/schema_version/const" });
}

constexpr auto kIdentity = "0190a1b2-c3d4-7e5f-8a6b-7c8d9e0f1a2b";
constexpr auto kNodeId = "0190a1b2-c3d4-7e5f-8a6b-7c8d9e0f1a2c";
constexpr auto kNilKey = "00000000-0000-0000-0000-000000000000";

auto Valid() -> json
{
  return json {
    { "schema_version", Owned("/properties/schema_version/const") },
    { "source", "Model.gltf" },
    { "settings", json::object() },
    { "source_identity", kIdentity },
  };
}

auto Node() -> json
{
  return json {
    { "anchor", "Root/Body" },
    { "node_id", kNodeId },
  };
}

auto Maximal() -> json
{
  auto document = Valid();
  document["settings"] = json {
    { "gltf_scene", "Main" },
    { "generate_lods", true },
    { "lod_levels", 3 },
  };
  document["surface_provenance"] = json {
    { "schema_version", ProvenanceVersion() },
    { "geometries", json::array() },
  };
  document["node_continuity"] = json::array({
    json {
      { "anchor", "Root/Body" },
      { "continues", "Root/Old" },
    },
  });
  document["published_nodes"] = json::array({ Node() });
  document["new_nodes"] = json::array({ Node() });
  document["vanished_nodes"] = json::array({ Node() });
  document["source_fingerprint"] = json {
    { "size", 12 },
    { "sha256", std::string(64, 'a') },
    {
      "files",
      json::array({
        json {
          { "path", "Model.bin" },
          { "size", 4 },
          { "sha256", std::string(64, 'b') },
        },
      }),
    },
  };
  return document;
}

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

//! Keeps generated case names alive for the whole run.
auto Keep(std::string text) -> std::string_view
{
  static auto storage = std::deque<std::string> {};
  return storage.emplace_back(std::move(text));
}

auto Cases() -> std::vector<SchemaCase>
{
  auto cases = std::vector<SchemaCase> {
    Accepts("AcceptsMinimalRecord", Valid()),
    Accepts("AcceptsMaximalRecord", Maximal()),
    Accepts("AcceptsAbsoluteSource",
      With(Valid(), "/source", "/Content/Models/Model.gltf")),
    Accepts("AcceptsEmptyNodeLists",
      With(With(With(Valid(), "/published_nodes", json::array()), "/new_nodes",
             json::array()),
        "/vanished_nodes", json::array())),

    Rejects("RejectsMissingSchemaVersion", Without(Valid(), "/schema_version"),
      "'schema_version'"),
    Rejects("RejectsMissingSource", Without(Valid(), "/source"), "'source'"),
    Rejects(
      "RejectsMissingSettings", Without(Valid(), "/settings"), "'settings'"),
    Rejects("RejectsMissingSourceIdentity",
      Without(Maximal(), "/source_identity"), "'source_identity'"),
    Rejects("RejectsMissingNodeContinuityAnchor",
      Without(Maximal(), "/node_continuity/0/anchor"), "'anchor'"),
    Rejects("RejectsMissingNodeContinuityTarget",
      Without(Maximal(), "/node_continuity/0/continues"), "'continues'"),
    Rejects("RejectsMissingPublishedNodeAnchor",
      Without(Maximal(), "/published_nodes/0/anchor"), "'anchor'"),
    Rejects("RejectsMissingPublishedNodeId",
      Without(Maximal(), "/published_nodes/0/node_id"), "'node_id'"),
    Rejects("RejectsMissingNewNodeId",
      Without(Maximal(), "/new_nodes/0/node_id"), "'node_id'"),
    Rejects("RejectsMissingVanishedNodeId",
      Without(Maximal(), "/vanished_nodes/0/node_id"), "'node_id'"),
    Rejects("RejectsMissingFingerprintSize",
      Without(Maximal(), "/source_fingerprint/size"), "'size'"),
    Rejects("RejectsMissingFingerprintDigest",
      Without(Maximal(), "/source_fingerprint/sha256"), "'sha256'"),
    Rejects("RejectsMissingFingerprintFiles",
      Without(Maximal(), "/source_fingerprint/files"), "'files'"),
    Rejects("RejectsMissingFingerprintFilePath",
      Without(Maximal(), "/source_fingerprint/files/0/path"), "'path'"),
    Rejects("RejectsMissingFingerprintFileSize",
      Without(Maximal(), "/source_fingerprint/files/0/size"), "'size'"),
    Rejects("RejectsMissingFingerprintFileDigest",
      Without(Maximal(), "/source_fingerprint/files/0/sha256"), "'sha256'"),

    Rejects("RejectsUnknownTopLevelField", With(Valid(), "/unknown", true),
      "'unknown'"),
    Rejects("RejectsUnknownNodeContinuityField",
      With(Maximal(), "/node_continuity/0/unknown", true), "'unknown'"),
    Rejects("RejectsUnknownPublishedNodeField",
      With(Maximal(), "/published_nodes/0/unknown", true), "'unknown'"),
    Rejects("RejectsUnknownFingerprintField",
      With(Maximal(), "/source_fingerprint/unknown", true), "'unknown'"),
    Rejects("RejectsUnknownFingerprintFileField",
      With(Maximal(), "/source_fingerprint/files/0/unknown", true),
      "'unknown'"),

    Rejects("RejectsOtherSchemaVersion",
      With(Valid(), "/schema_version",
        Owned("/properties/schema_version/const").get<std::int64_t>() + 1),
      "/schema_version: "),
    Rejects("RejectsNonStringSource", With(Valid(), "/source", 7), "/source: "),
    Rejects("RejectsEmptySource", With(Valid(), "/source", ""), "/source: "),
    Rejects("RejectsSourceThatIsNotAReference",
      With(Valid(), "/source", "Sub/../Model.gltf"), "/source: "),
    Rejects("RejectsSourceWithoutExtension", With(Valid(), "/source", "Model"),
      "/source: "),

    Rejects(
      "RejectsNonObjectSettings", With(Valid(), "/settings", "x"), "/settings"),
    Rejects("RejectsUnknownSetting",
      With(Valid(), "/settings", json { { "unknown_setting", true } }),
      "/settings"),
    Rejects("RejectsWrongTypeSetting",
      With(Valid(), "/settings", json { { "generate_lods", "yes" } }),
      "/settings/generate_lods: "),
    Rejects("RejectsEmptyGltfScene",
      With(Valid(), "/settings", json { { "gltf_scene", "" } }),
      "/settings/gltf_scene: "),

    Rejects("RejectsUuidOfAnotherVersion",
      With(Valid(), "/source_identity", "0190a1b2-c3d4-4e5f-8a6b-7c8d9e0f1a2b"),
      "/source_identity: "),
    Rejects("RejectsUuidOfAnotherVariant",
      With(Valid(), "/source_identity", "0190a1b2-c3d4-7e5f-ca6b-7c8d9e0f1a2b"),
      "/source_identity: "),
    Rejects("RejectsUppercaseUuid",
      With(Valid(), "/source_identity", "0190A1B2-C3D4-7E5F-8A6B-7C8D9E0F1A2B"),
      "/source_identity: "),
    Rejects("RejectsNonStringSourceIdentity",
      With(Valid(), "/source_identity", 7), "/source_identity: "),

    Rejects("RejectsInvalidSurfaceProvenance",
      With(Maximal(), "/surface_provenance/schema_version",
        ProvenanceVersion().get<std::int64_t>() + 1),
      "/surface_provenance/schema_version: "),
    Rejects("RejectsSurfaceProvenanceWithoutGeometries",
      Without(Maximal(), "/surface_provenance/geometries"), "'geometries'"),

    Rejects("RejectsNonArrayNodeContinuity",
      With(Maximal(), "/node_continuity", "x"), "/node_continuity: "),
    Rejects("RejectsEmptyContinuityAnchor",
      With(Maximal(), "/node_continuity/0/anchor", ""),
      "/node_continuity/0/anchor: "),
    Rejects("RejectsEmptyContinuityTarget",
      With(Maximal(), "/node_continuity/0/continues", ""),
      "/node_continuity/0/continues: "),
    Rejects("RejectsNonArrayPublishedNodes",
      With(Maximal(), "/published_nodes", "x"), "/published_nodes: "),
    Rejects("RejectsEmptyPublishedNodeAnchor",
      With(Maximal(), "/published_nodes/0/anchor", ""),
      "/published_nodes/0/anchor: "),
    Rejects("RejectsMalformedPublishedNodeId",
      With(Maximal(), "/published_nodes/0/node_id", "x"),
      "/published_nodes/0/node_id: "),
    Rejects("RejectsNilPublishedNodeId",
      With(Maximal(), "/published_nodes/0/node_id", kNilKey),
      "/published_nodes/0/node_id: "),
    Rejects("RejectsNilNewNodeId",
      With(Maximal(), "/new_nodes/0/node_id", kNilKey),
      "/new_nodes/0/node_id: "),
    Rejects("RejectsNilVanishedNodeId",
      With(Maximal(), "/vanished_nodes/0/node_id", kNilKey),
      "/vanished_nodes/0/node_id: "),

    Rejects("RejectsNegativeFingerprintSize",
      With(Maximal(), "/source_fingerprint/size", -1),
      "/source_fingerprint/size: "),
    Rejects("RejectsMalformedFingerprintDigest",
      With(Maximal(), "/source_fingerprint/sha256", "abc"),
      "/source_fingerprint/sha256: "),
    Rejects("RejectsUppercaseFingerprintDigest",
      With(Maximal(), "/source_fingerprint/sha256", std::string(64, 'A')),
      "/source_fingerprint/sha256: "),
    Rejects("RejectsNonArrayFingerprintFiles",
      With(Maximal(), "/source_fingerprint/files", "x"),
      "/source_fingerprint/files: "),
    Rejects("RejectsEmptyFingerprintFilePath",
      With(Maximal(), "/source_fingerprint/files/0/path", ""),
      "/source_fingerprint/files/0/path: "),
    Rejects("RejectsNegativeFingerprintFileSize",
      With(Maximal(), "/source_fingerprint/files/0/size", -1),
      "/source_fingerprint/files/0/size: "),
    Rejects("RejectsMalformedFingerprintFileDigest",
      With(Maximal(), "/source_fingerprint/files/0/sha256", "abc"),
      "/source_fingerprint/files/0/sha256: "),
  };

  // The keys the record and its publisher own are refused in the settings.
  for (const auto& rule : Owned("/properties/settings/allOf/2/not/anyOf")) {
    const auto key = rule.at("required").at(0).get<std::string>();
    cases.push_back(Rejects(Keep("RejectsSettingsKey" + CamelCase(key)),
      With(Valid(), "/settings", json { { key, "x" } }), "/settings"));
  }
  return cases;
}

class ModelImportSchemaTest : public ::testing::TestWithParam<SchemaCase> { };

NOLINT_TEST_P(ModelImportSchemaTest, ValidatesDocument)
{
  oxygen::cooker::test::ExpectSchemaCase(Schema(), GetParam());
}

INSTANTIATE_TEST_SUITE_P(Cases, ModelImportSchemaTest,
  ::testing::ValuesIn(Cases()), oxygen::cooker::test::SchemaCaseName);

} // namespace
