//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Scripting/Module/ScriptingModule.h>
#include <Oxygen/Scripting/Test/Fakes/FakeAsyncEngine.h>

auto main(int /*argc*/, char** /*argv*/) -> int
{
  constexpr auto kTestPriority = oxygen::engine::ModulePriority { 100U };
  oxygen::scripting::ScriptingModule module { kTestPriority };
  oxygen::scripting::test::FakeAsyncEngine fake_engine;
  const auto attached = module.OnAttached(
    oxygen::observer_ptr<oxygen::IAsyncEngine> { &fake_engine });
  if (attached) {
    module.OnShutdown();
  }
  return 0;
}
