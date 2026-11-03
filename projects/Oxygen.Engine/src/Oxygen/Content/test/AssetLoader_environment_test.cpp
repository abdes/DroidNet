//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <optional>
#include <string_view>

#include "./AssetLoader_test.h"

#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/Materialize.h>
#include <Oxygen/Cooker/testing/SceneBuilder.h>
#include <Oxygen/Cooker/testing/TextureBuilder.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::testing {
namespace {
  using cooker::testing::LooseCookedRoot;
  using cooker::testing::SceneBuilder;
  using cooker::testing::TextureBuilder;

  constexpr auto kSceneSeed = uint8_t { 1U };
  constexpr auto kTextureSeed = uint8_t { 2U };

  //! Adds to `root` the scene `leaf`, whose fog names `texture`, bound by
  //! `bind` in the scene's builder, as its inscattering cubemap.
  template <typename Bind>
  auto AddFoggedScene(LooseCookedRoot& root, const std::string_view leaf,
    const Bind& bind) -> data::AssetKey
  {
    auto scene = SceneBuilder { "fogged" };
    auto fog = data::pak::world::FogEnvironmentRecord {};
    fog.inscattering_color_cubemap = bind(scene);
    scene.Environment(fog);
    return root.Scene(leaf, scene);
  }

  //! What a loaded scene's fog texture field shows, and whether the texture
  //! stays resident through a cache trim while the scene is held.
  struct FogTexture final {
    std::optional<ResourceKey> shown;
    bool held = false;
  };

  auto LoadFogTexture(AssetLoader& loader, const data::AssetKey scene_key)
    -> co::Co<FogTexture>
  {
    const auto scene
      = co_await loader.LoadAssetAsync<data::SceneAsset>(scene_key);
    const auto fog = scene ? scene->TryGetFogEnvironment() : std::nullopt;
    if (!fog) {
      co_return FogTexture {};
    }
    const auto shown = loader.MakeTextureResourceKeyForAsset(
      *scene, fog->inscattering_color_cubemap);
    loader.TrimCache();
    co_return FogTexture {
      .shown = shown,
      .held = shown && !shown->IsError() && loader.HasTexture(*shown),
    };
  }

  //! A scene's load holds the local texture its environment names: the
  //! texture survives a trim while the scene lives.
  NOLINT_TEST_F(AssetLoaderPoolTest, SceneHoldsItsLocalEnvironmentTexture)
  {
    auto content = LooseCookedRoot { "test", kSceneSeed };
    const auto row = content.AddTexture(TextureBuilder {});
    const auto scene_key = AddFoggedScene(content, "fogged.oscene",
      [row](SceneBuilder& scene) -> data::ResourceReferenceIndex {
        return scene.Texture(row);
      });
    cooker::testing::Materialize(content.Files(), TempDir() / "scene");
    loader_.AddLooseCookedRoot(TempDir() / "scene");

    const auto fog_run = WhileActive(LoadFogTexture(loader_, scene_key));
    ASSERT_HAS_VALUE(fog_run);
    const auto& fog = *fog_run;

    ASSERT_HAS_VALUE(fog.shown);
    EXPECT_FALSE(fog.shown->IsError());
    EXPECT_TRUE(fog.held);
  }

  //! A scene's environment may name a texture of another source: its load
  //! binds the one the mounts hold and keeps it resident.
  NOLINT_TEST_F(
    AssetLoaderPoolTest, SceneHoldsAnEnvironmentTextureOfAnotherSource)
  {
    auto textures = LooseCookedRoot { "test", kTextureSeed };
    const auto texture
      = data::AssetKey::FromVirtualPath(textures.DescribeTexture(
        "fog.otex", textures.AddTexture(TextureBuilder {})));
    auto content = LooseCookedRoot { "test", kSceneSeed };
    const auto scene_key = AddFoggedScene(content, "fogged.oscene",
      [&texture](SceneBuilder& scene) -> data::ResourceReferenceIndex {
        return scene.ExternalTexture(texture);
      });
    cooker::testing::Materialize(textures.Files(), TempDir() / "textures");
    cooker::testing::Materialize(content.Files(), TempDir() / "scene");
    loader_.AddLooseCookedRoot(TempDir() / "textures");
    loader_.AddLooseCookedRoot(TempDir() / "scene");

    const auto fog_run = WhileActive(LoadFogTexture(loader_, scene_key));
    ASSERT_HAS_VALUE(fog_run);
    const auto& fog = *fog_run;

    ASSERT_HAS_VALUE(fog.shown);
    EXPECT_FALSE(fog.shown->IsError());
    EXPECT_TRUE(fog.held);
  }

  //! An environment texture no mounted source holds is an optional input:
  //! the scene loads and the field shows the error texture.
  NOLINT_TEST_F(AssetLoaderPoolTest, MissingEnvironmentTextureShowsTheError)
  {
    const auto absent = data::AssetKey::FromVirtualPath("/test/absent.otex");
    auto content = LooseCookedRoot { "test", kSceneSeed };
    const auto scene_key = AddFoggedScene(content, "fogged.oscene",
      [&absent](SceneBuilder& scene) -> data::ResourceReferenceIndex {
        return scene.ExternalTexture(absent);
      });
    cooker::testing::Materialize(content.Files(), TempDir() / "scene");
    loader_.AddLooseCookedRoot(TempDir() / "scene");

    const auto fog_run = WhileActive(LoadFogTexture(loader_, scene_key));
    ASSERT_HAS_VALUE(fog_run);
    const auto& fog = *fog_run;

    EXPECT_EQ(fog.shown, ResourceKey::kError);
  }

} // namespace
} // namespace oxygen::content::testing
