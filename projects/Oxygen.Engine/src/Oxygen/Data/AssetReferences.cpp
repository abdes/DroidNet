//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Core/Meta/Data/ResourceIndex.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormatSerioLoaders.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Serio/Writer.h>

namespace oxygen::data {
namespace {

  auto IsValidKind(const ResourceKind kind) noexcept -> bool
  {
    switch (kind) {
    case ResourceKind::kBuffer:
    case ResourceKind::kTexture:
    case ResourceKind::kScript:
    case ResourceKind::kPhysics:
      return true;
    }
    return false;
  }

  auto IsValidKeyReference(const KeyReference& reference) noexcept -> bool
  {
    if (reference.key.IsNil()) {
      return false;
    }
    const auto valid_asset_type = reference.expected_type > AssetType::kUnknown
      && reference.expected_type <= AssetType::kMaxAssetType;
    switch (reference.kind) {
    case KeyReferenceKind::kAsset:
      return valid_asset_type;
    case KeyReferenceKind::kPhysicsResource:
      return reference.expected_type == AssetType::kUnknown;
    case KeyReferenceKind::kLogical:
      return valid_asset_type || reference.expected_type == AssetType::kUnknown;
    }
    return false;
  }

  auto KeyOrder(const KeyReference& reference) noexcept
  {
    return std::tie(reference.kind, reference.key, reference.expected_type);
  }

} // namespace

auto AssetReferences::Create(std::vector<ResourceBinding> resources,
  std::vector<KeyReference> keys) -> Result<AssetReferences, std::string>
{
  constexpr auto kMaxCount = std::numeric_limits<uint32_t>::max();
  if (resources.size() > kMaxCount || keys.size() > kMaxCount) {
    return Err(std::string("Asset reference count exceeds the wire limit"));
  }
  for (const auto& binding : resources) {
    if (!IsValidKind(binding.kind)) {
      return Err(std::string("Unknown asset resource binding kind"));
    }
    if (binding.index.get() == 0U && binding.kind != ResourceKind::kTexture) {
      return Err(
        std::string("Only texture bindings support entry-zero fallback"));
    }
    if (binding.index == pak::core::kErrorTextureResourceIndex
      && binding.kind != ResourceKind::kTexture) {
      return Err(std::string("Error-texture binding requires texture kind"));
    }
  }
  for (const auto& reference : keys) {
    if (!IsValidKeyReference(reference)) {
      return Err(std::string("Invalid asset key reference or expected type"));
    }
  }
  std::ranges::sort(keys, {}, KeyOrder);
  const auto duplicate = std::ranges::adjacent_find(
    keys, [](const KeyReference& lhs, const KeyReference& rhs) -> bool {
      return lhs.kind == rhs.kind && lhs.key == rhs.key;
    });
  if (duplicate != keys.end()) {
    return Err(std::string("Duplicate or conflicting asset key reference"));
  }
  AssetReferences result;
  result.resources_ = std::move(resources);
  result.keys_ = std::move(keys);
  return Ok(std::move(result));
}

auto AssetReferences::Decode(const std::span<const std::byte> bytes,
  const uint32_t resource_count, const uint32_t key_count)
  -> Result<AssetReferences, std::string>
{
  const auto expected_size = EncodedSize(resource_count, key_count);
  if (expected_size != bytes.size()) {
    return Err(
      std::string("Asset reference block size does not match its counts"));
  }
  serio::ReadOnlyMemoryStream stream(bytes);
  serio::Reader reader(stream);
  const auto packed = reader.ScopedAlignment(1);
  std::vector<ResourceBinding> resources(resource_count);
  for (auto& binding : resources) {
    if (!reader.ReadInto(binding.kind) || !serio::Load(reader, binding.index)) {
      return Err(std::string("Could not decode asset resource binding"));
    }
  }
  std::vector<KeyReference> keys(key_count);
  for (auto& reference : keys) {
    if (!serio::Load(reader, reference.key) || !reader.ReadInto(reference.kind)
      || !reader.ReadInto(reference.expected_type)) {
      return Err(std::string("Could not decode asset key reference"));
    }
  }
  return Create(std::move(resources), std::move(keys));
}

auto AssetReferences::Encode() const
  -> Result<std::vector<std::byte>, std::string>
{
  const auto size = (uint64_t { resources_.size() } * kResourceBindingSize)
    + (uint64_t { keys_.size() } * kKeyReferenceSize);
  if (size > std::numeric_limits<size_t>::max()) {
    return Err(std::string("Asset reference block exceeds addressable size"));
  }
  std::vector<std::byte> bytes(static_cast<size_t>(size));
  serio::MemoryStream stream { std::span(bytes) };
  serio::Writer writer(stream);
  const auto packed = writer.ScopedAlignment(1);
  for (const auto& binding : resources_) {
    if (!writer.Write(binding.kind) || !writer.Write(binding.index.get())) {
      return Err(std::string("Could not encode asset resource binding"));
    }
  }
  for (const auto& reference : keys_) {
    if (!writer.Write(reference.key) || !writer.Write(reference.kind)
      || !writer.Write(reference.expected_type)) {
      return Err(std::string("Could not encode asset key reference"));
    }
  }
  return Ok(std::move(bytes));
}

auto AssetReferences::ResolveResource(const ResourceReferenceIndex reference,
  const ResourceKind expected_kind) const
  -> Result<std::optional<ResourceIndexT>, std::string>
{
  if (!IsValidKind(expected_kind)) {
    return Err(std::string("Unknown expected asset resource kind"));
  }
  if (reference == kNoResourceReference) {
    return Ok(std::optional<ResourceIndexT> {});
  }
  if (reference.get() >= resources_.size()) {
    return Err(
      fmt::format("Local resource reference {} exceeds binding count {}",
        reference.get(), resources_.size()));
  }
  const auto& binding = resources_.at(reference.get());
  if (binding.kind != expected_kind) {
    return Err(fmt::format(
      "Local resource reference {} has the wrong kind", reference.get()));
  }
  return Ok(std::optional<ResourceIndexT> { binding.index });
}

auto AssetReferences::ValidateUsage(
  const std::span<const ResourceReferenceUse> resources,
  const std::span<const KeyReference> keys) const -> Result<void, std::string>
{
  std::vector<bool> used(resources_.size(), false);
  for (const auto& use : resources) {
    const auto resolved = ResolveResource(use.reference, use.kind);
    if (!resolved) {
      return Err(resolved.error());
    }
    if (use.reference != kNoResourceReference) {
      used.at(use.reference.get()) = true;
    }
  }
  if (std::ranges::find(used, false) != used.end()) {
    return Err(
      std::string("Asset resource inventory contains unused bindings"));
  }

  std::vector<KeyReference> observed;
  observed.reserve(keys.size());
  for (const auto& key : keys) {
    if (key.key.IsNil()) {
      continue;
    }
    if (!IsValidKeyReference(key)) {
      return Err(
        std::string("Invalid descriptor key reference or expected type"));
    }
    observed.push_back(key);
  }
  std::ranges::sort(observed, {}, KeyOrder);
  const auto duplicates = std::ranges::unique(observed);
  observed.erase(duplicates.begin(), duplicates.end());
  if (!std::ranges::equal(observed, keys_)) {
    return Err(
      std::string("Asset key inventory does not match its descriptor"));
  }
  return Result<void, std::string>::Ok();
}

auto AssetReferences::ValidateResourceBounds(
  const ResourceTableCounts& counts) const -> Result<void, std::string>
{
  for (const auto& binding : resources_) {
    if (binding.kind == ResourceKind::kTexture
      && (binding.index == pak::core::kFallbackResourceIndex
        || binding.index == pak::core::kErrorTextureResourceIndex)) {
      continue;
    }
    const auto count = [&counts, &binding] -> uint64_t {
      switch (binding.kind) {
      case ResourceKind::kBuffer:
        return counts.buffers;
      case ResourceKind::kTexture:
        return counts.textures;
      case ResourceKind::kScript:
        return counts.scripts;
      case ResourceKind::kPhysics:
        return counts.physics;
      }
      return 0U;
    }();
    if (binding.index.get() >= count) {
      return Err(
        fmt::format("Asset resource binding {} exceeds table entry count {}",
          binding.index.get(), count));
    }
  }
  return Result<void, std::string>::Ok();
}

} // namespace oxygen::data
