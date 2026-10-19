//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>

#include <fmt/format.h>

#include <Oxygen/Content/VirtualPath.h>
#include <Oxygen/Cooker/Import/Naming.h>
#include <Oxygen/Core/Types/DescriptorExtensions.h>

namespace import = oxygen::content::import;

namespace {

auto FallbackName(const import::NamingContext& context) -> std::string
{
  auto kind = std::string_view {};
  switch (context.kind) {
  case import::ImportNameKind::kSceneNode:
    kind = "Node";
    break;
  case import::ImportNameKind::kMesh:
    kind = "Mesh";
    break;
  case import::ImportNameKind::kMaterial:
    kind = "Material";
    break;
  case import::ImportNameKind::kTexture:
    kind = "texture";
    break;
  }
  return fmt::format("{}_{}", kind, context.ordinal);
}

//! The leaf extension of an asset kind; scene nodes are not leaves.
auto LeafExtension(const import::ImportNameKind kind) -> std::string_view
{
  switch (kind) {
  case import::ImportNameKind::kSceneNode:
    return {};
  case import::ImportNameKind::kMesh:
    return oxygen::data::kGeometryDescriptorExtension;
  case import::ImportNameKind::kMaterial:
    return oxygen::data::kMaterialDescriptorExtension;
  case import::ImportNameKind::kTexture:
    return oxygen::data::kTextureDescriptorExtension;
  }
  return {};
}

//! Bytes a texture name leaves for the longest usage it can take.
constexpr std::size_t kTextureUsageReserve
  = std::string_view(".metallic_roughness").size();

//! Cuts UTF-8 text to at most `max_bytes`, at a code point boundary.
auto ShortenUtf8(std::string text, const std::size_t max_bytes) -> std::string
{
  if (text.size() <= max_bytes) {
    return text;
  }
  auto end = max_bytes;
  constexpr auto kContinuationMask = 0xC0U;
  constexpr auto kContinuation = 0x80U;
  while (end > 0
    && (static_cast<unsigned char>(text[end]) & kContinuationMask)
      == kContinuation) {
    --end;
  }
  text.resize(end);
  return text;
}

//! The base name of an asset: authored or fallback, after replacement.
auto AssetBaseName(const std::string_view authored_name,
  const import::NamingContext& context) -> std::string
{
  auto name = oxygen::content::ToSegmentName(authored_name);
  return name.empty() ? FallbackName(context) : name;
}

//! The asset name with the given suffix, shortened so that its leaf fits.
auto AssetCandidate(const std::string_view base,
  const import::NamingContext& context, const std::string_view suffix)
  -> std::string
{
  const auto usage = context.kind == import::ImportNameKind::kTexture
    ? kTextureUsageReserve
    : std::size_t { 0 };
  const auto fixed = context.variant.size() + suffix.size()
    + LeafExtension(context.kind).size() + usage;
  const auto budget = oxygen::content::kMaxVirtualPathSegmentBytes > fixed
    ? oxygen::content::kMaxVirtualPathSegmentBytes - fixed
    : std::size_t { 1 };
  auto name = std::string(base);
  if (name.size() > budget) {
    // Shortening can expose a trailing space or dot; replace it again.
    name = oxygen::content::ToSegmentName(ShortenUtf8(std::move(name), budget));
  }
  name.append(context.variant);
  name.append(suffix);
  return name;
}

auto RegistryKey(const import::ImportNameKind kind, const std::string_view name)
  -> std::string
{
  if (kind == import::ImportNameKind::kSceneNode) {
    return std::string(name);
  }
  return oxygen::content::FoldCase(name).value_or(std::string(name));
}

} // namespace

namespace oxygen::content::import {

auto NamingService::MakeUniqueName(const std::string_view authored_name,
  const NamingContext& context) -> std::string
{
  const auto is_node = context.kind == ImportNameKind::kSceneNode;
  const auto base = is_node
    ? (authored_name.empty() ? FallbackName(context)
                             : std::string(authored_name))
    : AssetBaseName(authored_name, context);
  const auto candidate = [&](const std::string_view suffix) -> std::string {
    if (is_node) {
      return base + std::string(context.variant) + std::string(suffix);
    }
    return AssetCandidate(base, context, suffix);
  };

  auto& [next_suffix, mutex]
    = registries_.at(static_cast<size_t>(context.kind));
  std::unique_lock lock(mutex);

  auto name = candidate({});
  const auto base_key = RegistryKey(context.kind, name);
  const auto it = next_suffix.find(base_key);
  if (it == next_suffix.end()) {
    next_suffix.emplace(base_key, 1U);
    return name;
  }

  auto ordinal = it->second;
  auto key = std::string {};
  do {
    name = candidate(fmt::format("_{}", ordinal));
    key = RegistryKey(context.kind, name);
    ++ordinal;
  } while (next_suffix.contains(key));
  it->second = ordinal;
  next_suffix.emplace(std::move(key), 1U);
  return name;
}

auto NamingService::HasName(
  const ImportNameKind kind, const std::string_view name) const -> bool
{
  const auto& [next_suffix, mutex] = registries_.at(static_cast<size_t>(kind));
  std::shared_lock lock(mutex);
  return next_suffix.contains(RegistryKey(kind, name));
}

auto NamingService::GetNameCount(const ImportNameKind kind) const -> size_t
{
  const auto& [next_suffix, mutex] = registries_.at(static_cast<size_t>(kind));
  std::shared_lock lock(mutex);
  return next_suffix.size();
}

auto NamingService::Reset() -> void
{
  for (auto& [next_suffix, mutex] : registries_) {
    std::unique_lock lock(mutex);
    next_suffix.clear();
  }
}

} // namespace oxygen::content::import
