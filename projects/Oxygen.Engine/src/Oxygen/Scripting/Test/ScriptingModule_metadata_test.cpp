//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "ScriptingModule_test_fixture.h"

namespace oxygen::scripting::test {

NOLINT_TEST_F(ScriptingModuleTest, MetadataIsStable)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));

  EXPECT_EQ(module.GetName(), "ScriptingModule");
  EXPECT_EQ(module.GetPriority().get(), kDefaultTestPriority.get());
}

NOLINT_TEST_F(ScriptingModuleTest, PriorityIsInjectable)
{
  constexpr auto kCustomPriority = engine::ModulePriority { 120U };
  ScriptingModule module { kCustomPriority };
  ASSERT_TRUE(AttachModule(module));

  EXPECT_EQ(module.GetPriority().get(), kCustomPriority.get());
}

} // namespace oxygen::scripting::test
