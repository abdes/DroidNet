//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Schemas/oxygen.physics-material-descriptor.schema.json

#include <string>

#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/test/Support/AuthoredFiles.h>
#include <Oxygen/Cooker/test/Support/JsonSchema.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using nlohmann::json;
using oxygen::cooker::test::LoadSchema;
using oxygen::cooker::test::PhysicsMaterialDescriptor;
using oxygen::cooker::test::SchemaCase;

auto Schema() -> const json&
{
  static const auto schema = LoadSchema(
    "Import/Schemas/oxygen.physics-material-descriptor.schema.json");
  return schema;
}

auto Ground() -> PhysicsMaterialDescriptor
{
  return PhysicsMaterialDescriptor { "/Cooked/Physics/Materials/ground.opmat" };
}

class PhysicsMaterialDescriptorSchemaTest
  : public ::testing::TestWithParam<SchemaCase> { };

NOLINT_TEST_P(PhysicsMaterialDescriptorSchemaTest, ValidatesDocument)
{
  oxygen::cooker::test::ExpectSchemaCase(Schema(), GetParam());
}

INSTANTIATE_TEST_SUITE_P(Cases, PhysicsMaterialDescriptorSchemaTest,
  ::testing::Values(
    SchemaCase {
      "AcceptsCanonicalDocument",
      Ground()
        .Set("/$schema",
          "./src/Oxygen/Cooker/Import/Schemas/"
          "oxygen.physics-material-descriptor.schema.json")
        .Set("/static_friction", 0.95)
        .Set("/dynamic_friction", 0.70)
        .Set("/restitution", 0.05)
        .Set("/density", 1800.0)
        .Set("/combine_mode_friction", "max")
        .Set("/combine_mode_restitution", "average")
        .Text(),
      true,
      "",
    },
    SchemaCase {
      "RejectsUnknownFields",
      Ground()
        .Set("/static_friction", 0.95)
        .Set("/unknown_setting", true)
        .Text(),
      false,
      "'unknown_setting'",
    },
    SchemaCase {
      "RejectsStaleIdentityField",
      Ground()
        .Set("/virtual_path", "/Cooked/Physics/Materials/ground.opmat")
        .Text(),
      false,
      "'virtual_path'",
    },
    SchemaCase {
      "RejectsNonPositiveDensity",
      Ground().Set("/density", 0.0).Text(),
      false,
      "/density: ",
    }),
  oxygen::cooker::test::SchemaCaseName);

} // namespace
