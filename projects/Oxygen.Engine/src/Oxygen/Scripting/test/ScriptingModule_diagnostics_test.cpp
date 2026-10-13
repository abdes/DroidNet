//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ScriptingModule_slot_helpers.h"

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scripting/Analysis/DiagnosticRenderer.h>
#include <Oxygen/Scripting/Analysis/ScriptDiagnostic.h>
#include <Oxygen/Scripting/Module/ScriptingModule.h>
#include <Oxygen/Scripting/test/ScriptingModule_test_fixture.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::scripting::test {

namespace {

  constexpr std::string_view kScriptName = "Follow";
  constexpr std::string_view kNodeName = "Camera Rig";

  constexpr uint64_t kNestedErrorHash = 0x4E45535445440001ULL;
  constexpr uint64_t kUnsetTargetHash = 0x554E534554000001ULL;
  constexpr uint64_t kNotAScriptHash = 0x4E4F545343520001ULL;
  constexpr uint64_t kLoadErrorHash = 0x4C4F414445520001ULL;

  constexpr std::string_view kNestedErrorScript
    = "local script = oxygen.script {}\n"
      "local function explode()\n"
      "  error(\"boom from helper\")\n"
      "end\n"
      "function script.on_gameplay(self, _dt)\n"
      "  explode()\n"
      "end\n"
      "return script\n";
  constexpr uint32_t kRaiseLine = 3;
  constexpr uint32_t kCallLine = 6;

  constexpr std::string_view kUnsetTargetScript
    = "local script = oxygen.script {\n"
      "  params = { target = oxygen.param.node {} },\n"
      "}\n"
      "function script.on_gameplay(self, _dt)\n"
      "  self.params.target:set_position(1)\n"
      "end\n"
      "return script\n";
  constexpr uint32_t kIndexLine = 5;

  auto SlotOf(const std::string_view hook) -> std::optional<SlotContext>
  {
    return SlotContext {
      .node = std::string(kNodeName),
      .slot_index = 0,
      .hook = std::string(hook),
    };
  }

  auto MakeNamedScriptAsset(
    std::span<const data::pak::scripting::ScriptParamRecord> declarations = {})
    -> std::shared_ptr<const data::ScriptAsset>
  {
    auto desc = data::pak::scripting::ScriptAssetDesc {};
    std::ranges::copy(kScriptName, std::begin(desc.header.name));
    return std::make_shared<data::ScriptAsset>(
      data::AssetKey {}, desc, declarations);
  }

  class ScriptDiagnosticsTest : public ScriptingModuleTest {
  protected:
    //! Builds a scene whose node `kNodeName` runs `source` in slot 0.
    auto MakeScene(const std::string_view source, const uint64_t hash,
      std::span<const data::pak::scripting::ScriptParamRecord> declarations
      = {}) -> std::shared_ptr<scene::Scene>
    {
      auto scene = std::make_shared<scene::Scene>(
        "diagnostics", ScriptingModuleTest::kDefaultSceneCapacity);
      auto node = scene->CreateNode(std::string(kNodeName));
      if (!node.IsValid() || !node.AttachScripting()) {
        return {};
      }
      auto scripting = node.GetScripting();
      if (!scripting.AddSlot(MakeNamedScriptAsset(declarations))) {
        return {};
      }
      if (!scripting.MarkSlotReady(
            scripting.Slots().front(), CompileSlotExecutable(source, hash))) {
        return {};
      }
      return scene;
    }

    //! Attaches the module and records every diagnostic it publishes.
    auto Attach(ScriptingModule& module) -> bool
    {
      if (!AttachModule(module)) {
        return false;
      }
      static_cast<void>(module.AddDiagnosticHandler(
        [this](const ScriptDiagnostic& diagnostic) -> void {
          diagnostics_.push_back(diagnostic);
        }));
      return true;
    }

    [[nodiscard]] auto Diagnostics() const
      -> const std::vector<ScriptDiagnostic>&
    {
      return diagnostics_;
    }

  private:
    std::vector<ScriptDiagnostic> diagnostics_;
  };

} // namespace

//! A runtime error inside a nested function reports the line that raised it,
//! the call chain, and the hook, node and slot.
NOLINT_TEST_F(ScriptDiagnosticsTest, NestedRuntimeErrorReportsLineAndSlot)
{
  auto module = MakeModule();
  ASSERT_TRUE(Attach(module));
  auto sources = std::make_shared<SourceTextCache>();
  sources->Add(std::string(kScriptName), std::string(kNestedErrorScript));
  module.SetDiagnosticSources(sources);
  auto scene = MakeScene(kNestedErrorScript, kNestedErrorHash);
  ASSERT_NE(scene, nullptr);
  engine::FrameContext frame;
  frame.SetScene(observer_ptr { scene.get() });

  RunSlotHooks(module, frame);
  RunSlotHooks(module, frame);

  ASSERT_EQ(Diagnostics().size(), 1U);
  const auto& diagnostic = Diagnostics().front();
  EXPECT_EQ(diagnostic.code, DiagnosticCode::kRuntimeError);
  EXPECT_EQ(diagnostic.message, "boom from helper");
  const auto* const span = diagnostic.PrimarySpan();
  ASSERT_NE(span, nullptr);
  EXPECT_EQ(span->source_name, kScriptName);
  EXPECT_EQ(span->begin.line, kRaiseLine);
  EXPECT_EQ(span->begin.column, 3U);
  EXPECT_THAT(diagnostic.notes,
    ::testing::Contains(::testing::AllOf(::testing::HasSubstr("on_gameplay"),
      ::testing::HasSubstr(
        std::string(kScriptName) + ":" + std::to_string(kCallLine)))));
  EXPECT_EQ(diagnostic.slot, SlotOf("on_gameplay"));
  ASSERT_EQ(frame.GetErrors().size(), 1U);
  EXPECT_NE(
    frame.GetErrors().front().message.find("on_gameplay"), std::string::npos);
}

//! Indexing an unset `node` parameter labels the expression and explains
//! how to set it.
NOLINT_TEST_F(ScriptDiagnosticsTest, UnsetNodeParameterGetsHelp)
{
  auto module = MakeModule();
  ASSERT_TRUE(Attach(module));
  auto sources = std::make_shared<SourceTextCache>();
  sources->Add(std::string(kScriptName), std::string(kUnsetTargetScript));
  module.SetDiagnosticSources(sources);
  const auto target = data::MakeScriptParamRecord(
    "target", data::ScriptParam { data::ScriptNodeParam {} });
  const auto declarations = target.has_value()
    ? std::vector { *target }
    : std::vector<data::pak::scripting::ScriptParamRecord> {};
  ASSERT_EQ(declarations.size(), 1U);
  auto scene = MakeScene(kUnsetTargetScript, kUnsetTargetHash, declarations);
  ASSERT_NE(scene, nullptr);
  engine::FrameContext frame;
  frame.SetScene(observer_ptr { scene.get() });

  RunSlotHooks(module, frame);

  ASSERT_EQ(Diagnostics().size(), 1U);
  const auto& diagnostic = Diagnostics().front();
  EXPECT_EQ(diagnostic.code, DiagnosticCode::kRuntimeError);
  const auto* const span = diagnostic.PrimarySpan();
  ASSERT_NE(span, nullptr);
  EXPECT_EQ(span->begin.line, kIndexLine);
  EXPECT_EQ(span->begin.column, 3U);
  EXPECT_EQ(span->end.column, 21U);
  EXPECT_THAT(diagnostic.notes,
    ::testing::Contains("parameter `target` is not set on this slot"));
  EXPECT_THAT(diagnostic.help,
    ::testing::Optional(::testing::Field(
      &Suggestion::message, ::testing::HasSubstr("`target`"))));
}

//! A chunk that does not return its `oxygen.script` table is a binding
//! diagnostic of the slot's load.
NOLINT_TEST_F(ScriptDiagnosticsTest, NonScriptReturnIsBindingDiagnostic)
{
  auto module = MakeModule();
  ASSERT_TRUE(Attach(module));
  auto scene
    = MakeScene("return { on_gameplay = function() end }\n", kNotAScriptHash);
  ASSERT_NE(scene, nullptr);
  engine::FrameContext frame;
  frame.SetScene(observer_ptr { scene.get() });

  RunSlotHooks(module, frame);

  ASSERT_EQ(Diagnostics().size(), 1U);
  const auto& diagnostic = Diagnostics().front();
  EXPECT_EQ(diagnostic.code, DiagnosticCode::kNotAScript);
  EXPECT_EQ(diagnostic.slot, SlotOf("load"));
  EXPECT_TRUE(diagnostic.help.has_value());
}

//! An error raised while the chunk runs is a runtime diagnostic of the load.
NOLINT_TEST_F(ScriptDiagnosticsTest, ChunkErrorIsRuntimeDiagnosticOfLoad)
{
  auto module = MakeModule();
  ASSERT_TRUE(Attach(module));
  auto scene = MakeScene("local x = nil\nreturn x.field\n", kLoadErrorHash);
  ASSERT_NE(scene, nullptr);
  engine::FrameContext frame;
  frame.SetScene(observer_ptr { scene.get() });

  RunSlotHooks(module, frame);

  ASSERT_EQ(Diagnostics().size(), 1U);
  const auto& diagnostic = Diagnostics().front();
  EXPECT_EQ(diagnostic.code, DiagnosticCode::kRuntimeError);
  const auto* const span = diagnostic.PrimarySpan();
  ASSERT_NE(span, nullptr);
  EXPECT_EQ(span->source_name, kScriptName);
  EXPECT_EQ(span->begin.line, 2U);
  EXPECT_EQ(diagnostic.slot, SlotOf("load"));
}

//! A removed handler receives nothing.
NOLINT_TEST_F(ScriptDiagnosticsTest, RemovedHandlerIsNotCalled)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));
  auto calls = 0;
  const auto id = module.AddDiagnosticHandler(
    [&calls](const ScriptDiagnostic& /*diagnostic*/) -> void { ++calls; });
  EXPECT_TRUE(module.RemoveDiagnosticHandler(id));
  EXPECT_FALSE(module.RemoveDiagnosticHandler(id));
  auto scene = MakeScene("return 1\n", kNotAScriptHash);
  ASSERT_NE(scene, nullptr);
  engine::FrameContext frame;
  frame.SetScene(observer_ptr { scene.get() });

  RunSlotHooks(module, frame);

  EXPECT_EQ(calls, 0);
}

} // namespace oxygen::scripting::test
