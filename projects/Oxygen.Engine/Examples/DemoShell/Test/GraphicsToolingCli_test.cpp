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

NOLINT_TEST(DemoResolutionCli, ParsesPhysicalPixelDimensions)
{
  const auto full_hd = cli::ParseWindowResolution("1920x1080");
  EXPECT_EQ(full_hd.width, 1920U);
  EXPECT_EQ(full_hd.height, 1080U);
  const auto qhd = cli::ParseWindowResolution("2560X1440");
  EXPECT_EQ(qhd.width, 2560U);
  EXPECT_EQ(qhd.height, 1440U);
}

NOLINT_TEST(DemoResolutionCli, RejectsMalformedOrUnrepresentableDimensions)
{
  for (const auto* value : { "", "1920", "1920x", "x1080", "0x1080",
         "1920x0", "-1x1080", "2147483648x1080", "4294967296x1080",
         "1920x1080x60", "1920x1080extra" }) {
    SCOPED_TRACE(value);
    EXPECT_THROW(cli::ParseWindowResolution(value), std::invalid_argument);
  }
}

NOLINT_TEST(DemoResolutionCli, SharedRuntimeOptionResolvesAndRejectsHeadlessUse)
{
  std::string resolution;
  const clap::Command::Ptr command = clap::CommandBuilder(clap::Command::DEFAULT)
    .WithOptions(cli::MakeRuntimeOptions({ .resolution = &resolution }));
  auto parser = cli::BuildCli("resolution-test", "Resolution selection", command);
  const char* arguments[] = { "resolution-test", "--resolution", "1920x1080" };
  const auto context = parser->Parse(3, arguments);
  const auto extent = cli::ResolveWindowResolution(context, resolution, false);
  ASSERT_TRUE(extent.has_value());
  EXPECT_EQ(extent->width, 1920U);
  EXPECT_EQ(extent->height, 1080U);
  EXPECT_THROW(cli::ResolveWindowResolution(context, resolution, true),
    std::invalid_argument);
}

} // namespace oxygen::examples::testing
