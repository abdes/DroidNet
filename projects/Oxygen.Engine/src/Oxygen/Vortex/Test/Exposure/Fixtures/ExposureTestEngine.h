//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Engine/IAsyncEngine.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::vortex::testing::exposure {

class ExposureTestEngine : public IAsyncEngine {
public:
  MOCK_METHOD(observer_ptr<content::IAssetLoader>, GetAssetLoader, (),
    (const, noexcept, override));
  MOCK_METHOD(scripting::IScriptCompilationService&,
    GetScriptCompilationService, (), (noexcept, override));
  MOCK_METHOD(const scripting::IScriptCompilationService&,
    GetScriptCompilationService, (), (const, noexcept, override));
  MOCK_METHOD(
    const PathFinder&, GetPathFinder, (), (const, noexcept, override));
  MOCK_METHOD(
    std::weak_ptr<Graphics>, GetGraphics, (), (const, noexcept, override));
  MOCK_METHOD(std::shared_ptr<Platform>, GetPlatformShared, (),
    (const, noexcept, override));
  MOCK_METHOD(
    const EngineConfig&, GetEngineConfig, (), (const, noexcept, override));
  MOCK_METHOD(console::Console&, GetConsole, (), (noexcept, override));
  MOCK_METHOD(
    const console::Console&, GetConsole, (), (const, noexcept, override));
  MOCK_METHOD(bool, IsRunning, (), (const, override));
  MOCK_METHOD(void, Stop, (), (override));
  MOCK_METHOD(ModuleSubscription, SubscribeModuleAttached,
    (engine::ModuleAttachedCallback, bool), (override));
  MOCK_METHOD(std::optional<std::reference_wrapper<engine::EngineModule>>,
    GetModuleByType, (TypeId), (const, noexcept, override));
};

} // namespace oxygen::vortex::testing::exposure
