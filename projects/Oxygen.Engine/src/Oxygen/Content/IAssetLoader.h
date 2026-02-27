//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/EvictionEvents.h>
#include <Oxygen/Content/ResidencyPolicy.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Content/ResourceTypeList.h>
#include <Oxygen/Content/api_export.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/InputActionAsset.h>
#include <Oxygen/Data/InputMappingContextAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/PhysicsResource.h>
#include <Oxygen/Data/PhysicsSceneAsset.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/OxCo/Co.h>

namespace oxygen::console {
class Console;
} // namespace oxygen::console

namespace oxygen::content {

//! Cooked bytes input for decoding a resource from an in-memory buffer.
/*!
 Provides a typed wrapper over a cooked byte payload plus the `ResourceKey`
 identity under which the decoded result will be cached.

 Buffer-provided loads are treated as *ad hoc inputs*: they do not require a
 mounted content source and are not enumerable through the loader.

 @tparam T The resource type (must satisfy PakResource).
*/
template <PakResource T> struct CookedResourceData final {
  //! Cache identity for the decoded resource.
  ResourceKey key {};

  //! Cooked bytes required to decode `T`.
  std::span<const uint8_t> bytes;
};

//! Minimal texture loading interface for renderer subsystems.
/*!
 This interface intentionally exposes only the callback-based texture loading
 entrypoint that renderer systems require.

 The primary production implementation is `content::AssetLoader`, but tests can
 supply fakes that return deterministic CPU-side `data::TextureResource`
 payloads without requiring coroutine activation.
*/
class IAssetLoader {
public:
  enum class ContentSourceKind : uint8_t {
    kPak = 0,
    kLooseCooked = 1,
  };

  struct MountedSceneEntry final {
    data::AssetKey scene_key {};
    data::SourceKey source_key {};
    uint16_t source_id { 0 };
    ContentSourceKind source_kind { ContentSourceKind::kPak };
    std::filesystem::path source_path;
    std::string display_name;
    std::string virtual_path;
  };

  struct MountedSourceEntry final {
    data::SourceKey source_key {};
    uint16_t source_id { 0 };
    ContentSourceKind source_kind { ContentSourceKind::kPak };
    std::filesystem::path source_path;
  };

  struct HydratedScriptSlot final {
    data::AssetKey script_asset_key {};
    data::pak::scripting::ScriptSlotFlags flags {
      data::pak::scripting::ScriptSlotFlags::kNone
    };
    std::vector<data::pak::scripting::ScriptParamRecord> params;
  };

  IAssetLoader() = default;
  virtual ~IAssetLoader() = default;

  OXYGEN_MAKE_NON_COPYABLE(IAssetLoader)
  OXYGEN_DEFAULT_MOVABLE(IAssetLoader)

  using TextureCallback
    = std::function<void(std::shared_ptr<data::TextureResource>)>;
  using BufferCallback
    = std::function<void(std::shared_ptr<data::BufferResource>)>;
  using MaterialCallback
    = std::function<void(std::shared_ptr<data::MaterialAsset>)>;
  using GeometryCallback
    = std::function<void(std::shared_ptr<data::GeometryAsset>)>;
  using SceneCallback = std::function<void(std::shared_ptr<data::SceneAsset>)>;
  using ScriptCallback
    = std::function<void(std::shared_ptr<data::ScriptAsset>)>;
  using PhysicsSceneCallback
    = std::function<void(std::shared_ptr<data::PhysicsSceneAsset>)>;
  using PhysicsResourceCallback
    = std::function<void(std::shared_ptr<data::PhysicsResource>)>;
  using EvictionHandler = std::function<void(const EvictionEvent&)>;

  //! RAII handle for resource eviction subscriptions.
  class EvictionSubscription {
  public:
    EvictionSubscription() noexcept = default;

    ~EvictionSubscription() noexcept { Cancel(); }

    OXYGEN_MAKE_NON_COPYABLE(EvictionSubscription)

    EvictionSubscription(EvictionSubscription&& other) noexcept
      : id_(other.id_)
      , resource_type_(other.resource_type_)
      , owner_(other.owner_)
      , alive_token_(std::move(other.alive_token_))
    {
      other.id_ = 0;
      other.resource_type_ = kInvalidTypeId;
      other.owner_ = nullptr;
      other.alive_token_.reset();
    }

    auto operator=(EvictionSubscription&& other) noexcept
      -> EvictionSubscription&
    {
      if (this != &other) {
        Cancel();
        id_ = other.id_;
        resource_type_ = other.resource_type_;
        owner_ = other.owner_;
        alive_token_ = std::move(other.alive_token_);
        other.id_ = 0;
        other.resource_type_ = kInvalidTypeId;
        other.owner_ = nullptr;
        other.alive_token_.reset();
      }
      return *this;
    }

    //! Cancel this subscription early.
    void Cancel() noexcept
    {
      if (id_ == 0 || !owner_) {
        return;
      }
      if (alive_token_.expired()) {
        id_ = 0;
        resource_type_ = kInvalidTypeId;
        owner_ = nullptr;
        return;
      }
      owner_->UnsubscribeResourceEvictions(resource_type_, id_);
      id_ = 0;
      resource_type_ = kInvalidTypeId;
      owner_ = nullptr;
    }

  private:
    friend class IAssetLoader;
    uint64_t id_ { 0 };
    TypeId resource_type_ { kInvalidTypeId };
    observer_ptr<IAssetLoader> owner_ { nullptr };
    std::weak_ptr<int> alive_token_;
  };

  //! Begin loading a texture resource and invoke `on_complete` on completion.
  virtual void StartLoadTexture(ResourceKey key, TextureCallback on_complete)
    = 0;
  virtual void StartLoadTexture(
    ResourceKey key, LoadRequest request, TextureCallback on_complete)
  {
    static_cast<void>(request);
    StartLoadTexture(key, std::move(on_complete));
  }

  //! Decode a texture resource from caller-provided cooked bytes.
  virtual void StartLoadTexture(
    CookedResourceData<data::TextureResource> cooked,
    TextureCallback on_complete)
    = 0;
  virtual void StartLoadTexture(
    CookedResourceData<data::TextureResource> cooked, LoadRequest request,
    TextureCallback on_complete)
  {
    static_cast<void>(request);
    StartLoadTexture(cooked, std::move(on_complete));
  }

  //! Begin loading a buffer resource and invoke `on_complete` on completion.
  virtual void StartLoadBuffer(ResourceKey key, BufferCallback on_complete) = 0;
  virtual void StartLoadBuffer(
    ResourceKey key, LoadRequest request, BufferCallback on_complete)
  {
    static_cast<void>(request);
    StartLoadBuffer(key, std::move(on_complete));
  }

  //! Decode a buffer resource from caller-provided cooked bytes.
  virtual void StartLoadBuffer(
    CookedResourceData<data::BufferResource> cooked, BufferCallback on_complete)
    = 0;
  virtual void StartLoadBuffer(CookedResourceData<data::BufferResource> cooked,
    LoadRequest request, BufferCallback on_complete)
  {
    static_cast<void>(request);
    StartLoadBuffer(cooked, std::move(on_complete));
  }

  //! Begin loading a material asset and invoke `on_complete` on completion.
  virtual void StartLoadMaterialAsset(
    const data::AssetKey& key, MaterialCallback on_complete)
    = 0;
  virtual void StartLoadMaterialAsset(const data::AssetKey& key,
    LoadRequest request, MaterialCallback on_complete)
  {
    static_cast<void>(request);
    StartLoadMaterialAsset(key, std::move(on_complete));
  }

  //! Begin loading a geometry asset and invoke `on_complete` on completion.
  virtual void StartLoadGeometryAsset(
    const data::AssetKey& key, GeometryCallback on_complete)
    = 0;
  virtual void StartLoadGeometryAsset(const data::AssetKey& key,
    LoadRequest request, GeometryCallback on_complete)
  {
    static_cast<void>(request);
    StartLoadGeometryAsset(key, std::move(on_complete));
  }

  //! Begin loading a scene asset and invoke `on_complete` on completion.
  virtual void StartLoadScene(
    const data::AssetKey& key, SceneCallback on_complete)
    = 0;
  virtual void StartLoadScene(
    const data::AssetKey& key, LoadRequest request, SceneCallback on_complete)
  {
    static_cast<void>(request);
    StartLoadScene(key, std::move(on_complete));
  }

  //! Begin loading a physics scene sidecar and invoke `on_complete` on
  //! completion.
  virtual void StartLoadPhysicsSceneAsset(
    const data::AssetKey& key, PhysicsSceneCallback on_complete)
    = 0;
  virtual void StartLoadPhysicsSceneAsset(const data::AssetKey& key,
    LoadRequest request, PhysicsSceneCallback on_complete)
  {
    static_cast<void>(request);
    StartLoadPhysicsSceneAsset(key, std::move(on_complete));
  }
  //! Begin loading a physics resource and invoke `on_complete` on completion.
  virtual void StartLoadPhysicsResource(
    ResourceKey key, PhysicsResourceCallback on_complete)
    = 0;
  virtual void StartLoadPhysicsResource(
    ResourceKey key, LoadRequest request, PhysicsResourceCallback on_complete)
  {
    static_cast<void>(request);
    StartLoadPhysicsResource(key, std::move(on_complete));
  }

  //! Begin loading a script asset and invoke `on_complete` on completion.
  virtual void StartLoadScriptAsset(
    const data::AssetKey& key, ScriptCallback on_complete)
    = 0;
  virtual void StartLoadScriptAsset(
    const data::AssetKey& key, LoadRequest request, ScriptCallback on_complete)
  {
    static_cast<void>(request);
    StartLoadScriptAsset(key, std::move(on_complete));
  }

  //! Mount a pak file for asset loading.
  virtual auto AddPakFile(const std::filesystem::path& path) -> void = 0;

  //! Mount a loose cooked content root for asset loading.
  virtual auto AddLooseCookedRoot(const std::filesystem::path& path) -> void
    = 0;

  //! Clear all mounted roots and pak files.
  virtual auto ClearMounts() -> void = 0;

  //! Clear cached assets/resources without unmounting sources.
  virtual auto TrimCache() -> void = 0;

  //! Set runtime residency policy (budget/trim/priority defaults).
  virtual auto SetResidencyPolicy(const ResidencyPolicy& policy) -> void = 0;

  //! Read back active runtime residency policy.
  [[nodiscard]] virtual auto GetResidencyPolicy() const noexcept
    -> ResidencyPolicy
    = 0;

  //! Query runtime residency state from the active cache.
  [[nodiscard]] virtual auto QueryResidencyPolicyState() const
    -> ResidencyPolicyState
    = 0;

  //! Enumerate scene entries available from currently mounted sources.
  [[nodiscard]] virtual auto EnumerateMountedScenes() const
    -> std::vector<MountedSceneEntry>
    = 0;

  //! Enumerate mounted content sources.
  [[nodiscard]] virtual auto EnumerateMountedSources() const
    -> std::vector<MountedSourceEntry>
    = 0;

  //! Register content-owned commands/CVars (`cntt.*`).
  virtual auto RegisterConsoleBindings(
    observer_ptr<console::Console> console) noexcept -> void
    = 0;

  //! Apply content runtime CVar values at the engine-defined sync point.
  virtual auto ApplyConsoleCVars(const console::Console& console) -> void = 0;

  //! Get cached resource without triggering a load.
  [[nodiscard]] virtual auto GetTexture(ResourceKey key) const noexcept
    -> std::shared_ptr<data::TextureResource>
    = 0;

  //! Get cached resource without triggering a load.
  [[nodiscard]] virtual auto GetBuffer(ResourceKey key) const noexcept
    -> std::shared_ptr<data::BufferResource>
    = 0;

  //! Get cached asset without triggering a load.
  [[nodiscard]] virtual auto GetMaterialAsset(
    const data::AssetKey& key) const noexcept
    -> std::shared_ptr<data::MaterialAsset>
    = 0;

  //! Get cached asset without triggering a load.
  [[nodiscard]] virtual auto GetGeometryAsset(
    const data::AssetKey& key) const noexcept
    -> std::shared_ptr<data::GeometryAsset>
    = 0;

  //! Get cached asset without triggering a load.
  [[nodiscard]] virtual auto GetScriptAsset(
    const data::AssetKey& key) const noexcept
    -> std::shared_ptr<data::ScriptAsset>
    = 0;
  //! Get cached script resource without triggering a load.
  [[nodiscard]] virtual auto GetScriptResource(ResourceKey key) const noexcept
    -> std::shared_ptr<data::ScriptResource>
    = 0;
  //! Async script resource load.
  virtual auto LoadScriptResourceAsync(ResourceKey key)
    -> co::Co<std::shared_ptr<data::ScriptResource>>
    = 0;
  //! Build script resource key from a loaded asset's source and table index.
  [[nodiscard]] virtual auto MakeScriptResourceKeyForAsset(
    const data::AssetKey& context_asset_key,
    data::pak::core::ResourceIndexT resource_index) const noexcept
    -> std::optional<ResourceKey>
    = 0;
  //! Read a script resource by table index from the context asset's source.
  [[nodiscard]] virtual auto ReadScriptResourceForAsset(
    const data::AssetKey& context_asset_key,
    data::pak::core::ResourceIndexT resource_index) const
    -> std::shared_ptr<const data::ScriptResource>
    = 0;

  //! Get cached physics scene sidecar asset without triggering a load.
  [[nodiscard]] virtual auto GetPhysicsSceneAsset(
    const data::AssetKey& key) const noexcept
    -> std::shared_ptr<data::PhysicsSceneAsset>
    = 0;
  //! Get cached physics resource without triggering a load.
  [[nodiscard]] virtual auto GetPhysicsResource(ResourceKey key) const noexcept
    -> std::shared_ptr<data::PhysicsResource>
    = 0;
  //! Async physics resource load.
  virtual auto LoadPhysicsResourceAsync(ResourceKey key)
    -> co::Co<std::shared_ptr<data::PhysicsResource>>
    = 0;
  //! Build physics resource key from source identity and table index.
  [[nodiscard]] virtual auto MakePhysicsResourceKey(data::SourceKey source_key,
    data::pak::core::ResourceIndexT resource_index) const noexcept
    -> std::optional<ResourceKey>
    = 0;
  //! Build physics resource key from a loaded asset's source and table index.
  [[nodiscard]] virtual auto MakePhysicsResourceKeyForAsset(
    const data::AssetKey& context_asset_key,
    data::pak::core::ResourceIndexT resource_index) const noexcept
    -> std::optional<ResourceKey>
    = 0;
  //! Read a collision shape descriptor by global asset index in the context
  //! asset's source.
  [[nodiscard]] virtual auto ReadCollisionShapeAssetDescForAsset(
    const data::AssetKey& context_asset_key,
    data::pak::core::ResourceIndexT shape_asset_index) const
    -> std::optional<data::pak::physics::CollisionShapeAssetDesc>
    = 0;
  //! Read a physics material descriptor by global asset index in the context
  //! asset's source.
  [[nodiscard]] virtual auto ReadPhysicsMaterialAssetDescForAsset(
    const data::AssetKey& context_asset_key,
    data::pak::core::ResourceIndexT material_asset_index) const
    -> std::optional<data::pak::physics::PhysicsMaterialAssetDesc>
    = 0;
  //! Find a physics scene sidecar in the same source that targets `scene_key`.
  [[nodiscard]] virtual auto FindPhysicsSidecarAssetKeyForScene(
    const data::AssetKey& scene_key) const -> std::optional<data::AssetKey>
    = 0;

  //! Get cached input action asset without triggering a load.
  [[nodiscard]] virtual auto GetInputActionAsset(
    const data::AssetKey& key) const noexcept
    -> std::shared_ptr<data::InputActionAsset>
    = 0;

  //! Get cached input mapping context asset without triggering a load.
  [[nodiscard]] virtual auto GetInputMappingContextAsset(
    const data::AssetKey& key) const noexcept
    -> std::shared_ptr<data::InputMappingContextAsset>
    = 0;

  //! Hydrate script slots for one scripting component from the scene source.
  [[nodiscard]] virtual auto GetHydratedScriptSlots(
    const data::SceneAsset& scene_asset,
    const data::pak::scripting::ScriptingComponentRecord& component) const
    -> std::vector<HydratedScriptSlot>
    = 0;

  //! Check whether a texture resource is cached.
  [[nodiscard]] virtual auto HasTexture(ResourceKey key) const noexcept -> bool
    = 0;

  //! Check whether a buffer resource is cached.
  [[nodiscard]] virtual auto HasBuffer(ResourceKey key) const noexcept -> bool
    = 0;

  //! Check whether a material asset is cached.
  [[nodiscard]] virtual auto HasMaterialAsset(
    const data::AssetKey& key) const noexcept -> bool
    = 0;

  //! Check whether a geometry asset is cached.
  [[nodiscard]] virtual auto HasGeometryAsset(
    const data::AssetKey& key) const noexcept -> bool
    = 0;

  //! Check whether a script asset is cached.
  [[nodiscard]] virtual auto HasScriptAsset(
    const data::AssetKey& key) const noexcept -> bool
    = 0;

  //! Check whether a physics scene sidecar asset is cached.
  [[nodiscard]] virtual auto HasPhysicsSceneAsset(
    const data::AssetKey& key) const noexcept -> bool
    = 0;

  //! Check whether an input action asset is cached.
  [[nodiscard]] virtual auto HasInputActionAsset(
    const data::AssetKey& key) const noexcept -> bool
    = 0;

  //! Check whether an input mapping context asset is cached.
  [[nodiscard]] virtual auto HasInputMappingContextAsset(
    const data::AssetKey& key) const noexcept -> bool
    = 0;

  //! Release (check in) a resource usage.
  virtual auto ReleaseResource(ResourceKey key) -> bool = 0;

  //! Release (check in) an asset usage.
  virtual auto ReleaseAsset(const data::AssetKey& key) -> bool = 0;

  //! Explicitly pin a resource in residency cache.
  virtual auto PinResource(ResourceKey key) -> bool = 0;

  //! Release one explicit resource pin.
  virtual auto UnpinResource(ResourceKey key) -> bool = 0;

  //! Explicitly pin an asset in residency cache.
  virtual auto PinAsset(const data::AssetKey& key) -> bool = 0;

  //! Release one explicit asset pin.
  virtual auto UnpinAsset(const data::AssetKey& key) -> bool = 0;

  //! Subscribe to eviction notifications for a resource or asset type.
  virtual auto SubscribeResourceEvictions(
    TypeId resource_type, EvictionHandler handler) -> EvictionSubscription
    = 0;

  //=== Hot Reloading ===-----------------------------------------------------//

  //! Trigger a hot-reload of a script asset from a file path.
  virtual auto ReloadScript(const std::filesystem::path& path) -> void = 0;

  //! Trigger a full reload of all currently loaded script assets.
  virtual auto ReloadAllScripts() -> void = 0;

  using ScriptReloadCallback = std::function<void(
    const data::AssetKey&, std::shared_ptr<const data::ScriptResource>)>;

  //! Subscribe to script reload events.
  virtual auto SubscribeScriptReload(ScriptReloadCallback callback)
    -> EvictionSubscription
    = 0;

  //! Mint a synthetic texture key suitable for buffer-driven workflows.
  [[nodiscard]] virtual auto MintSyntheticTextureKey() -> ResourceKey = 0;

  //! Mint a synthetic buffer key suitable for buffer-driven workflows.
  [[nodiscard]] virtual auto MintSyntheticBufferKey() -> ResourceKey = 0;

protected:
  virtual void UnsubscribeResourceEvictions(
    TypeId resource_type, uint64_t id) noexcept
    = 0;

  [[nodiscard]] auto MakeEvictionSubscription(TypeId resource_type, uint64_t id,
    observer_ptr<IAssetLoader> owner, const std::shared_ptr<int>& alive_token)
    -> EvictionSubscription
  {
    EvictionSubscription subscription;
    subscription.id_ = id;
    subscription.resource_type_ = resource_type;
    subscription.owner_ = owner;
    subscription.alive_token_ = alive_token;
    return subscription;
  }
};

} // namespace oxygen::content
