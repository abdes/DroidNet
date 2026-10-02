//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

#include <Oxygen/Cooker/Import/Internal/AssetReferenceBuilder.h>
#include <Oxygen/Core/Meta/Data/ResourceIndex.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>

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
    resource_indices_.try_emplace(ResourceIdentity(binding.kind, binding.index),
      data::ResourceReferenceIndex { static_cast<uint32_t>(i) });
  }
}

AssetReferenceBuilder::AssetReferenceBuilder(
  const data::AssetReferences& references)
  : AssetReferenceBuilder(references.Resources())
{
  for (const auto& reference : references.Keys()) {
    AddKey(reference.kind, reference.key, reference.expected_type);
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
