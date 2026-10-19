//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <span>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

#include <Oxygen/Cooker/Import/Internal/AssetReferenceBuilder.h>
#include <Oxygen/Core/Meta/Data/ResourceIndex.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat_core.h>

namespace oxygen::content::import {
namespace {
  auto ResourceIdentity(const data::ResourceKind kind,
    const ResourceIndexT index) noexcept -> uint64_t
  {
    constexpr uint32_t kIndexBits = std::numeric_limits<uint32_t>::digits;
    return (static_cast<uint64_t>(kind) << kIndexBits) | index.get();
  }
}

AssetReferenceBuilder::AssetReferenceBuilder(
  const std::span<const data::ResourceBinding> resources)
  : resources_(resources.begin(), resources.end())
{
  if (resources_.size() > data::kNoResourceReference.get()) {
    throw std::length_error("Descriptor resource reference table is too large");
  }
  for (size_t i = 0; i < resources_.size(); ++i) {
    const auto& binding = resources_.at(i);
    if (binding.kind == data::ResourceKind::kExternalTexture) {
      throw std::invalid_argument(
        "An external texture binding needs the key reference it names");
    }
    resource_indices_.try_emplace(ResourceIdentity(binding.kind, binding.index),
      data::ResourceReferenceIndex { static_cast<uint32_t>(i) });
  }
}

AssetReferenceBuilder::AssetReferenceBuilder(
  const data::AssetReferences& references,
  const std::function<bool(const data::KeyReference&)>& keep)
  : resources_(references.Resources().begin(), references.Resources().end())
{
  for (size_t i = 0; i < resources_.size(); ++i) {
    const auto& binding = resources_.at(i);
    const auto reference
      = data::ResourceReferenceIndex { static_cast<uint32_t>(i) };
    if (binding.kind == data::ResourceKind::kExternalTexture) {
      external_textures_.try_emplace(
        references.Keys()[binding.index.get()].key, reference);
    } else {
      resource_indices_.try_emplace(
        ResourceIdentity(binding.kind, binding.index), reference);
    }
  }
  for (const auto& reference : references.Keys()) {
    if (reference.kind == data::KeyReferenceKind::kTexture || !keep
      || keep(reference)) {
      AddKey(reference.kind, reference.key, reference.expected_type);
    }
  }
}

auto AssetReferenceBuilder::AddResource(const data::ResourceKind kind,
  const ResourceIndexT index) -> data::ResourceReferenceIndex
{
  const auto identity = ResourceIdentity(kind, index);
  if (const auto found = resource_indices_.find(identity);
    found != resource_indices_.end()) {
    return found->second;
  }
  if (resources_.size() >= data::kNoResourceReference.get()) {
    throw std::length_error("Descriptor resource reference table is full");
  }
  const data::ResourceReferenceIndex reference { static_cast<uint32_t>(
    resources_.size()) };
  resources_.push_back({ .kind = kind, .index = index });
  try {
    resource_indices_.emplace(identity, reference);
  } catch (...) {
    resources_.pop_back();
    throw;
  }
  return reference;
}

auto AssetReferenceBuilder::AddKey(const data::KeyReferenceKind kind,
  const data::AssetKey& key, const data::AssetType type) -> void
{
  if (key.IsNil()) {
    return;
  }
  const auto [entry, inserted]
    = keys_.try_emplace(std::pair { kind, key }, type);
  if (!inserted && entry->second != type) {
    throw std::invalid_argument(
      "Descriptor key has conflicting expected types");
  }
}

auto AssetReferenceBuilder::AddAsset(
  const data::AssetKey& key, const data::AssetType type) -> void
{
  AddKey(data::KeyReferenceKind::kAsset, key, type);
}

auto AssetReferenceBuilder::AddPhysicsResource(const data::AssetKey& key)
  -> void
{
  AddKey(
    data::KeyReferenceKind::kPhysicsResource, key, data::AssetType::kUnknown);
}

auto AssetReferenceBuilder::AddExternalTexture(
  const data::AssetKey& texture_key) -> data::ResourceReferenceIndex
{
  if (texture_key.IsNil()) {
    throw std::invalid_argument("External texture key must not be nil");
  }
  if (const auto found = external_textures_.find(texture_key);
    found != external_textures_.end()) {
    return found->second;
  }
  if (resources_.size() >= data::kNoResourceReference.get()) {
    throw std::length_error("Descriptor resource reference table is full");
  }
  const data::ResourceReferenceIndex reference { static_cast<uint32_t>(
    resources_.size()) };
  AddKey(
    data::KeyReferenceKind::kTexture, texture_key, data::AssetType::kUnknown);
  resources_.push_back(
    { .kind = data::ResourceKind::kExternalTexture, .index = {} });
  external_textures_.emplace(texture_key, reference);
  return reference;
}

auto AssetReferenceBuilder::AddTexture(const data::TextureTarget& target)
  -> data::ResourceReferenceIndex
{
  if (const auto* key = std::get_if<data::AssetKey>(&target)) {
    return AddExternalTexture(*key);
  }
  const auto* index = std::get_if<ResourceIndexT>(&target);
  return index == nullptr || *index == data::pak::core::kNoResourceIndex
    ? data::kNoResourceReference
    : AddResource(data::ResourceKind::kTexture, *index);
}

auto AssetReferenceBuilder::AddLogical(
  const data::AssetKey& key, const data::AssetType type) -> void
{
  AddKey(data::KeyReferenceKind::kLogical, key, type);
}

auto AssetReferenceBuilder::Build() && -> data::AssetReferences
{
  std::vector<data::KeyReference> references;
  references.reserve(keys_.size());
  for (const auto& [target, type] : keys_) {
    if (target.first == data::KeyReferenceKind::kTexture) {
      const auto binding = external_textures_.at(target.second);
      resources_.at(binding.get()).index
        = ResourceIndexT { static_cast<uint32_t>(references.size()) };
    }
    references.push_back(
      { .key = target.second, .kind = target.first, .expected_type = type });
  }
  auto result = data::AssetReferences::Create(
    std::move(resources_), std::move(references));
  if (!result) {
    throw std::invalid_argument(result.error());
  }
  return std::move(*result);
}

} // namespace oxygen::content::import
