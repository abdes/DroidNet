//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/EvictionEvents.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Renderer/RendererTag.h>
#include <Oxygen/Renderer/Resources/GeometryUploader.h>
#include <Oxygen/Renderer/Test/Fakes/Graphics.h>
#include <Oxygen/Renderer/Upload/StagingProvider.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>

namespace oxygen::renderer::testing {

//! Minimal asset loader fake that supports eviction subscriptions.
class FakeAssetLoader final : public content::IAssetLoader {
public:
  FakeAssetLoader() = default;
  ~FakeAssetLoader() override = default;

  OXYGEN_MAKE_NON_COPYABLE(FakeAssetLoader)
  OXYGEN_DEFAULT_MOVABLE(FakeAssetLoader)

  void StartLoadTexture(
    content::ResourceKey /*key*/, TextureCallback on_complete) override
  {
    on_complete(nullptr);
  }

  void StartLoadTexture(
    content::CookedResourceData<data::TextureResource> /*cooked*/,
    TextureCallback on_complete) override
  {
    on_complete(nullptr);
  }

  void StartLoadBuffer(
    content::ResourceKey /*key*/, BufferCallback on_complete) override
  {
    on_complete(nullptr);
  }

  void StartLoadBuffer(
    content::CookedResourceData<data::BufferResource> /*cooked*/,
    BufferCallback on_complete) override
  {
    on_complete(nullptr);
  }

  void StartLoadMaterialAsset(
    const data::AssetKey& /*key*/, MaterialCallback on_complete) override
  {
    on_complete(nullptr);
  }

  void StartLoadGeometryAsset(
    const data::AssetKey& /*key*/, GeometryCallback on_complete) override
  {
    on_complete(nullptr);
  }

  void StartLoadScene(
    const data::AssetKey& /*key*/, SceneCallback on_complete) override
  {
    on_complete(nullptr);
  }

  auto AddPakFile(const std::filesystem::path& /*path*/) -> void override { }

  auto AddLooseCookedRoot(const std::filesystem::path& /*path*/)
    -> void override
  {
  }

  auto ClearMounts() -> void override { }

  auto TrimCache() -> void override { }

  auto RegisterConsoleBindings(
    observer_ptr<console::Console> /*console*/) noexcept -> void override
  {
  }

  auto ApplyConsoleCVars(const console::Console& /*console*/) -> void override
  {
  }

  [[nodiscard]] auto GetTexture(content::ResourceKey /*key*/) const noexcept
    -> std::shared_ptr<data::TextureResource> override
  {
    return nullptr;
  }

  [[nodiscard]] auto GetBuffer(content::ResourceKey /*key*/) const noexcept
    -> std::shared_ptr<data::BufferResource> override
  {
    return nullptr;
  }

  [[nodiscard]] auto GetMaterialAsset(
    const data::AssetKey& /*key*/) const noexcept
    -> std::shared_ptr<data::MaterialAsset> override
  {
    return nullptr;
  }

  [[nodiscard]] auto GetGeometryAsset(
    const data::AssetKey& /*key*/) const noexcept
    -> std::shared_ptr<data::GeometryAsset> override
  {
    return nullptr;
  }

  [[nodiscard]] auto HasTexture(content::ResourceKey /*key*/) const noexcept
    -> bool override
  {
    return false;
  }

  [[nodiscard]] auto HasBuffer(content::ResourceKey /*key*/) const noexcept
    -> bool override
  {
    return false;
  }

  [[nodiscard]] auto HasMaterialAsset(
    const data::AssetKey& /*key*/) const noexcept -> bool override
  {
    return false;
  }

  [[nodiscard]] auto HasGeometryAsset(
    const data::AssetKey& /*key*/) const noexcept -> bool override
  {
    return false;
  }

  auto ReleaseResource(content::ResourceKey /*key*/) -> bool override
  {
    return false;
  }

  auto ReleaseAsset(const data::AssetKey& /*key*/) -> bool override
  {
    return false;
  }

  auto SubscribeResourceEvictions(TypeId resource_type, EvictionHandler handler)
    -> EvictionSubscription override
  {
    const auto id = next_subscription_id_++;
    eviction_handlers_[resource_type].insert_or_assign(id, std::move(handler));
    return MakeEvictionSubscription(resource_type, id,
      observer_ptr<IAssetLoader> { this }, eviction_alive_token_);
  }

  [[nodiscard]] auto MintSyntheticTextureKey() -> content::ResourceKey override
  {
    return content::ResourceKey { next_key_++ };
  }

  [[nodiscard]] auto MintSyntheticBufferKey() -> content::ResourceKey override
  {
    return content::ResourceKey { next_key_++ };
  }

  auto EmitGeometryAssetEviction(
    const data::AssetKey& key, content::EvictionReason reason) -> void
  {
    const auto type_id = data::GeometryAsset::ClassTypeId();
    const auto it = eviction_handlers_.find(type_id);
    if (it == eviction_handlers_.end()) {
      return;
    }

    content::EvictionEvent event {
      .asset_key = key,
      .key = content::ResourceKey {},
      .type_id = type_id,
      .reason = reason,
    };

    for (const auto& [id, handler] : it->second) {
      (void)id;
      handler(event);
    }
  }

private:
  void UnsubscribeResourceEvictions(
    TypeId resource_type, uint64_t id) noexcept override
  {
    const auto it = eviction_handlers_.find(resource_type);
    if (it == eviction_handlers_.end()) {
      return;
    }
    it->second.erase(id);
  }

  std::unordered_map<TypeId, std::unordered_map<std::uint64_t, EvictionHandler>>
    eviction_handlers_;
  std::uint64_t next_subscription_id_ { 1U };
  std::shared_ptr<int> eviction_alive_token_ { std::make_shared<int>(0) };
  std::uint64_t next_key_ { 1U };
};

class GeometryUploaderTest : public ::testing::Test {
protected:
  auto SetUp() -> void override;

  [[nodiscard]] auto GfxPtr() const -> observer_ptr<Graphics>;

  [[nodiscard]] auto Uploader() const -> engine::upload::UploadCoordinator&;
  [[nodiscard]] auto Staging() const -> engine::upload::StagingProvider&;
  [[nodiscard]] auto GeoUploader() const -> resources::GeometryUploader&;
  [[nodiscard]] auto Loader() const -> FakeAssetLoader&;

  auto BeginFrame(frame::Slot slot) -> void;

  [[nodiscard]] auto MakeValidTriangleMesh(std::string_view name,
    bool indexed = true) const -> std::shared_ptr<const data::Mesh>;

  [[nodiscard]] auto MakeInvalidMesh_NoVertices(std::string_view name) const
    -> std::shared_ptr<const data::Mesh>;

  [[nodiscard]] auto MakeInvalidMesh_NonFiniteVertex(
    std::string_view name) const -> std::shared_ptr<const data::Mesh>;

private:
  [[nodiscard]] auto DefaultMaterial() const
    -> std::shared_ptr<const data::MaterialAsset>;

  std::shared_ptr<FakeGraphics> gfx_;
  std::unique_ptr<engine::upload::UploadCoordinator> uploader_;
  std::shared_ptr<engine::upload::StagingProvider> staging_provider_;
  std::unique_ptr<FakeAssetLoader> asset_loader_;
  std::unique_ptr<resources::GeometryUploader> geo_uploader_;
  std::shared_ptr<const data::MaterialAsset> default_material_;
};

} // namespace oxygen::renderer::testing
