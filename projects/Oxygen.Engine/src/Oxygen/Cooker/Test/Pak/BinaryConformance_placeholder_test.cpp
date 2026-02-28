//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Cooker/Pak/PakBuilder.h>

namespace {

NOLINT_TEST(CookerPakBinaryConformancePhase1, PlaceholderRegistered)
{
  using oxygen::content::pak::BuildMode;
  const auto mode = BuildMode::kFull;
  EXPECT_EQ(mode, BuildMode::kFull);
  GTEST_SKIP()
    << "Phase 1 API complete; binary conformance tests land in Phase 8.";
}

} // namespace
