//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Cooker/Import/Naming.h>

namespace {

using oxygen::content::import::ImportNameKind;
using oxygen::content::import::NamingContext;
using oxygen::content::import::NamingService;
using oxygen::content::import::NoOpNamingStrategy;
using oxygen::content::import::NormalizeNamingStrategy;

class AssetNamingTest : public testing::TestWithParam<ImportNameKind> { };

NOLINT_TEST_P(AssetNamingTest, CaseCollisionsPreserveSpellingAndDistinctNames)
{
  auto naming = NamingService({
    .strategy = std::make_shared<NoOpNamingStrategy>(),
  });
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

NOLINT_TEST_P(AssetNamingTest, SkipsOccupiedSuffixRegardlessOfCase)
{
  auto naming = NamingService({
    .strategy = std::make_shared<NoOpNamingStrategy>(),
  });
  const auto context = NamingContext { .kind = GetParam() };

  EXPECT_EQ(naming.MakeUniqueName("metalgrey_1", context), "metalgrey_1");
  EXPECT_EQ(naming.MakeUniqueName("MetalGrey", context), "MetalGrey");
  EXPECT_EQ(naming.MakeUniqueName("Metalgrey", context), "Metalgrey_2");
  EXPECT_EQ(naming.MakeUniqueName("METALGREY", context), "METALGREY_3");
  EXPECT_TRUE(naming.HasName(GetParam(), "METALGREY_2"));
  EXPECT_EQ(naming.GetNameCount(GetParam()), 4U);
}

NOLINT_TEST_P(AssetNamingTest, NamespaceParticipatesInPortableStorageIdentity)
{
  auto naming = NamingService({
    .strategy = std::make_shared<NoOpNamingStrategy>(),
  });
  auto context = NamingContext {
    .kind = GetParam(),
    .scene_namespace = "Town",
  };

  EXPECT_EQ(naming.MakeUniqueName("MetalGrey", context), "Town/MetalGrey");
  context.scene_namespace = "TOWN";
  EXPECT_EQ(naming.MakeUniqueName("metalgrey", context), "TOWN/metalgrey_1");
  context.scene_namespace = "Other";
  EXPECT_EQ(naming.MakeUniqueName("MetalGrey", context), "Other/MetalGrey");
  EXPECT_TRUE(naming.HasName(GetParam(), "town/METALGREY"));
  EXPECT_TRUE(naming.HasName(GetParam(), "town/METALGREY_1"));
  EXPECT_TRUE(naming.HasName(GetParam(), "other/metalgrey"));
  EXPECT_EQ(naming.GetNameCount(GetParam()), 3U);
}

INSTANTIATE_TEST_SUITE_P(StorageKinds, AssetNamingTest,
  testing::Values(
    ImportNameKind::kMesh, ImportNameKind::kMaterial, ImportNameKind::kScene));

NOLINT_TEST(NamingServiceTest, SceneNodesKeepCaseSensitiveDisplayNames)
{
  auto naming = NamingService({
    .strategy = std::make_shared<NoOpNamingStrategy>(),
  });
  const auto context = NamingContext {
    .kind = ImportNameKind::kSceneNode,
    .scene_namespace = "Town",
  };

  EXPECT_EQ(naming.MakeUniqueName("Light", context), "Light");
  EXPECT_EQ(naming.MakeUniqueName("light", context), "light");
  EXPECT_EQ(naming.MakeUniqueName("Light", context), "Light_1");
  EXPECT_TRUE(naming.HasName(context.kind, "Light"));
  EXPECT_TRUE(naming.HasName(context.kind, "light"));
  EXPECT_FALSE(naming.HasName(context.kind, "LIGHT"));
  EXPECT_FALSE(naming.HasName(context.kind, "Town/Light"));
  EXPECT_EQ(naming.GetNameCount(context.kind), 3U);
}

NOLINT_TEST(NamingServiceTest, NormalizationPrecedesPortableCollisionChecks)
{
  auto naming = NamingService({
    .strategy = std::make_shared<NormalizeNamingStrategy>(),
  });
  const auto context = NamingContext { .kind = ImportNameKind::kMaterial };

  EXPECT_EQ(naming.MakeUniqueName(" Metal-Grey ", context), "M_Metal_Grey");
  EXPECT_EQ(naming.MakeUniqueName("metal_GREY", context), "M_metal_GREY_1");
  EXPECT_TRUE(naming.HasName(context.kind, "m_metal_grey_1"));
}

NOLINT_TEST(NamingServiceTest, ResetReleasesCaseInsensitiveRegistrations)
{
  auto naming = NamingService({
    .strategy = std::make_shared<NoOpNamingStrategy>(),
  });
  const auto context = NamingContext { .kind = ImportNameKind::kMaterial };
  EXPECT_EQ(naming.MakeUniqueName("MetalGrey", context), "MetalGrey");
  EXPECT_TRUE(naming.HasName(context.kind, "metalgrey"));

  naming.Reset();

  EXPECT_FALSE(naming.HasName(context.kind, "METALGREY"));
  EXPECT_EQ(naming.GetNameCount(context.kind), 0U);
  EXPECT_EQ(naming.MakeUniqueName("metalgrey", context), "metalgrey");
}

NOLINT_TEST(
  NamingServiceTest, ExplicitlyDisabledUniquenessKeepsExistingContract)
{
  auto naming = NamingService({
    .strategy = std::make_shared<NoOpNamingStrategy>(),
    .enforce_uniqueness = false,
  });
  const auto context = NamingContext { .kind = ImportNameKind::kMaterial };

  EXPECT_EQ(naming.MakeUniqueName("MetalGrey", context), "MetalGrey");
  EXPECT_EQ(naming.MakeUniqueName("Metalgrey", context), "Metalgrey");
  EXPECT_EQ(naming.MakeUniqueName("MetalGrey", context), "MetalGrey");
  EXPECT_FALSE(naming.HasName(context.kind, "MetalGrey"));
  EXPECT_EQ(naming.GetNameCount(context.kind), 0U);
}

} // namespace
