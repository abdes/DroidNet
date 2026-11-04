//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/InputSource.cpp

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Content/VirtualPath.h>
#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/InputSource.h>
#include <Oxygen/Cooker/test/Support/AuthoredFiles.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {
namespace {
  using nlohmann::json;
  using oxygen::cooker::test::HasDiagnosticCode;
  using oxygen::cooker::test::InputSource;

  constexpr auto kIdentity = std::string_view { "/Content/Input/Player.input" };

  auto Source() -> InputSource
  {
    return InputSource { std::string(kIdentity) };
  }

  //! A source declaring `Jump` with the context `Gameplay`, which maps `Jump`
  //! to the slot `Space`.
  auto JumpSource() -> InputSource
  {
    return Source()
      .Action("Jump", "bool")
      .Context("Gameplay")
      .Append("/contexts/0/mappings",
        json::parse(R"({ "action": "Jump", "slot": "Space" })"));
  }

  struct Prepared final {
    std::optional<InputSourceDocument> document;
    std::vector<ImportDiagnostic> diagnostics;
  };

  auto Prepare(const std::string_view text) -> Prepared
  {
    auto prepared = Prepared {};
    prepared.document = PrepareInputDocument(
      text, "Player.input.json", kIdentity, prepared.diagnostics);
    return prepared;
  }

  auto Prepare(const InputSource& source) -> Prepared
  {
    return Prepare(source.Text());
  }

  //! The pointer of the first diagnostic with `code`, or a marker.
  auto PointerOf(const Prepared& prepared, const std::string_view code)
    -> std::string
  {
    const auto it = std::ranges::find_if(prepared.diagnostics,
      [&](const ImportDiagnostic& d) -> bool { return d.code == code; });
    return it == prepared.diagnostics.end() ? "<no such diagnostic>"
                                            : it->json_pointer;
  }

  NOLINT_TEST(InputPreparationTest, AcceptsADocumentAndNamesItsProducts)
  {
    const auto prepared = Prepare(JumpSource()
        .Action("Move", "axis2d")
        .Set("/actions/1/consumes_input", true)
        .Set("/actions/1/accumulation", "cumulative")
        .Append("/contexts/0/mappings", json::parse(R"({
            "action": "Move",
            "slot": "Up",
            "triggers": [ { "type": "chord", "chord_action": "Jump" } ]
          })")));

    ASSERT_HAS_VALUE(prepared.document);
    EXPECT_TRUE(prepared.diagnostics.empty());
    const auto& document = *prepared.document;
    ASSERT_EQ(document.actions.size(), 2U);
    EXPECT_EQ(document.actions[0].virtual_path,
      "/Content/Input/Player.input/Jump.oiact");
    EXPECT_TRUE(document.actions[1].consumes_input);
    EXPECT_TRUE(document.actions[1].cumulative);
    EXPECT_EQ(document.local_action_keys.at("Jump"), document.actions[0].key);
    EXPECT_EQ(document.local_action_keys.at(document.actions[0].virtual_path),
      document.actions[0].key);
    ASSERT_EQ(document.contexts.size(), 1U);
    EXPECT_EQ(document.contexts[0].json_pointer, "/contexts/0");
    ASSERT_EQ(document.contexts[0].mappings.size(), 2U);
    EXPECT_EQ(
      document.contexts[0].mappings[1].json_pointer, "/contexts/0/mappings/1");
    EXPECT_EQ(document.contexts[0].mappings[1].triggers[0].json_pointer,
      "/contexts/0/mappings/1/triggers/0");
  }

  NOLINT_TEST(InputPreparationTest, RefusesTextThatIsNotJson)
  {
    const auto prepared = Prepare("{ not json");

    EXPECT_FALSE(prepared.document.has_value());
    EXPECT_TRUE(HasDiagnosticCode(
      prepared.diagnostics, import_codes::kInputImportParseFailed));
    EXPECT_EQ(prepared.diagnostics.front().source_path, "Player.input.json");
  }

  NOLINT_TEST(InputPreparationTest, RefusesComments)
  {
    const auto prepared = Prepare("{ /* comment */ \"actions\": [] }");

    EXPECT_FALSE(prepared.document.has_value());
    EXPECT_TRUE(HasDiagnosticCode(
      prepared.diagnostics, import_codes::kInputImportParseFailed));
  }

  NOLINT_TEST(InputPreparationTest, RefusesARootThatIsNotAnObject)
  {
    const auto prepared = Prepare("[]");

    EXPECT_FALSE(prepared.document.has_value());
    EXPECT_TRUE(HasDiagnosticCode(
      prepared.diagnostics, import_codes::kInputSchemaValidationFailed));
    EXPECT_EQ(
      PointerOf(prepared, import_codes::kInputSchemaValidationFailed), "");
  }

  NOLINT_TEST(InputPreparationTest, LocatesEverySchemaIssue)
  {
    const auto prepared = Prepare(Source()
        .Action("Move", "axis3d")
        .Action("Look", "axis1d")
        .Set("/actions/1/accumulation", "average")
        .Remove("/contexts"));

    EXPECT_FALSE(prepared.document.has_value());
    auto pointers = std::vector<std::string> {};
    for (const auto& d : prepared.diagnostics) {
      EXPECT_EQ(d.code, import_codes::kInputSchemaValidationFailed);
      pointers.push_back(d.json_pointer);
    }
    EXPECT_THAT(pointers, ::testing::Contains("/actions/0/type"));
    EXPECT_THAT(pointers, ::testing::Contains("/actions/1/accumulation"));
  }

  NOLINT_TEST(InputPreparationTest, RefusesAnActionNameThatGivesARefusedPath)
  {
    const auto prepared = Prepare(
      Source().Action(std::string(300, 'a'), "bool").Remove("/contexts"));

    EXPECT_FALSE(prepared.document.has_value());
    const auto code = std::string(to_string(VirtualPathError::kSegmentTooLong));
    EXPECT_TRUE(HasDiagnosticCode(prepared.diagnostics, code));
    EXPECT_EQ(PointerOf(prepared, code), "/actions/0/name");
  }

  NOLINT_TEST(InputPreparationTest, RefusesAContextNameThatGivesARefusedPath)
  {
    const auto prepared = Prepare(Source().Context(std::string(300, 'c')));

    EXPECT_FALSE(prepared.document.has_value());
    const auto code = std::string(to_string(VirtualPathError::kSegmentTooLong));
    EXPECT_EQ(PointerOf(prepared, code), "/contexts/0/name");
  }

  NOLINT_TEST(InputPreparationTest, RefusesADuplicateAction)
  {
    const auto prepared
      = Prepare(Source().Action("Jump", "bool").Action("Jump", "axis1d"));

    EXPECT_FALSE(prepared.document.has_value());
    EXPECT_EQ(PointerOf(prepared, import_codes::kInputAssetActionConflict),
      "/actions/1/name");
  }

  NOLINT_TEST(InputPreparationTest, RefusesADuplicateContext)
  {
    const auto prepared
      = Prepare(Source().Context("Gameplay").Context("Gameplay"));

    EXPECT_FALSE(prepared.document.has_value());
    EXPECT_EQ(PointerOf(prepared, import_codes::kInputContextNameDuplicate),
      "/contexts/1/name");
  }

  NOLINT_TEST(InputPreparationTest, RefusesATimeOutOfRange)
  {
    const auto prepared = Prepare(
      JumpSource().Set("/contexts/0/mappings/0/triggers", json::parse(R"([ {
          "type": "combo",
          "combo_actions": [ { "action": "Jump", "time_to_complete": 1e30 } ]
        } ])")));

    EXPECT_FALSE(prepared.document.has_value());
    EXPECT_EQ(PointerOf(prepared, import_codes::kInputContextTriggerInvalid),
      "/contexts/0/mappings/0/triggers/0/combo_actions/0/time_to_complete");
  }

  NOLINT_TEST(InputPreparationTest, WarnsOfAuxRecordsOnATriggerThatIsNotACombo)
  {
    const auto prepared = Prepare(
      JumpSource().Set("/contexts/0/mappings/0/triggers", json::parse(R"([ {
          "type": "pressed",
          "aux": [ { "action": "Jump" } ]
        } ])")));

    ASSERT_HAS_VALUE(prepared.document);
    ASSERT_EQ(prepared.diagnostics.size(), 1U);
    EXPECT_EQ(prepared.diagnostics[0].code,
      import_codes::kInputContextTriggerAuxIgnored);
    EXPECT_EQ(prepared.diagnostics[0].severity, ImportSeverity::kWarning);
    EXPECT_EQ(prepared.diagnostics[0].json_pointer,
      "/contexts/0/mappings/0/triggers/0");
    EXPECT_TRUE(
      prepared.document->contexts[0].mappings[0].triggers[0].aux.empty());
  }

  NOLINT_TEST(
    InputPreparationTest, RefusesAMappingOfAnActionTheSourceDoesNotDeclare)
  {
    const auto prepared
      = Prepare(JumpSource().Set("/contexts/0/mappings/0/action", "Missing"));

    EXPECT_FALSE(prepared.document.has_value());
    EXPECT_EQ(PointerOf(prepared, import_codes::kInputContextActionUnresolved),
      "/contexts/0/mappings/0/action");
  }

  NOLINT_TEST(
    InputPreparationTest, RefusesAChordOfAnActionTheSourceDoesNotDeclare)
  {
    const auto prepared
      = Prepare(JumpSource().Set("/contexts/0/mappings/0/triggers",
        json::parse(R"([ { "type": "chord", "chord_action": "Missing" } ])")));

    EXPECT_FALSE(prepared.document.has_value());
    EXPECT_EQ(PointerOf(prepared, import_codes::kInputContextActionUnresolved),
      "/contexts/0/mappings/0/triggers/0/chord_action");
  }

  NOLINT_TEST(
    InputPreparationTest, RefusesAComboOfAnActionTheSourceDoesNotDeclare)
  {
    const auto prepared = Prepare(
      JumpSource().Set("/contexts/0/mappings/0/triggers", json::parse(R"([ {
          "type": "combo",
          "combo_actions": [ { "action": "Missing" } ]
        } ])")));

    EXPECT_FALSE(prepared.document.has_value());
    EXPECT_EQ(PointerOf(prepared, import_codes::kInputContextActionUnresolved),
      "/contexts/0/mappings/0/triggers/0/combo_actions/0/action");
  }

  NOLINT_TEST(InputPreparationTest, LeavesActionReferencesToTheCaller)
  {
    auto source = JumpSource();
    source.Reference("/contexts/0/mappings/0/action",
      "/Content/Input/Common.input/Jump.oiact");
    const auto prepared = Prepare(source);

    ASSERT_HAS_VALUE(prepared.document);
    EXPECT_TRUE(prepared.diagnostics.empty());
  }

} // namespace
} // namespace oxygen::content::import::test
