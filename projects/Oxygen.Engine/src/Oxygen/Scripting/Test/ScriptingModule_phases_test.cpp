//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "ScriptingModule_test_fixture.h"

namespace oxygen::scripting::test {

NOLINT_TEST_F(
  ScriptingModuleTest, SupportedPhasesIncludeScriptingRelevantPhases)
{
  auto module = MakeModule();
  ASSERT_TRUE(AttachModule(module));
  const auto mask = module.GetSupportedPhases();

  EXPECT_NE(mask & MakePhaseMask(PhaseId::kFrameStart), 0U);
  EXPECT_NE(mask & MakePhaseMask(PhaseId::kFixedSimulation), 0U);
  EXPECT_NE(mask & MakePhaseMask(PhaseId::kGameplay), 0U);
  EXPECT_NE(mask & MakePhaseMask(PhaseId::kSceneMutation), 0U);
  EXPECT_NE(mask & MakePhaseMask(PhaseId::kFrameEnd), 0U);
}

} // namespace oxygen::scripting::test
