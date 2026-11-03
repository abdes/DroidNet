//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Schemas/oxygen.input.schema.json

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/test/Support/AuthoredFiles.h>
#include <Oxygen/Cooker/test/Support/JsonSchema.h>
#include <Oxygen/Platform/Input.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using nlohmann::json;
using oxygen::cooker::test::InputSource;
using oxygen::cooker::test::LoadSchema;
using oxygen::cooker::test::SchemaCase;

auto PrimarySchema() -> const json&
{
  static const auto schema
    = LoadSchema("Import/Schemas/oxygen.input.schema.json");
  return schema;
}

auto Source() -> InputSource
{
  return InputSource { "/Content/Input/Common.input" };
}

//! A source with the context `Gameplay` whose one mapping binds `slot`; the
//! test names its action.
auto GameplayMapping(const std::string_view slot) -> InputSource
{
  return Source()
    .Context("Gameplay")
    .Append("/contexts/0/mappings",
      json::parse(R"({ "slot": ")" + std::string { slot } + R"(" })"));
}

class InputSchemaCaseTest : public ::testing::TestWithParam<SchemaCase> { };

//! The platform owns slot names: the schema admits exactly the slots it
//! registers, so a cooked mapping always names a slot the runtime resolves.
NOLINT_TEST(InputSchemaTest, SlotNamesAreTheSlotsThePlatformRegisters)
{
  auto slots = std::vector<oxygen::platform::InputSlot> {};
  oxygen::platform::InputSlots::Initialize();
  oxygen::platform::InputSlots::GetAllInputSlots(slots);
  auto registered = std::vector<std::string> {};
  for (const auto& slot : slots) {
    registered.emplace_back(slot.GetName());
  }
  auto admitted
    = PrimarySchema()
        .at(json::json_pointer("/$defs/mapping/properties/slot/enum"))
        .get<std::vector<std::string>>();
  std::ranges::sort(registered);
  std::ranges::sort(admitted);

  EXPECT_EQ(admitted, registered);
}

NOLINT_TEST_P(InputSchemaCaseTest, ValidatesDocument)
{
  oxygen::cooker::test::ExpectSchemaCase(PrimarySchema(), GetParam());
}

INSTANTIATE_TEST_SUITE_P(Cases, InputSchemaCaseTest,
  ::testing::Values(
    SchemaCase {
      "PrimarySchemaAcceptsCanonicalDocument",
      Source()
        .Set("/$schema",
          "./src/Oxygen/Cooker/Import/Schemas/oxygen.input.schema.json")
        .Action("Move", "axis2d")
        .Set("/actions/0/consumes_input", true)
        .Set("/actions/0/accumulation", "cumulative")
        .Action("Jump", "bool")
        .Context("Gameplay", 100)
        .Append("/contexts/0/mappings",
          json::parse(
            R"({ "action": "Jump", "slot": "Space", "trigger": "pressed" })"))
        .Append("/contexts/0/mappings", json::parse(R"({
            "action": "Move",
            "slot": "Up",
            "triggers": [
              {
                "type": "combo",
                "behavior": "implicit",
                "combo_actions": [
                  { "action": "Move", "completion_states": 1, "time_to_complete": 0.25 }
                ]
              }
            ]
          })"))
        .Text(),
      true,
      "",
    },
    SchemaCase {
      "AcceptsActionsWithoutContexts",
      Source().Action("Jump", "bool").Remove("/contexts").Text(),
      true,
      "",
    },
    SchemaCase {
      "AcceptsEmptySource",
      Source().Remove("/actions").Remove("/contexts").Text(),
      true,
      "",
    },
    SchemaCase {
      "AcceptsActionReferenceByVirtualPath",
      GameplayMapping("Space")
        .Reference("/contexts/0/mappings/0/action",
          "/Content/Input/Common.input/Jump.oiact")
        .Append("/contexts/0/mappings/0/triggers",
          json::parse(R"({ "type": "chord" })"))
        .Reference("/contexts/0/mappings/0/triggers/0/chord_action",
          "/Content/Input/Common.input/Modifier.oiact")
        .Text(),
      true,
      "",
    },
    SchemaCase {
      "AcceptsLocalActionName",
      GameplayMapping("Space")
        .Action("Jump", "bool")
        .Set("/contexts/0/mappings/0/action", "Jump")
        .Text(),
      true,
      "",
    },
    SchemaCase {
      "AcceptsActionReferenceRelativeToTheSource",
      GameplayMapping("Space")
        .Set("/contexts/0/mappings/0/action", "Car.input/Jump.oiact")
        .Text(),
      true,
      "",
    },
    SchemaCase {
      "AcceptsActionReferenceClimbingFolders",
      GameplayMapping("Space")
        .Set("/contexts/0/mappings/0/action", "../Shared.input/Use.oiact")
        .Text(),
      true,
      "",
    },
    SchemaCase {
      "RejectsActionNameEndingWithActionExtension",
      Source().Action("Jump.oiact", "bool").Remove("/contexts").Text(),
      false,
      "/actions/0/name",
    },
    SchemaCase {
      "RejectsMappingActionThatIsNeitherNameNorReference",
      GameplayMapping("Space")
        .Set("/contexts/0/mappings/0/action", "Car.input/Jump.omat")
        .Text(),
      false,
      "/contexts/0/mappings/0/action",
    },
    SchemaCase {
      "RejectsActionReferenceToAnotherAssetType",
      GameplayMapping("W")
        .Reference("/contexts/0/mappings/0/action",
          "/Content/Input/Common.input/Gameplay.oimap")
        .Text(),
      false,
      "/contexts/0/mappings/0/action",
    },
    SchemaCase {
      "RejectsSlotNameThePlatformDoesNotRegister",
      GameplayMapping("UpArrow")
        .Set("/contexts/0/mappings/0/action", "Jump")
        .Text(),
      false,
      "/contexts/0/mappings/0/slot",
    },
    SchemaCase {
      "RejectsUnknownFields",
      Source()
        .Action("Move", "axis2d")
        .Set("/actions/0/unknown", true)
        .Context("Gameplay")
        .Append("/contexts/0/mappings",
          json::parse(R"({ "action": "Move", "slot": "W" })"))
        .Text(),
      false,
      "'unknown'",
    },
    SchemaCase {
      "RejectsInvalidAccumulation",
      Source()
        .Action("Move", "axis2d")
        .Set("/actions/0/accumulation", "average")
        .Remove("/contexts")
        .Text(),
      false,
      "/actions/0/accumulation",
    },
    SchemaCase {
      "RejectsInvalidActionType",
      Source().Action("Move", "axis3d").Remove("/contexts").Text(),
      false,
      "/actions/0/type",
    }),
  oxygen::cooker::test::SchemaCaseName);

} // namespace
