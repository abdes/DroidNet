//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/LooseCookedWriter.cpp

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <ios>
#include <stdexcept>

#include "LooseCookedWriterTestSupport.h"

#include <Oxygen/Cooker/Import/Internal/LooseCookedWriter.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Cooker/Test/Support/TempDir.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Testing/GTest.h>

// NOLINTBEGIN(*-magic-numbers)

namespace oxygen::content::testing {

namespace {

  using oxygen::content::import::LooseCookedWriter;
  using oxygen::cooker::test::ScopedTempDir;
  using CollisionPolicy
    = oxygen::content::import::LooseCookedWriter::CollisionPolicy;
  using oxygen::content::lc::Inspection;
  using oxygen::data::AssetType;
  using oxygen::data::loose_cooked::FileKind;

  NOLINT_TEST(LooseCookedWriterExternalRegistrationTest,
    ExternalAppendRefreshesMetadataWithoutCollisionOrStaleWriterRollback)
  {
    const ScopedTempDir temp;
    const auto root = temp.Path() / "external_append_refresh";
    const auto initial = std::array { std::byte { 1U } };
    {
      auto writer = LooseCookedWriter(root);
      writer.WriteFile(FileKind::kBuffersData, "buffers.data", initial);
      writer.WriteFile(FileKind::kBuffersTable, "buffers.table", initial);
      static_cast<void>(writer.Finish());
    }
    auto stale_writer = LooseCookedWriter(root);
    auto refreshed_writer = LooseCookedWriter(root);
    refreshed_writer.SetCollisionPolicy(CollisionPolicy::kError);
    {
      auto stream = serio::FileStream<>(
        root / "buffers.data", std::ios::out | std::ios::trunc);
      const auto appended = std::array { std::byte { 1U }, std::byte { 2U } };
      ASSERT_TRUE(stream.Write(appended));
      ASSERT_TRUE(stream.Flush());
    }
    refreshed_writer.RegisterExternalFile(
      FileKind::kBuffersData, "buffers.data");
    EXPECT_EQ(refreshed_writer.Finish().collision_summary.file_collisions, 0U);
    EXPECT_EQ(stale_writer.Finish().collision_summary.file_collisions, 0U);
    Inspection inspection;
    inspection.LoadFromRoot(root);
    const auto files = inspection.Files();
    const auto data = std::ranges::find(
      files, FileKind::kBuffersData, &Inspection::FileEntry::kind);
    ASSERT_NE(data, files.end());
    EXPECT_EQ(data->size, 2U);
  }

  NOLINT_TEST(LooseCookedWriterExternalRegistrationTest,
    ExternalDescriptorRejectsMalformedBytesBeforeIndexPublication)
  {
    const ScopedTempDir temp;
    const auto root = temp.Path() / "external_descriptor_rejection";
    std::filesystem::create_directories(root);
    const auto invalid_path = root / "invalid.omat";
    {
      serio::FileStream<> stream(invalid_path, std::ios::out);
      constexpr auto invalid = std::array { std::byte { 1U } };
      ASSERT_TRUE(stream.Write(invalid));
      ASSERT_TRUE(stream.Flush());
    }
    import::LooseCookedWriter writer(root);
    EXPECT_THROW(writer.RegisterExternalAssetDescriptor(
                   MakeFirstByteAssetKey(1U), data::AssetType::kMaterial,
                   "/Test/invalid.omat", "invalid.omat", 1U, {}),
      std::runtime_error);
    EXPECT_FALSE(std::filesystem::exists(root / "container.index.bin"));
    EXPECT_TRUE(writer.Finish().assets.empty());
  }
} // namespace

} // namespace oxygen::content::testing

// NOLINTEND(*-magic-numbers)
