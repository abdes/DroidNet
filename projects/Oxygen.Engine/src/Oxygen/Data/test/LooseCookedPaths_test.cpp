//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <stdexcept>
#include <string_view>

#include <Oxygen/Data/LooseCookedPaths.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::data::loose_cooked::RequireIndexRelPath;

//! A path relative to the cooked root, `/`-separated, is listed.
NOLINT_TEST(LooseCookedPathsTest, AcceptsAContainerRelativePath)
{
  EXPECT_NO_THROW(RequireIndexRelPath("Materials/Wood.omat"));
  EXPECT_NO_THROW(RequireIndexRelPath("Resources/textures.data"));
  EXPECT_NO_THROW(RequireIndexRelPath("a.b/c..d/e"));
}

class LooseCookedPathsRefusalTest
  : public ::testing::TestWithParam<std::string_view> { };

//! Every path that breaks a rule is refused.
NOLINT_TEST_P(LooseCookedPathsRefusalTest, RefusesThePath)
{
  EXPECT_THROW(RequireIndexRelPath(GetParam()), std::runtime_error)
    << GetParam();
}

INSTANTIATE_TEST_SUITE_P(BrokenRules, LooseCookedPathsRefusalTest,
  ::testing::Values(std::string_view {}, "container.index.bin",
    ".generation.lock", "Materials\\Wood.omat", "C:/Wood.omat", "/Wood.omat",
    "Materials/", "Materials//Wood.omat", "./Wood.omat", "Materials/./Wood",
    "../Wood.omat", "Materials/../Wood.omat", "Materials/..",
    std::string_view { "Materials/a\0b", 13 }, "Materials/\xFF.omat"));

} // namespace
