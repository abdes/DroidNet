//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Core/EngineTag.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Scripting/Module/ScriptingModule.h>
#include <Oxygen/Scripting/Test/Fakes/FakeAsyncEngine.h>

namespace oxygen::engine::internal {
struct EngineTagFactory {
  static auto Get() noexcept -> EngineTag { return EngineTag {}; }
};
} // namespace oxygen::engine::internal

namespace oxygen::scripting::test {

using oxygen::core::MakePhaseMask;
using oxygen::core::PhaseId;
using oxygen::engine::ModuleTimingData;
using namespace std::chrono_literals;

class ScriptingModuleTest : public ::testing::Test {
public:
  static constexpr size_t kDefaultSceneCapacity = 1024;

protected:
  static constexpr auto kDefaultTestPriority = engine::ModulePriority { 100U };

  using Tag = oxygen::engine::internal::EngineTagFactory;

  static auto MakeModule() -> ScriptingModule
  {
    return ScriptingModule { kDefaultTestPriority };
  }

  auto AttachModule(ScriptingModule& module) -> bool
  {
    return module.OnAttached(observer_ptr { &fake_engine_ });
  }

  [[nodiscard]] auto FakeEngine() noexcept -> FakeAsyncEngine&
  {
    return fake_engine_;
  }

  [[nodiscard]] auto FakeEngine() const noexcept -> const FakeAsyncEngine&
  {
    return fake_engine_;
  }

private:
  FakeAsyncEngine fake_engine_;
};

} // namespace oxygen::scripting::test
