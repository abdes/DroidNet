//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Engine/IAsyncEngine.h>
#include <Oxygen/Scripting/Module/ScriptingModule.h>

auto main(int /*argc*/, char** /*argv*/) -> int
{
  constexpr auto kTestPriority = oxygen::engine::ModulePriority { 100U };
  oxygen::scripting::ScriptingModule module { kTestPriority };
  const auto attached
    = module.OnAttached(oxygen::observer_ptr<oxygen::IAsyncEngine> {});
  if (attached) {
    module.OnShutdown();
  }
  return 0;
}
