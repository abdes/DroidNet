//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/OxCo/Co.h>

namespace oxygen::content::testing {

//! An asset loader that has nothing: every query returns an empty result and
//! every load completes with nothing. Tests derive from it and override only
//! the queries their code under test makes.
// NOLINTBEGIN(*-convert-member-functions-to-static, *-identifier-length)
class StubAssetLoader : public IAssetLoader {
public:
  using IAssetLoader::StartLoadBuffer;
  using IAssetLoader::StartLoadGeometryAsset;
  using IAssetLoader::StartLoadMaterialAsset;
  using IAssetLoader::StartLoadPhysicsResource;
  using IAssetLoader::StartLoadPhysicsSceneAsset;
  using IAssetLoader::StartLoadScene;
  using IAssetLoader::StartLoadScriptAsset;
  using IAssetLoader::StartLoadTexture;

  auto ResolveTextureResourceKey(const TextureResourceLocator& /*locator*/)
    -> std::optional<ResourceKey> override
  {
    return {};
  }
  auto MakeTextureResourceKey(data::SourceKey /*source_key*/,
    data::pak::core::ResourceIndexT /*resource_index*/)
    -> std::optional<ResourceKey> override
  {
    return {};
  }
  auto MakeTextureResourceKeyForAsset(const data::Asset& /*context_asset*/,
    data::ResourceReferenceIndex /*resource_index*/)
    -> std::optional<ResourceKey> override
  {
    return {};
  }
  void StartLoadTexture(
    ResourceKey /*key*/, TextureCallback on_complete) override
  {
    on_complete(nullptr);
  }
  void StartLoadTexture(CookedResourceData<data::TextureResource> /*cooked*/,
    TextureCallback on_complete) override
  {
    on_complete(nullptr);
  }
  void StartLoadBuffer(ResourceKey /*key*/, BufferCallback on_complete) override
  {
    on_complete(nullptr);
  }
  void StartLoadBuffer(CookedResourceData<data::BufferResource> /*cooked*/,
    BufferCallback on_complete) override
  {
    on_complete(nullptr);
  }
  void StartLoadMaterialAsset(const data::AssetKey& /*key*/,
    data::SourceKey /*source_key*/, const LoadRequest& /*request*/,
    MaterialCallback on_complete) override
  {
    on_complete(nullptr);
  }
  void StartLoadMaterialAsset(
    const data::AssetKey& /*key*/, MaterialCallback on_complete) override
  {
    on_complete(nullptr);
  }
  void StartLoadGeometryAsset(const data::AssetKey& /*key*/,
    data::SourceKey /*source_key*/, const LoadRequest& /*request*/,
    GeometryCallback on_complete) override
  {
    on_complete(nullptr);
  }
  void StartLoadGeometryAsset(
    const data::AssetKey& /*key*/, GeometryCallback on_complete) override
  {
    on_complete(nullptr);
  }
  void StartLoadScene(const data::AssetKey& /*key*/,
    data::SourceKey /*source_key*/, const LoadRequest& /*request*/,
    SceneCallback on_complete) override
  {
    on_complete(nullptr);
  }
  void StartLoadScene(
    const data::AssetKey& /*key*/, SceneCallback on_complete) override
  {
    on_complete(nullptr);
  }
  void StartLoadPhysicsSceneAsset(const data::AssetKey& /*key*/,
    data::SourceKey /*source_key*/, const LoadRequest& /*request*/,
    PhysicsSceneCallback on_complete) override
  {
    on_complete(nullptr);
  }
  void StartLoadPhysicsSceneAsset(
    const data::AssetKey& /*key*/, PhysicsSceneCallback on_complete) override
  {
    on_complete(nullptr);
  }
  void StartLoadPhysicsResource(
    ResourceKey /*key*/, PhysicsResourceCallback on_complete) override
  {
    on_complete(nullptr);
  }
  void StartLoadScriptAsset(const data::AssetKey& /*key*/,
    data::SourceKey /*source_key*/, const LoadRequest& /*request*/,
    ScriptCallback on_complete) override
  {
    on_complete(nullptr);
  }
  void StartLoadScriptAsset(
    const data::AssetKey& /*key*/, ScriptCallback on_complete) override
  {
    on_complete(nullptr);
  }
  void StartLoadInputMappingContextAsset(const data::AssetKey& /*key*/,
    InputMappingContextCallback on_complete) override
  {
    on_complete(nullptr);
  }
  auto AddPakFile(const std::filesystem::path& /*path*/) -> void override { }
  auto AddLooseCookedRoot(const std::filesystem::path& /*path*/)
    -> void override
  {
  }
  auto RetireLooseCookedGeneration(data::SourceKey /*source_key*/)
    -> bool override
  {
    return {};
  }
  auto PrepareLooseCookedRootsAsync(
    std::vector<std::filesystem::path> /*roots*/)
    -> co::Co<PreparedMountSet> override
  {
    // Mount sets are made only by AssetLoader.
    throw std::logic_error("StubAssetLoader does not prepare mounts");
  }
  auto CommitPreparedMounts(PreparedMountSet&& /*prepared*/)
    -> MountRetirement override
  {
    throw std::logic_error("StubAssetLoader does not commit mounts");
  }
  auto ClearMounts() -> void override { }
  auto WaitForPendingLoadsAsync() -> co::Co<> override { co_return; }
  auto TrimCache() -> void override { }
  auto ProcessPendingReleases() -> void override { }
  auto SetResidencyPolicy(const ResidencyPolicy& /*policy*/) -> void override {
  }
  auto GetResidencyPolicy() const noexcept -> ResidencyPolicy override
  {
    return {};
  }
  auto QueryResidencyPolicyState() const -> ResidencyPolicyState override
  {
    return {};
  }
  auto EnumerateMountedScenes() const -> std::vector<MountedSceneEntry> override
  {
    return {};
  }
  auto EnumerateMountedInputContexts() const
    -> std::vector<MountedInputContextEntry> override
  {
    return {};
  }
  auto EnumerateMountedSources() const
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
  auto PeekTexture(ResourceKey /*key*/) const
    -> observer_ptr<const data::TextureResource> override
  {
    return {};
  }
  auto GetTexture(ResourceKey /*key*/)
    -> std::shared_ptr<data::TextureResource> override
  {
    return {};
  }
  auto GetBuffer(ResourceKey /*key*/)
    -> std::shared_ptr<data::BufferResource> override
  {
    return {};
  }
  auto GetMaterialAsset(
    const data::AssetKey& /*key*/, const data::Asset& /*context*/)
    -> std::shared_ptr<data::MaterialAsset> override
  {
    return {};
  }
  auto GetMaterialAsset(const data::AssetKey& /*key*/)
    -> std::shared_ptr<data::MaterialAsset> override
  {
    return {};
  }
  auto GetGeometryAsset(
    const data::AssetKey& /*key*/, const data::Asset& /*context*/)
    -> std::shared_ptr<data::GeometryAsset> override
  {
    return {};
  }
  auto GetGeometryAsset(const data::AssetKey& /*key*/)
    -> std::shared_ptr<data::GeometryAsset> override
  {
    return {};
  }
  auto GetScriptAsset(
    const data::AssetKey& /*key*/, const data::Asset& /*context*/)
    -> std::shared_ptr<data::ScriptAsset> override
  {
    return {};
  }
  auto GetScriptAsset(const data::AssetKey& /*key*/)
    -> std::shared_ptr<data::ScriptAsset> override
  {
    return {};
  }
  auto GetScriptResource(ResourceKey /*key*/)
    -> std::shared_ptr<data::ScriptResource> override
  {
    return {};
  }
  auto LoadScriptResourceAsync(ResourceKey /*key*/)
    -> co::Co<std::shared_ptr<data::ScriptResource>> override
  {
    co_return {};
  }
  auto MakeScriptResourceKeyForAsset(const data::Asset& /*context_asset*/,
    data::ResourceReferenceIndex /*resource_index*/)
    -> std::optional<ResourceKey> override
  {
    return {};
  }
  auto ReadScriptResourceForAsset(const data::Asset& /*context_asset*/,
    data::ResourceReferenceIndex /*resource_index*/) const
    -> std::shared_ptr<const data::ScriptResource> override
  {
    return {};
  }
  auto GetPhysicsSceneAsset(const data::AssetKey& /*key*/)
    -> std::shared_ptr<data::PhysicsSceneAsset> override
  {
    return {};
  }
  auto GetPhysicsResource(ResourceKey /*key*/)
    -> std::shared_ptr<data::PhysicsResource> override
  {
    return {};
  }
  auto LoadPhysicsResourceAsync(ResourceKey /*key*/)
    -> co::Co<std::shared_ptr<data::PhysicsResource>> override
  {
    co_return {};
  }
  auto MakePhysicsResourceKey(data::SourceKey /*source_key*/,
    data::pak::core::ResourceIndexT /*resource_index*/)
    -> std::optional<ResourceKey> override
  {
    return {};
  }
  auto MakePhysicsResourceKeyForAsset(const data::Asset& /*context_asset*/,
    data::pak::core::ResourceIndexT /*resource_index*/)
    -> std::optional<ResourceKey> override
  {
    return {};
  }
  auto MakePhysicsResourceKeyForAsset(const data::Asset& /*context_asset*/,
    const data::AssetKey& /*resource_asset_key*/)
    -> std::optional<ResourceKey> override
  {
    return {};
  }
  auto ReadCollisionShapeAssetDescForAsset(const data::Asset& /*context_asset*/,
    const data::AssetKey& /*shape_asset_key*/) const
    -> std::optional<data::pak::physics::CollisionShapeAssetDesc> override
  {
    return {};
  }
  auto ReadPhysicsMaterialAssetDescForAsset(
    const data::Asset& /*context_asset*/,
    const data::AssetKey& /*material_asset_key*/) const
    -> std::optional<data::pak::physics::PhysicsMaterialAssetDesc> override
  {
    return {};
  }
  auto FindPhysicsSidecarAssetKeyForScene(const data::Asset& /*scene_asset*/,
    const content::ContentLoadScope& /*scope*/)
    -> std::optional<data::AssetKey> override
  {
    return {};
  }
  auto GetInputActionAsset(
    const data::AssetKey& /*key*/, const data::Asset& /*context*/)
    -> std::shared_ptr<data::InputActionAsset> override
  {
    return {};
  }
  auto GetInputActionAsset(const data::AssetKey& /*key*/)
    -> std::shared_ptr<data::InputActionAsset> override
  {
    return {};
  }
  auto GetInputMappingContextAsset(const data::AssetKey& /*key*/)
    -> std::shared_ptr<data::InputMappingContextAsset> override
  {
    return {};
  }
  auto GetHydratedScriptSlots(const data::SceneAsset& /*scene_asset*/,
    const data::pak::scripting::ScriptingComponentRecord& /*component*/) const
    -> std::vector<HydratedScriptSlot> override
  {
    return {};
  }
  auto HasTexture(ResourceKey /*key*/) const noexcept -> bool override
  {
    return {};
  }
  auto HasBuffer(ResourceKey /*key*/) const noexcept -> bool override
  {
    return {};
  }
  auto HasMaterialAsset(const data::AssetKey& /*key*/) const noexcept
    -> bool override
  {
    return {};
  }
  auto HasGeometryAsset(const data::AssetKey& /*key*/) const noexcept
    -> bool override
  {
    return {};
  }
  auto HasScriptAsset(const data::AssetKey& /*key*/) const noexcept
    -> bool override
  {
    return {};
  }
  auto HasPhysicsSceneAsset(const data::AssetKey& /*key*/) const noexcept
    -> bool override
  {
    return {};
  }
  auto HasInputActionAsset(const data::AssetKey& /*key*/) const noexcept
    -> bool override
  {
    return {};
  }
  auto HasInputMappingContextAsset(const data::AssetKey& /*key*/) const noexcept
    -> bool override
  {
    return {};
  }
  auto PinResource(ResourceKey /*key*/) -> ResidencyPin override { return {}; }
  auto PinAsset(const data::AssetKey& /*key*/) -> ResidencyPin override
  {
    return {};
  }
  auto SubscribeResourceEvictions(TypeId /*resource_type*/,
    EvictionHandler /*handler*/) -> EvictionSubscription override
  {
    return {};
  }
  auto MintSyntheticTextureKey() -> ResourceKey override { return {}; }
  auto MintSyntheticBufferKey() -> ResourceKey override { return {}; }
  auto MintSyntheticScriptKey() -> ResourceKey override { return {}; }
  auto DoMountLooseCookedGeneration(const std::filesystem::path& /*path*/,
    std::optional<data::SourceKey> /*replaces*/) -> data::SourceKey override
  {
    return {};
  }
  void UnsubscribeResourceEvictions(
    TypeId /*resource_type*/, EvictionSubscriptionId /*id*/) noexcept override
  {
  }
};
// NOLINTEND(*-convert-member-functions-to-static, *-identifier-length)

} // namespace oxygen::content::testing
