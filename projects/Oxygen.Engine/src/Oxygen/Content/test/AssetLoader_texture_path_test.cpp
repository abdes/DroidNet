//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <ios>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "AssetLoader_test.h"
#include "Fixtures/LooseCookedTestWriter.h"

#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/TextureResourceDescriptor.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Writer.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::testing {
namespace {

  using Descriptor = data::pak::core::TextureResourceDesc;

  constexpr auto kMeterPath
    = std::string_view { "/Content/Textures/Meter.otex" };
  constexpr auto kMeterFile = std::string_view { "Textures/Meter.otex" };

  auto SidecarBytes(const Descriptor& descriptor,
    const std::uint32_t index = 1U) -> std::vector<std::byte>
  {
    return data::EncodeTextureResourceDescriptor({
      .index = data::pak::core::ResourceIndexT { index },
      .descriptor = descriptor,
    });
  }

  auto WriteSidecar(const std::filesystem::path& path,
    const Descriptor& descriptor, const std::uint32_t index = 1U) -> void
  {
    auto stream = serio::FileStream<>(path, std::ios::out);
    auto writer = serio::Writer(stream);
    ASSERT_TRUE(writer.WriteBlob(SidecarBytes(descriptor, index)));
  }

  struct SourceRecipe {
    std::uint8_t identity {};
    std::uint32_t width {};
  };

  //! Writes a root with one texture recorded under `kMeterPath`.
  auto WriteSource(const std::filesystem::path& root, const SourceRecipe recipe)
    -> Descriptor
  {
    auto descriptor = Descriptor {};
    descriptor.width = recipe.width;
    descriptor.height = 1U;
    descriptor.depth = 1U;
    descriptor.array_layers = 1U;
    descriptor.mip_levels = 1U;
    descriptor.size_bytes = 4U;
    auto records = std::array { Descriptor {}, descriptor };
    const auto payload = std::array<std::byte, 4> {};
    auto writer = LooseCookedTestWriter(root);
    writer.WriteFile(data::loose_cooked::FileKind::kTexturesTable,
      "textures.table", std::as_bytes(std::span { records }));
    writer.WriteFile(
      data::loose_cooked::FileKind::kTexturesData, "textures.data", payload);
    writer.WriteFile(data::loose_cooked::FileKind::kTextureDescriptor,
      std::string(kMeterFile), SidecarBytes(descriptor),
      std::string(kMeterPath));
    const auto written = writer.Finish();
    auto stream = serio::FileStream<>(written.index_path);
    auto index = serio::Writer(stream);
    if (!stream.Seek(offsetof(data::loose_cooked::IndexHeader, source_identity))
      || !index.Write(recipe.identity) || !stream.Flush()) {
      throw std::runtime_error("Test source identity could not be written");
    }
    return descriptor;
  }

  //! Runtime key of texture 1 in the mounted source opened from `root`.
  auto TextureKeyIn(IAssetLoader& loader, const std::filesystem::path& root)
    -> std::optional<ResourceKey>
  {
    for (const auto& source : loader.EnumerateMountedSources()) {
      if (std::filesystem::equivalent(source.source_path, root)) {
        return loader.MakeTextureResourceKey(
          source.source_key, data::pak::core::ResourceIndexT { 1U });
      }
    }
    return std::nullopt;
  }

  //! The newest mount recording a texture path wins, as it does for assets.
  NOLINT_TEST_F(AssetLoaderBasicTest, TexturePathResolvesInTheNewestMount)
  {
    // Arrange
    const auto first = temp_dir_ / "first";
    const auto second = temp_dir_ / "second";
    static_cast<void>(WriteSource(first, { .identity = 31U, .width = 1U }));
    static_cast<void>(WriteSource(second, { .identity = 32U, .width = 2U }));

    // Act
    asset_loader_->AddLooseCookedRoot(first);
    asset_loader_->AddLooseCookedRoot(second);
    const auto second_wins
      = asset_loader_->ResolveTextureResourceKey(kMeterPath);
    const auto expected_second = TextureKeyIn(*asset_loader_, second);
    asset_loader_->ClearMounts();
    asset_loader_->AddLooseCookedRoot(second);
    asset_loader_->AddLooseCookedRoot(first);
    const auto first_wins
      = asset_loader_->ResolveTextureResourceKey(kMeterPath);
    const auto expected_first = TextureKeyIn(*asset_loader_, first);

    // Assert
    ASSERT_TRUE(expected_second.has_value());
    ASSERT_TRUE(expected_first.has_value());
    EXPECT_EQ(second_wins, expected_second);
    EXPECT_EQ(first_wins, expected_first);
    EXPECT_FALSE(asset_loader_->ResolveTextureResourceKey(
      "/Content/Textures/Missing.otex"));
  }

  //! A texture field naming another source's texture by key resolves in the
  //! newest mount holding it, and to nothing when no mount does.
  NOLINT_TEST_F(AssetLoaderBasicTest, ExternalTextureBindingResolvesByKey)
  {
    // Arrange
    const auto first = temp_dir_ / "first";
    const auto second = temp_dir_ / "second";
    static_cast<void>(WriteSource(first, { .identity = 33U, .width = 1U }));
    static_cast<void>(WriteSource(second, { .identity = 34U, .width = 2U }));
    auto material = data::MaterialAsset(
      data::AssetKey {}, data::pak::render::MaterialAssetDesc {});
    auto references = data::AssetReferences::Create(
      { {
        .kind = data::ResourceKind::kExternalTexture,
        .index = data::pak::core::ResourceIndexT { 0U },
      } },
      { {
        .key = data::TextureKeyFromVirtualPath(kMeterPath),
        .kind = data::KeyReferenceKind::kTexture,
      } });
    ASSERT_TRUE(references.has_value());
    material.SetReferences(std::move(*references));
    asset_loader_->AddLooseCookedRoot(first);
    asset_loader_->AddLooseCookedRoot(second);

    // Act
    const auto resolved = asset_loader_->MakeTextureResourceKeyForAsset(
      material, data::ResourceReferenceIndex { 0U });
    const auto expected = TextureKeyIn(*asset_loader_, second);
    asset_loader_->ClearMounts();
    const auto unresolved = asset_loader_->MakeTextureResourceKeyForAsset(
      material, data::ResourceReferenceIndex { 0U });

    // Assert
    ASSERT_TRUE(expected.has_value());
    EXPECT_EQ(resolved, expected);
    EXPECT_FALSE(unresolved.has_value());
  }

  //! A recorded descriptor that disagrees with its table, or a path that is
  //! not canonical, is an error rather than an absent texture.
  NOLINT_TEST_F(AssetLoaderBasicTest, TexturePathRejectsInvalidDescriptors)
  {
    // Arrange
    const auto root = temp_dir_ / "source";
    const auto descriptor = WriteSource(root, { .identity = 33U, .width = 1U });
    asset_loader_->AddLooseCookedRoot(root);
    const auto sidecar = root / std::filesystem::path(kMeterFile);
    const auto resolve = [this]() -> void {
      static_cast<void>(asset_loader_->ResolveTextureResourceKey(kMeterPath));
    };
    auto stale = descriptor;
    stale.width = 2U;
    constexpr std::uint32_t kOutOfRangeTextureIndex = 99U;
    constexpr std::size_t kReservedFieldOffset = 6U;

    // Act / Assert
    EXPECT_THROW(static_cast<void>(asset_loader_->ResolveTextureResourceKey(
                   "Content/Textures/Meter.otex")),
      std::invalid_argument);
    WriteSidecar(sidecar, stale);
    EXPECT_THROW(resolve(), std::runtime_error);
    WriteSidecar(sidecar, descriptor, 0U);
    EXPECT_THROW(resolve(), std::runtime_error);
    WriteSidecar(sidecar, descriptor, kOutOfRangeTextureIndex);
    EXPECT_THROW(resolve(), std::runtime_error);
    WriteSidecar(sidecar, descriptor);
    {
      auto stream = serio::FileStream<>(sidecar);
      auto writer = serio::Writer(stream);
      ASSERT_TRUE(stream.Seek(kReservedFieldOffset));
      ASSERT_TRUE(writer.Write(std::uint16_t { 1U }));
    }
    EXPECT_THROW(resolve(), std::runtime_error);
  }

} // namespace

//! A refreshed source gets a fresh runtime key for the same texture path.
NOLINT_TEST_F(AssetLoaderBasicTest,
  TextureSourceIdentityRebindsToFreshRuntimeKeyAfterRefresh)
{
  const auto root = temp_dir_ / "source";
  static_cast<void>(WriteSource(root, { .identity = 1U, .width = 1U }));
  asset_loader_->AddLooseCookedRoot(root);
  const auto source = asset_loader_->EnumerateMountedSources().front();
  const auto key = asset_loader_->MakeTextureResourceKey(
    source.source_key, data::pak::core::ResourceIndexT { 1U });
  ASSERT_TRUE(key);
  EXPECT_EQ(key, asset_loader_->ResolveTextureResourceKey(kMeterPath));
  EXPECT_FALSE(asset_loader_->MakeTextureResourceKey(
    source.source_key, data::pak::core::ResourceIndexT { 0U }));
  EXPECT_FALSE(asset_loader_->MakeTextureResourceKey(
    source.source_key, data::pak::core::ResourceIndexT { 2U }));
  asset_loader_->AddLooseCookedRoot(root);
  const auto refreshed = asset_loader_->MakeTextureResourceKey(
    source.source_key, data::pak::core::ResourceIndexT { 1U });
  ASSERT_TRUE(refreshed);
  EXPECT_NE(key, refreshed);
  EXPECT_EQ(refreshed, asset_loader_->ResolveTextureResourceKey(kMeterPath));
  asset_loader_->ClearMounts();
  EXPECT_FALSE(asset_loader_->MakeTextureResourceKey(
    source.source_key, data::pak::core::ResourceIndexT { 1U }));
}

} // namespace oxygen::content::testing
