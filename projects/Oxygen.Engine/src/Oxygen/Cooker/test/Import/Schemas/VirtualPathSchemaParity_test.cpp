//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Schemas/oxygen.name.schema.json,
// Import/Schemas/oxygen.reference.schema.json,
// Import/Schemas/oxygen.mounts.schema.json

#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/test/Support/JsonSchema.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using nlohmann::json;

//! The virtual path case table, the contract of the C++ and managed rules.
auto Cases() -> const json&
{
  static const auto cases
    = oxygen::cooker::test::LoadJson(OXYGEN_CONTENT_VIRTUAL_PATH_CASES);
  return cases;
}

//! A schema whose instance is checked by the definition `reference`.
auto SchemaOf(const std::string_view reference) -> json
{
  return json {
    { "$schema", "http://json-schema.org/draft-07/schema#" },
    { "$ref", std::string(reference) },
  };
}

//! The rows of a section of the case table and the field each checks.
struct CaseColumn final {
  std::string_view section;
  std::string_view field;
};

//! Checks that `schema` accepts each row's field exactly when the virtual
//! path rules do, except where the row states that only the cooker refuses
//! it (`schema_accepts`): a rule a pattern cannot express.
auto ExpectSchemaFollowsTheRules(const json& schema, const CaseColumn& column)
  -> void
{
  const auto field = std::string(column.field);
  for (const auto& entry : Cases().at(std::string(column.section))) {
    if (!entry.contains(field)) {
      continue; // Bytes that are not UTF-8 are no JSON string.
    }
    const auto refused = !entry.at("refusal").is_null();
    const auto schema_accepts = entry.value("schema_accepts", false);
    EXPECT_FALSE(schema_accepts && !refused)
      << "schema_accepts marks only a refusal: " << entry.dump();

    const auto errors
      = oxygen::cooker::test::ValidateJson(schema, entry.at(field));

    EXPECT_EQ(errors.empty(), !refused || schema_accepts) << entry.dump();
  }
}

//! The name schema refuses every segment the segment rule refuses, but for
//! the rules the cooker checks after it.
NOLINT_TEST(VirtualPathSchemaParityTest, NameSchemaFollowsTheSegmentRule)
{
  ExpectSchemaFollowsTheRules(
    SchemaOf("oxygen.name.schema.json#/definitions/segment"),
    { .section = "segments", .field = "segment" });
}

//! The mounts schema names a mount exactly as the mount rule does.
NOLINT_TEST(VirtualPathSchemaParityTest, MountsSchemaFollowsTheMountRule)
{
  ExpectSchemaFollowsTheRules(
    SchemaOf("oxygen.mounts.schema.json#/definitions/mount_name"),
    { .section = "mounts", .field = "mount" });
}

//! The reference schema refuses every reference text the reference rule
//! refuses, but for what depends on the referring file or the cooker checks.
NOLINT_TEST(VirtualPathSchemaParityTest, ReferenceSchemaFollowsTheReferenceRule)
{
  ExpectSchemaFollowsTheRules(
    SchemaOf("oxygen.reference.schema.json#/definitions/reference"),
    { .section = "references", .field = "text" });
}

} // namespace
