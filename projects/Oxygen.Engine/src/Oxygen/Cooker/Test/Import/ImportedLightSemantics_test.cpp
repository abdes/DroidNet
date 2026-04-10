//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Cooker/Import/Internal/ImportedLightSemantics.h>

namespace {

using oxygen::content::import::detail::ImportedLightSemantics;
using oxygen::content::import::detail::ResolveImportedLightCommon;

NOLINT_TEST(ImportedLightSemanticsTest, DefaultsFlowThroughWithoutOverrides)
{
  const auto common = ResolveImportedLightCommon(ImportedLightSemantics {},
    ImportedLightSemantics {},
    /*default_affects_world=*/true, /*default_casts_shadows=*/false);

  EXPECT_EQ(common.affects_world, 1U);
  EXPECT_EQ(common.casts_shadows, 0U);
}

NOLINT_TEST(ImportedLightSemanticsTest, LightOverridesBeatImporterDefaults)
{
  const auto common = ResolveImportedLightCommon(ImportedLightSemantics {},
    ImportedLightSemantics {
      .affects_world = false,
      .casts_shadows = true,
    },
    /*default_affects_world=*/true, /*default_casts_shadows=*/false);

  EXPECT_EQ(common.affects_world, 0U);
  EXPECT_EQ(common.casts_shadows, 1U);
}

NOLINT_TEST(ImportedLightSemanticsTest, NodeOverridesBeatLightOverrides)
{
  const auto common = ResolveImportedLightCommon(
    ImportedLightSemantics {
      .affects_world = false,
      .casts_shadows = false,
    },
    ImportedLightSemantics {
      .affects_world = true,
      .casts_shadows = true,
    },
    /*default_affects_world=*/true, /*default_casts_shadows=*/true);

  EXPECT_EQ(common.affects_world, 0U);
  EXPECT_EQ(common.casts_shadows, 0U);
}

} // namespace
