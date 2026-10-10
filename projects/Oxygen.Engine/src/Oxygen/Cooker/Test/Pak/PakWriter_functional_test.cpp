//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Pak/PakWriter.cpp

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "PakTestSupport.h"
#include "PakWriterTestSupport.h"

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Cooker/Pak/PakPlanBuilder.h>
#include <Oxygen/Cooker/Pak/PakWriter.h>
#include <Oxygen/Cooker/Test/Support/DescriptorFixtures.h>
#include <Oxygen/Data/PakFormatSerioLoaders.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Testing/GTest.h>

namespace {
namespace content = oxygen::content;
namespace core = oxygen::data::pak::core;
namespace data = oxygen::data;
namespace pak = oxygen::content::pak;
namespace paktest = oxygen::content::pak::test;
namespace cooktest = oxygen::cooker::test;
namespace serio = oxygen::serio;

using paktest::HasError;
using paktest::MakeAssetKey;
using paktest::MakePatternBytes;

auto ReadFooter(const std::filesystem::path& path) -> core::PakFooter
{
  auto stream = serio::FileStream<>(path, std::ios::binary | std::ios::in);
  const auto size_result = stream.Size();
  if (!size_result.has_value()) {
    throw std::runtime_error("cannot query size of " + path.string());
  }
  if (size_result.value() < sizeof(core::PakFooter)) {
    throw std::runtime_error(
      "file too small for a pak footer: " + path.string());
  }
  if (!stream.Seek(size_result.value() - sizeof(core::PakFooter))) {
    throw std::runtime_error("cannot seek to pak footer in " + path.string());
  }

  auto reader = serio::Reader(stream);
  auto align_guard = reader.ScopedAlignment(1);
  (void)align_guard;
  auto footer_result = reader.Read<core::PakFooter>();
  if (!footer_result.has_value()) {
    throw std::runtime_error("cannot read pak footer from " + path.string());
  }
  return footer_result.value();
}

class PakWriterTest : public paktest::TempDirFixture { };

NOLINT_TEST_F(PakWriterTest, CrcEnabledWritesValidPakAndPatchesFooterCrc)
{
  using pak::BuildMode;
  using pak::PakPlanBuilder;
  using pak::PakWriter;

  const auto output_path = Root() / "crc_enabled.pak";
  const auto request
    = paktest::MakeFullRequest(output_path, { .content_version = 3U });

  const auto plan_result = PakPlanBuilder {}.Build(request);
  ASSERT_FALSE(HasError(plan_result.diagnostics));
  ASSERT_TRUE(plan_result.plan.has_value());

  const auto write_result = PakWriter {}.Write(request, *plan_result.plan);
  ASSERT_FALSE(HasError(write_result.diagnostics));
  EXPECT_EQ(write_result.file_size, plan_result.plan->PlannedFileSize());
  EXPECT_NE(write_result.pak_crc32, 0U);
  EXPECT_TRUE(write_result.writing_duration.has_value());

  const auto footer = ReadFooter(output_path);
  EXPECT_EQ(footer.pak_crc32, write_result.pak_crc32);

  EXPECT_NO_THROW({
    auto pak_file = content::PakFile(output_path);
    pak_file.ValidateCrc32Integrity();
  });
}

NOLINT_TEST_F(
  PakWriterTest, FullModeWritesInputAssetDescriptorsIntoFinalPakDirectory)
{
  using data::CookedSource;
  using data::CookedSourceKind;
  using pak::BuildMode;
  using pak::PakPlanBuilder;
  using pak::PakWriter;

  const auto source = Root() / "input_assets_loose";
  const auto output_path = Root() / "input_assets_output.pak";

  const auto action_desc_rel = std::string { "Descriptors/Input/Move.oiact" };
  const auto context_desc_rel
    = std::string { "Descriptors/Input/Gameplay.oimap" };
  const auto scene_desc_rel = std::string { "Descriptors/Scenes/Main.oscene" };

  const auto action_desc_bytes = MakePatternBytes(0x31U, 48U);
  const auto context_desc_bytes = MakePatternBytes(0x41U, 64U);
  const auto scene_desc_bytes
    = oxygen::content::test::MakeEmptySceneDescriptor();

  const auto action_key = MakeAssetKey(0x61U);
  const auto context_key = MakeAssetKey(0x62U);
  const auto scene_key = MakeAssetKey(0x63U);
  const auto assets = std::array<paktest::AssetSpec, 3> {
    paktest::AssetSpec {
      .key = action_key,
      .asset_type = data::AssetType::kInputAction,
      .descriptor_relpath = action_desc_rel,
      .virtual_path = "/Game/Input/Move.oiact",
      .descriptor_size = static_cast<uint64_t>(action_desc_bytes.size()),
      .descriptor_sha = paktest::MakeDigest(0x61U),
      .descriptor_payload = action_desc_bytes,
    },
    paktest::AssetSpec {
      .key = context_key,
      .asset_type = data::AssetType::kInputMappingContext,
      .descriptor_relpath = context_desc_rel,
      .virtual_path = "/Game/Input/Gameplay.oimap",
      .descriptor_size = static_cast<uint64_t>(context_desc_bytes.size()),
      .descriptor_sha = paktest::MakeDigest(0x62U),
      .descriptor_payload = context_desc_bytes,
    },
    paktest::AssetSpec {
      .key = scene_key,
      .asset_type = data::AssetType::kScene,
      .descriptor_relpath = scene_desc_rel,
      .virtual_path = "/Game/Scenes/Main.oscene",
      .descriptor_size = static_cast<uint64_t>(scene_desc_bytes.size()),
      .descriptor_sha = paktest::MakeDigest(0x63U),
      .descriptor_payload = scene_desc_bytes,
    },
  };
  ASSERT_TRUE(paktest::WriteLooseIndex(source,
    std::span<const paktest::AssetSpec>(assets.data(), assets.size()),
    std::span<const paktest::FileSpec> {}, 0x71U));

  const auto request = paktest::MakeFullRequest(output_path,
    { .sources = { CookedSource {
        .kind = CookedSourceKind::kLooseCooked, .path = source } },
      .content_version = 1U,
      .embed_browse_index = true });

  const auto plan_result = PakPlanBuilder {}.Build(request);
  ASSERT_FALSE(HasError(plan_result.diagnostics));
  ASSERT_TRUE(plan_result.plan.has_value());

  const auto write_result = PakWriter {}.Write(request, *plan_result.plan);
  ASSERT_FALSE(HasError(write_result.diagnostics));

  const auto pak_bytes = cooktest::ReadBytes(output_path);
  auto pak_file = content::PakFile(output_path);
  const auto directory = pak_file.Directory();
  ASSERT_EQ(directory.size(), assets.size());

  auto saw_input_action = false;
  auto saw_input_mapping_context = false;
  auto saw_scene = false;
  for (const auto& entry : directory) {
    const auto descriptor_offset = static_cast<size_t>(entry.desc_offset);
    const auto descriptor_size = static_cast<size_t>(entry.desc_size);
    ASSERT_LE(descriptor_offset + descriptor_size, pak_bytes.size());
    const auto payload = std::span<const std::byte>(
      pak_bytes.data() + descriptor_offset, descriptor_size);

    if (entry.asset_key == action_key) {
      EXPECT_EQ(static_cast<data::AssetType>(entry.asset_type),
        data::AssetType::kInputAction);
      EXPECT_EQ(payload.size(), action_desc_bytes.size());
      EXPECT_TRUE(std::equal(payload.begin(), payload.end(),
        action_desc_bytes.begin(), action_desc_bytes.end()));
      saw_input_action = true;
    } else if (entry.asset_key == context_key) {
      EXPECT_EQ(static_cast<data::AssetType>(entry.asset_type),
        data::AssetType::kInputMappingContext);
      EXPECT_EQ(payload.size(), context_desc_bytes.size());
      EXPECT_TRUE(std::equal(payload.begin(), payload.end(),
        context_desc_bytes.begin(), context_desc_bytes.end()));
      saw_input_mapping_context = true;
    } else if (entry.asset_key == scene_key) {
      EXPECT_EQ(static_cast<data::AssetType>(entry.asset_type),
        data::AssetType::kScene);
      EXPECT_EQ(payload.size(), scene_desc_bytes.size());
      EXPECT_TRUE(std::equal(payload.begin(), payload.end(),
        scene_desc_bytes.begin(), scene_desc_bytes.end()));
      saw_scene = true;
    } else {
      FAIL() << "Unexpected asset key in directory";
    }
  }

  EXPECT_TRUE(saw_input_action);
  EXPECT_TRUE(saw_input_mapping_context);
  EXPECT_TRUE(saw_scene);
}

NOLINT_TEST_F(
  PakWriterTest, FullModeWritesPhysicsSceneDescriptorsIntoFinalPakDirectory)
{
  using data::CookedSource;
  using data::CookedSourceKind;
  using pak::BuildMode;
  using pak::PakPlanBuilder;
  using pak::PakWriter;

  const auto source = Root() / "physics_assets_loose";
  const auto output_path = Root() / "physics_assets_output.pak";

  const auto physics_desc_rel = std::string { "Scenes/Main.opscene" };
  const auto scene_key = MakeAssetKey(0xA2U);
  const auto scene_bytes = oxygen::content::test::MakeEmptySceneDescriptor();
  data::pak::physics::PhysicsSceneAssetDesc physics_desc {};
  physics_desc.header.asset_type
    = static_cast<uint8_t>(data::AssetType::kPhysicsScene);
  physics_desc.header.version = 1U;
  physics_desc.target_scene_key = scene_key;
  const auto hash = oxygen::base::ComputeSha256(scene_bytes);
  std::ranges::copy(hash, std::begin(physics_desc.target_scene_content_hash));
  const auto physics_desc_bytes = std::as_bytes(std::span(&physics_desc, 1U));

  const auto physics_key = MakeAssetKey(0xA1U);
  const auto assets = std::array<paktest::AssetSpec, 2> {
    paktest::AssetSpec {
      .key = physics_key,
      .asset_type = data::AssetType::kPhysicsScene,
      .descriptor_relpath = physics_desc_rel,
      .virtual_path = "/Game/Scenes/Main.opscene",
      .descriptor_size = static_cast<uint64_t>(physics_desc_bytes.size()),
      .descriptor_sha = oxygen::base::ComputeSha256(physics_desc_bytes),
      .descriptor_payload
      = { physics_desc_bytes.begin(), physics_desc_bytes.end() },
      .references = data::AssetReferences::Create({},
        { { .key = scene_key,
          .kind = data::KeyReferenceKind::kLogical,
          .expected_type = data::AssetType::kScene } })
        .value(),
    },
    paktest::AssetSpec {
      .key = scene_key,
      .asset_type = data::AssetType::kScene,
      .descriptor_relpath = "Scenes/Main.oscene",
      .virtual_path = "/Game/Main.oscene",
      .descriptor_size = scene_bytes.size(),
      .descriptor_sha = {},
      .descriptor_payload = scene_bytes,
      .references = {},
    },
  };
  ASSERT_TRUE(paktest::WriteLooseIndex(source,
    std::span<const paktest::AssetSpec>(assets.data(), assets.size()),
    std::span<const paktest::FileSpec> {}, 0xA2U));

  const auto request = paktest::MakeFullRequest(output_path,
    { .sources = { CookedSource {
        .kind = CookedSourceKind::kLooseCooked, .path = source } },
      .content_version = 1U,
      .embed_browse_index = true });

  const auto plan_result = PakPlanBuilder {}.Build(request);
  ASSERT_FALSE(HasError(plan_result.diagnostics));
  ASSERT_TRUE(plan_result.plan.has_value());

  const auto write_result = PakWriter {}.Write(request, *plan_result.plan);
  ASSERT_FALSE(HasError(write_result.diagnostics));

  const auto pak_bytes = cooktest::ReadBytes(output_path);
  auto pak_file = content::PakFile(output_path);
  const auto directory = pak_file.Directory();
  ASSERT_EQ(directory.size(), assets.size());

  const auto found = pak_file.FindEntry(physics_key);
  ASSERT_TRUE(found.has_value());
  const auto& entry = *found;
  EXPECT_EQ(entry.asset_key, physics_key);
  EXPECT_EQ(static_cast<data::AssetType>(entry.asset_type),
    data::AssetType::kPhysicsScene);

  const auto descriptor_offset = static_cast<size_t>(entry.desc_offset);
  const auto descriptor_size = static_cast<size_t>(entry.desc_size);
  ASSERT_LE(descriptor_offset + descriptor_size, pak_bytes.size());

  const auto payload = std::span<const std::byte>(
    pak_bytes.data() + descriptor_offset, descriptor_size);
  ASSERT_EQ(payload.size(), physics_desc_bytes.size());
  EXPECT_TRUE(std::equal(payload.begin(), payload.end(),
    physics_desc_bytes.begin(), physics_desc_bytes.end()));
}

NOLINT_TEST_F(PakWriterTest, DeterministicModeProducesBitExactOutputs)
{
  using pak::BuildMode;
  using pak::PakPlanBuilder;
  using pak::PakWriter;

  auto request = paktest::MakeFullRequest(Root() / "deterministic_a.pak",
    { .content_version = 9U, .embed_browse_index = true });

  const auto plan_result = PakPlanBuilder {}.Build(request);
  ASSERT_FALSE(HasError(plan_result.diagnostics));
  ASSERT_TRUE(plan_result.plan.has_value());

  const auto first = PakWriter {}.Write(request, *plan_result.plan);
  ASSERT_FALSE(HasError(first.diagnostics));

  request.output_pak_path = Root() / "deterministic_b.pak";
  const auto second = PakWriter {}.Write(request, *plan_result.plan);
  ASSERT_FALSE(HasError(second.diagnostics));

  const auto first_bytes = cooktest::ReadBytes(Root() / "deterministic_a.pak");
  const auto second_bytes = cooktest::ReadBytes(Root() / "deterministic_b.pak");

  EXPECT_EQ(first.pak_crc32, second.pak_crc32);
  EXPECT_EQ(first.file_size, second.file_size);
  EXPECT_EQ(first_bytes, second_bytes);
}

} // namespace
