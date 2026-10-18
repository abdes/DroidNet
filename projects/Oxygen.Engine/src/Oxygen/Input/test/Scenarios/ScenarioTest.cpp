//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/EngineTag.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/ActionTriggers.h>
#include <Oxygen/Input/ActionValue.h>
#include <Oxygen/Input/InputActionMapping.h>
#include <Oxygen/Input/InputMappingContext.h>
#include <Oxygen/Input/test/InputTestOwner.h>
#include <Oxygen/Input/test/Scenarios/ScenarioTest.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Platform/Input.h>

using oxygen::input::MakeCodeAction;
using oxygen::input::MakeCodeContext;
using oxygen::input::testing::kTestInputOwner;

namespace oxygen::engine::internal {
struct EngineTagFactory {
  static auto Get() noexcept -> EngineTag { return EngineTag {}; }
};
} // namespace oxygen::engine::internal

namespace oxygen::input::testing {

auto ScenarioTest::Frame(const FrameTime dt) -> co::Co<>
{
  engine::ModuleTimingData timing;
  timing.game_delta_time = time::CanonicalDuration { dt.game };
  timing.physical_delta_time = time::CanonicalDuration { dt.physical };
  Context()->SetModuleTimingData(
    timing, engine::internal::EngineTagFactory::Get());
  System().OnFrameStart(Context());
  co_await System().OnInput(Context());
}

auto ScenarioTest::MakeAction(const std::string& name, ActionValueType type)
  -> std::shared_ptr<Action>
{
  auto action = MakeCodeAction(kTestInputOwner, name, type);
  System().AddAction(action);
  return action;
}

auto ScenarioTest::MakeContext(const std::string& name, const int32_t priority)
  -> std::shared_ptr<InputMappingContext>
{
  auto context = MakeCodeContext(kTestInputOwner, name);
  System().AddMappingContext(context, priority);
  System().ActivateMappingContext(context);
  return context;
}

auto ScenarioTest::Map(InputMappingContext& context,
  const std::shared_ptr<Action>& action, const platform::InputSlot& slot,
  std::shared_ptr<ActionTrigger> trigger) -> std::shared_ptr<InputActionMapping>
{
  auto mapping = std::make_shared<InputActionMapping>(action, slot);
  mapping->AddTrigger(std::move(trigger));
  context.AddMapping(mapping);
  return mapping;
}

} // namespace oxygen::input::testing
