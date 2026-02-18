//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <string>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Core/EngineTag.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Scripting/Module/ScriptingModule.h>

namespace oxygen::scripting::test {

using oxygen::core::MakePhaseMask;
using oxygen::core::PhaseId;
using oxygen::engine::ModuleTimingData;
using namespace std::chrono_literals;

class ScriptingModuleTest : public ::testing::Test {
protected:
  static constexpr auto kDefaultTestPriority = engine::ModulePriority { 100U };

  static auto MakeModule() -> ScriptingModule
  {
    return ScriptingModule { kDefaultTestPriority };
  }
};

NOLINT_TEST_F(ScriptingModuleTest, MetadataIsStable)
{
  auto module = MakeModule();

  EXPECT_EQ(module.GetName(), "ScriptingModule");
  EXPECT_EQ(module.GetPriority().get(), kDefaultTestPriority.get());
}

NOLINT_TEST_F(ScriptingModuleTest, PriorityIsInjectable)
{
  constexpr auto kCustomPriority = engine::ModulePriority { 120U };
  ScriptingModule module { kCustomPriority };

  EXPECT_EQ(module.GetPriority().get(), kCustomPriority.get());
}

NOLINT_TEST_F(
  ScriptingModuleTest, SupportedPhasesIncludeScriptingRelevantPhases)
{
  auto module = MakeModule();
  const auto mask = module.GetSupportedPhases();

  EXPECT_NE(mask & MakePhaseMask(PhaseId::kFrameStart), 0U);
  EXPECT_NE(mask & MakePhaseMask(PhaseId::kFixedSimulation), 0U);
  EXPECT_NE(mask & MakePhaseMask(PhaseId::kGameplay), 0U);
  EXPECT_NE(mask & MakePhaseMask(PhaseId::kSceneMutation), 0U);
  EXPECT_NE(mask & MakePhaseMask(PhaseId::kFrameEnd), 0U);
}

NOLINT_TEST_F(ScriptingModuleTest, AttachAndShutdownAreCallable)
{
  auto module = MakeModule();

  EXPECT_TRUE(module.OnAttached(observer_ptr<AsyncEngine> {}));
  module.OnShutdown();
  module.OnShutdown();
}

NOLINT_TEST_F(ScriptingModuleTest, PhaseHandlersAreInvocableAfterAttach)
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

NOLINT_TEST_F(ScriptingModuleTest, ExecuteScriptSucceedsForValidScript)
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

NOLINT_TEST_F(ScriptingModuleTest, ExecuteScriptFailsForSyntaxError)
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

NOLINT_TEST_F(
  ScriptingModuleTest, ExecuteScriptRuntimeErrorReturnsStructuredFailure)
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

NOLINT_TEST_F(ScriptingModuleTest, SandboxBlocksUnsafeGlobals)
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

NOLINT_TEST_F(
  ScriptingModuleTest, OnFrameStartHookErrorIsReportedToFrameContext)
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

NOLINT_TEST_F(ScriptingModuleTest, ExecuteScriptTimeBindingRequiresFrameContext)
{
  auto module = MakeModule();
  ASSERT_TRUE(module.OnAttached(observer_ptr<AsyncEngine> {}));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local _ = oxygen.time.delta_seconds()
)lua" },
    .chunk_name = ScriptChunkName { "time_no_context" },
  });
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.stage, "runtime");
  EXPECT_NE(
    result.message.find("requires active FrameContext"), std::string::npos);
}

NOLINT_TEST_F(ScriptingModuleTest, ExecuteScriptAppBindingRequiresAsyncEngine)
{
  auto module = MakeModule();
  ASSERT_TRUE(module.OnAttached(observer_ptr<AsyncEngine> {}));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local _ = oxygen.app.name()
)lua" },
    .chunk_name = ScriptChunkName { "app_no_engine" },
  });
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.stage, "runtime");
  EXPECT_NE(
    result.message.find("requires active AsyncEngine"), std::string::npos);
}

NOLINT_TEST_F(ScriptingModuleTest, ExecuteScriptLogBindingRequiresAsyncEngine)
{
  auto module = MakeModule();
  ASSERT_TRUE(module.OnAttached(observer_ptr<AsyncEngine> {}));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
oxygen.log.info("hello")
)lua" },
    .chunk_name = ScriptChunkName { "log_no_engine" },
  });
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.stage, "runtime");
  EXPECT_NE(
    result.message.find("requires active AsyncEngine"), std::string::npos);
}

NOLINT_TEST_F(
  ScriptingModuleTest, OnFrameStartTimeBindingUsesFrameContextValues)
{
  auto module = MakeModule();
  ASSERT_TRUE(module.OnAttached(observer_ptr<AsyncEngine> {}));

  const auto hook_result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
function on_frame_start()
  local dt = oxygen.time.delta_seconds()
  local fdt = oxygen.time.fixed_delta_seconds()
  local seq = oxygen.time.frame_sequence_number()
  local scale = oxygen.time.time_scale()
  local paused = oxygen.time.is_paused()
  local alpha = oxygen.time.interpolation_alpha()
  local fps = oxygen.time.current_fps()

  if math.abs(dt - 0.01) > 0.0001 then
    error("bad dt")
  end
  if math.abs(fdt - 0.02) > 0.0001 then
    error("bad fixed dt")
  end
  if seq ~= 42 then
    error("bad sequence")
  end
  if math.abs(scale - 1.5) > 0.0001 then
    error("bad scale")
  end
  if not paused then
    error("bad paused")
  end
  if math.abs(alpha - 0.25) > 0.0001 then
    error("bad alpha")
  end
  if math.abs(fps - 120.0) > 0.0001 then
    error("bad fps")
  end
end
)lua" },
    .chunk_name = ScriptChunkName { "time_context_values" },
  });
  ASSERT_TRUE(hook_result.ok);

  engine::FrameContext context;
  const auto tag = engine::internal::EngineTagFactory::Get();
  context.SetFrameSequenceNumber(frame::SequenceNumber { 42 }, tag);
  context.SetModuleTimingData(
    ModuleTimingData {
      .game_delta_time = time::CanonicalDuration { 10ms },
      .fixed_delta_time = time::CanonicalDuration { 20ms },
      .time_scale = 1.5f,
      .is_paused = true,
      .interpolation_alpha = 0.25f,
      .current_fps = 120.0f,
    },
    tag);

  module.OnFrameStart(observer_ptr<engine::FrameContext> { &context });
  EXPECT_FALSE(context.HasErrors());
}

} // namespace oxygen::scripting::test
