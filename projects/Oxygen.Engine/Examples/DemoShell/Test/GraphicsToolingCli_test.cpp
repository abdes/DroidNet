//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <string>

#include <Oxygen/Config/GraphicsConfig.h>
#include <Oxygen/Testing/GTest.h>

#include "Common/DemoCli.h"

namespace oxygen::examples::testing {

NOLINT_TEST(GraphicsToolingCli, DefaultsMatchBuildPolicy)
{
  const cli::GraphicsToolingCliState state {};

  EXPECT_EQ(
    state.enable_debug_layer, oxygen::DefaultGraphicsDebugLayerEnabled());
  EXPECT_EQ(state.enable_aftermath, oxygen::DefaultGraphicsAftermathEnabled());
}

NOLINT_TEST(GraphicsToolingCli, RejectsMutuallyExclusiveSelection)
{
  const cli::GraphicsToolingCliState state {
    .enable_debug_layer = true,
    .enable_aftermath = true,
  };

  try {
    cli::ValidateGraphicsToolingOptions(state);
    FAIL() << "Expected mutually exclusive graphics tooling options to fail";
  } catch (const cli::GraphicsToolingCliError& ex) {
    EXPECT_THAT(std::string(ex.what()),
      ::testing::HasSubstr(
        "--debug-layer and --aftermath are mutually exclusive"));
  }
}

NOLINT_TEST(GraphicsToolingCli, AcceptsSingleToolSelection)
{
  const cli::GraphicsToolingCliState state {
    .enable_debug_layer = false,
    .enable_aftermath = true,
  };

  EXPECT_NO_THROW(cli::ValidateGraphicsToolingOptions(state));
}

} // namespace oxygen::examples::testing
