//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "ScriptingModule_test_fixture.h"

namespace oxygen::scripting::test {

NOLINT_TEST_F(ScriptingModuleTest, AttachAndShutdownAreCallable)
{
  auto module = MakeModule();

  EXPECT_TRUE(module.OnAttached(observer_ptr<IAsyncEngine> {}));
  module.OnShutdown();
  module.OnShutdown();
}

NOLINT_TEST_F(ScriptingModuleTest, PhaseHandlersAreInvocableAfterAttach)
{
  auto module = MakeModule();
  ASSERT_TRUE(module.OnAttached(observer_ptr<IAsyncEngine> {}));

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

} // namespace oxygen::scripting::test
