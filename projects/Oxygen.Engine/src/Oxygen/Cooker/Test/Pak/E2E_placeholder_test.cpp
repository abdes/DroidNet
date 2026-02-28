//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Cooker/Pak/PakBuilder.h>

namespace {

NOLINT_TEST(CookerPakE2EPhase1, PlaceholderRegistered)
{
  using oxygen::content::pak::PakBuilder;
  PakBuilder builder;
  EXPECT_NE(&builder, nullptr);
  GTEST_SKIP() << "Phase 1 API complete; full E2E tests land in Phase 8.";
}

} // namespace
