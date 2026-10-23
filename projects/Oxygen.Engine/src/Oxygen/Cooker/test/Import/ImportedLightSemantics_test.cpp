//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/ImportedLightSemantics.h

#include <Oxygen/Cooker/Import/Internal/ImportedLightSemantics.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::content::import::detail::ImportedLightSemantics;
using oxygen::content::import::detail::ResolveImportedLightCommon;
using oxygen::data::pak::world::LightCommonRecord;

NOLINT_TEST(ImportedLightSemanticsTest, DefaultsFlowThroughWithoutOverrides)
{
  const auto common = ResolveImportedLightCommon(LightCommonRecord {},
    ImportedLightSemantics {}, ImportedLightSemantics {},
    /*default_affects_world=*/true, /*default_casts_shadows=*/false);

  EXPECT_EQ(common.affects_world, 1U);
  EXPECT_EQ(common.casts_shadows, 0U);
}

NOLINT_TEST(ImportedLightSemanticsTest, LightOverridesBeatImporterDefaults)
{
  const auto common = ResolveImportedLightCommon(LightCommonRecord {},
    ImportedLightSemantics {},
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
  const auto common = ResolveImportedLightCommon(LightCommonRecord {},
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

NOLINT_TEST(ImportedLightSemanticsTest, KeepsEveryOtherFieldOfTheGivenCommon)
{
  constexpr auto kColor = 0.25F;
  constexpr auto kNormalBias = 0.5F;
  auto base = LightCommonRecord {};
  base.color_rgb[1] = kColor;
  base.shadow.normal_bias = kNormalBias;

  const auto common = ResolveImportedLightCommon(base,
    ImportedLightSemantics {}, ImportedLightSemantics {},
    /*default_affects_world=*/true, /*default_casts_shadows=*/true);

  EXPECT_FLOAT_EQ(common.color_rgb[1], kColor);
  EXPECT_FLOAT_EQ(common.shadow.normal_bias, kNormalBias);
}

} // namespace
