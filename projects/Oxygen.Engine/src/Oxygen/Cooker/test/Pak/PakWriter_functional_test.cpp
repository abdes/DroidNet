//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Pak/PakWriter.cpp

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/PackedPak.h>
#include <Oxygen/Cooker/testing/PakWorkspace.h>
#include <Oxygen/Cooker/testing/PhysicsSidecarBuilder.h>
#include <Oxygen/Cooker/testing/SceneBuilder.h>
#include <Oxygen/Cooker/testing/Seeds.h>
#include <Oxygen/Core/Meta/Data/ResourceIndex.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/CookedSource.h>
#include <Oxygen/Serio/Testing/TestBytes.h>
#include <Oxygen/Testing/GTest.h>

namespace {
namespace data = oxygen::data;

using oxygen::ResourceIndexT;
using oxygen::cooker::test::HasNoErrors;
using oxygen::cooker::testing::LooseCookedRoot;
using oxygen::cooker::testing::PackedPak;
using oxygen::cooker::testing::PakWorkspace;
using oxygen::cooker::testing::PhysicsSidecarBuilder;
using oxygen::cooker::testing::SceneBuilder;
using oxygen::cooker::testing::SeedBytes;
using oxygen::serio::testing::BytesEqual;
using oxygen::serio::testing::PatternBytes;
using oxygen::serio::testing::PatternSeed;

//! Checks the pak lists `key` as a `type` whose descriptor is `bytes`.
auto ExpectDescriptor(const PackedPak& pak, const data::AssetKey& key,
  const data::AssetType type, const std::span<const std::byte> bytes) -> void
{
  SCOPED_TRACE(data::to_string(type));
  const auto entry = pak.Pak().FindEntry(key);
  ASSERT_HAS_VALUE(entry);
  EXPECT_EQ(entry->asset_type, type);
  EXPECT_THAT(pak.Descriptor(key), BytesEqual(bytes));
}

class PakWriterTest : public ::testing::Test {
protected:
  [[nodiscard]] auto Workspace() -> PakWorkspace& { return workspace_; }

  //! The smallest library a pak is built from: one empty scene.
  auto MinimalSource() -> data::CookedSource
  {
    auto game = LooseCookedRoot {};
    game.Scene("Scenes/Main.oscene", SceneBuilder { "Empty" });
    return workspace_.Add(game);
  }

private:
  PakWorkspace workspace_;
};

//! The writer hashes the pak as it writes it and patches the CRC into the
//! footer; opening the pak checks it.
NOLINT_TEST_F(PakWriterTest, CrcEnabledWritesValidPakAndPatchesFooterCrc)
{
  const auto request = Workspace().FullRequest({ MinimalSource() });

  const auto packed = PakWorkspace::Pack(request);

  ASSERT_THAT(packed.Diagnostics(), HasNoErrors());
  ASSERT_HAS_VALUE(packed.written);
  ASSERT_HAS_VALUE(packed.planned.plan);
  EXPECT_EQ(packed.written->file_size, packed.planned.plan->PlannedFileSize());
  EXPECT_NE(packed.written->pak_crc32, 0U);
  EXPECT_TRUE(packed.written->writing_duration.has_value());
  const auto pak = Workspace().Open(request.output_pak_path);
  EXPECT_EQ(pak.Pak().Footer().pak_crc32, packed.written->pak_crc32);
}

NOLINT_TEST_F(
  PakWriterTest, FullModeWritesInputAssetDescriptorsIntoFinalPakDirectory)
{
  constexpr auto kActionSize = size_t { 48U };
  constexpr auto kContextSize = size_t { 64U };
  auto game = LooseCookedRoot {};
  const auto action_bytes = PatternBytes(kActionSize);
  const auto context_bytes = PatternBytes(kContextSize, PatternSeed::kSecond);
  const auto scene_bytes = SceneBuilder { "Empty" }.Bytes();
  const auto action = game.Add({
    .type = data::AssetType::kInputAction,
    .leaf = "Input/Move.oiact",
    .descriptor = action_bytes,
  });
  const auto context = game.Add({
    .type = data::AssetType::kInputMappingContext,
    .leaf = "Input/Gameplay.oimap",
    .descriptor = context_bytes,
  });
  const auto scene = game.Add({
    .type = data::AssetType::kScene,
    .leaf = "Scenes/Main.oscene",
    .descriptor = scene_bytes,
  });

  const auto pak = Workspace().PackOrThrow(
    Workspace().FullRequest({ Workspace().Add(game) }));

  ASSERT_EQ(pak.Pak().Directory().size(), 3U);
  ExpectDescriptor(pak, action, data::AssetType::kInputAction, action_bytes);
  ExpectDescriptor(
    pak, context, data::AssetType::kInputMappingContext, context_bytes);
  ExpectDescriptor(pak, scene, data::AssetType::kScene, scene_bytes);
}

NOLINT_TEST_F(
  PakWriterTest, FullModeWritesPhysicsSceneDescriptorsIntoFinalPakDirectory)
{
  auto game = LooseCookedRoot {};
  const auto main = SceneBuilder { "Main" };
  const auto scene = game.Scene("Main.oscene", main);
  const auto sidecar = game.PhysicsSidecar("Scenes/Main.opscene", scene);
  // The sidecar's descriptor name is its file name.
  const auto sidecar_bytes
    = PhysicsSidecarBuilder {}.Bytes(scene, main.Bytes(), "Main");

  const auto pak = Workspace().PackOrThrow(
    Workspace().FullRequest({ Workspace().Add(game) }));

  ASSERT_EQ(pak.Pak().Directory().size(), 2U);
  ExpectDescriptor(pak, sidecar, data::AssetType::kPhysicsScene, sidecar_bytes);
}

//! Packing one request twice, into two paks, yields the same bytes.
NOLINT_TEST_F(PakWriterTest, DeterministicModeProducesBitExactOutputs)
{
  const auto source = MinimalSource();
  const auto first_request = Workspace().FullRequest({ source }, "first.pak");
  auto second_request = first_request;
  second_request.output_pak_path = PakWorkspace::PakPath("second.pak");

  const auto first = PakWorkspace::Pack(first_request);
  const auto second = PakWorkspace::Pack(second_request);

  ASSERT_THAT(first.Diagnostics(), HasNoErrors());
  ASSERT_THAT(second.Diagnostics(), HasNoErrors());
  ASSERT_HAS_VALUE(first.written);
  ASSERT_HAS_VALUE(second.written);
  EXPECT_EQ(first.written->pak_crc32, second.written->pak_crc32);
  EXPECT_EQ(first.written->file_size, second.written->file_size);
  EXPECT_THAT(Workspace().Storage().Bytes(second_request.output_pak_path),
    BytesEqual(Workspace().Storage().Bytes(first_request.output_pak_path)));
}

//! A loose texture descriptor's virtual path reaches the pak's browse index and
//! resolves to the texture's index in the pak, which drops the texture no
//! descriptor lists.
NOLINT_TEST_F(PakWriterTest, TextureDescriptorPathsResolveInThePak)
{
  constexpr auto kUnlistedSeed = uint8_t { 0x11U };
  constexpr auto kSkySeed = uint8_t { 0x22U };
  auto content = LooseCookedRoot { "Content" };
  static_cast<void>(content.AddTexture(SeedBytes(kUnlistedSeed)));
  static_cast<void>(content.DescribeTexture(
    "Textures/Sky.otex", content.AddTexture(SeedBytes(kSkySeed))));

  const auto pak = Workspace().PackOrThrow(
    Workspace().FullRequest({ Workspace().Add(content) }));

  EXPECT_THAT(pak.Pak().BrowseTextures(), ::testing::SizeIs(1U));
  EXPECT_EQ(
    pak.Pak().ResolveTextureIndexByVirtualPath("/Content/Textures/Sky.otex"),
    ResourceIndexT { 1U });
  EXPECT_FALSE(pak.Pak().ResolveTextureIndexByVirtualPath(
    "/Content/Textures/Missing.otex"));
}

} // namespace
