//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Schemas/oxygen.transform-component.schema.json

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
    "Import/Schemas/oxygen.transform-component.schema.json");
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

//! The kind the schema owns.
auto Kind() -> json
{
  return Schema().at(json::json_pointer { "/properties/kind/const" });
}

auto Valid() -> json
{
  return json {
    { "kind", Kind() },
    { "local_position", json::array({ 0, 0, 0 }) },
    { "local_rotation_euler_degrees", json::array({ 0, 0, 0 }) },
    { "local_scale", json::array({ 1, 1, 1 }) },
  };
}

class TransformComponentSchemaTest
  : public ::testing::TestWithParam<SchemaCase> { };

NOLINT_TEST_P(TransformComponentSchemaTest, ValidatesDocument)
{
  oxygen::cooker::test::ExpectSchemaCase(Schema(), GetParam());
}

INSTANTIATE_TEST_SUITE_P(Cases, TransformComponentSchemaTest,
  ::testing::Values(Accepts("AcceptsMinimalDocument", Valid()),
    Accepts("AcceptsFractionalAndNegativeComponents",
      With(With(Valid(), "/local_position", json::array({ -1.5, 2.25, 1e3 })),
        "/local_rotation_euler_degrees", json::array({ -90.0, 180, 0.5 }))),
    Rejects("RejectsMissingKind", Without(Valid(), "/kind"), "'kind'"),
    Rejects("RejectsMissingLocalPosition", Without(Valid(), "/local_position"),
      "'local_position'"),
    Rejects("RejectsMissingLocalRotation",
      Without(Valid(), "/local_rotation_euler_degrees"),
      "'local_rotation_euler_degrees'"),
    Rejects("RejectsMissingLocalScale", Without(Valid(), "/local_scale"),
      "'local_scale'"),
    Rejects(
      "RejectsUnknownField", With(Valid(), "/unknown", true), "'unknown'"),
    Rejects("RejectsOtherKind",
      With(Valid(), "/kind", Kind().get<std::string>() + "x"), "/kind: "),
    Rejects("RejectsNonStringKind", With(Valid(), "/kind", 7), "/kind: "),
    Rejects("RejectsNonArrayPosition", With(Valid(), "/local_position", "x"),
      "/local_position: "),
    Rejects("RejectsNonNumberPositionComponent",
      With(Valid(), "/local_position", json::array({ 0, "1", 0 })),
      "/local_position/1: "),
    Rejects("RejectsPositionWithTwoComponents",
      With(Valid(), "/local_position", json::array({ 0, 0 })),
      "/local_position: "),
    Rejects("RejectsRotationWithFourComponents",
      With(
        Valid(), "/local_rotation_euler_degrees", json::array({ 0, 0, 0, 0 })),
      "/local_rotation_euler_degrees: "),
    Rejects("RejectsNullScaleComponent",
      With(Valid(), "/local_scale", json::array({ 1, nullptr, 1 })),
      "/local_scale/1: ")),
  oxygen::cooker::test::SchemaCaseName);

} // namespace
