//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Schemas/oxygen.reference.schema.json

#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/test/Support/JsonSchema.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using nlohmann::json;
using oxygen::cooker::test::SchemaCase;

//! A schema whose instance is one reference, checked by the shared definition.
auto Schema() -> const json&
{
  static const auto schema = json {
    { "$schema", "http://json-schema.org/draft-07/schema#" },
    { "$ref", "oxygen.reference.schema.json#/definitions/reference" },
  };
  return schema;
}

class ReferenceSchemaTest : public ::testing::TestWithParam<SchemaCase> { };

NOLINT_TEST_P(ReferenceSchemaTest, ValidatesReference)
{
  oxygen::cooker::test::ExpectSchemaCase(Schema(), GetParam());
}

constexpr auto kPatternError = "does not match regex pattern";

INSTANTIATE_TEST_SUITE_P(Cases, ReferenceSchemaTest,
  ::testing::Values(
    SchemaCase { "AcceptsSibling", R"("Brick.otex")", true, "" },
    SchemaCase { "AcceptsLeadingDot", R"("./Brick.otex")", true, "" },
    SchemaCase { "AcceptsClimb", R"("../../Textures/Brick.otex")", true, "" },
    SchemaCase {
      "AcceptsAbsolute", R"("/Engine/Materials/Default.omat")", true, "" },
    SchemaCase {
      "AcceptsSpaces", R"("../Sniper Rifle/Stand.fbx/Paint.omat")", true, "" },
    SchemaCase { "AcceptsPercentAsText", R"("Brick%20Wall.otex")", true, "" },
    SchemaCase { "RejectsEmpty", R"("")", false, "too short" },
    SchemaCase { "RejectsDot", R"(".")", false, kPatternError },
    SchemaCase { "RejectsOnlyClimb", R"("../..")", false, kPatternError },
    SchemaCase {
      "RejectsClimbAfterDot", R"("./../Brick.otex")", false, kPatternError },
    SchemaCase {
      "RejectsDotAfterClimb", R"(".././Brick.otex")", false, kPatternError },
    SchemaCase {
      "RejectsInnerClimb", R"("Sub/../Brick.otex")", false, kPatternError },
    SchemaCase {
      "RejectsInnerDot", R"("Sub/./Brick.otex")", false, kPatternError },
    SchemaCase { "RejectsTrailingSlash", R"("Sub/")", false, kPatternError },
    SchemaCase {
      "RejectsEmptySegment", R"("Sub//Brick.otex")", false, kPatternError },
    SchemaCase {
      "RejectsBackslash", R"("Sub\\Brick.otex")", false, kPatternError },
    SchemaCase { "RejectsAssetUri", R"("asset:///Content/Brick.otex")", false,
      kPatternError },
    SchemaCase {
      "RejectsClimbAfterRoot", R"("/../Brick.otex")", false, kPatternError },
    SchemaCase { "RejectsNonString", "7", false, "unexpected instance type" }),
  oxygen::cooker::test::SchemaCaseName);

} // namespace
