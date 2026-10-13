//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <set>
#include <string>

#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Diagnostics/ShaderDebugModeRegistry.h>
#include <Oxygen/Vortex/Diagnostics/ViewModeCatalog.h>
#include <Oxygen/Vortex/RenderMode.h>
#include <Oxygen/Vortex/ShaderDebugMode.h>

namespace {

using oxygen::vortex::EnumerateShaderDebugModes;
using oxygen::vortex::EnumerateViewModes;
using oxygen::vortex::FindViewModeInfo;
using oxygen::vortex::RenderMode;
using oxygen::vortex::ShaderDebugMode;
using oxygen::vortex::ShaderDebugModeFamily;
using oxygen::vortex::ViewModeInfo;

NOLINT_TEST(ViewModeCatalogTest, StartsWithTheCommonModesInEditorOrder)
{
  const auto modes = EnumerateViewModes();
  const auto expected = std::array {
    "lit",
    "unlit",
    "wireframe",
    "lit-wireframe",
    "direct-lighting",
    "indirect-lighting",
    "world-normals",
    "roughness",
    "metalness",
    "linear-depth",
    "shadow-mask",
  };
  ASSERT_GE(modes.size(), expected.size());
  for (std::size_t i = 0; i < expected.size(); ++i) {
    EXPECT_EQ(modes[i].canonical_name, expected.at(i));
    EXPECT_TRUE(modes[i].IsCommon()) << expected.at(i);
  }
  EXPECT_FALSE(modes[expected.size()].IsCommon());
}

NOLINT_TEST(ViewModeCatalogTest, ListsEveryDebugModeOnceExceptDisabled)
{
  for (const auto& debug : EnumerateShaderDebugModes()) {
    const auto count = std::ranges::count(EnumerateViewModes(),
      std::optional { debug.mode }, &ViewModeInfo::shader_debug_mode);
    const auto expected = debug.mode == ShaderDebugMode::kDisabled ? 0 : 1;
    EXPECT_EQ(count, expected) << debug.canonical_name;
  }
}

NOLINT_TEST(ViewModeCatalogTest, UnsupportedModesCarryTheirReason)
{
  bool any_unsupported = false;
  for (const auto& mode : EnumerateViewModes()) {
    if (mode.supported) {
      EXPECT_TRUE(mode.unsupported_reason.empty()) << mode.canonical_name;
      continue;
    }
    any_unsupported = true;
    EXPECT_FALSE(mode.unsupported_reason.empty()) << mode.canonical_name;
  }
  EXPECT_TRUE(any_unsupported);
}

NOLINT_TEST(ViewModeCatalogTest, FamiliesAreContiguous)
{
  auto seen = std::set<ShaderDebugModeFamily> {};
  auto current = ShaderDebugModeFamily::kNone;
  for (const auto& mode : EnumerateViewModes()) {
    if (mode.family != current) {
      EXPECT_TRUE(seen.insert(mode.family).second)
        << "family split: " << mode.canonical_name;
      current = mode.family;
    }
  }
}

NOLINT_TEST(ViewModeCatalogTest, NamesAreUniqueAndResolvable)
{
  auto names = std::set<std::string> {};
  for (const auto& mode : EnumerateViewModes()) {
    EXPECT_TRUE(names.insert(std::string(mode.canonical_name)).second)
      << mode.canonical_name;
    EXPECT_FALSE(mode.display_name.empty()) << mode.canonical_name;
    EXPECT_EQ(FindViewModeInfo(mode.canonical_name), &mode);
  }
  EXPECT_EQ(FindViewModeInfo("no-such-mode"), nullptr);
}

NOLINT_TEST(ViewModeCatalogTest, LitCarriesNoOverride)
{
  const auto* lit = FindViewModeInfo("lit");
  ASSERT_NE(lit, nullptr);
  EXPECT_FALSE(lit->render_mode.has_value());
  EXPECT_FALSE(lit->shader_debug_mode.has_value());
  EXPECT_EQ(
    FindViewModeInfo(RenderMode::kSolid, ShaderDebugMode::kDisabled), lit);
}

NOLINT_TEST(ViewModeCatalogTest, FindsAnEntryByItsModes)
{
  EXPECT_EQ(
    FindViewModeInfo(RenderMode::kWireframe, ShaderDebugMode::kDisabled),
    FindViewModeInfo("wireframe"));
  EXPECT_EQ(FindViewModeInfo(RenderMode::kSolid, ShaderDebugMode::kBaseColor),
    FindViewModeInfo("unlit"));
  EXPECT_EQ(
    FindViewModeInfo(RenderMode::kWireframe, ShaderDebugMode::kBaseColor),
    nullptr);
}

} // namespace
