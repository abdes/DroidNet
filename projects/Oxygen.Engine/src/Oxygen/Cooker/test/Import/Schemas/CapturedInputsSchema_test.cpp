//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Schemas/oxygen.captured-inputs.schema.json

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/test/Support/JsonSchema.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using nlohmann::json;
using oxygen::cooker::test::SchemaCase;

auto Schema() -> const json&
{
  static const auto schema = oxygen::cooker::test::LoadSchema(
    "Import/Schemas/oxygen.captured-inputs.schema.json");
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

//! A schema constant or limit, read from the schema.
auto Owned(const std::string_view pointer) -> const json&
{
  return Schema().at(json::json_pointer { std::string { pointer } });
}

constexpr auto kMetadata
  = "/properties/inputs/items/properties/metadata/oneOf/1/properties/";

auto Limit(const std::string_view property, const std::string_view bound)
  -> std::int64_t
{
  return Owned(std::string { kMetadata } + std::string { property } + "/"
    + std::string { bound })
    .get<std::int64_t>();
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

auto File() -> json
{
  return json {
    { "path", "inputs/Brick.png" },
    { "size", 12 },
    { "sha256", std::string(64, 'a') },
  };
}

auto Input() -> json
{
  return json {
    { "logical_path", "/Content/Brick.png" },
    { "exists", true },
    { "metadata", Metadata() },
    { "file", File() },
  };
}

auto Valid() -> json
{
  return json {
    { "schema_version", Owned("/properties/schema_version/const") },
    { "inputs", json::array({ Input() }) },
  };
}

auto Absent() -> json
{
  auto input = Input();
  input["exists"] = false;
  input["metadata"] = nullptr;
  input["file"] = nullptr;
  return With(Valid(), "/inputs", json::array({ std::move(input) }));
}

class CapturedInputsSchemaTest : public ::testing::TestWithParam<SchemaCase> {
};

NOLINT_TEST_P(CapturedInputsSchemaTest, ValidatesDocument)
{
  oxygen::cooker::test::ExpectSchemaCase(Schema(), GetParam());
}

constexpr auto kMeta = "/inputs/0/metadata";
constexpr auto kFile = "/inputs/0/file";

INSTANTIATE_TEST_SUITE_P(Cases, CapturedInputsSchemaTest,
  ::testing::Values(
    Accepts("AcceptsNoInputs", With(Valid(), "/inputs", json::array())),
    Accepts("AcceptsCapturedInput", Valid()),
    Accepts("AcceptsAbsentInput", Absent()),
    Accepts(
      "AcceptsMetadataWithoutFile", With(Valid(), "/inputs/0/file", nullptr)),
    Accepts("AcceptsEarliestModificationTime",
      With(Valid(), "/inputs/0/metadata/last_modified_seconds",
        Limit("last_modified_seconds", "minimum"))),
    Accepts("AcceptsLatestModificationTime",
      With(With(Valid(), "/inputs/0/metadata/last_modified_seconds",
             Limit("last_modified_seconds", "maximum")),
        "/inputs/0/metadata/last_modified_nanoseconds",
        Limit("last_modified_nanoseconds", "maximum"))),

    Rejects("RejectsMissingSchemaVersion", Without(Valid(), "/schema_version"),
      "'schema_version'"),
    Rejects("RejectsMissingInputs", Without(Valid(), "/inputs"), "'inputs'"),
    Rejects("RejectsMissingLogicalPath",
      Without(Valid(), "/inputs/0/logical_path"), "'logical_path'"),
    Rejects(
      "RejectsMissingExists", Without(Valid(), "/inputs/0/exists"), "'exists'"),
    Rejects("RejectsMissingMetadata", Without(Valid(), "/inputs/0/metadata"),
      "'metadata'"),
    Rejects("RejectsMissingFile", Without(Valid(), "/inputs/0/file"), "'file'"),
    Rejects("RejectsMissingMetadataSize",
      Without(Valid(), "/inputs/0/metadata/size"), "'size'"),
    Rejects("RejectsMissingMetadataIsDirectory",
      Without(Valid(), "/inputs/0/metadata/is_directory"), "'is_directory'"),
    Rejects("RejectsMissingMetadataIsSymlink",
      Without(Valid(), "/inputs/0/metadata/is_symlink"), "'is_symlink'"),
    Rejects("RejectsMissingModificationSeconds",
      Without(Valid(), "/inputs/0/metadata/last_modified_seconds"),
      "'last_modified_seconds'"),
    Rejects("RejectsMissingModificationNanoseconds",
      Without(Valid(), "/inputs/0/metadata/last_modified_nanoseconds"),
      "'last_modified_nanoseconds'"),
    Rejects("RejectsMissingFilePath", Without(Valid(), "/inputs/0/file/path"),
      "'path'"),
    Rejects("RejectsMissingFileSize", Without(Valid(), "/inputs/0/file/size"),
      "'size'"),
    Rejects("RejectsMissingFileDigest",
      Without(Valid(), "/inputs/0/file/sha256"), "'sha256'"),

    Rejects("RejectsUnknownTopLevelField", With(Valid(), "/unknown", true),
      "'unknown'"),
    Rejects("RejectsUnknownInputField",
      With(Valid(), "/inputs/0/unknown", true), "'unknown'"),
    Rejects("RejectsUnknownMetadataField",
      With(Valid(), "/inputs/0/metadata/unknown", true), kMeta),
    Rejects("RejectsUnknownFileField",
      With(Valid(), "/inputs/0/file/unknown", true), kFile),

    Rejects("RejectsOtherSchemaVersion",
      With(Valid(), "/schema_version",
        Owned("/properties/schema_version/const").get<std::int64_t>() + 1),
      "/schema_version: "),
    Rejects(
      "RejectsNonArrayInputs", With(Valid(), "/inputs", "x"), "/inputs: "),
    Rejects("RejectsNonStringLogicalPath",
      With(Valid(), "/inputs/0/logical_path", 7), "/inputs/0/logical_path: "),
    Rejects("RejectsEmptyLogicalPath",
      With(Valid(), "/inputs/0/logical_path", ""), "/inputs/0/logical_path: "),
    Rejects("RejectsNonBooleanExists", With(Valid(), "/inputs/0/exists", "yes"),
      "/inputs/0/exists: "),
    Rejects("RejectsNonObjectMetadata", With(Valid(), "/inputs/0/metadata", 7),
      kMeta),
    Rejects(
      "RejectsNonObjectFile", With(Valid(), "/inputs/0/file", "x"), kFile),
    Rejects("RejectsNegativeMetadataSize",
      With(Valid(), "/inputs/0/metadata/size", -1), kMeta),
    Rejects("RejectsNonBooleanIsDirectory",
      With(Valid(), "/inputs/0/metadata/is_directory", 0), kMeta),
    Rejects("RejectsNonBooleanIsSymlink",
      With(Valid(), "/inputs/0/metadata/is_symlink", "no"), kMeta),
    Rejects("RejectsFractionalModificationSeconds",
      With(Valid(), "/inputs/0/metadata/last_modified_seconds", 1.5), kMeta),
    Rejects("RejectsModificationSecondsBelowMinimum",
      With(Valid(), "/inputs/0/metadata/last_modified_seconds",
        Limit("last_modified_seconds", "minimum") - 1),
      kMeta),
    Rejects("RejectsModificationSecondsAboveMaximum",
      With(Valid(), "/inputs/0/metadata/last_modified_seconds",
        Limit("last_modified_seconds", "maximum") + 1),
      kMeta),
    Rejects("RejectsNegativeModificationNanoseconds",
      With(Valid(), "/inputs/0/metadata/last_modified_nanoseconds", -1), kMeta),
    Rejects("RejectsModificationNanosecondsAboveMaximum",
      With(Valid(), "/inputs/0/metadata/last_modified_nanoseconds",
        Limit("last_modified_nanoseconds", "maximum") + 1),
      kMeta),
    Rejects(
      "RejectsEmptyFilePath", With(Valid(), "/inputs/0/file/path", ""), kFile),
    Rejects("RejectsNegativeFileSize", With(Valid(), "/inputs/0/file/size", -1),
      kFile),
    Rejects("RejectsUppercaseDigest",
      With(Valid(), "/inputs/0/file/sha256", std::string(64, 'A')), kFile),
    Rejects("RejectsShortDigest",
      With(Valid(), "/inputs/0/file/sha256", std::string(63, 'a')), kFile),
    Rejects("RejectsLongDigest",
      With(Valid(), "/inputs/0/file/sha256", std::string(65, 'a')), kFile),
    Rejects("RejectsNonHexDigest",
      With(Valid(), "/inputs/0/file/sha256", std::string(64, 'g')), kFile)),
  oxygen::cooker::test::SchemaCaseName);

} // namespace
