//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

#include "AssetLoader_test.h"
#include "Fixtures/AssetLoaderSources.h"

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/EvictionEvents.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::testing {
namespace {
  using co::testing::TestEventLoop;

  auto CheckPinnedPublication(TestEventLoop* loop, std::filesystem::path root,
    data::AssetKey key, data::SourceKey source) -> co::Co<>
  {
    co::ThreadPool pool(*loop, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr { &pool };
    AssetLoader loader(engine::internal::EngineTagFactory::Get(), config);
    OXCO_WITH_NURSERY(nursery)
    {
      co_await nursery.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();
      loader.AddLooseCookedRoot(root);
      std::vector<EvictionEvent> evictions;
      auto subscription = loader.SubscribeResourceEvictions(
        data::TextureResource::ClassTypeId(),
        [&evictions](
          const EvictionEvent& event) -> void { evictions.push_back(event); });
      auto first = co_await loader.LoadAssetAsync<data::MaterialAsset>(key);
      auto second = co_await loader.LoadAssetAsync<data::MaterialAsset>(key);
      if (!first || !second) {
        ADD_FAILURE() << "Material publication failed";
        loader.Stop();
        co_return co::kJoin;
      }
      EXPECT_EQ(first, second);
      EXPECT_EQ(first->GetSourceKey(), source);
      const auto texture_key = first->GetBaseColorTextureKey();
      EXPECT_EQ(first->GetNormalTextureKey(), texture_key);
      EXPECT_EQ(loader.MakeTextureResourceKey(source, ResourceIndexT { 1U }),
        std::optional { texture_key });
      EXPECT_TRUE(loader.HasTexture(texture_key));
      auto pin = loader.PinAsset(key);
      EXPECT_TRUE(pin);
      first.reset();
      second.reset();
      loader.TrimCache();
      EXPECT_TRUE(loader.HasMaterialAsset(key));
      EXPECT_TRUE(loader.HasTexture(texture_key));
      EXPECT_TRUE(evictions.empty());
      pin.Reset();
      loader.TrimCache();
      EXPECT_FALSE(loader.HasMaterialAsset(key));
      EXPECT_FALSE(loader.HasTexture(texture_key));
      EXPECT_EQ(evictions.size(), 1U);
      if (!evictions.empty()) {
        EXPECT_EQ(evictions.front().key, texture_key);
        EXPECT_EQ(evictions.front().reason, EvictionReason::kTrim);
      }
      loader.TrimCache();
      EXPECT_EQ(evictions.size(), 1U);
      loader.Stop();
      co_return co::kJoin;
    };
  }

  auto CheckPrecedence(TestEventLoop* loop, std::filesystem::path first_root,
    std::filesystem::path second_root, data::AssetKey key,
    data::SourceKey first_source, data::SourceKey second_source) -> co::Co<>
  {
    co::ThreadPool pool(*loop, 2);
    AssetLoaderConfig config {};
    config.thread_pool = observer_ptr { &pool };
    AssetLoader loader(engine::internal::EngineTagFactory::Get(), config);
    OXCO_WITH_NURSERY(nursery)
    {
      co_await nursery.Start(&AssetLoader::ActivateAsync, &loader);
      loader.Run();
      loader.AddLooseCookedRoot(first_root);
      loader.AddLooseCookedRoot(second_root);
      auto newest = co_await loader.LoadAssetAsync<data::MaterialAsset>(key);
      auto qualified = co_await loader.LoadAssetAsync<data::MaterialAsset>(
        key, first_source);
      if (!newest || !qualified) {
        ADD_FAILURE() << "Both source-qualified publications must load";
        loader.Stop();
        co_return co::kJoin;
      }
      EXPECT_EQ(newest->GetSourceKey(), second_source);
      EXPECT_EQ(qualified->GetSourceKey(), first_source);
      EXPECT_THAT(
        newest->GetBaseColor(), ::testing::ElementsAre(0.0F, 0.0F, 1.0F, 1.0F));
      EXPECT_THAT(qualified->GetBaseColor(),
        ::testing::ElementsAre(1.0F, 0.0F, 0.0F, 1.0F));
      loader.ClearMounts();
      loader.AddLooseCookedRoot(second_root);
      loader.AddLooseCookedRoot(first_root);
      const auto reversed
        = co_await loader.LoadAssetAsync<data::MaterialAsset>(key);
      EXPECT_NE(reversed, nullptr);
      if (reversed) {
        EXPECT_EQ(reversed->GetSourceKey(), first_source);
        EXPECT_THAT(reversed->GetBaseColor(),
          ::testing::ElementsAre(1.0F, 0.0F, 0.0F, 1.0F));
      }
      EXPECT_THAT(
        newest->GetBaseColor(), ::testing::ElementsAre(0.0F, 0.0F, 1.0F, 1.0F));
      loader.Stop();
      co_return co::kJoin;
    };
  }

  NOLINT_TEST_F(
    AssetLoaderBasicTest, SharedTextureBindingIsPinnedAndEvictedExactlyOnce)
  {
    const auto root = temp_dir_ / "source";
    const auto key = data::AssetKey::FromVirtualPath("/Test/Surface.omat");
    const auto source = WriteTexturedMaterialSource(root, key);
    TestEventLoop loop;
    co::Run(loop, CheckPinnedPublication(&loop, root, key, source));
  }

  NOLINT_TEST_F(
    AssetLoaderBasicTest, NewestMountWinsWithoutChangingQualifiedPublications)
  {
    const auto first = temp_dir_ / "red";
    const auto second = temp_dir_ / "blue";
    const auto key = data::AssetKey::FromVirtualPath("/Test/Surface.omat");
    const auto red
      = WriteMaterialSource(first, key, { 1.0F, 0.0F, 0.0F, 1.0F });
    const auto blue
      = WriteMaterialSource(second, key, { 0.0F, 0.0F, 1.0F, 1.0F });
    TestEventLoop loop;
    co::Run(loop, CheckPrecedence(&loop, first, second, key, red, blue));
  }
} // namespace
} // namespace oxygen::content::testing
