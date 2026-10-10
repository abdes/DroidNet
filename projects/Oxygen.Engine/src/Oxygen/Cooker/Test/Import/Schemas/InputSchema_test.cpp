//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Schemas/oxygen.input.schema.json,
//   Import/Schemas/oxygen.input-action.schema.json

#include <string>

#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/Test/Support/JsonSchema.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using nlohmann::json;
using oxygen::cooker::test::LoadSchema;
using oxygen::cooker::test::SchemaCase;

auto PrimarySchema() -> const json&
{
  static const auto schema
    = LoadSchema("Import/Schemas/oxygen.input.schema.json");
  return schema;
}

auto ActionSchema() -> const json&
{
  static const auto schema
    = LoadSchema("Import/Schemas/oxygen.input-action.schema.json");
  return schema;
}

class InputSchemaCaseTest : public ::testing::TestWithParam<SchemaCase> { };

NOLINT_TEST_P(InputSchemaCaseTest, ValidatesDocument)
{
  oxygen::cooker::test::ExpectSchemaCase(PrimarySchema(), GetParam());
}

INSTANTIATE_TEST_SUITE_P(Cases, InputSchemaCaseTest,
  ::testing::Values(
    SchemaCase {
      "PrimarySchemaAcceptsCanonicalDocument",
      R"({
    "$schema": "./src/Oxygen/Cooker/Import/Schemas/oxygen.input.schema.json",
    "actions": [
      { "name": "Move", "type": "axis2d", "consumes_input": true },
      { "name": "Jump", "type": "bool" }
    ],
    "contexts": [
      {
        "name": "Gameplay",
        "auto_load": true,
        "auto_activate": true,
        "priority": 100,
        "mappings": [
          { "action": "Jump", "slot": "Space", "trigger": "pressed" },
          {
            "action": "Move",
            "slot": "UpArrow",
            "triggers": [
              {
                "type": "combo",
                "behavior": "implicit",
                "combo_actions": [
                  { "action": "Move", "completion_states": 1, "time_to_complete": 0.25 }
                ]
              }
            ]
          }
        ]
      }
    ]
  })",
      true,
      "",
    },
    SchemaCase {
      "PrimarySchemaRejectsUnknownFields",
      R"({
    "actions": [
      { "name": "Move", "type": "axis2d", "unknown": true }
    ],
    "contexts": [
      {
        "name": "Gameplay",
        "mappings": [
          { "action": "Move", "slot": "W" }
        ]
      }
    ]
  })",
      false,
      "'unknown'",
    }),
  oxygen::cooker::test::SchemaCaseName);

class InputActionSchemaCaseTest : public ::testing::TestWithParam<SchemaCase> {
};

NOLINT_TEST_P(InputActionSchemaCaseTest, ValidatesDocument)
{
  oxygen::cooker::test::ExpectSchemaCase(ActionSchema(), GetParam());
}

INSTANTIATE_TEST_SUITE_P(Cases, InputActionSchemaCaseTest,
  ::testing::Values(
    SchemaCase {
      "ActionSchemaAcceptsCanonicalDocument",
      R"({
    "$schema": "./src/Oxygen/Cooker/Import/Schemas/oxygen.input-action.schema.json",
    "name": "Move",
    "type": "axis2d",
    "consumes_input": true
  })",
      true,
      "",
    },
    SchemaCase {
      "ActionSchemaRejectsInvalidType",
      R"({
    "name": "Move",
    "type": "axis3d"
  })",
      false,
      "/type: ",
    }),
  oxygen::cooker::test::SchemaCaseName);

} // namespace
