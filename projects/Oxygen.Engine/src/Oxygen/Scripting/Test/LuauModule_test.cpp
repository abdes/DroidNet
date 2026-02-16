//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <string>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Scripting/Module/LuauModule.h>

namespace oxygen::engine::internal {
auto EngineTagFactory::Get() noexcept -> EngineTag { return {}; }
} // namespace oxygen::engine::internal

namespace oxygen::scripting::test {

using oxygen::core::MakePhaseMask;
using oxygen::core::PhaseId;
using oxygen::engine::ModuleTimingData;
using oxygen::engine::internal::EngineTagFactory;
using namespace std::chrono_literals;

class LuauModuleTest : public ::testing::Test {
protected:
  static constexpr auto kDefaultTestPriority = engine::ModulePriority { 100U };

  static auto MakeModule() -> LuauModule
  {
    return LuauModule { kDefaultTestPriority };
  }
};

NOLINT_TEST_F(LuauModuleTest, MetadataIsStable)
{
  auto module = MakeModule();

  EXPECT_EQ(module.GetName(), "LuauModule");
  EXPECT_EQ(module.GetPriority().get(), kDefaultTestPriority.get());
}

NOLINT_TEST_F(LuauModuleTest, PriorityIsInjectable)
{
  constexpr auto kCustomPriority = engine::ModulePriority { 120U };
  LuauModule module { kCustomPriority };

  EXPECT_EQ(module.GetPriority().get(), kCustomPriority.get());
}

NOLINT_TEST_F(LuauModuleTest, SupportedPhasesIncludeScriptingRelevantPhases)
{
  auto module = MakeModule();
  const auto mask = module.GetSupportedPhases();

  EXPECT_NE(mask & MakePhaseMask(PhaseId::kFrameStart), 0U);
  EXPECT_NE(mask & MakePhaseMask(PhaseId::kFixedSimulation), 0U);
  EXPECT_NE(mask & MakePhaseMask(PhaseId::kGameplay), 0U);
  EXPECT_NE(mask & MakePhaseMask(PhaseId::kSceneMutation), 0U);
  EXPECT_NE(mask & MakePhaseMask(PhaseId::kFrameEnd), 0U);
}

NOLINT_TEST_F(LuauModuleTest, AttachAndShutdownAreCallable)
{
  auto module = MakeModule();

  EXPECT_TRUE(module.OnAttached(observer_ptr<AsyncEngine> {}));
  module.OnShutdown();
  module.OnShutdown();
}

NOLINT_TEST_F(LuauModuleTest, PhaseHandlersAreInvocableAfterAttach)
{
  auto module = MakeModule();
  ASSERT_TRUE(module.OnAttached(observer_ptr<AsyncEngine> {}));

  module.OnFrameStart(observer_ptr<engine::FrameContext> {});
  auto fixed = module.OnFixedSimulation(observer_ptr<engine::FrameContext> {});
  auto gameplay = module.OnGameplay(observer_ptr<engine::FrameContext> {});
  auto scene = module.OnSceneMutation(observer_ptr<engine::FrameContext> {});
  module.OnFrameEnd(observer_ptr<engine::FrameContext> {});
  module.OnShutdown();

  EXPECT_TRUE(fixed.IsValid());
  EXPECT_TRUE(gameplay.IsValid());
  EXPECT_TRUE(scene.IsValid());
}

NOLINT_TEST_F(LuauModuleTest, ExecuteScriptSucceedsForValidScript)
{
  auto module = MakeModule();
  ASSERT_TRUE(module.OnAttached(observer_ptr<AsyncEngine> {}));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
value = 41
value = value + 1
)lua" },
    .chunk_name = ScriptChunkName { "valid_script" },
  });
  EXPECT_TRUE(result.ok);
  EXPECT_EQ(result.stage, "ok");
  EXPECT_TRUE(result.message.empty());
}

NOLINT_TEST_F(LuauModuleTest, ExecuteScriptFailsForSyntaxError)
{
  auto module = MakeModule();
  ASSERT_TRUE(module.OnAttached(observer_ptr<AsyncEngine> {}));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
function bad_syntax(
)lua" },
    .chunk_name = ScriptChunkName { "syntax_error_script" },
  });
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.stage, "compile_or_load");
  EXPECT_FALSE(result.message.empty());
}

NOLINT_TEST_F(LuauModuleTest, ExecuteScriptRuntimeErrorReturnsStructuredFailure)
{
  auto module = MakeModule();
  ASSERT_TRUE(module.OnAttached(observer_ptr<AsyncEngine> {}));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
error("boom from script")
)lua" },
    .chunk_name = ScriptChunkName { "runtime_error_script" },
  });
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.stage, "runtime");
  EXPECT_NE(result.message.find("boom from script"), std::string::npos);
}

NOLINT_TEST_F(LuauModuleTest, SandboxBlocksUnsafeGlobals)
{
  auto module = MakeModule();
  ASSERT_TRUE(module.OnAttached(observer_ptr<AsyncEngine> {}));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
if os ~= nil then
  error("os should be sandboxed")
end
)lua" },
    .chunk_name = ScriptChunkName { "sandbox_check" },
  });
  EXPECT_TRUE(result.ok);
}

NOLINT_TEST_F(LuauModuleTest, OnFrameStartHookErrorIsReportedToFrameContext)
{
  auto module = MakeModule();
  ASSERT_TRUE(module.OnAttached(observer_ptr<AsyncEngine> {}));

  const auto hook_result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
function on_frame_start()
  error("frame start failure")
end
)lua" },
    .chunk_name = ScriptChunkName { "hook_script" },
  });
  ASSERT_TRUE(hook_result.ok);

  engine::FrameContext context;
  module.OnFrameStart(observer_ptr<engine::FrameContext> { &context });
  EXPECT_TRUE(context.HasErrors());

  const auto errors = context.GetErrors();
  ASSERT_FALSE(errors.empty());
  EXPECT_NE(errors.front().message.find("on_frame_start"), std::string::npos);
  EXPECT_NE(
    errors.front().message.find("frame start failure"), std::string::npos);
}

} // namespace oxygen::scripting::test
