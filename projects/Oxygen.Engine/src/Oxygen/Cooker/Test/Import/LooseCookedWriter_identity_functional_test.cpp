//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/LooseCookedWriter.cpp

#include <filesystem>

#include "LooseCookedWriterTestSupport.h"

#include <Oxygen/Cooker/Import/Internal/LooseCookedWriter.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Cooker/Test/Pak/PakTestSupport.h>
#include <Oxygen/Cooker/Test/Support/DescriptorFixtures.h>
#include <Oxygen/Cooker/Test/Support/TempDir.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Testing/GTest.h>

// NOLINTBEGIN(*-magic-numbers)

namespace oxygen::content::testing {
namespace fixtures = oxygen::content::test;

namespace {

  using oxygen::content::import::LooseCookedWriter;
  using oxygen::content::lc::Inspection;
  using oxygen::content::pak::test::MakeSourceKey;
  using oxygen::cooker::test::ScopedTempDir;
  using oxygen::data::AssetType;

  NOLINT_TEST(
    LooseCookedWriterIdentityTest, FinishEmptyContainerWritesLoadableIndex)
  {
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "loose_cooked_writer_empty";
    const auto source_key = MakeSourceKey(1);

    LooseCookedWriter writer(cooked_root);
    writer.SetSourceKey(source_key);

    const auto result = writer.Finish();
    Inspection inspection;
    inspection.LoadFromFile(cooked_root / "container.index.bin");

    EXPECT_EQ(result.source_key, source_key);
    EXPECT_EQ(inspection.Guid(), source_key);
    EXPECT_TRUE(inspection.Assets().empty());
    EXPECT_TRUE(inspection.Files().empty());
  }

  NOLINT_TEST(LooseCookedWriterIdentityTest, FinishPreservesExistingSourceKey)
  {
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "loose_cooked_writer_guid_preserve";
    const auto source_key = MakeSourceKey(7);

    {
      LooseCookedWriter writer(cooked_root);
      writer.SetSourceKey(source_key);
      (void)writer.Finish();
    }

    LooseCookedWriter writer(cooked_root);
    const auto result = writer.Finish();

    Inspection inspection;
    inspection.LoadFromFile(cooked_root / "container.index.bin");

    EXPECT_EQ(result.source_key, source_key);
    EXPECT_EQ(inspection.Guid(), source_key);
  }

  NOLINT_TEST(LooseCookedWriterIdentityTest,
    ExplicitSourceKeyReidentifiesASeededGeneration)
  {
    const ScopedTempDir temp;
    const auto root = temp.Path() / "generation_source_identity";
    const auto old_root = root / "old";
    const auto new_root = root / "new";
    const auto old_key = MakeSourceKey(31);
    const auto new_key = MakeSourceKey(32);
    const auto asset_key = MakeFirstByteAssetKey(7);
    const auto bytes
      = fixtures::MaterialDescriptor("bytes", fixtures::MaterialVariant::kPlain)
          .bytes;
    {
      LooseCookedWriter writer(old_root);
      writer.SetSourceKey(old_key);
      writer.WriteAssetDescriptor(asset_key, AssetType::kMaterial,
        "/Content/Materials/kept.omat", "Materials/kept.omat", bytes, {});
      static_cast<void>(writer.Finish());
    }
    std::filesystem::copy(
      old_root, new_root, std::filesystem::copy_options::recursive);
    LooseCookedWriter candidate(new_root);
    candidate.SetSourceKey(new_key);
    EXPECT_EQ(candidate.Finish().source_key, new_key);
    Inspection previous;
    Inspection next;
    previous.LoadFromRoot(old_root);
    next.LoadFromRoot(new_root);
    EXPECT_EQ(previous.Guid(), old_key);
    EXPECT_EQ(next.Guid(), new_key);
    ASSERT_EQ(next.Assets().size(), 1U);
    EXPECT_EQ(next.Assets().front().key, asset_key);
    EXPECT_EQ(next.Assets().front().descriptor_sha256,
      previous.Assets().front().descriptor_sha256);
  }

  NOLINT_TEST(
    LooseCookedWriterIdentityTest, FinishPreservesExistingContentVersion)
  {
    const ScopedTempDir temp;
    const auto cooked_root
      = temp.Path() / "loose_cooked_writer_version_preserve";

    {
      LooseCookedWriter writer(cooked_root);
      writer.SetContentVersion(123);
      (void)writer.Finish();
    }

    LooseCookedWriter writer(cooked_root);
    const auto result = writer.Finish();

    EXPECT_EQ(result.content_version, 123);
  }
} // namespace

} // namespace oxygen::content::testing

// NOLINTEND(*-magic-numbers)
