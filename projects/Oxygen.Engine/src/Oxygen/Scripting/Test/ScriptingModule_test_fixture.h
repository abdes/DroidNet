//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

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

} // namespace oxygen::scripting::test
