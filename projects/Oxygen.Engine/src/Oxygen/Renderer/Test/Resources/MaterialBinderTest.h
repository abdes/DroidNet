//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <unordered_map>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Graphics/Common/DescriptorAllocationHandle.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Renderer/Resources/IResourceBinder.h>
#include <Oxygen/Renderer/Resources/MaterialBinder.h>
#include <Oxygen/Renderer/Test/Fakes/Graphics.h>

namespace oxygen::renderer::testing {

class MaterialBinderTest : public ::testing::Test {
protected:
  auto SetUp() -> void override;
  auto TearDown() -> void override;

  [[nodiscard]] auto GfxPtr() const -> observer_ptr<Graphics>;

  [[nodiscard]] auto Uploader() const -> engine::upload::UploadCoordinator&;
  [[nodiscard]] auto TexBinder() const -> resources::IResourceBinder&;
  [[nodiscard]] auto MatBinder() const -> resources::MaterialBinder&;
  void EmitMaterialAssetEviction(
    const data::AssetKey& key, content::EvictionReason reason) const;

  [[nodiscard]] auto AllocatedTextureSrvCount() const -> uint32_t;

  [[nodiscard]] auto TexBinderGetOrAllocateTotalCalls() const -> uint32_t;
  [[nodiscard]] auto TexBinderGetOrAllocateCallsForKey(
    const content::ResourceKey& key) const -> uint32_t;

  // Test helpers to control / observe FakeTextureBinder behavior.
  [[nodiscard]] auto GetPlaceholderIndexForKey(
    const content::ResourceKey& key) const -> ShaderVisibleIndex;
  void SetTextureBinderAllocateOnRequest(bool v) const;

  // Test helper: mark a ResourceKey which the FakeTextureBinder will report as
  // error.
  void SetTextureBinderErrorKey(const content::ResourceKey& key) const;

private:
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
    void StartLoadPhysicsResource(content::ResourceKey /*key*/,
      PhysicsResourceCallback on_complete) override
    {
      on_complete(nullptr);
    }
    auto AddPakFile(const std::filesystem::path& /*path*/) -> void override { }
    auto AddLooseCookedRoot(const std::filesystem::path& /*path*/)
      -> void override
    {
    }
    auto ClearMounts() -> void override { }
    auto ReloadScript(const std::filesystem::path& /*path*/) -> void override {
    }
    auto ReloadAllScripts() -> void override { }
    auto SubscribeScriptReload(ScriptReloadCallback /*callback*/)
      -> EvictionSubscription override
    {
      return {};
    }
    auto TrimCache() -> void override { }
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
    auto ReleaseResource(content::ResourceKey /*key*/) -> bool override
    {
      return false;
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
    auto SubscribeResourceEvictions(TypeId resource_type,
      EvictionHandler handler) -> EvictionSubscription override
    {
      const auto id = next_subscription_id_++;
      eviction_handlers_[resource_type].insert_or_assign(
        id, std::move(handler));
      return MakeEvictionSubscription(resource_type, id,
        observer_ptr<IAssetLoader> { this }, eviction_alive_token_);
    }
    [[nodiscard]] auto MintSyntheticTextureKey()
      -> content::ResourceKey override
    {
      return content::ResourceKey { next_key_++ };
    }
    [[nodiscard]] auto MintSyntheticBufferKey() -> content::ResourceKey override
    {
      return content::ResourceKey { next_key_++ };
    }

    auto EmitMaterialAssetEviction(
      const data::AssetKey& key, content::EvictionReason reason) -> void
    {
      const auto type_id = data::MaterialAsset::ClassTypeId();
      const auto it = eviction_handlers_.find(type_id);
      if (it == eviction_handlers_.end()) {
        return;
      }

      const content::EvictionEvent event {
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

    std::unordered_map<TypeId,
      std::unordered_map<std::uint64_t, EvictionHandler>>
      eviction_handlers_;
    std::uint64_t next_subscription_id_ { 1U };
    std::shared_ptr<int> eviction_alive_token_ { std::make_shared<int>(0) };
    std::uint64_t next_key_ { 1U };
    content::ResidencyPolicy residency_policy_ {};
  };

  class FakeTextureBinder final : public resources::IResourceBinder {
  public:
    FakeTextureBinder() = default;

    void SetDescriptorAllocator(graphics::DescriptorAllocator* a)
    {
      allocator_ = a;
    }

    [[nodiscard]] auto GetDescriptorAllocator() const
      -> graphics::DescriptorAllocator*
    {
      return allocator_;
    }

    void SetErrorKey(content::ResourceKey key) { error_key_ = key; }
    void SetAllocateOnRequest(bool v) { allocate_on_request_ = v; }

    [[nodiscard]] auto GetOrAllocateTotalCalls() const -> uint32_t
    {
      return get_or_allocate_total_calls_;
    }

    [[nodiscard]] auto GetOrAllocateCallsForKey(
      const content::ResourceKey& key) const -> uint32_t
    {
      const auto it = get_or_allocate_calls_by_key_.find(key);
      return it == get_or_allocate_calls_by_key_.end() ? 0U : it->second;
    }

    [[nodiscard]] auto GetOrAllocate(const content::ResourceKey& key)
      -> ShaderVisibleIndex override
    {
      ++get_or_allocate_total_calls_;
      ++get_or_allocate_calls_by_key_[key];

      if (error_key_.has_value() && error_key_.value() == key) {
        return GetErrorTextureIndex();
      }

      const auto it = map_.find(key);
      if (it != map_.end()) {
        return it->second;
      }

      // If a descriptor allocator is provided and explicit allocation is
      // enabled, allocate a shader-visible descriptor to reflect real
      // TextureBinder behavior in tests. When allocation is disabled the
      // binder returns placeholder indices without consuming descriptors so
      // MaterialBinder can be exercised without triggering SRV allocations.
      if ((allocator_ != nullptr) && allocate_on_request_) {
        auto handle = allocator_->AllocateBindless(
          oxygen::bindless::generated::kTexturesDomain,
          graphics::ResourceViewType::kTexture_SRV);
        const auto sv = allocator_->GetShaderVisibleIndex(handle);
        handles_.emplace(key, std::move(handle));
        map_.emplace(key, sv);
        return sv;
      }

      auto [newIt, inserted]
        = map_.try_emplace(key, ShaderVisibleIndex { next_ });
      if (inserted) {
        ++next_;
      }
      return newIt->second;
    }

    [[nodiscard]] static auto GetErrorTextureIndex() -> ShaderVisibleIndex
    {
      return ShaderVisibleIndex { 0U };
    }

  private:
    std::unordered_map<content::ResourceKey, ShaderVisibleIndex> map_;
    std::unordered_map<content::ResourceKey, graphics::BindlessHandle> handles_;
    std::unordered_map<content::ResourceKey, uint32_t>
      get_or_allocate_calls_by_key_;
    uint32_t get_or_allocate_total_calls_ { 0U };
    std::uint32_t next_ { 1U };
    std::optional<content::ResourceKey> error_key_;
    graphics::DescriptorAllocator* allocator_ { nullptr };
    // The fake should mimic real TextureBinder: allocate shader-visible
    // descriptors for per-entry placeholders immediately. Tests may toggle
    // this for diagnostics, but the default behavior matches production.
    bool allocate_on_request_ { true };
  };

  std::shared_ptr<FakeGraphics> gfx_;
  std::unique_ptr<engine::upload::UploadCoordinator> uploader_;
  std::shared_ptr<engine::upload::StagingProvider> staging_provider_;
  std::unique_ptr<FakeTextureBinder> texture_binder_;
  std::unique_ptr<graphics::DescriptorAllocator> texture_descriptor_allocator_;
  std::unique_ptr<FakeAssetLoader> asset_loader_;
  std::unique_ptr<resources::MaterialBinder> material_binder_;
};

} // namespace oxygen::renderer::testing
