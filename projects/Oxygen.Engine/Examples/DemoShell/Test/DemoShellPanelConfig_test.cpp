//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include "DemoShell/DemoShell.h"
#include "DemoShell/UI/DemoShellUi.h"

namespace oxygen::examples::testing {

NOLINT_TEST(DemoShellPanelConfig, EnablesGroundGridByDefault)
{
  const DemoShellPanelConfig config {};

  EXPECT_TRUE(config.ground_grid);
}

NOLINT_TEST(DemoShellPanelConfig,
  RuntimeConfigKeepsGroundGridWhenRendererBoundPanelsAreDisabled)
{
  DemoShellPanelConfig config {};
  config.lighting = true;
  config.ground_grid = true;

  const auto runtime_config
    = ui::MakeRuntimePanelConfig(config, false);

  EXPECT_FALSE(runtime_config.lighting);
  EXPECT_TRUE(runtime_config.ground_grid);
}

} // namespace oxygen::examples::testing
