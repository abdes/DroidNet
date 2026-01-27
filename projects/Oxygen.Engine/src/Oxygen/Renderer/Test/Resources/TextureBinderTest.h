//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <unordered_map>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/EvictionEvents.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Renderer/Resources/TextureBinder.h>
#include <Oxygen/Renderer/Test/Fakes/Graphics.h>
#include <Oxygen/Renderer/Test/Resources/TextureBinderTestPayloads.h>
#include <Oxygen/Renderer/Upload/StagingProvider.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>

namespace oxygen::renderer::testing {

//! Deterministic fake for callback-based texture loads.
/*!
 This fake avoids OxCo activation requirements by completing loads
 synchronously on the calling thread.
*/
class FakeAssetLoader final : public content::IAssetLoader {
public:
  FakeAssetLoader() = default;
  ~FakeAssetLoader() = default;

  OXYGEN_MAKE_NON_COPYABLE(FakeAssetLoader)
  OXYGEN_DEFAULT_MOVABLE(FakeAssetLoader)

  void StartLoadTexture(content::ResourceKey key,
    std::function<void(std::shared_ptr<data::TextureResource>)> on_complete)
    override
  {
    const auto it = textures_.find(key);
    if (it == textures_.end()) {
      on_complete(nullptr);
      return;
    }
    on_complete(it->second);
  }

  void StartLoadTexture(
    content::CookedResourceData<data::TextureResource> cooked,
    TextureCallback on_complete) override
  {
    const auto decoded = DecodeCookedTexturePayload(cooked.bytes);
    textures_.insert_or_assign(cooked.key, decoded);
    on_complete(std::move(decoded));
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

  [[nodiscard]] auto GetTexture(content::ResourceKey key) const noexcept
    -> std::shared_ptr<data::TextureResource> override
  {
    const auto it = textures_.find(key);
    if (it == textures_.end()) {
      return nullptr;
    }
    return it->second;
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

  [[nodiscard]] auto HasTexture(content::ResourceKey key) const noexcept
    -> bool override
  {
    return textures_.contains(key);
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

  auto ReleaseResource(content::ResourceKey key) -> bool override
  {
    return textures_.erase(key) > 0U;
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

  auto EmitTextureEviction(
    content::ResourceKey key, content::EvictionReason reason) -> void
  {
    EmitEviction(key, data::TextureResource::ClassTypeId(), reason);
  }

  [[nodiscard]] auto MintSyntheticTextureKey() -> content::ResourceKey override
  {
    return content::ResourceKey { next_key_++ };
  }

  [[nodiscard]] auto MintSyntheticBufferKey() -> content::ResourceKey override
  {
    return content::ResourceKey { next_key_++ };
  }

  auto SetTexture(content::ResourceKey key,
    std::shared_ptr<data::TextureResource> texture) -> void
  {
    textures_.insert_or_assign(key, std::move(texture));
  }

  auto SetLoadFailure(content::ResourceKey key) -> void
  {
    textures_.insert_or_assign(key, nullptr);
  }

  [[nodiscard]] auto PreloadCookedTexture(std::span<const std::uint8_t> payload)
    -> content::ResourceKey
  {
    const auto key = MintSyntheticTextureKey();
    PreloadCookedTexture(key, payload);
    return key;
  }

  auto PreloadCookedTexture(
    content::ResourceKey key, std::span<const std::uint8_t> payload) -> void
  {
    const auto decoded = DecodeCookedTexturePayload(payload);
    if (decoded == nullptr) {
      throw std::runtime_error("DecodeCookedTexturePayload failed");
    }
    SetTexture(key, decoded);
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

  auto EmitEviction(content::ResourceKey key, TypeId type_id,
    content::EvictionReason reason) -> void
  {
    const auto it = eviction_handlers_.find(type_id);
    if (it == eviction_handlers_.end()) {
      return;
    }

    content::EvictionEvent event {
      .key = key,
      .type_id = type_id,
      .reason = reason,
    };

    for (const auto& [id, handler] : it->second) {
      (void)id;
      handler(event);
    }
  }

  std::unordered_map<content::ResourceKey,
    std::shared_ptr<data::TextureResource>>
    textures_;

  std::unordered_map<TypeId, std::unordered_map<std::uint64_t, EvictionHandler>>
    eviction_handlers_;
  std::uint64_t next_subscription_id_ { 1U };
  std::shared_ptr<int> eviction_alive_token_ { std::make_shared<int>(0) };

  std::uint64_t next_key_ { 1U };
};

//! Test harness for TextureBinder unit tests.
/*! Provides a minimal renderer upload environment (FakeGraphics + real
    UploadCoordinator + staging provider) without depending on the
    UploadCoordinator test suite.

    The harness also owns a fake IAssetLoader and constructs a
    TextureBinder ready for tests.
*/
class TextureBinderTest : public ::testing::Test {
protected:
  TextureBinderTest() = default;

  virtual auto ConfigureGraphics(FakeGraphics& /*gfx*/) -> void { }

  auto SetUp() -> void override;

  [[nodiscard]] auto Gfx() const -> FakeGraphics& { return *gfx_; }
  [[nodiscard]] auto GfxPtr() const -> observer_ptr<Graphics>
  {
    return observer_ptr<Graphics>(gfx_.get());
  }

  [[nodiscard]] auto Uploader() const -> engine::upload::UploadCoordinator&
  {
    return *uploader_;
  }

  [[nodiscard]] auto Staging() const -> engine::upload::StagingProvider&
  {
    return *staging_provider_;
  }

  [[nodiscard]] auto Loader() const -> FakeAssetLoader&
  {
    return *texture_loader_;
  }

  [[nodiscard]] auto TexBinder() const -> resources::TextureBinder&
  {
    return *texture_binder_;
  }

  [[nodiscard]] auto AllocatedSrvCount() const -> uint32_t;

private:
  std::shared_ptr<FakeGraphics> gfx_;
  std::unique_ptr<engine::upload::UploadCoordinator> uploader_;
  std::shared_ptr<engine::upload::StagingProvider> staging_provider_;
  std::unique_ptr<FakeAssetLoader> texture_loader_;
  std::unique_ptr<resources::TextureBinder> texture_binder_;
};

} // namespace oxygen::renderer::testing
