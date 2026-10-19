//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Naming.cpp

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Content/VirtualPath.h>
#include <Oxygen/Cooker/Import/Naming.h>
#include <Oxygen/Core/Types/DescriptorExtensions.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::content::kMaxVirtualPathSegmentBytes;
using oxygen::content::ValidateVirtualPathSegment;
using oxygen::content::import::ImportNameKind;
using oxygen::content::import::NamingContext;
using oxygen::content::import::NamingService;

class AssetNamingTest : public testing::TestWithParam<ImportNameKind> { };

NOLINT_TEST_P(AssetNamingTest, CaseCollisionsPreserveSpellingAndDistinctNames)
{
  NamingService naming;
  const auto context = NamingContext { .kind = GetParam() };

  EXPECT_EQ(naming.MakeUniqueName("MetalGrey", context), "MetalGrey");
  EXPECT_EQ(naming.MakeUniqueName("Metalgrey", context), "Metalgrey_1");
  EXPECT_EQ(naming.MakeUniqueName("METALGREY", context), "METALGREY_2");
  EXPECT_TRUE(naming.HasName(GetParam(), "metalgrey"));
  EXPECT_TRUE(naming.HasName(GetParam(), "METALGREY_1"));
  EXPECT_TRUE(naming.HasName(GetParam(), "metalgrey_2"));
  EXPECT_FALSE(naming.HasName(GetParam(), "metalgrey_3"));
  EXPECT_EQ(naming.GetNameCount(GetParam()), 3U);
}

NOLINT_TEST_P(AssetNamingTest, CaseFoldingCoversUnicode)
{
  NamingService naming;
  const auto context = NamingContext { .kind = GetParam() };

  EXPECT_EQ(naming.MakeUniqueName("\xC3\x84rger", context), "\xC3\x84rger");
  EXPECT_EQ(naming.MakeUniqueName("\xC3\xA4rger", context), "\xC3\xA4rger_1");
}

NOLINT_TEST_P(AssetNamingTest, SkipsOccupiedSuffixRegardlessOfCase)
{
  NamingService naming;
  const auto context = NamingContext { .kind = GetParam() };

  EXPECT_EQ(naming.MakeUniqueName("metalgrey_1", context), "metalgrey_1");
  EXPECT_EQ(naming.MakeUniqueName("MetalGrey", context), "MetalGrey");
  EXPECT_EQ(naming.MakeUniqueName("Metalgrey", context), "Metalgrey_2");
  EXPECT_EQ(naming.MakeUniqueName("METALGREY", context), "METALGREY_3");
  EXPECT_TRUE(naming.HasName(GetParam(), "METALGREY_2"));
  EXPECT_EQ(naming.GetNameCount(GetParam()), 4U);
}

NOLINT_TEST_P(AssetNamingTest, KeepsAuthoredNamesAndConventions)
{
  NamingService naming;
  const auto context = NamingContext { .kind = GetParam() };

  for (const auto* name :
    { "Paint.001", "Wood-Trim", "Wood Mat", "SM_Hull", "M_Paint", "G_Hull" }) {
    EXPECT_EQ(naming.MakeUniqueName(name, context), name);
  }
}

NOLINT_TEST_P(AssetNamingTest, ReplacesOnlyRefusedCharacters)
{
  NamingService naming;
  const auto context = NamingContext { .kind = GetParam() };

  EXPECT_EQ(naming.MakeUniqueName("a/b:c*d", context), "a_b_c_d");
  EXPECT_EQ(naming.MakeUniqueName("\x01x", context), "_x");
  EXPECT_EQ(naming.MakeUniqueName("\xFF"
                                  "ab",
              context),
    "_ab");
  // U+0378 is unassigned in Unicode 15.1.
  EXPECT_EQ(naming.MakeUniqueName("q\xCD\xB8", context), "q_");
}

NOLINT_TEST_P(AssetNamingTest, ReplacementPrecedesUniqueness)
{
  NamingService naming;
  const auto context = NamingContext { .kind = GetParam() };

  EXPECT_EQ(naming.MakeUniqueName("Wood:Mat", context), "Wood_Mat");
  EXPECT_EQ(naming.MakeUniqueName("Wood_Mat", context), "Wood_Mat_1");
}

NOLINT_TEST_P(AssetNamingTest, FixesNamesTheSegmentRuleRefusesWhole)
{
  NamingService naming;
  const auto context = NamingContext { .kind = GetParam() };

  EXPECT_EQ(naming.MakeUniqueName(".Hidden", context), "_Hidden");
  EXPECT_EQ(naming.MakeUniqueName(" Paint ", context), "_Paint_");
  EXPECT_EQ(naming.MakeUniqueName("Trim.", context), "Trim_");
  EXPECT_EQ(naming.MakeUniqueName("CON", context), "CON_");
  EXPECT_EQ(naming.MakeUniqueName("nul.v2", context), "nul_.v2");
  EXPECT_EQ(naming.MakeUniqueName("COM1", context), "COM1_");
}

NOLINT_TEST_P(AssetNamingTest, NormalizesToNfc)
{
  NamingService naming;
  const auto context = NamingContext { .kind = GetParam() };

  EXPECT_EQ(naming.MakeUniqueName("Cafe\xCC\x81", context), "Caf\xC3\xA9");
}

//! A name whose leaf exceeds the segment bound is shortened, at a code point
//! boundary, so that the whole leaf fits: name, `_N`, the usage of each of a
//! texture's usages, and the extension.
NOLINT_TEST_P(AssetNamingTest, ShortensLongNamesToTheLeafBound)
{
  NamingService naming;
  const auto context = NamingContext { .kind = GetParam() };
  const auto suffixes = GetParam() == ImportNameKind::kMesh
    ? std::vector<std::string> { std::string(
        oxygen::data::kGeometryDescriptorExtension) }
    : GetParam() == ImportNameKind::kMaterial
    ? std::vector<std::string> { std::string(
        oxygen::data::kMaterialDescriptorExtension) }
    : std::vector<std::string> {
        ".base_color.otex",
        ".normal.otex",
        ".metallic_roughness.otex",
        ".clearcoat_normal.otex",
      };
  const auto fits = [&suffixes](const std::string& name) -> void {
    for (const auto& suffix : suffixes) {
      const auto leaf = name + suffix;
      EXPECT_LE(leaf.size(), kMaxVirtualPathSegmentBytes) << suffix;
      EXPECT_FALSE(ValidateVirtualPathSegment(leaf).has_value()) << suffix;
    }
  };

  const auto ascii = std::string(300, 'a');
  const auto first = naming.MakeUniqueName(ascii, context);
  fits(first);
  const auto second = naming.MakeUniqueName(ascii, context);
  EXPECT_TRUE(second.ends_with("_1"));
  fits(second);
  EXPECT_NE(first, second);

  auto accented = std::string {};
  for (auto i = 0; i < 200; ++i) {
    accented.append("Ã©");
  }
  const auto shortened = naming.MakeUniqueName(accented, context);
  fits(shortened);
  EXPECT_EQ(shortened.size() % 2, 0U);
}

INSTANTIATE_TEST_SUITE_P(StorageKinds, AssetNamingTest,
  testing::Values(ImportNameKind::kMesh, ImportNameKind::kMaterial,
    ImportNameKind::kTexture));

NOLINT_TEST(NamingServiceTest, UnnamedAssetsTakeKindFallbacks)
{
  NamingService naming;

  EXPECT_EQ(
    naming.MakeUniqueName(
      "", NamingContext { .kind = ImportNameKind::kMaterial, .ordinal = 3 }),
    "Material_3");
  EXPECT_EQ(naming.MakeUniqueName("",
              NamingContext { .kind = ImportNameKind::kTexture, .ordinal = 0 }),
    "texture_0");
  EXPECT_EQ(naming.MakeUniqueName(
              "/", NamingContext { .kind = ImportNameKind::kMaterial }),
    "_");
}

NOLINT_TEST(NamingServiceTest, FallbacksAndVariantsPassThroughTheRegistry)
{
  NamingService naming;
  const auto mesh = [](const uint32_t ordinal,
                      const std::string_view variant) -> NamingContext {
    return NamingContext {
      .kind = ImportNameKind::kMesh,
      .ordinal = ordinal,
      .variant = variant,
    };
  };

  EXPECT_EQ(naming.MakeUniqueName("Mesh_0", mesh(5, {})), "Mesh_0");
  EXPECT_EQ(naming.MakeUniqueName("", mesh(0, {})), "Mesh_0_1");
  EXPECT_EQ(naming.MakeUniqueName("", mesh(2, "__baked_1")), "Mesh_2__baked_1");
  EXPECT_EQ(
    naming.MakeUniqueName("Hull", mesh(3, "__baked_1")), "Hull__baked_1");
  EXPECT_EQ(
    naming.MakeUniqueName("Hull__baked_1", mesh(4, {})), "Hull__baked_1_1");
}

NOLINT_TEST(NamingServiceTest, SceneNodesKeepVerbatimCaseSensitiveNames)
{
  NamingService naming;
  const auto context = NamingContext {
    .kind = ImportNameKind::kSceneNode,
    .ordinal = 4,
  };

  EXPECT_EQ(naming.MakeUniqueName("Light", context), "Light");
  EXPECT_EQ(naming.MakeUniqueName("light", context), "light");
  EXPECT_EQ(naming.MakeUniqueName("Light", context), "Light_1");
  EXPECT_EQ(naming.MakeUniqueName("Arm/Hand: L ", context), "Arm/Hand: L ");
  EXPECT_EQ(naming.MakeUniqueName("", context), "Node_4");
  EXPECT_TRUE(naming.HasName(context.kind, "Light"));
  EXPECT_TRUE(naming.HasName(context.kind, "light"));
  EXPECT_FALSE(naming.HasName(context.kind, "LIGHT"));
  EXPECT_EQ(naming.GetNameCount(context.kind), 5U);
}

NOLINT_TEST(NamingServiceTest, ResetReleasesCaseInsensitiveRegistrations)
{
  NamingService naming;
  const auto context = NamingContext { .kind = ImportNameKind::kMaterial };
  EXPECT_EQ(naming.MakeUniqueName("MetalGrey", context), "MetalGrey");
  EXPECT_TRUE(naming.HasName(context.kind, "metalgrey"));

  naming.Reset();

  EXPECT_FALSE(naming.HasName(context.kind, "METALGREY"));
  EXPECT_EQ(naming.GetNameCount(context.kind), 0U);
  EXPECT_EQ(naming.MakeUniqueName("metalgrey", context), "metalgrey");
}

} // namespace
