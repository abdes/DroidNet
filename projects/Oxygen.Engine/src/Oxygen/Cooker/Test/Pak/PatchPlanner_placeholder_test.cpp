//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Cooker/Pak/PakBuilder.h>

namespace {

NOLINT_TEST(CookerPakPatchPlannerPhase1, PlaceholderRegistered)
{
  using oxygen::content::pak::PakDiagnosticSeverity;
  const auto severity = PakDiagnosticSeverity::kInfo;
  EXPECT_EQ(severity, PakDiagnosticSeverity::kInfo);
  GTEST_SKIP() << "Phase 1 API complete; patch planner tests land in Phase 8.";
}

} // namespace
