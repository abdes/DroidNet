//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <span>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/EvictionEvents.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Content/ResourceTypeList.h>
#include <Oxygen/Content/api_export.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/data/AssetKey.h>
#include <Oxygen/data/GeometryAsset.h>
#include <Oxygen/data/MaterialAsset.h>
#include <Oxygen/data/SceneAsset.h>

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
  std::span<const uint8_t> bytes {};
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
  virtual ~IAssetLoader() = default;

  using TextureCallback
    = std::function<void(std::shared_ptr<data::TextureResource>)>;
  using BufferCallback
    = std::function<void(std::shared_ptr<data::BufferResource>)>;
  using MaterialCallback
    = std::function<void(std::shared_ptr<data::MaterialAsset>)>;
  using GeometryCallback
    = std::function<void(std::shared_ptr<data::GeometryAsset>)>;
  using SceneCallback = std::function<void(std::shared_ptr<data::SceneAsset>)>;
  using EvictionHandler = std::function<void(const EvictionEvent&)>;

  //! RAII handle for resource eviction subscriptions.
  class EvictionSubscription {
  public:
    EvictionSubscription() noexcept = default;
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

    EvictionSubscription& operator=(EvictionSubscription&& other) noexcept
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

    ~EvictionSubscription() noexcept { Cancel(); }

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
    std::weak_ptr<int> alive_token_ {};
  };

  //! Begin loading a texture resource and invoke `on_complete` on completion.
  virtual void StartLoadTexture(ResourceKey key, TextureCallback on_complete)
    = 0;

  //! Decode a texture resource from caller-provided cooked bytes.
  virtual void StartLoadTexture(
    CookedResourceData<data::TextureResource> cooked,
    TextureCallback on_complete)
    = 0;

  //! Begin loading a buffer resource and invoke `on_complete` on completion.
  virtual void StartLoadBuffer(ResourceKey key, BufferCallback on_complete) = 0;

  //! Decode a buffer resource from caller-provided cooked bytes.
  virtual void StartLoadBuffer(
    CookedResourceData<data::BufferResource> cooked, BufferCallback on_complete)
    = 0;

  //! Begin loading a material asset and invoke `on_complete` on completion.
  virtual void StartLoadMaterialAsset(
    const data::AssetKey& key, MaterialCallback on_complete)
    = 0;

  //! Begin loading a geometry asset and invoke `on_complete` on completion.
  virtual void StartLoadGeometryAsset(
    const data::AssetKey& key, GeometryCallback on_complete)
    = 0;

  //! Begin loading a scene asset and invoke `on_complete` on completion.
  virtual void StartLoadScene(
    const data::AssetKey& key, SceneCallback on_complete)
    = 0;

  //! Mount a pak file for asset loading.
  virtual auto AddPakFile(const std::filesystem::path& path) -> void = 0;

  //! Mount a loose cooked content root for asset loading.
  virtual auto AddLooseCookedRoot(const std::filesystem::path& path) -> void
    = 0;

  //! Clear all mounted roots and pak files.
  virtual auto ClearMounts() -> void = 0;

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

  //! Release (check in) a resource usage.
  virtual auto ReleaseResource(ResourceKey key) -> bool = 0;

  //! Release (check in) an asset usage.
  virtual auto ReleaseAsset(const data::AssetKey& key) -> bool = 0;

  //! Subscribe to eviction notifications for a resource or asset type.
  virtual auto SubscribeResourceEvictions(
    TypeId resource_type, EvictionHandler handler) -> EvictionSubscription
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
    observer_ptr<IAssetLoader> owner, std::shared_ptr<int> alive_token)
    -> EvictionSubscription
  {
    EvictionSubscription subscription;
    subscription.id_ = id;
    subscription.resource_type_ = resource_type;
    subscription.owner_ = owner;
    subscription.alive_token_ = std::move(alive_token);
    return subscription;
  }
};

} // namespace oxygen::content
