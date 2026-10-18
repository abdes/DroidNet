//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Schemas/oxygen.name.schema.json

#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/test/Support/JsonSchema.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using nlohmann::json;
using oxygen::cooker::test::SchemaCase;

//! A schema whose instance is one name, checked by the shared definition.
auto Schema() -> const json&
{
  static const auto schema = json {
    { "$schema", "http://json-schema.org/draft-07/schema#" },
    { "$ref", "oxygen.name.schema.json#/definitions/segment" },
  };
  return schema;
}

class NameSchemaTest : public ::testing::TestWithParam<SchemaCase> { };

NOLINT_TEST_P(NameSchemaTest, ValidatesName)
{
  oxygen::cooker::test::ExpectSchemaCase(Schema(), GetParam());
}

constexpr auto kPatternError = "does not match regex pattern";

INSTANTIATE_TEST_SUITE_P(Cases, NameSchemaTest,
  ::testing::Values(
    SchemaCase { "AcceptsIdentifier", R"("Ship_01")", true, "" },
    SchemaCase {
      "AcceptsSpacesDotsAndParentheses", R"j("Level 1.v2 (2)")j", true, "" },
    SchemaCase { "AcceptsNonAscii", R"("Caf\u00e9 \u00fcber")", true, "" },
    SchemaCase { "AcceptsLeadingDot", R"(".hidden")", true, "" },
    SchemaCase { "AcceptsLeadingDigitAndHyphen", R"("-1st")", true, "" },
    SchemaCase {
      "AcceptsNameLongerThan63Bytes",
      R"("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")",
      true,
      "",
    },
    SchemaCase { "AcceptsDeviceNamePrefix", R"("Console")", true, "" },
    SchemaCase { "RejectsEmpty", R"("")", false, "too short" },
    SchemaCase { "RejectsLeadingSpace", R"(" Ship")", false, kPatternError },
    SchemaCase { "RejectsTrailingSpace", R"("Ship ")", false, kPatternError },
    SchemaCase { "RejectsTrailingDot", R"("Ship.")", false, kPatternError },
    SchemaCase { "RejectsDot", R"(".")", false, kPatternError },
    SchemaCase { "RejectsDotDot", R"("..")", false, kPatternError },
    SchemaCase { "RejectsSlash", R"("a/b")", false, kPatternError },
    SchemaCase { "RejectsBackslash", R"("a\\b")", false, kPatternError },
    SchemaCase { "RejectsColon", R"("a:b")", false, kPatternError },
    SchemaCase { "RejectsWildcards", R"("a*b?")", false, kPatternError },
    SchemaCase { "RejectsQuote", R"("a\"b")", false, kPatternError },
    SchemaCase { "RejectsAngleAndPipe", R"("a<b>|c")", false, kPatternError },
    SchemaCase {
      "RejectsControlCharacter", R"("a\u0001b")", false, kPatternError },
    SchemaCase { "RejectsDelete", R"("a\u007fb")", false, kPatternError },
    SchemaCase { "RejectsDeviceName", R"("CON")", false, kPatternError },
    SchemaCase {
      "RejectsDeviceNameAnyCase", R"("lpt9")", false, kPatternError },
    SchemaCase {
      "RejectsDeviceNameWithExtension", R"("Com1.txt")", false, kPatternError },
    SchemaCase { "RejectsNonString", "7", false, "unexpected instance type" }),
  oxygen::cooker::test::SchemaCaseName);

} // namespace
