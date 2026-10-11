//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <Oxygen/Core/Scripting/ScriptExecutable.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Scene/Scripting/ScriptingComponent.h>
#include <Oxygen/Scene/Types/NodeHandle.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::data::AssetKey;
using oxygen::data::ScriptAsset;
using oxygen::data::pak::scripting::ScriptAssetDesc;
using oxygen::scene::ScriptingComponent;
using oxygen::scripting::ScriptExecutable;

auto MakeScriptAsset() -> std::shared_ptr<const ScriptAsset>
{
  ScriptAssetDesc desc {};
  return std::make_shared<ScriptAsset>(AssetKey {}, desc);
}

class CountingExecutable final : public ScriptExecutable {
public:
  explicit CountingExecutable(std::atomic<uint32_t>* run_count) noexcept
    : run_count_(run_count)
  {
  }

  auto Run() const noexcept -> void override
  {
    run_count_->fetch_add(1U, std::memory_order_relaxed);
  }

private:
  std::atomic<uint32_t>* run_count_ { nullptr };
};

class ScriptingComponentTest : public testing::Test {
protected:
  ScriptingComponent component_;
};

NOLINT_TEST_F(
  ScriptingComponentTest, AddSlot_StartsPendingWithNoDiagnosticsAndNoOpRun)
{
  component_.AddSlot(MakeScriptAsset());
  const auto slots = component_.Slots();

  ASSERT_EQ(slots.size(), 1U);
  EXPECT_EQ(slots.front().State(),
    ScriptingComponent::Slot::CompileState::kPendingCompilation);
  EXPECT_TRUE(slots.front().Diagnostics().empty());
  EXPECT_FALSE(slots.front().IsDisabled());
  EXPECT_NO_THROW(slots.front().Run());
}

//! A node parameter binds to one node; binding it again replaces the node,
//! and an unbound parameter has no node.
NOLINT_TEST_F(ScriptingComponentTest, BindNodeParameter_RecordsTheLatestNode)
{
  using oxygen::scene::NodeHandle;
  component_.AddSlot(MakeScriptAsset());
  const auto& slot = component_.Slots().front();

  EXPECT_TRUE(component_.BindNodeParameter(slot, "target", NodeHandle(3U)));
  EXPECT_TRUE(component_.BindNodeParameter(slot, "target", NodeHandle(5U)));

  const auto& bound = component_.Slots().front();
  EXPECT_EQ(bound.TryGetBoundNode("target"), NodeHandle(5U));
  EXPECT_EQ(bound.TryGetBoundNode("other"), std::nullopt);
}

//! A slot keeps the execution order it was added with.
NOLINT_TEST_F(ScriptingComponentTest, AddSlot_KeepsExecutionOrder)
{
  constexpr int32_t kOrder = 7;
  component_.AddSlot(MakeScriptAsset(), kOrder);
  component_.AddSlot(MakeScriptAsset());

  EXPECT_THAT(component_.Slots(),
    testing::ElementsAre(
      testing::Property(&ScriptingComponent::Slot::ExecutionOrder, kOrder),
      testing::Property(&ScriptingComponent::Slot::ExecutionOrder, 0)));
}

NOLINT_TEST_F(ScriptingComponentTest, MarkSlotReady_InstallsExecutableAndRuns)
{
  component_.AddSlot(MakeScriptAsset());
  ASSERT_EQ(component_.Slots().size(), 1U);

  std::atomic<uint32_t> run_count { 0U };
  const auto executable
    = std::make_shared<const CountingExecutable>(&run_count);

  EXPECT_TRUE(component_.MarkSlotReady(component_.Slots().front(), executable));
  EXPECT_EQ(component_.Slots().front().State(),
    ScriptingComponent::Slot::CompileState::kReady);
  EXPECT_FALSE(component_.Slots().front().IsDisabled());

  component_.Slots().front().Run();
  EXPECT_EQ(run_count.load(std::memory_order_relaxed), 1U);
}

NOLINT_TEST_F(
  ScriptingComponentTest, MarkSlotCompilationFailed_DisablesAndLogsOnlyOnce)
{
  component_.AddSlot(MakeScriptAsset());
  ASSERT_EQ(component_.Slots().size(), 1U);

  EXPECT_TRUE(component_.MarkSlotCompilationFailed(
    component_.Slots().front(), "syntax error"));
  EXPECT_EQ(component_.Slots().front().State(),
    ScriptingComponent::Slot::CompileState::kCompilationFailed);
  EXPECT_TRUE(component_.Slots().front().IsDisabled());
  ASSERT_EQ(component_.Slots().front().Diagnostics().size(), 1U);
  EXPECT_EQ(
    component_.Slots().front().Diagnostics().front().message, "syntax error");

  EXPECT_TRUE(component_.MarkSlotCompilationFailed(
    component_.Slots().front(), "new error"));
  ASSERT_EQ(component_.Slots().front().Diagnostics().size(), 1U);
  EXPECT_EQ(
    component_.Slots().front().Diagnostics().front().message, "syntax error");
  EXPECT_NO_THROW(component_.Slots().front().Run());
}

NOLINT_TEST_F(ScriptingComponentTest, MarkSlotReady_RejectsNullExecutable)
{
  component_.AddSlot(MakeScriptAsset());
  ASSERT_EQ(component_.Slots().size(), 1U);

  EXPECT_FALSE(component_.MarkSlotReady(component_.Slots().front(), nullptr));
  EXPECT_EQ(component_.Slots().front().State(),
    ScriptingComponent::Slot::CompileState::kPendingCompilation);
}

NOLINT_TEST_F(
  ScriptingComponentTest, CopiesAndClonesHaveFreshRuntimeIncarnations)
{
  component_.AddSlot(MakeScriptAsset());
  const auto original = component_.IncarnationId();
  EXPECT_TRUE(original.IsValidV7());
  ScriptingComponent copied(component_);
  EXPECT_NE(copied.IncarnationId(), original);
  EXPECT_TRUE(copied.IncarnationId().IsValidV7());
  EXPECT_EQ(copied.Slots().size(), component_.Slots().size());

  ScriptingComponent assigned;
  const auto previous = assigned.IncarnationId();
  assigned = component_;
  EXPECT_NE(assigned.IncarnationId(), previous);
  EXPECT_NE(assigned.IncarnationId(), original);
  EXPECT_EQ(assigned.Slots().size(), component_.Slots().size());

  const auto clone = component_.Clone();
  ASSERT_NE(clone, nullptr);
  ASSERT_EQ(clone->GetTypeId(), ScriptingComponent::ClassTypeId());
  // C++ RTTI is disabled; the engine's type ID was checked immediately above.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
  const auto& cloned = static_cast<const ScriptingComponent&>(*clone);
  EXPECT_NE(cloned.IncarnationId(), original);
  EXPECT_NE(cloned.IncarnationId(), copied.IncarnationId());
  EXPECT_TRUE(cloned.IncarnationId().IsValidV7());
  EXPECT_EQ(cloned.Slots().size(), component_.Slots().size());
}

NOLINT_TEST_F(ScriptingComponentTest, StorageMovesPreserveRuntimeIncarnation)
{
  component_.AddSlot(MakeScriptAsset());
  const auto original = component_.IncarnationId();
  ScriptingComponent moved(std::move(component_));
  EXPECT_EQ(moved.IncarnationId(), original);
  EXPECT_TRUE(component_.IncarnationId().IsNil());
  ASSERT_EQ(moved.Slots().size(), 1U);

  ScriptingComponent assigned;
  assigned = std::move(moved);
  EXPECT_EQ(assigned.IncarnationId(), original);
  // The runtime identity move contract explicitly leaves the source ID nil.
  // NOLINTNEXTLINE(bugprone-use-after-move)
  EXPECT_TRUE(moved.IncarnationId().IsNil());
  EXPECT_EQ(assigned.Slots().size(), 1U);
}

} // namespace
