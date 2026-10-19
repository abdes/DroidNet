//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Schemas/oxygen.input.schema.json

#include <string>

#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/test/Support/JsonSchema.h>
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
      {
        "name": "Move",
        "type": "axis2d",
        "consumes_input": true,
        "accumulation": "cumulative"
      },
      { "name": "Jump", "type": "bool" }
    ],
    "contexts": [
      {
        "name": "Gameplay",
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
      "AcceptsActionsWithoutContexts",
      R"({ "actions": [ { "name": "Jump", "type": "bool" } ] })",
      true,
      "",
    },
    SchemaCase {
      "AcceptsEmptySource",
      R"({})",
      true,
      "",
    },
    SchemaCase {
      "AcceptsActionReferenceByVirtualPath",
      R"({
    "contexts": [
      {
        "name": "Gameplay",
        "mappings": [
          {
            "action": "/Content/Input/Common.input/Jump.oiact",
            "slot": "Space",
            "triggers": [
              {
                "type": "chord",
                "chord_action": "/Content/Input/Common.input/Modifier.oiact"
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
      "RejectsActionReferenceToAnotherAssetType",
      R"({
    "contexts": [
      {
        "name": "Gameplay",
        "mappings": [
          { "action": "/Content/Input/Common.input/Gameplay.oimap", "slot": "W" }
        ]
      }
    ]
  })",
      false,
      "/contexts/0/mappings/0/action",
    },
    SchemaCase {
      "RejectsUnknownFields",
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
    },
    SchemaCase {
      "RejectsInvalidAccumulation",
      R"({
    "actions": [
      { "name": "Move", "type": "axis2d", "accumulation": "average" }
    ]
  })",
      false,
      "/actions/0/accumulation",
    },
    SchemaCase {
      "RejectsInvalidActionType",
      R"({ "actions": [ { "name": "Move", "type": "axis3d" } ] })",
      false,
      "/actions/0/type",
    }),
  oxygen::cooker::test::SchemaCaseName);

} // namespace
