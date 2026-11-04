//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <Oxygen/Content/VirtualPath.h>
#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
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

//! How a diagnostic names an asset of the kind.
auto KindLabel(const import::ImportNameKind kind) -> std::string_view
{
  switch (kind) {
  case import::ImportNameKind::kMesh:
    return "mesh";
  case import::ImportNameKind::kMaterial:
    return "material";
  case import::ImportNameKind::kTexture:
    return "texture";
  case import::ImportNameKind::kSceneNode:
    return "node";
  }
  return "object";
}

} // namespace

namespace oxygen::content::import {

auto NamingService::MakeUniqueName(const std::string_view authored_name,
  const NamingContext& context) -> std::string
{
  const auto is_node = context.kind == ImportNameKind::kSceneNode;
  const auto base = [&] -> std::string {
    if (!is_node) {
      return AssetBaseName(authored_name, context);
    }
    return authored_name.empty() ? FallbackName(context)
                                 : std::string(authored_name);
  }();
  const auto candidate = [&](const std::string_view suffix) -> std::string {
    if (is_node) {
      return base + std::string(context.variant) + std::string(suffix);
    }
    return AssetCandidate(base, context, suffix);
  };

  auto& registry = registries_.at(static_cast<size_t>(context.kind));
  std::unique_lock lock(registry.mutex);

  auto name = candidate({});
  const auto base_key = RegistryKey(context.kind, name);
  const auto it = registry.next_suffix.find(base_key);
  if (it == registry.next_suffix.end()) {
    registry.next_suffix.emplace(base_key, 1U);
    return name;
  }

  auto taken = name;
  auto ordinal = it->second;
  name = candidate(fmt::format("_{}", ordinal));
  auto key = RegistryKey(context.kind, name);
  while (registry.next_suffix.contains(key)) {
    ++ordinal;
    name = candidate(fmt::format("_{}", ordinal));
    key = RegistryKey(context.kind, name);
  }
  it->second = ordinal + 1;
  registry.next_suffix.emplace(std::move(key), 1U);
  registry.collisions.push_back(
    { .taken = std::move(taken), .assigned = name });
  return name;
}

auto NamingService::HasName(
  const ImportNameKind kind, const std::string_view name) const -> bool
{
  const auto& registry = registries_.at(static_cast<size_t>(kind));
  std::shared_lock lock(registry.mutex);
  return registry.next_suffix.contains(RegistryKey(kind, name));
}

auto NamingService::GetNameCount(const ImportNameKind kind) const -> size_t
{
  const auto& registry = registries_.at(static_cast<size_t>(kind));
  std::shared_lock lock(registry.mutex);
  return registry.next_suffix.size();
}

auto NamingService::CollisionWarnings(const std::string_view source_path) const
  -> std::vector<ImportDiagnostic>
{
  auto warnings = std::vector<ImportDiagnostic> {};
  for (const auto kind : {
         ImportNameKind::kMesh,
         ImportNameKind::kMaterial,
         ImportNameKind::kTexture,
       }) {
    const auto& registry = registries_.at(static_cast<size_t>(kind));
    std::shared_lock lock(registry.mutex);
    for (const auto& [taken, assigned] : registry.collisions) {
      warnings.push_back({
        .severity = ImportSeverity::kWarning,
        .code = std::string(import_codes::kNameCollision),
        .message = fmt::format(
          "another {} of the source is named '{}', so this one is named "
          "'{}'; the two swap names, and the references to them, if the "
          "source reorders them: rename one in the source",
          KindLabel(kind), taken, assigned),
        .source_path = std::string(source_path),
        .object_path = {},
        .object_name = assigned,
      });
    }
  }
  return warnings;
}

auto NamingService::Reset() -> void
{
  for (auto& registry : registries_) {
    std::unique_lock lock(registry.mutex);
    registry.next_suffix.clear();
    registry.collisions.clear();
  }
}

} // namespace oxygen::content::import
