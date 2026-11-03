//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "LooseCookedWriterTestSupport.h"

#include <Oxygen/Content/LooseCookedIndex.h>
#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedWriter.h>
#include <Oxygen/Cooker/testing/MaterialBuilder.h>
#include <Oxygen/Cooker/testing/MemoryFileSystem.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/LooseCookedIndexEncoder.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/LooseCookedPaths.h>
#include <Oxygen/Data/LooseCookedResources.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::testing {
namespace fixtures = oxygen::cooker::testing;

namespace {

  using oxygen::content::cooked::CookedContent;
  using oxygen::content::import::LooseCookedRootBusy;
  using oxygen::content::import::LooseCookedWriter;
  using oxygen::content::lc::LooseCookedIndex;
  using oxygen::cooker::testing::MemoryFileSystem;
  using oxygen::data::AssetType;
  using oxygen::data::loose_cooked::FileKind;
  using oxygen::data::loose_cooked::IndexedRequirement;

  constexpr auto kVirtualPath = "/Content/Materials/Wall.omat";
  constexpr auto kLayoutPath = "Materials/Wall.omat";

  auto Root() -> std::filesystem::path { return MemoryRoot() / "Content"; }

  auto MaterialBytes(const std::string_view name) -> std::vector<std::byte>
  {
    return fixtures::MaterialBuilder { name }.Bytes();
  }

  //! Cooks the wall material with the bytes of `name` in one session, and
  //! returns what it published.
  auto CookWall(MemoryFileSystem& storage, const std::string_view name)
    -> import::LooseCookedWriteResult
  {
    auto writer = LooseCookedWriter(Root(), storage);
    writer.WriteAssetDescriptor(MakeFirstByteAssetKey(0x11),
      AssetType::kMaterial, kVirtualPath, kLayoutPath, MaterialBytes(name), {},
      FromSource("/Content/Materials/Wall.omat.json"));
    return writer.Finish();
  }

  auto IndexOf(const MemoryFileSystem& storage) -> LooseCookedIndex
  {
    return LooseCookedIndex::LoadFromBytes(
      storage.Bytes(Root() / data::loose_cooked::kIndexFileName));
  }

  /*!
   Scenario: a build agent and a developer cook one root at once. The second
   writer is refused before it writes anything; once the first is done, the
   root is free.
  */
  NOLINT_TEST(LooseCookedWriterPublicationTest, ASecondWriterIsRefused)
  {
    auto storage = MemoryFileSystem {};
    {
      const auto first = LooseCookedWriter(Root(), storage);

      NOLINT_EXPECT_THROW(static_cast<void>(LooseCookedWriter(Root(), storage)),
        LooseCookedRootBusy);
    }

    NOLINT_EXPECT_NO_THROW(
      static_cast<void>(LooseCookedWriter(Root(), storage)));
  }

  //! Each publication is the next commit, and its descriptor has the content
  //! name of its bytes.
  NOLINT_TEST(LooseCookedWriterPublicationTest, CommitsCountUpUnderContentNames)
  {
    auto storage = MemoryFileSystem {};

    const auto first = CookWall(storage, "first");
    const auto same = CookWall(storage, "first");

    EXPECT_EQ(first.commit, 1U);
    EXPECT_EQ(same.commit, 2U);
    ASSERT_EQ(first.assets.size(), 1U);
    const auto& relpath = first.assets.front().descriptor_relpath;
    EXPECT_TRUE(data::loose_cooked::IsContentRelPath(relpath)) << relpath;
    EXPECT_TRUE(relpath.starts_with("Materials/Wall."));
    EXPECT_EQ(same.assets.front().descriptor_relpath, relpath);
    EXPECT_TRUE(same.removed_files.empty());
    EXPECT_EQ(IndexOf(storage).Commit(), 2U);
  }

  /*!
   Scenario: a preview has the root mounted when a cook changes the wall.
   The old descriptor stays while the preview reads the commit that lists it,
   and the first cook after the preview lets go deletes it.
  */
  NOLINT_TEST(LooseCookedWriterPublicationTest, ARetiredFileOutlivesItsReaders)
  {
    auto storage = MemoryFileSystem {};
    const auto first = CookWall(storage, "first");
    const auto old_file = Root() / first.assets.front().descriptor_relpath;
    auto reader = std::optional { CookedContent::Open(storage, Root()) };

    const auto changed = CookWall(storage, "changed");

    EXPECT_EQ(changed.removed_files,
      std::vector { first.assets.front().descriptor_relpath });
    EXPECT_TRUE(Exists(storage, old_file));
    ASSERT_EQ(IndexOf(storage).RetiredFiles().size(), 1U);
    EXPECT_EQ(IndexOf(storage).RetiredFiles().front().retired_at, 2U);

    reader.reset();
    static_cast<void>(LooseCookedWriter(Root(), storage));

    EXPECT_FALSE(Exists(storage, old_file));
  }

  /*!
   Scenario: a cook was killed after writing a descriptor, a publication
   sibling and payload bytes. The next writer drops what no commit records
   and keeps a file someone else put there.
  */
  NOLINT_TEST(LooseCookedWriterPublicationTest, TheNextWriterDropsALeftCook)
  {
    auto storage = MemoryFileSystem {};
    {
      auto writer = LooseCookedWriter(Root(), storage);
      const auto table = std::vector<std::byte>(
        data::loose_cooked::ResourceRowSize(FileKind::kBuffersTable));
      writer.WriteFile(
        FileKind::kBuffersTable, "Resources/buffers.table", table);
      writer.WriteFile(FileKind::kBuffersData, "Resources/buffers.data",
        std::vector<std::byte>(4));
      static_cast<void>(writer.Finish());
    }
    const auto leftover = Root() / "Materials/Wall.0123456789abcdef.omat";
    const auto sibling
      = Root() / "Materials/.oxygen-0190f3a2-5c1b-7d2e-8f3a-1b2c3d4e5f60.tmp";
    const auto foreign = Root() / "Materials/Notes.txt";
    storage.AddFile(leftover, "left");
    storage.AddFile(sibling, "sibling");
    storage.AddFile(foreign, "notes");
    const auto data_path = Root() / "Resources/buffers.data";
    storage.AddFile(data_path, std::vector<std::byte>(9));

    static_cast<void>(LooseCookedWriter(Root(), storage));

    EXPECT_FALSE(Exists(storage, leftover));
    EXPECT_FALSE(Exists(storage, sibling));
    EXPECT_TRUE(Exists(storage, foreign));
    EXPECT_EQ(storage.Bytes(data_path).size(), 4U);
  }

  //! A data file only grows: bytes that do not begin with its committed ones
  //! are refused.
  NOLINT_TEST(LooseCookedWriterPublicationTest, ADataFileOnlyGrows)
  {
    auto storage = MemoryFileSystem {};
    const auto table = std::vector<std::byte>(
      data::loose_cooked::ResourceRowSize(FileKind::kBuffersTable));
    {
      auto writer = LooseCookedWriter(Root(), storage);
      writer.WriteFile(
        FileKind::kBuffersTable, "Resources/buffers.table", table);
      writer.WriteFile(FileKind::kBuffersData, "Resources/buffers.data",
        std::vector<std::byte>(4, std::byte { 1 }));
      static_cast<void>(writer.Finish());
    }
    auto writer = LooseCookedWriter(Root(), storage);

    NOLINT_EXPECT_THROW(
      writer.WriteFile(FileKind::kBuffersData, "Resources/buffers.data",
        std::vector<std::byte>(6, std::byte { 2 })),
      std::runtime_error);
    auto grown = std::vector<std::byte>(4, std::byte { 1 });
    grown.push_back(std::byte { 2 });
    NOLINT_EXPECT_NO_THROW(writer.WriteFile(
      FileKind::kBuffersData, "Resources/buffers.data", grown));
  }

  /*!
   Scenario: a wall of `/Content` uses a material of `/Engine`. The index
   records the requirement with the wall, keeps it while the wall is not
   cooked again, and drops it when a cook of the wall requires nothing.
  */
  NOLINT_TEST(LooseCookedWriterPublicationTest, RequirementsAreKeptPerAsset)
  {
    auto storage = MemoryFileSystem {};
    const auto requirement = IndexedRequirement {
      .mount = "Engine",
      .kind = data::KeyReferenceKind::kAsset,
      .key = data::AssetKey::FromVirtualPath("/Engine/Base.omat"),
      .type = AssetType::kMaterial,
    };
    const auto bytes = MaterialBytes("wall");
    const auto digest = ProducedDigest(bytes);
    const auto relpath
      = data::loose_cooked::ContentRelPath(kLayoutPath, digest.sha256);
    const auto register_wall
      = [&](const std::span<const IndexedRequirement> requirements) -> void {
      auto writer = LooseCookedWriter(Root(), storage);
      // The writer deletes an unlisted content file when it opens the root, so
      // the file is put there once the writer exists.
      storage.AddFile(Root() / relpath, bytes);
      writer.RegisterProducedAssetDescriptor(MakeFirstByteAssetKey(0x11),
        AssetType::kMaterial, kVirtualPath, relpath, digest, {},
        FromSource("/Content/Materials/Wall.omat.json"), requirements);
      static_cast<void>(writer.Finish());
    };

    register_wall(std::span { &requirement, 1 });
    static_cast<void>(LooseCookedWriter(Root(), storage).Finish());
    const auto kept = IndexOf(storage);
    register_wall({});

    ASSERT_EQ(kept.Requirements().size(), 1U);
    EXPECT_EQ(kept.Requirements().front().requirement, requirement);
    EXPECT_EQ(kept.Commit(), 2U);
    EXPECT_TRUE(IndexOf(storage).Requirements().empty());
  }

} // namespace
} // namespace oxygen::content::testing
