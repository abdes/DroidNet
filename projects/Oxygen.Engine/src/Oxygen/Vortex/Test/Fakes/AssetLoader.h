//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/EvictionEvents.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/InputActionAsset.h>
#include <Oxygen/Data/InputMappingContextAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PhysicsResource.h>
#include <Oxygen/Data/PhysicsSceneAsset.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Data/ScriptResource.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Vortex/Test/Fixtures/TextureBinderPayloads.h>

namespace oxygen::vortex::testing {

class FakeAssetLoader final : public content::IAssetLoader {
public:
  FakeAssetLoader() = default;
  ~FakeAssetLoader() override = default;

  OXYGEN_MAKE_NON_COPYABLE(FakeAssetLoader)
  OXYGEN_DEFAULT_MOVABLE(FakeAssetLoader)

  void StartLoadTexture(content::ResourceKey key,
    std::function<void(std::shared_ptr<data::TextureResource>)> on_complete)
    override
  {
    const auto it = textures_.find(key);
    if (it != textures_.end()) {
      on_complete(it->second);
      return;
    }

    const auto cooked = cooked_payloads_.find(key);
    if (cooked != cooked_payloads_.end()) {
      const auto decoded
        = DecodeCookedTexturePayload(std::span { cooked->second });
      textures_.insert_or_assign(key, decoded);
      on_complete(decoded);
      return;
    }

    on_complete(nullptr);
  }

  void StartLoadTexture(
    content::CookedResourceData<data::TextureResource> cooked,
    TextureCallback on_complete) override
  {
    auto decoded = DecodeCookedTexturePayload(cooked.bytes);
    cooked_payloads_.insert_or_assign(cooked.key,
      std::vector<uint8_t>(cooked.bytes.begin(), cooked.bytes.end()));
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

  void StartLoadScriptAsset(
    const data::AssetKey& /*key*/, ScriptCallback on_complete) override
  {
    on_complete(nullptr);
  }

  void StartLoadPhysicsSceneAsset(
    const data::AssetKey& /*key*/, PhysicsSceneCallback on_complete) override
  {
    on_complete(nullptr);
  }

  void StartLoadPhysicsResource(
    content::ResourceKey /*key*/, PhysicsResourceCallback on_complete) override
  {
    on_complete(nullptr);
  }

  auto AddPakFile(const std::filesystem::path& /*path*/) -> void override { }

  auto AddLooseCookedRoot(const std::filesystem::path& /*path*/)
    -> void override
  {
  }

  auto ClearMounts() -> void override { }

  auto ReloadScript(const std::filesystem::path& /*path*/) -> void override { }
  auto ReloadAllScripts() -> void override { }

  auto SubscribeScriptReload(ScriptReloadCallback /*callback*/)
    -> EvictionSubscription override
  {
    return {};
  }

  auto TrimCache() -> void override { textures_.clear(); }

  auto SetResidencyPolicy(const content::ResidencyPolicy& policy)
    -> void override
  {
    residency_policy_ = policy;
  }

  [[nodiscard]] auto GetResidencyPolicy() const noexcept
    -> content::ResidencyPolicy override
  {
    return residency_policy_;
  }

  [[nodiscard]] auto QueryResidencyPolicyState() const
    -> content::ResidencyPolicyState override
  {
    return content::ResidencyPolicyState { .policy = residency_policy_ };
  }

  [[nodiscard]] auto EnumerateMountedScenes() const
    -> std::vector<MountedSceneEntry> override
  {
    return {};
  }

  [[nodiscard]] auto EnumerateMountedInputContexts() const
    -> std::vector<MountedInputContextEntry> override
  {
    return {};
  }

  [[nodiscard]] auto EnumerateMountedSources() const
    -> std::vector<MountedSourceEntry> override
  {
    return {};
  }

  auto RegisterConsoleBindings(
    observer_ptr<console::Console> /*console*/) noexcept -> void override
  {
  }

  auto ApplyConsoleCVars(const console::Console& /*console*/) -> void override
  {
  }

  [[nodiscard]] auto GetTexture(content::ResourceKey key) const noexcept
    -> std::shared_ptr<data::TextureResource> override
  {
    const auto it = textures_.find(key);
    return it == textures_.end() ? nullptr : it->second;
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

  [[nodiscard]] auto GetScriptAsset(
    const data::AssetKey& /*key*/) const noexcept
    -> std::shared_ptr<data::ScriptAsset> override
  {
    return nullptr;
  }

  [[nodiscard]] auto GetScriptResource(
    content::ResourceKey /*key*/) const noexcept
    -> std::shared_ptr<data::ScriptResource> override
  {
    return nullptr;
  }

  auto LoadScriptResourceAsync(content::ResourceKey /*key*/)
    -> co::Co<std::shared_ptr<data::ScriptResource>> override
  {
    co_return nullptr;
  }

  [[nodiscard]] auto MakeScriptResourceKeyForAsset(
    const data::AssetKey& /*context_asset_key*/,
    data::pak::core::ResourceIndexT /*resource_index*/) const noexcept
    -> std::optional<content::ResourceKey> override
  {
    return std::nullopt;
  }

  [[nodiscard]] auto ReadScriptResourceForAsset(
    const data::AssetKey& /*context_asset_key*/,
    data::pak::core::ResourceIndexT /*resource_index*/) const
    -> std::shared_ptr<const data::ScriptResource> override
  {
    return nullptr;
  }

  [[nodiscard]] auto GetPhysicsSceneAsset(
    const data::AssetKey& /*key*/) const noexcept
    -> std::shared_ptr<data::PhysicsSceneAsset> override
  {
    return nullptr;
  }

  [[nodiscard]] auto GetPhysicsResource(
    content::ResourceKey /*key*/) const noexcept
    -> std::shared_ptr<data::PhysicsResource> override
  {
    return nullptr;
  }

  auto LoadPhysicsResourceAsync(content::ResourceKey /*key*/)
    -> co::Co<std::shared_ptr<data::PhysicsResource>> override
  {
    co_return nullptr;
  }

  [[nodiscard]] auto MakePhysicsResourceKey(data::SourceKey /*source_key*/,
    data::pak::core::ResourceIndexT /*resource_index*/) const noexcept
    -> std::optional<content::ResourceKey> override
  {
    return std::nullopt;
  }

  [[nodiscard]] auto MakePhysicsResourceKeyForAsset(
    const data::AssetKey& /*context_asset_key*/,
    data::pak::core::ResourceIndexT /*resource_index*/) const noexcept
    -> std::optional<content::ResourceKey> override
  {
    return std::nullopt;
  }

  [[nodiscard]] auto MakePhysicsResourceKeyForAsset(
    const data::AssetKey& /*context_asset_key*/,
    const data::AssetKey& /*resource_asset_key*/) const noexcept
    -> std::optional<content::ResourceKey> override
  {
    return std::nullopt;
  }

  [[nodiscard]] auto ReadCollisionShapeAssetDescForAsset(
    const data::AssetKey& /*context_asset_key*/,
    const data::AssetKey& /*shape_asset_key*/) const
    -> std::optional<data::pak::physics::CollisionShapeAssetDesc> override
  {
    return std::nullopt;
  }

  [[nodiscard]] auto ReadPhysicsMaterialAssetDescForAsset(
    const data::AssetKey& /*context_asset_key*/,
    const data::AssetKey& /*material_asset_key*/) const
    -> std::optional<data::pak::physics::PhysicsMaterialAssetDesc> override
  {
    return std::nullopt;
  }

  [[nodiscard]] auto GetInputActionAsset(
    const data::AssetKey& /*key*/) const noexcept
    -> std::shared_ptr<data::InputActionAsset> override
  {
    return nullptr;
  }

  [[nodiscard]] auto GetInputMappingContextAsset(
    const data::AssetKey& /*key*/) const noexcept
    -> std::shared_ptr<data::InputMappingContextAsset> override
  {
    return nullptr;
  }

  [[nodiscard]] auto GetHydratedScriptSlots(
    const data::SceneAsset& /*scene_asset*/,
    const data::pak::scripting::ScriptingComponentRecord& /*component*/) const
    -> std::vector<HydratedScriptSlot> override
  {
    return {};
  }

  [[nodiscard]] auto FindPhysicsSidecarAssetKeyForScene(
    const data::AssetKey& /*scene_key*/) const
    -> std::optional<data::AssetKey> override
  {
    return std::nullopt;
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

  [[nodiscard]] auto HasScriptAsset(
    const data::AssetKey& /*key*/) const noexcept -> bool override
  {
    return false;
  }

  [[nodiscard]] auto HasPhysicsSceneAsset(
    const data::AssetKey& /*key*/) const noexcept -> bool override
  {
    return false;
  }

  [[nodiscard]] auto HasInputActionAsset(
    const data::AssetKey& /*key*/) const noexcept -> bool override
  {
    return false;
  }

  [[nodiscard]] auto HasInputMappingContextAsset(
    const data::AssetKey& /*key*/) const noexcept -> bool override
  {
    return false;
  }

  auto ReleaseResource(content::ResourceKey key) -> bool override
  {
    return textures_.erase(key) > 0U;
  }

  auto PinResource(content::ResourceKey /*key*/) -> bool override
  {
    return false;
  }

  auto UnpinResource(content::ResourceKey /*key*/) -> bool override
  {
    return false;
  }

  auto ReleaseAsset(const data::AssetKey& /*key*/) -> bool override
  {
    return false;
  }

  auto PinAsset(const data::AssetKey& /*key*/) -> bool override
  {
    return false;
  }

  auto UnpinAsset(const data::AssetKey& /*key*/) -> bool override
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
    cooked_payloads_.insert_or_assign(
      key, std::vector<uint8_t>(payload.begin(), payload.end()));
    SetTexture(key, decoded);
  }

  auto EmitTextureEviction(
    content::ResourceKey key, content::EvictionReason reason) -> void
  {
    ReleaseResource(key);
    EmitResourceEviction(key, data::TextureResource::ClassTypeId(), reason);
  }

  auto EmitGeometryAssetEviction(
    const data::AssetKey& key, content::EvictionReason reason) -> void
  {
    EmitAssetEviction(key, data::GeometryAsset::ClassTypeId(), reason);
  }

  auto EmitMaterialAssetEviction(
    const data::AssetKey& key, content::EvictionReason reason) -> void
  {
    EmitAssetEviction(key, data::MaterialAsset::ClassTypeId(), reason);
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

  auto EmitResourceEviction(content::ResourceKey key, TypeId type_id,
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

  auto EmitAssetEviction(const data::AssetKey& asset_key, TypeId type_id,
    content::EvictionReason reason) -> void
  {
    const auto it = eviction_handlers_.find(type_id);
    if (it == eviction_handlers_.end()) {
      return;
    }

    content::EvictionEvent event {
      .asset_key = asset_key,
      .key = content::ResourceKey {},
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
  std::unordered_map<content::ResourceKey, std::vector<uint8_t>>
    cooked_payloads_;
  std::unordered_map<TypeId, std::unordered_map<std::uint64_t, EvictionHandler>>
    eviction_handlers_;
  std::uint64_t next_subscription_id_ { 1U };
  std::shared_ptr<int> eviction_alive_token_ { std::make_shared<int>(0) };
  std::uint64_t next_key_ { 1U };
  content::ResidencyPolicy residency_policy_ {};
};

} // namespace oxygen::vortex::testing
