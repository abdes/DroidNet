//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "ScriptingModule_test_fixture.h"

#include <string>

#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>

namespace oxygen::scripting::test {

class EventsBindingsTest : public ScriptingModuleTest { };

namespace {

  using oxygen::co::testing::TestEventLoop;

  auto RunSceneMutationPhase(
    ScriptingModule& module, observer_ptr<engine::FrameContext> context) -> void
  {
    TestEventLoop loop;
    oxygen::co::Run(
      loop, [&]() -> co::Co<> { co_await module.OnSceneMutation(context); });
  }

} // namespace

NOLINT_TEST_F(EventsBindingsTest, EventsBindingOnOnceEmitAndStatsWork)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto setup_result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
events_hits = 0
events_once_hits = 0
events_conn = oxygen.events.on("demo.tick", function(payload)
  if payload == nil or payload.value ~= 123 then
    error("bad payload")
  end
  events_hits = events_hits + 1
end, { phase = "frame_start", priority = 5 })

oxygen.events.once("demo.tick", function(payload)
  if payload == nil or payload.value ~= 123 then
    error("bad payload")
  end
  events_once_hits = events_once_hits + 1
end, { phase = "frame_start", priority = 10 })

function on_frame_start()
  oxygen.events.emit("demo.tick", { value = 123 })
end
)lua" },
    .chunk_name = ScriptChunkName { "events_bindings_setup" },
  });
  ASSERT_TRUE(setup_result.ok) << setup_result.message;

  engine::FrameContext context;
  module.OnFrameStart(observer_ptr<engine::FrameContext> { &context });
  module.OnFrameStart(observer_ptr<engine::FrameContext> { &context });
  EXPECT_FALSE(context.HasErrors());

  const auto validate_result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
if events_hits ~= 2 then
  error("events_hits mismatch")
end
if events_once_hits ~= 1 then
  error("events_once_hits mismatch")
end
if not events_conn:connected() then
  error("connection unexpectedly disconnected")
end

local listener_count = oxygen.events.listener_count("demo.tick")
if listener_count ~= 1 then
  error("listener_count mismatch")
end

local stats = oxygen.events.stats("demo.tick")
if stats.fired ~= 3 then
  error("stats.fired mismatch")
end
if stats.listeners ~= 1 then
  error("stats.listeners mismatch")
end
if stats.errors ~= 0 then
  error("stats.errors mismatch")
end
if stats.dropped ~= 0 then
  error("stats.dropped mismatch")
end

events_conn:disconnect()
if events_conn:connected() then
  error("connection should be disconnected")
end

local ok = pcall(function()
  oxygen.events.emit("frame.start")
end)
if ok then
  error("reserved event emit should fail")
end
)lua" },
    .chunk_name = ScriptChunkName { "events_bindings_validate" },
  });
  EXPECT_TRUE(validate_result.ok) << validate_result.message;
  EXPECT_EQ(validate_result.stage, "ok");
}

NOLINT_TEST_F(EventsBindingsTest, EventsBindingRejectsInvalidOptionsShape)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local ok_on = pcall(function()
  oxygen.events.on("demo.tick", function() end, "bad_options")
end)
if ok_on then
  error("events.on must reject non-table options")
end

local ok_emit = pcall(function()
  oxygen.events.emit("demo.tick", nil, "bad_options")
end)
if ok_emit then
  error("events.emit must reject non-table options")
end
)lua" },
    .chunk_name = ScriptChunkName { "events_invalid_options" },
  });

  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.stage, "ok");
}

NOLINT_TEST_F(EventsBindingsTest, EventsBindingRejectsInvalidConnectionReceiver)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local conn = oxygen.events.on("demo.tick", function() end)

local ok_connected = pcall(function()
  conn.connected("not_connection")
end)
if ok_connected then
  error("connected must reject invalid receiver")
end

local ok_disconnect = pcall(function()
  conn.disconnect("not_connection")
end)
if ok_disconnect then
  error("disconnect must reject invalid receiver")
end
)lua" },
    .chunk_name = ScriptChunkName { "events_invalid_connection_receiver" },
  });

  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.stage, "ok");
}

NOLINT_TEST_F(
  EventsBindingsTest, EventsBindingPhaseRoutingAndPriorityOrderAreDeterministic)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto setup_result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
frame_order = {}
frame_hits = 0
scene_hits = 0
end_hits = 0

oxygen.events.on("demo.frame", function()
  table.insert(frame_order, "p5_first")
  frame_hits = frame_hits + 1
end, { phase = "frame_start", priority = 5 })

oxygen.events.on("demo.frame", function()
  table.insert(frame_order, "p10")
  frame_hits = frame_hits + 1
end, { phase = "frame_start", priority = 10 })

oxygen.events.on("demo.frame", function()
  table.insert(frame_order, "p5_second")
  frame_hits = frame_hits + 1
end, { phase = "frame_start", priority = 5 })

oxygen.events.on("demo.scene", function()
  scene_hits = scene_hits + 1
end, { phase = "scene_mutation", priority = 1 })

oxygen.events.on("demo.end", function()
  end_hits = end_hits + 1
end, { phase = "frame_end", priority = 1 })

function on_frame_start()
  oxygen.events.emit("demo.frame")
end

function on_scene_mutation()
  oxygen.events.emit("demo.scene")
end

function on_frame_end()
  oxygen.events.emit("demo.end")
end
)lua" },
    .chunk_name = ScriptChunkName { "events_phase_order_setup" },
  });
  ASSERT_TRUE(setup_result.ok) << setup_result.message;

  engine::FrameContext context;
  module.OnFrameStart(observer_ptr<engine::FrameContext> { &context });
  RunSceneMutationPhase(
    module, observer_ptr<engine::FrameContext> { &context });
  module.OnFrameEnd(observer_ptr<engine::FrameContext> { &context });
  EXPECT_FALSE(context.HasErrors());

  const auto validate_result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
if frame_hits ~= 3 then
  error("frame_hits mismatch")
end
if scene_hits ~= 1 then
  error("scene_hits mismatch")
end
if end_hits ~= 1 then
  error("end_hits mismatch")
end

local expected = { "p10", "p5_first", "p5_second" }
for i = 1, #expected do
  if frame_order[i] ~= expected[i] then
    error("frame_order mismatch at index " .. tostring(i))
  end
end
)lua" },
    .chunk_name = ScriptChunkName { "events_phase_order_validate" },
  });
  EXPECT_TRUE(validate_result.ok) << validate_result.message;
  EXPECT_EQ(validate_result.stage, "ok");
}

NOLINT_TEST_F(
  EventsBindingsTest, EventsBindingDisconnectDuringDispatchSkipsLaterListener)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto setup_result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
disconnect_hits_a = 0
disconnect_hits_b = 0
disconnect_conn_b = nil

disconnect_conn_b = oxygen.events.on("demo.disconnect", function()
  disconnect_hits_b = disconnect_hits_b + 1
end, { phase = "frame_start", priority = 1 })

oxygen.events.on("demo.disconnect", function()
  disconnect_hits_a = disconnect_hits_a + 1
  disconnect_conn_b:disconnect()
end, { phase = "frame_start", priority = 10 })

function on_frame_start()
  oxygen.events.emit("demo.disconnect")
end
)lua" },
    .chunk_name = ScriptChunkName { "events_disconnect_setup" },
  });
  ASSERT_TRUE(setup_result.ok) << setup_result.message;

  engine::FrameContext context;
  module.OnFrameStart(observer_ptr<engine::FrameContext> { &context });
  EXPECT_FALSE(context.HasErrors());

  const auto validate_result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
if disconnect_hits_a ~= 1 then
  error("disconnect_hits_a mismatch")
end
if disconnect_hits_b ~= 0 then
  error("disconnect_hits_b should be zero")
end
if disconnect_conn_b:connected() then
  error("disconnect_conn_b should be disconnected")
end
)lua" },
    .chunk_name = ScriptChunkName { "events_disconnect_validate" },
  });
  EXPECT_TRUE(validate_result.ok) << validate_result.message;
  EXPECT_EQ(validate_result.stage, "ok");
}

NOLINT_TEST_F(
  EventsBindingsTest, EventsBindingListenerFailuresDoNotStopDispatch)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto setup_result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
error_dispatch_success_hits = 0

oxygen.events.on("demo.err", function()
  error("first listener failure")
end, { phase = "frame_start", priority = 20 })

oxygen.events.on("demo.err", function()
  error_dispatch_success_hits = error_dispatch_success_hits + 1
end, { phase = "frame_start", priority = 10 })

oxygen.events.on("demo.err", function()
  error("second listener failure")
end, { phase = "frame_start", priority = 0 })

function on_frame_start()
  oxygen.events.emit("demo.err")
end
)lua" },
    .chunk_name = ScriptChunkName { "events_error_dispatch_setup" },
  });
  ASSERT_TRUE(setup_result.ok) << setup_result.message;

  engine::FrameContext context;
  module.OnFrameStart(observer_ptr<engine::FrameContext> { &context });
  ASSERT_TRUE(context.HasErrors());
  const auto errors = context.GetErrors();
  ASSERT_FALSE(errors.empty());
  EXPECT_NE(
    errors.front().message.find("first listener failure"), std::string::npos);

  const auto validate_result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
if error_dispatch_success_hits ~= 1 then
  error("error_dispatch_success_hits mismatch")
end

local stats = oxygen.events.stats("demo.err")
if stats.fired ~= 1 then
  error("stats.fired mismatch")
end
if stats.errors ~= 2 then
  error("stats.errors mismatch")
end
)lua" },
    .chunk_name = ScriptChunkName { "events_error_dispatch_validate" },
  });
  EXPECT_TRUE(validate_result.ok) << validate_result.message;
  EXPECT_EQ(validate_result.stage, "ok");
}

NOLINT_TEST_F(EventsBindingsTest, EventsBindingStatsRemainConsistentUnderChurn)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto setup_result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local conns = {}
events_churn_hits = { 0, 0, 0, 0, 0 }

for i = 1, 5 do
  conns[i] = oxygen.events.on("demo.churn", function()
    events_churn_hits[i] = events_churn_hits[i] + 1
  end, { phase = "frame_start", priority = 10 - i })
end

conns[4]:disconnect()
conns[5]:disconnect()

function on_frame_start()
  oxygen.events.emit("demo.churn")
end
)lua" },
    .chunk_name = ScriptChunkName { "events_churn_setup" },
  });
  ASSERT_TRUE(setup_result.ok) << setup_result.message;

  engine::FrameContext context;
  module.OnFrameStart(observer_ptr<engine::FrameContext> { &context });
  module.OnFrameStart(observer_ptr<engine::FrameContext> { &context });
  module.OnFrameStart(observer_ptr<engine::FrameContext> { &context });
  EXPECT_FALSE(context.HasErrors());

  const auto validate_result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
for i = 1, 3 do
  if events_churn_hits[i] ~= 3 then
    error("connected listener hit mismatch at " .. tostring(i))
  end
end
for i = 4, 5 do
  if events_churn_hits[i] ~= 0 then
    error("disconnected listener should not be hit at " .. tostring(i))
  end
end

local stats = oxygen.events.stats("demo.churn")
if stats.fired ~= 9 then
  error("stats.fired mismatch")
end
if stats.errors ~= 0 then
  error("stats.errors mismatch")
end
if stats.listeners ~= 3 then
  error("stats.listeners mismatch")
end
)lua" },
    .chunk_name = ScriptChunkName { "events_churn_validate" },
  });

  EXPECT_TRUE(validate_result.ok) << validate_result.message;
  EXPECT_EQ(validate_result.stage, "ok");
}

NOLINT_TEST_F(EventsBindingsTest, EventsBindingReservedPrefixMatrixRejectsEmit)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local reserved = {
  "app.tick",
  "frame.start",
  "input.key",
  "scene.loaded",
  "render.draw",
  "physics.contact"
}

for i = 1, #reserved do
  local ok = pcall(function()
    oxygen.events.emit(reserved[i])
  end)
  if ok then
    error("reserved emit should fail for " .. reserved[i])
  end
end
)lua" },
    .chunk_name = ScriptChunkName { "events_reserved_prefix_matrix" },
  });

  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.stage, "ok");
}

NOLINT_TEST_F(
  EventsBindingsTest, EventsBindingUnknownEventStatsAndCountsReturnZeroes)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
if oxygen.events.listener_count("demo.unknown") ~= 0 then
  error("unknown event listener_count must be zero")
end

local stats = oxygen.events.stats("demo.unknown")
if stats.fired ~= 0 then
  error("unknown stats.fired must be zero")
end
if stats.listeners ~= 0 then
  error("unknown stats.listeners must be zero")
end
if stats.errors ~= 0 then
  error("unknown stats.errors must be zero")
end
if stats.dropped ~= 0 then
  error("unknown stats.dropped must be zero")
end
)lua" },
    .chunk_name = ScriptChunkName { "events_unknown_stats_zero" },
  });

  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.stage, "ok");
}

NOLINT_TEST_F(EventsBindingsTest, EventsBindingEmitOptionsCanRouteToOtherPhase)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto setup_result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
events_cross_phase_hits = 0

oxygen.events.on("demo.cross_phase", function()
  events_cross_phase_hits = events_cross_phase_hits + 1
end, { phase = "scene_mutation", priority = 1 })

function on_frame_start()
  oxygen.events.emit("demo.cross_phase", nil, { phase = "scene_mutation" })
end
)lua" },
    .chunk_name = ScriptChunkName { "events_cross_phase_setup" },
  });
  ASSERT_TRUE(setup_result.ok) << setup_result.message;

  engine::FrameContext context;
  module.OnFrameStart(observer_ptr<engine::FrameContext> { &context });
  EXPECT_FALSE(context.HasErrors());

  const auto before_scene_phase = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
if events_cross_phase_hits ~= 0 then
  error("event should not fire before scene mutation phase")
end
)lua" },
    .chunk_name = ScriptChunkName { "events_cross_phase_before_scene" },
  });
  ASSERT_TRUE(before_scene_phase.ok) << before_scene_phase.message;

  RunSceneMutationPhase(
    module, observer_ptr<engine::FrameContext> { &context });
  EXPECT_FALSE(context.HasErrors());

  const auto after_scene_phase = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
if events_cross_phase_hits ~= 1 then
  error("event should fire exactly once in scene mutation")
end
)lua" },
    .chunk_name = ScriptChunkName { "events_cross_phase_after_scene" },
  });
  EXPECT_TRUE(after_scene_phase.ok) << after_scene_phase.message;
  EXPECT_EQ(after_scene_phase.stage, "ok");
}

NOLINT_TEST_F(EventsBindingsTest, EventsBindingNestedEmitDefersToNextPhaseTick)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto setup_result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
events_nested_hits = 0

oxygen.events.on("demo.nested", function()
  events_nested_hits = events_nested_hits + 1
  if events_nested_hits == 1 then
    oxygen.events.emit("demo.nested")
  end
end, { phase = "frame_start", priority = 1 })

function on_frame_start()
  oxygen.events.emit("demo.nested")
end
)lua" },
    .chunk_name = ScriptChunkName { "events_nested_emit_setup" },
  });
  ASSERT_TRUE(setup_result.ok) << setup_result.message;

  engine::FrameContext context;
  module.OnFrameStart(observer_ptr<engine::FrameContext> { &context });
  EXPECT_FALSE(context.HasErrors());

  const auto after_first_tick = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
if events_nested_hits ~= 1 then
  error("nested emit must be deferred, expected 1 hit after first tick")
end
)lua" },
    .chunk_name = ScriptChunkName { "events_nested_after_first_tick" },
  });
  ASSERT_TRUE(after_first_tick.ok) << after_first_tick.message;

  module.OnFrameStart(observer_ptr<engine::FrameContext> { &context });
  EXPECT_FALSE(context.HasErrors());

  const auto after_second_tick = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
if events_nested_hits ~= 2 then
  error("expected deferred nested event to run on second tick")
end
)lua" },
    .chunk_name = ScriptChunkName { "events_nested_after_second_tick" },
  });
  EXPECT_TRUE(after_second_tick.ok) << after_second_tick.message;
  EXPECT_EQ(after_second_tick.stage, "ok");
}

NOLINT_TEST_F(EventsBindingsTest,
  EventsRuntimeShutdownIsIdempotentAndFreshModuleReinitializes)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto before_shutdown = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local conn = oxygen.events.on("demo.shutdown", function() end)
if not conn:connected() then
  error("connection should be connected before shutdown")
end
)lua" },
    .chunk_name = ScriptChunkName { "events_shutdown_before" },
  });
  ASSERT_TRUE(before_shutdown.ok) << before_shutdown.message;

  module.OnShutdown();
  module.OnShutdown();

  auto module_after = MakeModule();
  ASSERT_TRUE(AttachModule(module_after));

  const auto after_restart = module_after.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local conn = oxygen.events.on("demo.shutdown.reinit", function() end)
if not conn:connected() then
  error("connection should be connected after reinit")
end

oxygen.events.emit("demo.shutdown.reinit")
local stats = oxygen.events.stats("demo.shutdown.reinit")
if stats.listeners ~= 1 then
  error("stats.listeners mismatch after reinit")
end
)lua" },
    .chunk_name = ScriptChunkName { "events_shutdown_after" },
  });
  EXPECT_TRUE(after_restart.ok) << after_restart.message;
  EXPECT_EQ(after_restart.stage, "ok");
}

} // namespace oxygen::scripting::test
