//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <string_view>

#include <Oxygen/Renderer/Passes/ConventionalShadowRasterPass.h>
#include <Oxygen/Renderer/Passes/VirtualShadowPageRasterPass.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::engine::ConventionalShadowRasterPass;
using oxygen::engine::DepthPrePassConfig;
using oxygen::engine::VirtualShadowPageRasterPass;

NOLINT_TEST(PassDebugNamesTest,
  DerivedDepthRasterPassesUseTheirOwnPassNamesInsteadOfDepthPrePass)
{
  auto conventional_config = std::make_shared<DepthPrePassConfig>();
  auto virtual_config = std::make_shared<DepthPrePassConfig>();

  ConventionalShadowRasterPass conventional(conventional_config);
  VirtualShadowPageRasterPass virtual_shadow(virtual_config);

  EXPECT_EQ(conventional.GetName(),
    std::string_view { "ConventionalShadowRasterPass" });
  EXPECT_EQ(virtual_shadow.GetName(),
    std::string_view { "VirtualShadowPageRasterPass" });
}

} // namespace
