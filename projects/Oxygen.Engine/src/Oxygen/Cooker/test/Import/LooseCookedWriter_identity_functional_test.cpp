//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/LooseCookedWriter.cpp

#include <cstddef>
#include <filesystem>
#include <vector>

#include "LooseCookedWriterTestSupport.h"

#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedWriter.h>
#include <Oxygen/Cooker/testing/Descriptors.h>
#include <Oxygen/Cooker/testing/MemoryFileSystem.h>
#include <Oxygen/Cooker/testing/Seeds.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::testing {
namespace fixtures = oxygen::cooker::testing;

namespace {

  using oxygen::content::cooked::CookedContent;
  using oxygen::content::import::LooseCookedWriter;
  using oxygen::cooker::testing::MemoryFileSystem;
  using oxygen::cooker::testing::SourceKeyOf;
  using oxygen::data::AssetType;

  NOLINT_TEST(
    LooseCookedWriterIdentityTest, FinishEmptyContainerWritesLoadableIndex)
  {
    auto storage = MemoryFileSystem {};
    const auto cooked_root = MemoryRoot() / "loose_cooked_writer_empty";
    const auto source_key = SourceKeyOf(1);

    LooseCookedWriter writer(cooked_root, storage);
    writer.SetSourceKey(source_key);

    const auto result = writer.Finish();
    const auto content = CookedContent::Open(storage, cooked_root);

    EXPECT_EQ(result.source_key, source_key);
    EXPECT_EQ(content.Summary().source_key, source_key);
    EXPECT_TRUE(content.Assets().empty());
    EXPECT_TRUE(content.NamedTextures().empty());
    EXPECT_TRUE(content.Sidecars().empty());
    for (const auto kind :
      { data::ResourceKind::kBuffer, data::ResourceKind::kTexture,
        data::ResourceKind::kScript, data::ResourceKind::kPhysics }) {
      EXPECT_TRUE(content.ResourceTable(kind).RowsStored().file.empty());
      EXPECT_TRUE(content.ResourceTable(kind).DataStored().file.empty());
    }
  }

  NOLINT_TEST(LooseCookedWriterIdentityTest, FinishPreservesExistingSourceKey)
  {
    auto storage = MemoryFileSystem {};
    const auto cooked_root = MemoryRoot() / "loose_cooked_writer_guid_preserve";
    const auto source_key = SourceKeyOf(7);

    {
      LooseCookedWriter writer(cooked_root, storage);
      writer.SetSourceKey(source_key);
      (void)writer.Finish();
    }

    LooseCookedWriter writer(cooked_root, storage);
    const auto result = writer.Finish();

    const auto content = CookedContent::Open(storage, cooked_root);

    EXPECT_EQ(result.source_key, source_key);
    EXPECT_EQ(content.Summary().source_key, source_key);
  }

  NOLINT_TEST(LooseCookedWriterIdentityTest,
    ExplicitSourceKeyReidentifiesASeededGeneration)
  {
    auto storage = MemoryFileSystem {};
    const auto root = MemoryRoot() / "generation_source_identity";
    const auto old_root = root / "old";
    const auto new_root = root / "new";
    const auto old_key = SourceKeyOf(31);
    const auto new_key = SourceKeyOf(32);
    const auto asset_key = MakeFirstByteAssetKey(7);
    const auto bytes
      = fixtures::MaterialDescriptor("bytes", fixtures::MaterialVariant::kPlain)
          .bytes;
    {
      LooseCookedWriter writer(old_root, storage);
      writer.SetSourceKey(old_key);
      writer.WriteAssetDescriptor(asset_key, AssetType::kMaterial,
        "/Content/Materials/kept.omat", "Materials/kept.omat", bytes, {},
        FromSource("/Content/Materials/kept.omat.json"));
      static_cast<void>(writer.Finish());
    }
    for (const auto* relpath :
      { "container.index.bin", "Materials/kept.omat" }) {
      const auto copied = storage.Bytes(old_root / relpath);
      storage.AddFile(new_root / relpath,
        std::vector<std::byte>(copied.begin(), copied.end()));
    }
    LooseCookedWriter candidate(new_root, storage);
    candidate.SetSourceKey(new_key);
    EXPECT_EQ(candidate.Finish().source_key, new_key);
    const auto previous = CookedContent::Open(storage, old_root);
    const auto next = CookedContent::Open(storage, new_root);
    EXPECT_EQ(previous.Summary().source_key, old_key);
    EXPECT_EQ(next.Summary().source_key, new_key);
    ASSERT_EQ(next.Assets().size(), 1U);
    EXPECT_EQ(next.Assets().front().key, asset_key);
    EXPECT_EQ(next.Assets().front().descriptor_sha256,
      previous.Assets().front().descriptor_sha256);
  }

  NOLINT_TEST(
    LooseCookedWriterIdentityTest, FinishPreservesExistingContentVersion)
  {
    auto storage = MemoryFileSystem {};
    const auto cooked_root
      = MemoryRoot() / "loose_cooked_writer_version_preserve";

    {
      LooseCookedWriter writer(cooked_root, storage);
      writer.SetContentVersion(123);
      (void)writer.Finish();
    }

    LooseCookedWriter writer(cooked_root, storage);
    const auto result = writer.Finish();

    EXPECT_EQ(result.content_version, 123);
  }
} // namespace

} // namespace oxygen::content::testing
