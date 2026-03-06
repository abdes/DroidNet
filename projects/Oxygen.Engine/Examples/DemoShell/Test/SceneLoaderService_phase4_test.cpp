//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include "DemoShell/Services/SceneLoaderService.h"
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Config/PathFinder.h>
#include <Oxygen/Config/PathFinderConfig.h>
#include <Oxygen/Content/EvictionEvents.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/InputActionAsset.h>
#include <Oxygen/Data/InputMappingContextAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PhysicsResource.h>
#include <Oxygen/Data/PhysicsSceneAsset.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Data/ScriptResource.h>
#include <Oxygen/Data/TextureResource.h>

namespace oxygen::examples::testing {

namespace {
  namespace pakw = data::pak::world;
  namespace pakp = data::pak::physics;

  auto BuildMinimalSceneDescriptorBytes(const uint32_t node_count)
    -> std::vector<std::byte>
  {
    auto desc = pakw::SceneAssetDesc {};
    desc.header.asset_type = static_cast<uint8_t>(data::AssetType::kScene);
    desc.header.version = pakw::kSceneAssetVersion;
    desc.nodes.offset = static_cast<data::pak::core::OffsetT>(sizeof(desc));
    desc.nodes.count = node_count;
    desc.nodes.entry_size = sizeof(pakw::NodeRecord);
    const auto nodes_bytes
      = static_cast<size_t>(node_count) * sizeof(pakw::NodeRecord);
    desc.scene_strings.offset
      = static_cast<data::pak::core::StringTableOffsetT>(
        desc.nodes.offset + static_cast<data::pak::core::OffsetT>(nodes_bytes));
    desc.scene_strings.size = 1;

    auto bytes = std::vector<std::byte> {};
    bytes.resize(
      static_cast<size_t>(desc.scene_strings.offset + desc.scene_strings.size),
      std::byte { 0 });
    std::memcpy(bytes.data(), &desc, sizeof(desc));

    for (uint32_t i = 0; i < node_count; ++i) {
      auto node = pakw::NodeRecord {};
      node.parent_index = (i == 0U) ? 0U : 0U;
      node.scene_name_offset = 0U;
      const auto node_offset
        = static_cast<size_t>(desc.nodes.offset) + i * sizeof(node);
      std::memcpy(bytes.data() + node_offset, &node, sizeof(node));
    }

    bytes[desc.scene_strings.offset] = std::byte { 0 };
    return bytes;
  }

  auto BuildMinimalPhysicsSidecarDescriptorBytes(
    const data::AssetKey& scene_key, const uint32_t node_count,
    const base::Sha256Digest& scene_hash) -> std::vector<std::byte>
  {
    auto desc = pakp::PhysicsSceneAssetDesc {};
    desc.header.asset_type
      = static_cast<uint8_t>(data::AssetType::kPhysicsScene);
    desc.header.version = pakp::kPhysicsSceneAssetVersion;
    desc.target_scene_key = scene_key;
    desc.target_node_count = node_count;
    std::copy(scene_hash.begin(), scene_hash.end(),
      std::begin(desc.target_scene_content_hash));
    desc.component_table_count = 0;
    desc.component_table_directory_offset = 0;

    auto bytes = std::vector<std::byte>(sizeof(desc));
    std::memcpy(bytes.data(), &desc, sizeof(desc));
    return bytes;
  }

  class SceneLoaderTestAssetLoader final : public content::IAssetLoader {
  public:
    SceneLoaderTestAssetLoader() = default;
    ~SceneLoaderTestAssetLoader() override = default;

    OXYGEN_MAKE_NON_COPYABLE(SceneLoaderTestAssetLoader)
    OXYGEN_DEFAULT_MOVABLE(SceneLoaderTestAssetLoader)

    auto PutScene(const data::AssetKey& key,
      std::shared_ptr<data::SceneAsset> scene) -> void
    {
      scenes_.insert_or_assign(key, std::move(scene));
    }

    auto PutPhysicsSidecar(const data::AssetKey& scene_key,
      const data::AssetKey& sidecar_key,
      std::shared_ptr<data::PhysicsSceneAsset> sidecar) -> void
    {
      sidecar_keys_by_scene_.insert_or_assign(scene_key, sidecar_key);
      sidecars_.insert_or_assign(sidecar_key, std::move(sidecar));
    }

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
      const data::AssetKey& key, SceneCallback on_complete) override
    {
      const auto it = scenes_.find(key);
      on_complete(it == scenes_.end() ? nullptr : it->second);
    }

    void StartLoadScriptAsset(
      const data::AssetKey& /*key*/, ScriptCallback on_complete) override
    {
      on_complete(nullptr);
    }

    void StartLoadPhysicsSceneAsset(
      const data::AssetKey& key, PhysicsSceneCallback on_complete) override
    {
      const auto it = sidecars_.find(key);
      on_complete(it == sidecars_.end() ? nullptr : it->second);
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
      const data::AssetKey& key) const noexcept
      -> std::shared_ptr<data::PhysicsSceneAsset> override
    {
      const auto it = sidecars_.find(key);
      return it == sidecars_.end() ? nullptr : it->second;
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
      const data::AssetKey& scene_key) const
      -> std::optional<data::AssetKey> override
    {
      const auto it = sidecar_keys_by_scene_.find(scene_key);
      return it == sidecar_keys_by_scene_.end()
        ? std::nullopt
        : std::optional<data::AssetKey> { it->second };
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
      const data::AssetKey& key) const noexcept -> bool override
    {
      return sidecars_.contains(key);
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
      return content::ResourceKey { next_resource_key_++ };
    }
    [[nodiscard]] auto MintSyntheticBufferKey() -> content::ResourceKey override
    {
      return content::ResourceKey { next_resource_key_++ };
    }

  private:
    void UnsubscribeResourceEvictions(
      TypeId resource_type, const uint64_t id) noexcept override
    {
      const auto it = eviction_handlers_.find(resource_type);
      if (it == eviction_handlers_.end()) {
        return;
      }
      it->second.erase(id);
    }

    content::ResidencyPolicy residency_policy_ {};
    std::unordered_map<data::AssetKey, std::shared_ptr<data::SceneAsset>>
      scenes_ {};
    std::unordered_map<data::AssetKey, data::AssetKey>
      sidecar_keys_by_scene_ {};
    std::unordered_map<data::AssetKey, std::shared_ptr<data::PhysicsSceneAsset>>
      sidecars_ {};
    std::unordered_map<TypeId, std::unordered_map<uint64_t, EvictionHandler>>
      eviction_handlers_ {};
    uint64_t next_subscription_id_ { 1U };
    std::shared_ptr<int> eviction_alive_token_ { std::make_shared<int>(0) };
    uint64_t next_resource_key_ { 1U };
  };
} // namespace

NOLINT_TEST(SceneLoaderServicePhase4Test,
  StartLoadFailsWhenTargetSceneContentHashMismatches)
{
  auto loader = SceneLoaderTestAssetLoader {};
  const auto scene_key
    = data::AssetKey::FromVirtualPath("/Game/Tests/Phase4/identity.oscene");
  const auto sidecar_key
    = data::AssetKey::FromVirtualPath("/Game/Tests/Phase4/identity.opscene");

  const auto scene_bytes = BuildMinimalSceneDescriptorBytes(1U);
  auto scene_asset = std::make_shared<data::SceneAsset>(scene_key, scene_bytes);
  const auto scene_hash = base::ComputeSha256(scene_asset->GetRawData());
  auto mismatched_hash = scene_hash;
  mismatched_hash[0] ^= 0xFFU;

  auto sidecar_asset = std::make_shared<data::PhysicsSceneAsset>(sidecar_key,
    BuildMinimalPhysicsSidecarDescriptorBytes(scene_key, 1U, mismatched_hash));

  loader.PutScene(scene_key, scene_asset);
  loader.PutPhysicsSidecar(scene_key, sidecar_key, sidecar_asset);

  auto path_finder = PathFinder(std::make_shared<const PathFinderConfig>(),
    std::filesystem::current_path());
  auto service = std::make_shared<SceneLoaderService>(loader,
    Extent<uint32_t> { 1280U, 720U }, std::filesystem::path {}, nullptr,
    nullptr, nullptr, std::move(path_finder));

  service->StartLoad(scene_key);

  EXPECT_TRUE(service->IsFailed());
  EXPECT_FALSE(service->IsReady());
}

NOLINT_TEST(
  SceneLoaderServicePhase4Test, StartLoadFailsWhenTargetSceneKeyMismatches)
{
  auto loader = SceneLoaderTestAssetLoader {};
  const auto scene_key
    = data::AssetKey::FromVirtualPath("/Game/Tests/Phase4/key_mismatch.oscene");
  const auto foreign_scene_key
    = data::AssetKey::FromVirtualPath("/Game/Tests/Phase4/foreign.oscene");
  const auto sidecar_key = data::AssetKey::FromVirtualPath(
    "/Game/Tests/Phase4/key_mismatch.opscene");

  const auto scene_bytes = BuildMinimalSceneDescriptorBytes(1U);
  auto scene_asset = std::make_shared<data::SceneAsset>(scene_key, scene_bytes);
  const auto scene_hash = base::ComputeSha256(scene_asset->GetRawData());

  auto sidecar_asset = std::make_shared<data::PhysicsSceneAsset>(sidecar_key,
    BuildMinimalPhysicsSidecarDescriptorBytes(
      foreign_scene_key, 1U, scene_hash));

  loader.PutScene(scene_key, scene_asset);
  loader.PutPhysicsSidecar(scene_key, sidecar_key, sidecar_asset);

  auto path_finder = PathFinder(std::make_shared<const PathFinderConfig>(),
    std::filesystem::current_path());
  auto service = std::make_shared<SceneLoaderService>(loader,
    Extent<uint32_t> { 1280U, 720U }, std::filesystem::path {}, nullptr,
    nullptr, nullptr, std::move(path_finder));

  service->StartLoad(scene_key);

  EXPECT_TRUE(service->IsFailed());
  EXPECT_FALSE(service->IsReady());
}

NOLINT_TEST(
  SceneLoaderServicePhase4Test, StartLoadFailsWhenTargetNodeCountMismatches)
{
  auto loader = SceneLoaderTestAssetLoader {};
  const auto scene_key = data::AssetKey::FromVirtualPath(
    "/Game/Tests/Phase4/node_count_mismatch.oscene");
  const auto sidecar_key = data::AssetKey::FromVirtualPath(
    "/Game/Tests/Phase4/node_count_mismatch.opscene");

  const auto scene_bytes = BuildMinimalSceneDescriptorBytes(1U);
  auto scene_asset = std::make_shared<data::SceneAsset>(scene_key, scene_bytes);
  const auto scene_hash = base::ComputeSha256(scene_asset->GetRawData());

  auto sidecar_asset = std::make_shared<data::PhysicsSceneAsset>(sidecar_key,
    BuildMinimalPhysicsSidecarDescriptorBytes(scene_key, 2U, scene_hash));

  loader.PutScene(scene_key, scene_asset);
  loader.PutPhysicsSidecar(scene_key, sidecar_key, sidecar_asset);

  auto path_finder = PathFinder(std::make_shared<const PathFinderConfig>(),
    std::filesystem::current_path());
  auto service = std::make_shared<SceneLoaderService>(loader,
    Extent<uint32_t> { 1280U, 720U }, std::filesystem::path {}, nullptr,
    nullptr, nullptr, std::move(path_finder));

  service->StartLoad(scene_key);

  EXPECT_TRUE(service->IsFailed());
  EXPECT_FALSE(service->IsReady());
}

NOLINT_TEST(SceneLoaderServicePhase4Test,
  StartLoadSucceedsWhenSceneIdentityHashKeyAndNodeCountMatch)
{
  auto loader = SceneLoaderTestAssetLoader {};
  const auto scene_key
    = data::AssetKey::FromVirtualPath("/Game/Tests/Phase4/valid.oscene");
  const auto sidecar_key
    = data::AssetKey::FromVirtualPath("/Game/Tests/Phase4/valid.opscene");

  const auto scene_bytes = BuildMinimalSceneDescriptorBytes(1U);
  auto scene_asset = std::make_shared<data::SceneAsset>(scene_key, scene_bytes);
  const auto scene_hash = base::ComputeSha256(scene_asset->GetRawData());

  auto sidecar_asset = std::make_shared<data::PhysicsSceneAsset>(sidecar_key,
    BuildMinimalPhysicsSidecarDescriptorBytes(scene_key, 1U, scene_hash));

  loader.PutScene(scene_key, scene_asset);
  loader.PutPhysicsSidecar(scene_key, sidecar_key, sidecar_asset);

  auto path_finder = PathFinder(std::make_shared<const PathFinderConfig>(),
    std::filesystem::current_path());
  auto service = std::make_shared<SceneLoaderService>(loader,
    Extent<uint32_t> { 1280U, 720U }, std::filesystem::path {}, nullptr,
    nullptr, nullptr, std::move(path_finder));

  service->StartLoad(scene_key);

  ASSERT_TRUE(service->IsReady());
  EXPECT_FALSE(service->IsFailed());
  auto result = service->GetResult();
  EXPECT_EQ(result.scene_key, scene_key);
  EXPECT_THAT(result.asset, ::testing::NotNull());
  EXPECT_THAT(result.physics_asset, ::testing::NotNull());
}

} // namespace oxygen::examples::testing
