//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <chrono>
#include <memory>
#include <string_view>
#include <vector>

#include "ScriptingModule_test_fixture.h"
#include <fmt/format.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Content/test/Fakes/StubAssetLoader.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Core/Time/Types.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/InputMappingContextAsset.h>
#include <Oxygen/Data/PakFormat_input.h>
#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/ActionTriggers.h>
#include <Oxygen/Input/ActionValue.h>
#include <Oxygen/Input/InputActionMapping.h>
#include <Oxygen/Input/InputMappingContext.h>
#include <Oxygen/Input/InputSnapshot.h>
#include <Oxygen/Input/InputSystem.h>
#include <Oxygen/OxCo/BroadcastChannel.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/test/Utils/TestEventLoop.h>
#include <Oxygen/Platform/Input.h>
#include <Oxygen/Platform/InputEvent.h>
#include <Oxygen/Platform/Types.h>
#include <Oxygen/Scripting/Module/ScriptingModule.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::scripting::test {

class InputBindingsTest : public ScriptingModuleTest { };

NOLINT_TEST_F(InputBindingsTest, InputBindingsRegisterActionListeners)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local speed_up = "0f1e2d3c-4b5a-6978-8796-a5b4c3d2e1f0"
local event_name = oxygen.input.event_name(speed_up, oxygen.input.edges.triggered)
if event_name ~= "input.action.0f1e2d3c-4b5a-6978-8796-a5b4c3d2e1f0.triggered" then
  error("unexpected input event name")
end

local conn = oxygen.input.on_action(speed_up, oxygen.input.edges.triggered, function() end)
if conn == nil then
  error("expected connection object")
end
if not conn:connected() then
  error("expected connected listener")
end
conn:disconnect()
if conn:connected() then
  error("expected disconnected listener")
end

local ok = pcall(function()
  oxygen.input.on_action(speed_up, "invalid_edge", function() end)
end)
if ok then
  error("expected invalid edge registration failure")
end

ok = pcall(function()
  oxygen.input.on_action("RotateSpeedUpAction", oxygen.input.edges.triggered, function() end)
end)
if ok then
  error("expected an action name to be rejected: actions are asset references")
end
)lua" },
    .chunk_name = ScriptChunkName { "input_bindings_test" },
  });

  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.stage, "ok");
}

NOLINT_TEST_F(
  InputBindingsTest, InputBindingsOnceActionWithOptionsRegistersConnection)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
input_once_conn = oxygen.input.once_action(
  "1a2b3c4d-5e6f-7081-92a3-b4c5d6e7f809",
  oxygen.input.edges.triggered,
  function(_) end,
  { phase = "frame_start", priority = 11 }
)

if input_once_conn == nil then
  error("once_action should return connection")
end
if not input_once_conn:connected() then
  error("once_action connection should be connected after registration")
end

input_once_conn:disconnect()
if input_once_conn:connected() then
  error("once_action connection should disconnect cleanly")
end
)lua" },
    .chunk_name = ScriptChunkName { "input_once_register_connection" },
  });

  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.stage, "ok");
}

NOLINT_TEST_F(InputBindingsTest, InputBindingsEventNameRejectsInvalidShapes)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local ok = pcall(function()
  oxygen.input.event_name(123, oxygen.input.edges.triggered)
end)
if ok then
  error("event_name should reject non-string action")
end

ok = pcall(function()
  oxygen.input.event_name("1a2b3c4d-5e6f-7081-92a3-b4c5d6e7f809", 123)
end)
if ok then
  error("event_name should reject non-string edge")
end

ok = pcall(function()
  oxygen.input.event_name("", oxygen.input.edges.triggered)
end)
if ok then
  error("event_name should reject empty action")
end

ok = pcall(function()
  oxygen.input.event_name("1a2b3c4d-5e6f-7081-92a3-b4c5d6e7f809", "bad_edge")
end)
if ok then
  error("event_name should reject unsupported edge")
end
)lua" },
    .chunk_name = ScriptChunkName { "input_event_name_invalid_shapes" },
  });

  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.stage, "ok");
}

NOLINT_TEST_F(InputBindingsTest, InputBindingsOnAndOnceRejectInvalidShapes)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
local fn = function() end
local ok = pcall(function()
  oxygen.input.on_action(77, oxygen.input.edges.triggered, fn)
end)
if ok then
  error("on_action should reject non-string action")
end

ok = pcall(function()
  oxygen.input.on_action("1a2b3c4d-5e6f-7081-92a3-b4c5d6e7f809", 77, fn)
end)
if ok then
  error("on_action should reject non-string edge")
end

ok = pcall(function()
  oxygen.input.on_action("1a2b3c4d-5e6f-7081-92a3-b4c5d6e7f809", oxygen.input.edges.triggered, "bad")
end)
if ok then
  error("on_action should reject non-function callback")
end

ok = pcall(function()
  oxygen.input.on_action("1a2b3c4d-5e6f-7081-92a3-b4c5d6e7f809", "bad_edge", fn)
end)
if ok then
  error("on_action should reject unsupported edge")
end

ok = pcall(function()
  oxygen.input.on_action(
    "1a2b3c4d-5e6f-7081-92a3-b4c5d6e7f809",
    oxygen.input.edges.triggered,
    fn,
    "bad_options"
  )
end)
if ok then
  error("on_action should reject non-table options")
end

ok = pcall(function()
  oxygen.input.once_action(
    "1a2b3c4d-5e6f-7081-92a3-b4c5d6e7f809",
    oxygen.input.edges.triggered,
    fn,
    "bad_options"
  )
end)
if ok then
  error("once_action should reject non-table options")
end
)lua" },
    .chunk_name = ScriptChunkName { "input_on_once_invalid_shapes" },
  });

  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_EQ(result.stage, "ok");
}

//! Two actions named Jump in different folders are distinct assets: a
//! script's on_action on each fires only for its own binding.
NOLINT_TEST_F(InputBindingsTest, OnActionFiresOnlyForItsOwnActionAsset)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  const auto key_a = data::AssetKey::FromVirtualPath("/Content/A/Jump.oiact");
  const auto key_b = data::AssetKey::FromVirtualPath("/Content/B/Jump.oiact");
  const auto registered = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { fmt::format(R"lua(
jump_a_count = 0
jump_b_count = 0
oxygen.input.on_action("{}", oxygen.input.edges.triggered, function()
  jump_a_count = jump_a_count + 1
end)
oxygen.input.on_action("{}", oxygen.input.edges.triggered, function()
  jump_b_count = jump_b_count + 1
end)
)lua",
      data::to_string(key_a), data::to_string(key_b)) },
    .chunk_name = ScriptChunkName { "input_on_action_by_asset" },
  });
  ASSERT_TRUE(registered.ok) << registered.message;

  // Both actions are labelled Jump; only A's binding (Space) is pressed.
  using platform::InputSlots;
  using platform::input::KeyInfo;
  const auto jump_a = std::make_shared<oxygen::input::Action>(
    "Jump", oxygen::input::ActionValueType::kBool, key_a);
  const auto jump_b = std::make_shared<oxygen::input::Action>(
    "Jump", oxygen::input::ActionValueType::kBool, key_b);
  oxygen::input::InputMappingContext gameplay("gameplay");
  const auto bind
    = [&gameplay](const std::shared_ptr<oxygen::input::Action>& action,
        const platform::InputSlot& slot) -> void {
    auto mapping
      = std::make_shared<oxygen::input::InputActionMapping>(action, slot);
    auto trigger = std::make_shared<oxygen::input::ActionTriggerPressed>();
    trigger->MakeExplicit();
    mapping->AddTrigger(trigger);
    gameplay.AddMapping(mapping);
  };
  bind(jump_a, InputSlots::Space);
  bind(jump_b, InputSlots::Return);
  const platform::KeyEvent space(
    time::PhysicalTime { std::chrono::steady_clock::now() },
    platform::kInvalidWindowId, KeyInfo(platform::Key::kSpace, false),
    platform::ButtonState::kPressed);
  gameplay.HandleInput(InputSlots::Space, space);
  static_cast<void>(gameplay.Update(time::CanonicalDuration {}));
  ASSERT_TRUE(jump_a->IsTriggered());
  ASSERT_FALSE(jump_b->IsTriggered());

  // The input system publishes the snapshot during kInput; scripts see it
  // in kGameplay.
  engine::FrameContext context;
  context.SetCurrentPhase(
    core::PhaseId::kInput, engine::internal::EngineTagFactory::Get());
  context.SetInputSnapshot(std::make_shared<const oxygen::input::InputSnapshot>(
                             std::vector { jump_a, jump_b }),
    engine::internal::EngineTagFactory::Get());
  context.SetCurrentPhase(
    core::PhaseId::kGameplay, engine::internal::EngineTagFactory::Get());
  co::testing::TestEventLoop loop;
  co::Run(loop, [&module, &context] -> co::Co<> {
    co_await module.OnGameplay(observer_ptr { &context });
  });

  const auto checked = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { R"lua(
if jump_a_count ~= 1 then
  error("A's handler fired " .. jump_a_count .. " times, expected 1")
end
if jump_b_count ~= 0 then
  error("B's handler fired " .. jump_b_count .. " times, expected 0")
end
)lua" },
    .chunk_name = ScriptChunkName { "input_on_action_by_asset_check" },
  });
  EXPECT_TRUE(checked.ok) << checked.message;
}

namespace {
  //! Serves one cached context and loads another on request.
  class ContextLoader final : public content::testing::StubAssetLoader {
  public:
    ContextLoader(data::AssetKey cached, data::AssetKey loadable)
      : cached_(MakeContext(cached, "Cached"))
      , loadable_(MakeContext(loadable, "Loadable"))
    {
    }

    [[nodiscard]] auto GetInputMappingContextAsset(const data::AssetKey& key)
      -> std::shared_ptr<data::InputMappingContextAsset> override
    {
      return key == cached_->GetAssetKey() ? cached_ : nullptr;
    }

    void StartLoadInputMappingContextAsset(const data::AssetKey& key,
      InputMappingContextCallback on_complete) override
    {
      on_complete(key == loadable_->GetAssetKey() ? loadable_ : nullptr);
    }

  private:
    static auto MakeContext(const data::AssetKey& key, std::string_view name)
      -> std::shared_ptr<data::InputMappingContextAsset>
    {
      auto context = std::make_shared<data::InputMappingContextAsset>(
        key, data::pak::input::InputMappingContextAssetDesc {});
      context->SetAssetName(std::string { name });
      return context;
    }

    std::shared_ptr<data::InputMappingContextAsset> cached_;
    std::shared_ptr<data::InputMappingContextAsset> loadable_;
  };
} // namespace

//! activate_context starts a loaded context by asset reference and refuses
//! one that is not loaded; deactivate_context needs an added context.
NOLINT_TEST_F(InputBindingsTest, ContextsSwitchByAssetReference)
{
  const auto cached = data::AssetKey::FromVirtualPath("/Content/Menu.oimap");
  const auto loadable = data::AssetKey::FromVirtualPath("/Content/Fly.oimap");
  auto loader = ContextLoader(cached, loadable);
  co::BroadcastChannel<platform::InputEvent> channel;
  engine::InputSystem input(channel.ForRead());
  FakeEngine().AddModule(input);
  FakeEngine().SetAssetLoader(observer_ptr<content::IAssetLoader> { &loader });

  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));
  const auto result = module.ExecuteScript(ScriptExecutionRequest {
    .source_text = ScriptSourceText { fmt::format(R"lua(
local input = oxygen.input
assert(input.activate_context("{0}"), "a loaded context starts")
assert(input.deactivate_context("{0}"), "an added context stops")
assert(input.activate_context("{0}"), "an added context restarts")
assert(not input.activate_context("{1}"), "a context not loaded is refused")
assert(not input.deactivate_context("{2}"), "an unknown context is not stopped")
assert(not pcall(input.activate_context, "Menu"), "a name is not a context")
)lua",
      data::to_string(cached), data::to_string(loadable),
      data::to_string(
        data::AssetKey::FromVirtualPath("/Content/None.oimap"))) },
    .chunk_name = ScriptChunkName { "input_context_switching" },
  });

  EXPECT_TRUE(result.ok) << result.message;
  EXPECT_NE(input.GetMappingContextByKey(cached), nullptr);
  EXPECT_EQ(input.GetMappingContextByKey(loadable), nullptr);
}

} // namespace oxygen::scripting::test
