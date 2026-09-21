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
#include <span>
#include <stdexcept>

#include "AssetLoader_test.h"
#include "Fixtures/LooseCookedTestWriter.h"

#include <Oxygen/Content/TextureResourceLocator.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Writer.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::testing {
namespace {

  using Descriptor = data::pak::core::TextureResourceDesc;

  auto WriteSidecar(const std::filesystem::path& path,
    const Descriptor& descriptor, const std::uint32_t index = 1U) -> void
  {
    constexpr auto kMagic = std::array { 'O', 'T', 'E', 'X' };
    std::filesystem::create_directories(path.parent_path());
    auto stream = serio::FileStream<>(path, std::ios::out);
    auto writer = serio::Writer(stream);
    const auto packed = writer.ScopedAlignment(1);
    ASSERT_TRUE(writer.WriteBlob(std::as_bytes(std::span { kMagic })));
    ASSERT_TRUE(writer.Write(std::uint16_t { 1U }));
    ASSERT_TRUE(writer.Write(std::uint16_t { 0U }));
    ASSERT_TRUE(writer.Write(index));
    ASSERT_TRUE(writer.WriteBlob(std::as_bytes(std::span { &descriptor, 1U })));
  }

  struct SourceRecipe {
    std::uint8_t identity {};
    std::uint32_t width {};
  };

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
    const auto written = writer.Finish();
    auto stream = serio::FileStream<>(written.index_path);
    auto index = serio::Writer(stream);
    if (!stream.Seek(offsetof(data::loose_cooked::IndexHeader, source_identity))
      || !index.Write(recipe.identity) || !stream.Flush()) {
      throw std::runtime_error("Test source identity could not be written");
    }
    WriteSidecar(root / "Textures/Meter.otex", descriptor);
    return descriptor;
  }

  NOLINT_TEST_F(AssetLoaderBasicTest,
    TextureLocatorKeepsSourceIdentityAndRejectsStaleDescriptors)
  {
    const auto first = temp_dir_ / "first";
    const auto second = temp_dir_ / "second";
    const auto first_descriptor
      = WriteSource(first, { .identity = 31U, .width = 1U });
    static_cast<void>(WriteSource(second, { .identity = 32U, .width = 2U }));
    asset_loader_->AddLooseCookedRoot(first);
    asset_loader_->AddLooseCookedRoot(second);
    const auto first_locator = TextureResourceLocator {
      .cooked_root = first,
      .descriptor_relative_path = "Textures/Meter.otex",
    };
    const auto second_locator = TextureResourceLocator {
      .cooked_root = second,
      .descriptor_relative_path = "Textures/Meter.otex",
    };
    const auto first_key
      = asset_loader_->ResolveTextureResourceKey(first_locator);
    const auto second_key
      = asset_loader_->ResolveTextureResourceKey(second_locator);
    ASSERT_TRUE(first_key.has_value());
    ASSERT_TRUE(second_key.has_value());
    EXPECT_NE(*first_key, *second_key);
    WriteSidecar(second / "Textures/Meter.otex", first_descriptor);
    EXPECT_THROW(static_cast<void>(
                   asset_loader_->ResolveTextureResourceKey(second_locator)),
      std::runtime_error);
    WriteSidecar(first / "Textures/Meter.otex", first_descriptor, 0U);
    EXPECT_THROW(static_cast<void>(
                   asset_loader_->ResolveTextureResourceKey(first_locator)),
      std::runtime_error);
    constexpr std::uint32_t kOutOfRangeTextureIndex = 99U;
    WriteSidecar(
      first / "Textures/Meter.otex", first_descriptor, kOutOfRangeTextureIndex);
    EXPECT_THROW(static_cast<void>(
                   asset_loader_->ResolveTextureResourceKey(first_locator)),
      std::runtime_error);
    EXPECT_FALSE(asset_loader_->ResolveTextureResourceKey(
      { temp_dir_ / "missing", "Textures/Meter.otex" }));
    EXPECT_THROW(static_cast<void>(asset_loader_->ResolveTextureResourceKey(
                   { first, "../second/Textures/Meter.otex" })),
      std::invalid_argument);
  }

  NOLINT_TEST_F(AssetLoaderBasicTest,
    TextureLocatorAcceptsOnlyAUniqueCurrentHashedDescriptor)
  {
    const auto root = temp_dir_ / "source";
    const auto descriptor = WriteSource(root, { .identity = 33U, .width = 1U });
    asset_loader_->AddLooseCookedRoot(root);
    std::filesystem::remove(root / "Textures/Meter.otex");
    const auto locator = TextureResourceLocator {
      .cooked_root = root,
      .descriptor_relative_path = "Textures/Meter.otex",
    };
    EXPECT_FALSE(asset_loader_->ResolveTextureResourceKey(locator));
    WriteSidecar(root / "Textures/Meter_0123456789abcdef.otex", descriptor);
    EXPECT_TRUE(asset_loader_->ResolveTextureResourceKey(locator));
    WriteSidecar(root / "Textures/Meter_abcdef0123456789.otex", descriptor);
    EXPECT_THROW(
      static_cast<void>(asset_loader_->ResolveTextureResourceKey(locator)),
      std::runtime_error);
    WriteSidecar(root / "Textures/Meter.otex", descriptor);
    {
      auto stream = serio::FileStream<>(root / "Textures/Meter.otex");
      auto writer = serio::Writer(stream);
      constexpr std::size_t kReservedFieldOffset = 6U;
      ASSERT_TRUE(stream.Seek(kReservedFieldOffset));
      ASSERT_TRUE(writer.Write(std::uint16_t { 1U }));
    }
    EXPECT_THROW(
      static_cast<void>(asset_loader_->ResolveTextureResourceKey(locator)),
      std::runtime_error);
  }

} // namespace
} // namespace oxygen::content::testing
