//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>

#include "AssetLoader_test.h"
#include "Fixtures/LooseCookedTestWriter.h"

#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Content/Internal/ContentSourceRegistry.h>
#include <Oxygen/Content/Internal/LooseCookedSource.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::testing {
namespace {

  NOLINT_TEST_F(AssetLoaderBasicTest, IndexOnlySourceAllowsUnpublishedCookFiles)
  {
    const auto root = temp_dir_ / "source";
    static_cast<void>(LooseCookedTestWriter(root).Finish());
    {
      std::ofstream extra(root / "unpublished.data");
      extra << "New output, not yet present in the published index";
      ASSERT_TRUE(extra.good());
    }
    using Mode = internal::LooseCookedSource::OpenMode;
    const internal::LooseCookedSource index(root, Mode::kIndexOnly);
    EXPECT_EQ(index.GetAssetCount(), 0U);
    EXPECT_THROW(internal::LooseCookedSource(root, Mode::kValidateMetadata),
      std::runtime_error);
  }

  NOLINT_TEST_F(
    AssetLoaderBasicTest, SourceRegistryDoesNotRecycleIdsAcrossClear)
  {
    const auto root = temp_dir_ / "source";
    static_cast<void>(LooseCookedTestWriter(root).Finish());
    auto source = std::make_shared<internal::LooseCookedSource>(
      root, internal::LooseCookedSource::OpenMode::kVerifyContent);
    internal::ContentSourceRegistry registry;
    const auto first = registry.MountLoose(source->DebugName(), source);
    registry.Clear();
    EXPECT_TRUE(registry.Sources().empty());
    EXPECT_FALSE(registry.AcquireSource(first.source_id));
    const auto second = registry.MountLoose(source->DebugName(), source);
    EXPECT_NE(first.source_id, second.source_id);
    EXPECT_EQ(registry.AcquireSource(second.source_id), source);
  }

  NOLINT_TEST_F(
    AssetLoaderBasicTest, SourceRegistryRetainsNoUnownedRetiredBytes)
  {
    const auto root = temp_dir_ / "source";
    static_cast<void>(LooseCookedTestWriter(root).Finish());
    auto source = std::make_shared<internal::LooseCookedSource>(
      root, internal::LooseCookedSource::OpenMode::kVerifyContent);
    internal::ContentSourceRegistry registry;
    const auto mounted = registry.MountGeneration(source, std::nullopt);
    const auto key = source->GetSourceKey();
    registry.Clear();
    EXPECT_TRUE(registry.AcquireSource(mounted.source_id));
    source.reset();
    EXPECT_FALSE(registry.AcquireSource(mounted.source_id));
    EXPECT_EQ(registry.GetSourceKey(mounted.source_id), key);
    EXPECT_FALSE(registry.FindSourceIdByKey(key));
    EXPECT_TRUE(registry.PruneExpiredSources().contains(mounted.source_id));
    EXPECT_FALSE(registry.IsKnownSource(mounted.source_id));
    EXPECT_TRUE(registry.PruneExpiredSources().empty());
  }

  NOLINT_TEST_F(AssetLoaderBasicTest, SourceInstancesDoNotAliasAcrossLoaders)
  {
    const auto root = temp_dir_ / "source";
    static_cast<void>(LooseCookedTestWriter(root).Finish());
    const auto source = std::make_shared<internal::LooseCookedSource>(
      root, internal::LooseCookedSource::OpenMode::kVerifyContent);
    internal::ContentSourceRegistry first_registry;
    internal::ContentSourceRegistry second_registry;
    const auto first = first_registry.MountLoose(source->DebugName(), source);
    const auto second = second_registry.MountLoose(source->DebugName(), source);
    EXPECT_NE(first.source_id, second.source_id);
    EXPECT_FALSE(first_registry.AcquireSource(second.source_id));
    EXPECT_FALSE(second_registry.AcquireSource(first.source_id));
  }

  NOLINT_TEST_F(AssetLoaderBasicTest, RetiredGenerationRemountIsIdempotent)
  {
    const auto root = temp_dir_ / "generation";
    static_cast<void>(LooseCookedTestWriter(root).Finish());
    auto first_source = std::make_shared<internal::LooseCookedSource>(
      root, internal::LooseCookedSource::OpenMode::kVerifyContent);
    internal::ContentSourceRegistry registry;
    const auto first = registry.MountGeneration(first_source, std::nullopt);
    const auto key = first_source->GetSourceKey();
    ASSERT_TRUE(registry.RetireGeneration(key));
    const auto new_source = std::make_shared<internal::LooseCookedSource>(
      root, internal::LooseCookedSource::OpenMode::kVerifyContent);
    const auto reopened = registry.MountGeneration(new_source, std::nullopt);
    EXPECT_NE(first.source_id, reopened.source_id);
    const auto repeated = registry.MountGeneration(
      std::make_shared<internal::LooseCookedSource>(
        root, internal::LooseCookedSource::OpenMode::kVerifyContent),
      std::nullopt);
    EXPECT_EQ(repeated.source_id, reopened.source_id);
    EXPECT_EQ(registry.Sources().size(), 1U);
    EXPECT_EQ(registry.FindSourceIdByKey(key), reopened.source_id);
    EXPECT_EQ(registry.AcquireSource(first.source_id), first_source);
    first_source.reset();
    EXPECT_TRUE(registry.PruneExpiredSources().contains(first.source_id));
    EXPECT_FALSE(registry.IsKnownSource(first.source_id));
    EXPECT_EQ(registry.AcquireSource(reopened.source_id), new_source);
    ASSERT_TRUE(registry.RetireGeneration(key));
    EXPECT_TRUE(registry.Sources().empty());
  }

  NOLINT_TEST_F(AssetLoaderBasicTest, RemountReplacementUsesOneActiveOpening)
  {
    const auto first_root = temp_dir_ / "first";
    const auto second_root = temp_dir_ / "second";
    static_cast<void>(LooseCookedTestWriter(first_root).Finish());
    LooseCookedTestWriter writer(second_root);
    writer.SetSourceKey(data::SourceKey { Uuid::Generate() });
    static_cast<void>(writer.Finish());
    const auto first = std::make_shared<internal::LooseCookedSource>(
      first_root, internal::LooseCookedSource::OpenMode::kVerifyContent);
    const auto second = std::make_shared<internal::LooseCookedSource>(
      second_root, internal::LooseCookedSource::OpenMode::kVerifyContent);
    internal::ContentSourceRegistry registry;
    const auto original = registry.MountGeneration(first, std::nullopt);
    static_cast<void>(registry.MountGeneration(second, first->GetSourceKey()));
    const auto restored = registry.MountGeneration(
      std::make_shared<internal::LooseCookedSource>(
        first_root, internal::LooseCookedSource::OpenMode::kVerifyContent),
      second->GetSourceKey());
    EXPECT_NE(original.source_id, restored.source_id);
    const auto repeated = registry.MountGeneration(
      std::make_shared<internal::LooseCookedSource>(
        first_root, internal::LooseCookedSource::OpenMode::kVerifyContent),
      first->GetSourceKey());
    EXPECT_EQ(restored.source_id, repeated.source_id);
    EXPECT_EQ(registry.Sources().size(), 1U);
    EXPECT_EQ(
      registry.FindSourceIdByKey(first->GetSourceKey()), restored.source_id);
  }

  NOLINT_TEST_F(AssetLoaderBasicTest, RefreshRevokesOldMutableReadCapability)
  {
    const auto root = temp_dir_ / "source";
    static_cast<void>(LooseCookedTestWriter(root).Finish());
    const auto source = std::make_shared<internal::LooseCookedSource>(
      root, internal::LooseCookedSource::OpenMode::kVerifyContent);
    internal::ContentSourceRegistry registry;
    const auto first = registry.MountLoose(source->DebugName(), source);
    const auto second = registry.MountLoose(source->DebugName(), source);
    EXPECT_NE(first.source_id, second.source_id);
    EXPECT_FALSE(registry.AcquireSource(first.source_id));
    EXPECT_EQ(registry.AcquireSource(second.source_id), source);
    EXPECT_EQ(
      registry.FindSourceIdByKey(source->GetSourceKey()), second.source_id);
  }

} // namespace
} // namespace oxygen::content::testing
