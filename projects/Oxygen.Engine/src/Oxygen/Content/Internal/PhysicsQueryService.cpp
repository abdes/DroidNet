//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstring>
#include <stdexcept>
#include <string>

#include <Oxygen/Content/Internal/PhysicsQueryService.h>
#include <Oxygen/Data/AssetType.h>

namespace oxygen::content::internal {

auto PhysicsQueryService::MakePhysicsResourceKey(
  const data::SourceKey source_key,
  const data::pak::ResourceIndexT resource_index,
  const Callbacks& callbacks) const noexcept -> std::optional<ResourceKey>
{
  const auto source_id = callbacks.resolve_source_id_for_source_key(source_key);
  if (!source_id.has_value()) {
    return std::nullopt;
  }
  return callbacks.make_physics_resource_key(*source_id, resource_index);
}

auto PhysicsQueryService::MakePhysicsResourceKeyForAsset(
  const data::AssetKey& context_asset_key,
  const data::pak::ResourceIndexT resource_index,
  const Callbacks& callbacks) const noexcept -> std::optional<ResourceKey>
{
  const auto source_id
    = callbacks.resolve_source_id_for_asset(context_asset_key);
  if (!source_id.has_value()) {
    return std::nullopt;
  }
  return callbacks.make_physics_resource_key(*source_id, resource_index);
}

auto PhysicsQueryService::ReadCollisionShapeAssetDescForAsset(
  const data::AssetKey& context_asset_key,
  const data::pak::ResourceIndexT shape_asset_index,
  const Callbacks& callbacks) const
  -> std::optional<data::pak::CollisionShapeAssetDesc>
{
  const auto source_id
    = callbacks.resolve_source_id_for_asset(context_asset_key);
  if (!source_id.has_value()) {
    return std::nullopt;
  }

  const auto* source = callbacks.resolve_source_for_id(*source_id);
  if (source == nullptr) {
    return std::nullopt;
  }
  const auto key_opt = source->GetAssetKeyByIndex(shape_asset_index.get());
  if (!key_opt.has_value()) {
    return std::nullopt;
  }
  const auto locator_opt = source->FindAsset(*key_opt);
  if (!locator_opt.has_value()) {
    return std::nullopt;
  }
  auto desc_reader = source->CreateAssetDescriptorReader(*locator_opt);
  if (!desc_reader) {
    return std::nullopt;
  }
  auto blob = desc_reader->ReadBlob(sizeof(data::pak::CollisionShapeAssetDesc));
  if (!blob) {
    return std::nullopt;
  }
  data::pak::CollisionShapeAssetDesc desc {};
  std::memcpy(&desc, blob->data(), sizeof(desc));
  if (static_cast<data::AssetType>(desc.header.asset_type)
    != data::AssetType::kCollisionShape) {
    return std::nullopt;
  }
  return desc;
}

auto PhysicsQueryService::ReadPhysicsMaterialAssetDescForAsset(
  const data::AssetKey& context_asset_key,
  const data::pak::ResourceIndexT material_asset_index,
  const Callbacks& callbacks) const
  -> std::optional<data::pak::PhysicsMaterialAssetDesc>
{
  const auto source_id
    = callbacks.resolve_source_id_for_asset(context_asset_key);
  if (!source_id.has_value()) {
    return std::nullopt;
  }

  const auto* source = callbacks.resolve_source_for_id(*source_id);
  if (source == nullptr) {
    return std::nullopt;
  }
  const auto key_opt = source->GetAssetKeyByIndex(material_asset_index.get());
  if (!key_opt.has_value()) {
    return std::nullopt;
  }
  const auto locator_opt = source->FindAsset(*key_opt);
  if (!locator_opt.has_value()) {
    return std::nullopt;
  }
  auto desc_reader = source->CreateAssetDescriptorReader(*locator_opt);
  if (!desc_reader) {
    return std::nullopt;
  }
  auto blob
    = desc_reader->ReadBlob(sizeof(data::pak::PhysicsMaterialAssetDesc));
  if (!blob) {
    return std::nullopt;
  }
  data::pak::PhysicsMaterialAssetDesc desc {};
  std::memcpy(&desc, blob->data(), sizeof(desc));
  if (static_cast<data::AssetType>(desc.header.asset_type)
    != data::AssetType::kPhysicsMaterial) {
    return std::nullopt;
  }
  return desc;
}

auto PhysicsQueryService::FindPhysicsSidecarAssetKeyForScene(
  const data::AssetKey& scene_key, const Callbacks& callbacks) const
  -> std::optional<data::AssetKey>
{
  const auto source_id = callbacks.resolve_source_id_for_asset(scene_key);
  if (!source_id.has_value()) {
    return std::nullopt;
  }

  const auto* source = callbacks.resolve_source_for_id(*source_id);
  if (source == nullptr) {
    return std::nullopt;
  }
  std::optional<data::AssetKey> matched_key {};

  const auto asset_count = source->GetAssetCount();
  for (size_t i = 0; i < asset_count; ++i) {
    const auto key_opt = source->GetAssetKeyByIndex(static_cast<uint32_t>(i));
    if (!key_opt.has_value()) {
      continue;
    }
    const auto locator_opt = source->FindAsset(*key_opt);
    if (!locator_opt.has_value()) {
      continue;
    }

    auto desc_reader = source->CreateAssetDescriptorReader(*locator_opt);
    if (!desc_reader) {
      continue;
    }
    auto header_blob = desc_reader->ReadBlob(sizeof(data::pak::AssetHeader));
    if (!header_blob) {
      continue;
    }
    data::pak::AssetHeader header {};
    std::memcpy(&header, header_blob->data(), sizeof(header));
    if (static_cast<data::AssetType>(header.asset_type)
      != data::AssetType::kPhysicsScene) {
      continue;
    }

    desc_reader = source->CreateAssetDescriptorReader(*locator_opt);
    if (!desc_reader) {
      continue;
    }
    auto blob = desc_reader->ReadBlob(sizeof(data::pak::PhysicsSceneAssetDesc));
    if (!blob) {
      continue;
    }
    data::pak::PhysicsSceneAssetDesc desc {};
    std::memcpy(&desc, blob->data(), sizeof(desc));
    if (desc.target_scene_key != scene_key) {
      continue;
    }

    if (matched_key.has_value()) {
      throw std::runtime_error(
        std::string("multiple physics sidecars reference scene key=")
        + data::to_string(scene_key) + " first=" + data::to_string(*matched_key)
        + " second=" + data::to_string(*key_opt));
    }
    matched_key = *key_opt;
  }

  return matched_key;
}

} // namespace oxygen::content::internal
