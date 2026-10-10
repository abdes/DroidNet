//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Schemas/oxygen.physics-material-descriptor.schema.json

#include <string>

#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Cooker/Test/Support/JsonSchema.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using nlohmann::json;
using oxygen::cooker::test::LoadSchema;
using oxygen::cooker::test::SchemaCase;

auto Schema() -> const json&
{
  static const auto schema = LoadSchema(
    "Import/Schemas/oxygen.physics-material-descriptor.schema.json");
  return schema;
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
      R"({
    "$schema": "./src/Oxygen/Cooker/Import/Schemas/oxygen.physics-material-descriptor.schema.json",
    "name": "ground",
    "static_friction": 0.95,
    "dynamic_friction": 0.70,
    "restitution": 0.05,
    "density": 1800.0,
    "combine_mode_friction": "max",
    "combine_mode_restitution": "average",
    "virtual_path": "/.cooked/Physics/Materials/ground.opmat"
  })",
      true,
      "",
    },
    SchemaCase {
      "RejectsUnknownFields",
      R"({
    "name": "ground",
    "static_friction": 0.95,
    "unknown_setting": true
  })",
      false,
      "'unknown_setting'",
    },
    SchemaCase {
      "RejectsNonPositiveDensity",
      R"({
    "name": "ground",
    "density": 0.0
  })",
      false,
      "/density: ",
    }),
  oxygen::cooker::test::SchemaCaseName);

} // namespace
