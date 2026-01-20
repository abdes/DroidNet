//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Naming.h"

#include <Oxygen/Base/Logging.h>

namespace oxygen::content::import {

NamingService::NamingService(Config config)
  : config_(std::move(config))
{
  CHECK_F(config_.strategy != nullptr, "Naming strategy must not be null");
}

auto NamingService::MakeUniqueName(const std::string_view authored_name,
  const NamingContext& context) -> std::string
{
  // Step 1: Apply naming strategy to get base name
  std::string base_name;
  if (config_.strategy) {
    if (const auto renamed = config_.strategy->Rename(authored_name, context);
      renamed.has_value() && !renamed->empty()) {
      base_name = *renamed;
    }
  }

  // Fallback if strategy returned nothing
  if (base_name.empty()) {
    if (!authored_name.empty()) {
      base_name = std::string(authored_name);
    } else {
      // Use default based on kind
      base_name = NormalizeNamingStrategy::DefaultBaseName(context);
      if (context.ordinal > 0) {
        base_name += "_" + std::to_string(context.ordinal);
      }
    }
  }

  // Step 2: Apply scene namespacing if enabled
  if (config_.enable_namespacing) {
    base_name = ApplyNamespacing(std::move(base_name), context);
  }

  // Step 3: Enforce uniqueness if enabled
  if (!config_.enforce_uniqueness) {
    return base_name;
  }

  const auto kind_index = static_cast<size_t>(context.kind);
  auto& registry = registries_[kind_index];

  std::unique_lock lock(registry.mutex);

  // Check for collision
  auto it = registry.usage_counts.find(base_name);
  if (it == registry.usage_counts.end()) {
    // First use of this name
    registry.usage_counts[base_name] = 1;
    return base_name;
  }

  // Name collision - append suffix
  const std::string original_name = base_name;
  uint32_t collision_ordinal = it->second;

  std::string unique_name;
  do {
    unique_name = original_name + "_" + std::to_string(collision_ordinal);
    ++collision_ordinal;
  } while (registry.usage_counts.contains(unique_name));

  // Register both the original (incremented) and the new unique name
  it->second = collision_ordinal;
  registry.usage_counts[unique_name] = 1;

  return unique_name;
}

auto NamingService::HasName(
  const ImportNameKind kind, const std::string_view name) const -> bool
{
  const auto kind_index = static_cast<size_t>(kind);
  const auto& registry = registries_[kind_index];

  std::shared_lock lock(registry.mutex);
  return registry.usage_counts.contains(std::string(name));
}

auto NamingService::GetNameCount(const ImportNameKind kind) const -> size_t
{
  const auto kind_index = static_cast<size_t>(kind);
  const auto& registry = registries_[kind_index];

  std::shared_lock lock(registry.mutex);
  return registry.usage_counts.size();
}

auto NamingService::Reset() -> void
{
  for (auto& registry : registries_) {
    std::unique_lock lock(registry.mutex);
    registry.usage_counts.clear();
  }
}

auto NamingService::ApplyNamespacing(
  std::string name, const NamingContext& context) const -> std::string
{
  // Only namespace assets, not scene nodes
  if (context.kind == ImportNameKind::kSceneNode) {
    return name;
  }

  // Check if we have a scene namespace to apply
  if (context.scene_namespace.empty()) {
    return name;
  }

  // Already namespaced? (contains '/')
  if (name.find('/') != std::string::npos) {
    return name;
  }

  // Apply namespace: "SceneName/AssetName"
  return std::string(context.scene_namespace) + "/" + name;
}

} // namespace oxygen::content::import
