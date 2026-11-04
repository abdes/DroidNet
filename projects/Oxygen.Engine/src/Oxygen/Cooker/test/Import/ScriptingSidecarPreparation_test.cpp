//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/ScriptingSidecarSource.cpp

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/ScriptingSidecarSource.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {
namespace {
  using nlohmann::json;
  using oxygen::cooker::test::DiagnosticSummary;
  using oxygen::cooker::test::HasDiagnosticCode;

  constexpr auto kSlotA = "00000000-0000-4000-8000-00000000000a";
  constexpr auto kSlotB = "00000000-0000-4000-8000-00000000000b";
  constexpr auto kNodeId = "00000000-0000-4000-8000-0000000000c1";
  constexpr auto kNilId = "00000000-0000-0000-0000-000000000000";
  constexpr auto kSchemaFailed = "script.sidecar.schema_validation_failed";

  auto Row(const std::string_view slot = kSlotA, const uint32_t node = 0)
    -> json
  {
    return json {
      { "node_index", node },
      { "slot_id", slot },
      { "script_virtual_path", "/Scripts/test.oscript" },
    };
  }

  auto Param(
    const std::string_view key, const std::string_view type, json value) -> json
  {
    return json {
      { "key", key },
      { "type", type },
      { "value", std::move(value) },
    };
  }

  auto Prepare(const std::string& text, std::vector<ImportDiagnostic>& out)
    -> std::optional<ScriptingSidecarDocument>
  {
    return PrepareScriptingSidecarDocument(text, "sidecar.json", out);
  }

  //! Whether `diagnostics` hold one error with `code` located at `pointer`.
  auto HasErrorAt(const std::vector<ImportDiagnostic>& diagnostics,
    const std::string_view code, const std::string_view pointer) -> bool
  {
    return std::ranges::any_of(
      diagnostics, [&](const ImportDiagnostic& diagnostic) -> bool {
        return diagnostic.code == code && diagnostic.json_pointer == pointer
          && diagnostic.severity == ImportSeverity::kError
          && diagnostic.source_path == "sidecar.json"
          && diagnostic.object_path.empty();
      });
  }

  //! Prepares `document`, which must be refused with `code` at `pointer`.
  auto ExpectRefused(const json& document, const std::string_view code,
    const std::string_view pointer) -> void
  {
    auto diagnostics = std::vector<ImportDiagnostic> {};
    const auto prepared = Prepare(document.dump(), diagnostics);
    EXPECT_FALSE(prepared.has_value());
    EXPECT_TRUE(HasErrorAt(diagnostics, code, pointer))
      << DiagnosticSummary(diagnostics);
  }

  NOLINT_TEST(ScriptingSidecarPreparationTest, AcceptsAValidDocument)
  {
    auto first = Row(kSlotB, 3);
    first["execution_order"] = -2;
    first["params"] = json::array({
      Param("speed", "float", 2.5),
      Param("name", "string", "hero"),
      Param("tint", "vec3", json::array({ 1, 0, 0 })),
      Param("target", "node",
        json { { "node", 2 }, { "path", json::array({ kNodeId }) } }),
    });
    const auto document = json {
      { "bindings", json::array({ first, Row(kSlotA, 1) }) },
      {
        "instance_bindings",
        json::array({
          json {
            { "node_index", 4 },
            { "node_path", json::array({ kNodeId }) },
            { "slot_id", kSlotA },
            { "params", json::array({ Param("on", "bool", true) }) },
          },
        }),
      },
    };

    auto diagnostics = std::vector<ImportDiagnostic> {};
    const auto prepared = Prepare(document.dump(), diagnostics);

    ASSERT_HAS_VALUE(prepared) << DiagnosticSummary(diagnostics);
    EXPECT_TRUE(diagnostics.empty());
    ASSERT_EQ(prepared->rows.size(), 2U);
    // Rows are sorted by node and slot.
    EXPECT_EQ(prepared->rows.at(0).node_index, 1U);
    EXPECT_EQ(prepared->rows.at(0).source_index, 1U);
    const auto& row = prepared->rows.at(1);
    EXPECT_EQ(row.node_index, 3U);
    EXPECT_EQ(row.source_index, 0U);
    EXPECT_EQ(row.execution_order, -2);
    EXPECT_EQ(row.script_virtual_path, "/Scripts/test.oscript");
    ASSERT_EQ(row.params.size(), 4U);
    EXPECT_EQ(row.params.at(1).string_value, "hero");
    EXPECT_EQ(row.params.at(3).record.value.as_node.node_index, 2U);
    EXPECT_EQ(row.params.at(3).node_path.size(), 1U);
    ASSERT_EQ(prepared->instance_rows.size(), 1U);
    EXPECT_EQ(prepared->instance_rows.at(0).node_index, 4U);
    EXPECT_EQ(prepared->instance_rows.at(0).params.size(), 1U);
  }

  NOLINT_TEST(ScriptingSidecarPreparationTest, ReadsABareArrayAsBindings)
  {
    auto diagnostics = std::vector<ImportDiagnostic> {};
    const auto prepared
      = Prepare(json::array({ Row(kSlotA, 7) }).dump(), diagnostics);

    ASSERT_HAS_VALUE(prepared) << DiagnosticSummary(diagnostics);
    ASSERT_EQ(prepared->rows.size(), 1U);
    EXPECT_EQ(prepared->rows.at(0).node_index, 7U);
  }

  NOLINT_TEST(ScriptingSidecarPreparationTest, AcceptsAnEmptyBindingsArray)
  {
    auto diagnostics = std::vector<ImportDiagnostic> {};
    const auto prepared = Prepare(R"({"bindings":[]})", diagnostics);

    ASSERT_HAS_VALUE(prepared);
    EXPECT_TRUE(prepared->rows.empty());
  }

  NOLINT_TEST(ScriptingSidecarPreparationTest, RefusesInvalidJson)
  {
    auto diagnostics = std::vector<ImportDiagnostic> {};
    const auto prepared = Prepare("{ not json", diagnostics);

    EXPECT_FALSE(prepared.has_value());
    EXPECT_TRUE(HasErrorAt(diagnostics, "script.sidecar.parse_failed", ""))
      << DiagnosticSummary(diagnostics);
  }

  NOLINT_TEST(ScriptingSidecarPreparationTest, RefusesComments)
  {
    auto diagnostics = std::vector<ImportDiagnostic> {};
    const auto prepared = Prepare("// note\n{\"bindings\":[]}", diagnostics);

    EXPECT_FALSE(prepared.has_value());
    EXPECT_TRUE(HasDiagnosticCode(diagnostics, "script.sidecar.parse_failed"));
  }

  NOLINT_TEST(
    ScriptingSidecarPreparationTest, RefusesARootThatIsNeitherObjectNorArray)
  {
    ExpectRefused(json(5), kSchemaFailed, "");
  }

  NOLINT_TEST(
    ScriptingSidecarPreparationTest, RefusesMissingOrMalformedBindings)
  {
    ExpectRefused(json::object(), kSchemaFailed, "");
    ExpectRefused(json { { "bindings", 1 } }, kSchemaFailed, "/bindings");
  }

  NOLINT_TEST(ScriptingSidecarPreparationTest, RefusesAMalformedRowAtItsPointer)
  {
    ExpectRefused(json::array({ Row(), 3 }), kSchemaFailed, "/bindings/1");

    auto no_node = Row();
    no_node.erase("node_index");
    ExpectRefused(json::array({ no_node }), kSchemaFailed, "/bindings/0");

    auto no_script = Row();
    no_script.erase("script_virtual_path");
    ExpectRefused(
      json::array({ Row(kSlotB), no_script }), kSchemaFailed, "/bindings/1");
  }

  NOLINT_TEST(ScriptingSidecarPreparationTest, RefusesAnInvalidSlotId)
  {
    ExpectRefused(json::array({ Row("not-a-uuid") }),
      "script.sidecar.slot_id_invalid", "/bindings/0/slot_id");
    ExpectRefused(json::array({ Row(kNilId) }),
      "script.sidecar.slot_id_invalid", "/bindings/0/slot_id");

    auto no_slot = Row();
    no_slot.erase("slot_id");
    ExpectRefused(json::array({ no_slot }), kSchemaFailed, "/bindings/0");

    auto number_slot = Row();
    number_slot["slot_id"] = 7;
    ExpectRefused(
      json::array({ number_slot }), kSchemaFailed, "/bindings/0/slot_id");
  }

  NOLINT_TEST(ScriptingSidecarPreparationTest, RefusesAnInvalidExecutionOrder)
  {
    auto fractional = Row();
    fractional["execution_order"] = 1.5;
    ExpectRefused(json::array({ fractional }), kSchemaFailed,
      "/bindings/0/execution_order");

    auto huge = Row();
    huge["execution_order"] = 4294967296LL;
    ExpectRefused(
      json::array({ huge }), kSchemaFailed, "/bindings/0/execution_order");
  }

  NOLINT_TEST(ScriptingSidecarPreparationTest, RefusesMalformedParams)
  {
    auto not_array = Row();
    not_array["params"] = 1;
    ExpectRefused(
      json::array({ not_array }), kSchemaFailed, "/bindings/0/params");

    auto bad_type = Row();
    bad_type["params"] = json::array({ Param("a", "quaternion", 1) });
    ExpectRefused(
      json::array({ bad_type }), kSchemaFailed, "/bindings/0/params/0/type");

    auto bad_value = Row();
    bad_value["params"]
      = json::array({ Param("ok", "bool", true), Param("a", "int32", "x") });
    ExpectRefused(
      json::array({ bad_value }), kSchemaFailed, "/bindings/0/params/1/value");

    auto out_of_range = Row();
    out_of_range["params"] = json::array({ Param("a", "int32", 2147483648LL) });
    ExpectRefused(json::array({ out_of_range }), kSchemaFailed,
      "/bindings/0/params/0/value");
  }

  NOLINT_TEST(ScriptingSidecarPreparationTest, RefusesANodeParamPathOfBadIds)
  {
    auto bad_path = Row();
    bad_path["params"] = json::array({
      Param("target", "node",
        json { { "node", 2 }, { "path", json::array({ kNilId }) } }),
    });

    ExpectRefused(json::array({ bad_path }), "script.sidecar.param_invalid",
      "/bindings/0/params/0");
  }

  NOLINT_TEST(ScriptingSidecarPreparationTest, RefusesAKeyOverriddenTwice)
  {
    auto twice = Row();
    twice["params"]
      = json::array({ Param("a", "float", 1.0), Param("a", "float", 2.0) });

    ExpectRefused(json::array({ twice }), "script.sidecar.param_duplicate",
      "/bindings/0/params/1/key");
  }

  NOLINT_TEST(ScriptingSidecarPreparationTest, RefusesMalformedInstanceBindings)
  {
    ExpectRefused(
      json { { "bindings", json::array() }, { "instance_bindings", 1 } },
      kSchemaFailed, "/instance_bindings");

    const auto valid = json {
      { "node_index", 1 },
      { "node_path", json::array({ kNodeId }) },
      { "slot_id", kSlotA },
    };
    const auto with_rows = [](json row) -> json {
      return json {
        { "bindings", json::array() },
        { "instance_bindings", json::array({ std::move(row) }) },
      };
    };

    auto no_node = valid;
    no_node.erase("node_index");
    ExpectRefused(with_rows(no_node), kSchemaFailed, "/instance_bindings/0");

    auto no_path = valid;
    no_path["node_path"] = json::array();
    ExpectRefused(
      with_rows(no_path), kSchemaFailed, "/instance_bindings/0/node_path");

    auto nil_path = valid;
    nil_path["node_path"] = json::array({ kNilId });
    ExpectRefused(with_rows(nil_path), "script.sidecar.node_path_invalid",
      "/instance_bindings/0/node_path");

    auto bad_slot = valid;
    bad_slot["slot_id"] = "x";
    ExpectRefused(with_rows(bad_slot), "script.sidecar.slot_id_invalid",
      "/instance_bindings/0/slot_id");
  }

  NOLINT_TEST(ScriptingSidecarPreparationTest, RefusesTwoRowsForOneSlotOfANode)
  {
    auto diagnostics = std::vector<ImportDiagnostic> {};
    const auto prepared = Prepare(
      json::array({ Row(kSlotA, 2), Row(kSlotB, 2), Row(kSlotA, 2) }).dump(),
      diagnostics);

    EXPECT_FALSE(prepared.has_value());
    // The sort does not say which of the two rows is the duplicate.
    EXPECT_TRUE(HasErrorAt(diagnostics,
                  "script.sidecar.duplicate_slot_conflict", "/bindings/0")
      || HasErrorAt(
        diagnostics, "script.sidecar.duplicate_slot_conflict", "/bindings/2"))
      << DiagnosticSummary(diagnostics);
  }

  NOLINT_TEST(
    ScriptingSidecarPreparationTest, RefusesTwoChangesOfOneInstancedSlot)
  {
    const auto instance = json {
      { "node_index", 1 },
      { "node_path", json::array({ kNodeId }) },
      { "slot_id", kSlotA },
    };

    auto diagnostics = std::vector<ImportDiagnostic> {};
    const auto prepared = Prepare(
      json {
        { "bindings", json::array() },
        { "instance_bindings", json::array({ instance, instance }) },
      }
        .dump(),
      diagnostics);

    EXPECT_FALSE(prepared.has_value());
    EXPECT_TRUE(
      HasErrorAt(diagnostics, "script.sidecar.duplicate_slot_conflict",
        "/instance_bindings/0")
      || HasErrorAt(diagnostics, "script.sidecar.duplicate_slot_conflict",
        "/instance_bindings/1"))
      << DiagnosticSummary(diagnostics);
  }

  NOLINT_TEST(ScriptingSidecarPreparationTest, KeepsEarlierDiagnosticsOfTheSink)
  {
    auto diagnostics = std::vector<ImportDiagnostic> {
      {
        .severity = ImportSeverity::kError,
        .code = "earlier",
        .message = "reported before",
      },
    };

    const auto prepared = Prepare(R"({"bindings":[]})", diagnostics);

    EXPECT_TRUE(prepared.has_value());
    EXPECT_EQ(diagnostics.size(), 1U);
  }

  NOLINT_TEST(
    ScriptingSidecarPreparationTest, NormalizesABareArrayToTheObjectForm)
  {
    const auto array_text = json::array({ Row() }).dump();
    const auto normalized = NormalizeScriptingSidecarText(array_text);
    ASSERT_HAS_VALUE(normalized);
    EXPECT_EQ(json::parse(*normalized),
      (json { { "bindings", json::parse(array_text) } }));

    const auto object_text = json { { "bindings", json::array() } }.dump();
    EXPECT_EQ(NormalizeScriptingSidecarText(object_text), object_text);
    EXPECT_FALSE(NormalizeScriptingSidecarText("{ nope").has_value());
  }

} // namespace
} // namespace oxygen::content::import::test
