//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "ScriptingModule_test_fixture.h"

namespace oxygen::scripting::test {

class EventsBindingsTest : public ScriptingModuleTest { };

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

} // namespace oxygen::scripting::test
