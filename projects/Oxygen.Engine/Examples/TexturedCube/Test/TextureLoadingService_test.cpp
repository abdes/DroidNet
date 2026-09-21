//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <ios>
#include <span>
#include <stdexcept>
#include <vector>

#include "TexturedCube/TextureLoadingService.h"

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/Loaders/TextureLoader.h>
#include <Oxygen/Content/Test/AssetLoader_test.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedWriter.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Event.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Test/Fixtures/TextureBinderPayloads.h>

namespace {

using oxygen::content::AssetLoader;
using oxygen::content::AssetLoaderConfig;
using oxygen::content::import::LooseCookedWriter;
using oxygen::data::loose_cooked::FileKind;
using oxygen::data::pak::core::TextureResourceDesc;
using oxygen::examples::textured_cube::TextureLoadingService;

class TextureLoadingServiceTest
  : public oxygen::content::testing::AssetLoaderBasicTest { };

auto WriteBytes(const std::filesystem::path& path,
  const std::span<const std::byte> bytes) -> void
{
  oxygen::serio::FileStream<> stream(path, std::ios::out);
  if (!stream.Write(bytes) || !stream.Flush()) {
    throw std::runtime_error("Could not write cooked texture test input");
  }
}

auto PublishTextures(const std::filesystem::path& root, const size_t count)
  -> void
{
  std::filesystem::create_directories(root);
  const auto packed
    = oxygen::vortex::testing::MakeCookedTexture1x1Rgba8Payload();
  auto descriptor = TextureResourceDesc {};
  std::memcpy(&descriptor, packed.data(), sizeof(descriptor));
  const auto payload
    = std::as_bytes(std::span(packed))
        .subspan(descriptor.data_offset, descriptor.size_bytes);
  auto table = std::vector<TextureResourceDesc>(count + 1U);
  auto data = std::vector<std::byte> {};
  for (size_t index = 1; index <= count; ++index) {
    descriptor.data_offset = data.size();
    table.at(index) = descriptor;
    data.insert(data.end(), payload.begin(), payload.end());
  }
  WriteBytes(root / "textures.table", std::as_bytes(std::span(table)));
  WriteBytes(root / "textures.data", data);
  auto writer = LooseCookedWriter(root);
  writer.RegisterExternalFile(FileKind::kTexturesTable, "textures.table");
  writer.RegisterExternalFile(FileKind::kTexturesData, "textures.data");
  const auto result = writer.Finish();
  EXPECT_EQ(result.collision_summary.file_collisions, 0U);
}

NOLINT_TEST_F(TextureLoadingServiceTest,
  BrowserRefreshPreservesAssignmentsAndImportKeepsKeysReloadable)
{
  PublishTextures(temp_dir_, 2U);
  oxygen::co::testing::TestEventLoop loop;
  oxygen::co::Run(loop,
    [](oxygen::observer_ptr<oxygen::co::testing::TestEventLoop> event_loop,
      std::filesystem::path root) -> oxygen::co::Co<> {
      oxygen::co::ThreadPool pool(*event_loop, 2);
      auto config = AssetLoaderConfig {};
      config.thread_pool = oxygen::observer_ptr { &pool };
      auto loader = AssetLoader(Tag::Get(), config);
      loader.RegisterLoader(oxygen::content::loaders::LoadTextureResource);
      OXCO_WITH_NURSERY(nursery)
      {
        co_await nursery.Start(&AssetLoader::ActivateAsync, &loader);
        loader.Run();
        auto service = TextureLoadingService(oxygen::observer_ptr { &loader });
        std::string error;
        EXPECT_TRUE(service.RefreshCookedTextureEntries(root, &error)) << error;
        EXPECT_EQ(service.GetCookedTextureEntries().size(), 2U);

        auto assigned = std::array<TextureLoadingService::LoadResult, 2> {};
        for (size_t index = 0; index < assigned.size(); ++index) {
          oxygen::co::Event complete;
          service.StartLoadCookedTexture(
            static_cast<uint32_t>(index + 1U), [&](auto result) -> void {
              assigned.at(index) = std::move(result);
              complete.Trigger();
            });
          co_await complete;
          EXPECT_TRUE(assigned.at(index).success);
        }
        EXPECT_NE(assigned.front().resource_key, assigned.back().resource_key);
        const auto cube = loader.GetTexture(assigned.front().resource_key);
        const auto sphere = loader.GetTexture(assigned.back().resource_key);
        EXPECT_NE(cube, nullptr);
        EXPECT_NE(sphere, nullptr);

        // Reopening the panel requests this exact browser refresh.
        EXPECT_TRUE(service.RefreshCookedTextureEntries(root, &error)) << error;
        EXPECT_TRUE(service.RefreshCookedTextureEntries(root, &error)) << error;
        EXPECT_EQ(loader.GetTexture(assigned.front().resource_key), cube);
        EXPECT_EQ(loader.GetTexture(assigned.back().resource_key), sphere);

        // A subsequent import legitimately refreshes the mounted source. The
        // renderer must still be able to reload both previously assigned keys.
        PublishTextures(root, 3U);
        EXPECT_TRUE(service.RefreshCookedTextureEntries(root, &error)) << error;
        for (const auto& assignment : assigned) {
          oxygen::co::Event complete;
          loader.StartLoadTexture(
            assignment.resource_key, [&](const auto& texture) -> void {
              EXPECT_NE(texture, nullptr);
              complete.Trigger();
            });
          co_await complete;
        }
        loader.Stop();
        co_return oxygen::co::kJoin;
      };
    }(oxygen::observer_ptr { &loop }, temp_dir_));
}

NOLINT_TEST_F(TextureLoadingServiceTest, RejectsReservedAndOutOfRangeEntries)
{
  PublishTextures(temp_dir_, 1U);
  auto service
    = TextureLoadingService(oxygen::observer_ptr { asset_loader_.get() });
  EXPECT_TRUE(service.RefreshCookedTextureEntries(temp_dir_, nullptr));
  for (const auto index : { 0U, 2U }) {
    bool called = false;
    service.StartLoadCookedTexture(index, [&](const auto& result) -> void {
      called = true;
      EXPECT_FALSE(result.success);
      EXPECT_FALSE(result.status_message.empty());
    });
    EXPECT_TRUE(called);
  }
}

} // namespace
