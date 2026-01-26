//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cctype>
#include <cstdint>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Import/Naming.h>

namespace import = oxygen::content::import;

namespace {

auto ApplyNamespacing(std::string name, const import::NamingContext& context)
  -> std::string
{
  if (context.kind == import::ImportNameKind::kSceneNode) {
    return name;
  }

  // Check if we have a scene namespace to apply
  if (context.scene_namespace.empty()) {
    return name;
  }

  // Already namespaced? (contains '/')
  if (name.contains('/')) {
    return name;
  }

  // Apply namespace: "SceneName/AssetName"
  return std::string(context.scene_namespace) + "/" + name;
}

} // namespace

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
  auto& [usage_counts, mutex] = registries_.at(kind_index);

  std::unique_lock lock(mutex);

  // Check for collision
  const auto it = usage_counts.find(base_name);
  if (it == usage_counts.end()) {
    // First use of this name
    usage_counts[base_name] = 1;
    return base_name;
  }

  // Name collision - append suffix
  const std::string original_name = base_name;
  uint32_t collision_ordinal = it->second;

  std::string unique_name;
  do {
    unique_name = original_name + "_" + std::to_string(collision_ordinal);
    ++collision_ordinal;
  } while (usage_counts.contains(unique_name));

  // Register both the original (incremented) and the new unique name
  it->second = collision_ordinal;
  usage_counts[unique_name] = 1;

  return unique_name;
}

auto NamingService::HasName(
  const ImportNameKind kind, const std::string_view name) const -> bool
{
  const auto kind_index = static_cast<size_t>(kind);
  const auto& [usage_counts, mutex] = registries_.at(kind_index);

  std::shared_lock lock(mutex);
  return usage_counts.contains(std::string(name));
}

auto NamingService::GetNameCount(const ImportNameKind kind) const -> size_t
{
  const auto kind_index = static_cast<size_t>(kind);
  const auto& [usage_counts, mutex] = registries_.at(kind_index);

  std::shared_lock lock(mutex);
  return usage_counts.size();
}

auto NamingService::Reset() -> void
{
  for (auto& [usage_counts, mutex] : registries_) {
    std::unique_lock lock(mutex);
    usage_counts.clear();
  }
}

auto NormalizeNamingStrategy::Rename(std::string_view authored_name,
  const NamingContext& context) const -> std::optional<std::string>
{
  auto normalized = Normalize(authored_name);

  if (normalized.empty()) {
    normalized = DefaultBaseName(context);
  }

  if (options_.apply_prefixes) {
    const auto prefix = PrefixFor(context.kind);
    if (!prefix.empty() && !normalized.starts_with(prefix)) {
      normalized = std::string(prefix) + normalized;
    }
  }

  if (normalized == authored_name) {
    return std::nullopt;
  }

  return normalized;
}

auto NormalizeNamingStrategy::Normalize(std::string_view input) const
  -> std::string
{
  auto s = std::string(input);

  if (options_.trim_whitespace) {
    const auto is_space
      = [](const unsigned char ch) -> bool { return std::isspace(ch) != 0; };
    while (!s.empty() && is_space(static_cast<unsigned char>(s.front()))) {
      s.erase(s.begin());
    }
    while (!s.empty() && is_space(static_cast<unsigned char>(s.back()))) {
      s.pop_back();
    }
  }

  if (s.empty()) {
    return s;
  }

  std::string out;
  out.reserve(s.size());

  bool last_was_underscore = false;
  bool in_whitespace = false;

  for (const auto ch : s) {
    const auto uch = static_cast<unsigned char>(ch);
    const auto is_space = (std::isspace(uch) != 0);

    if (is_space) {
      if (options_.collapse_whitespace) {
        in_whitespace = true;
        continue;
      }
      out.push_back('_');
      last_was_underscore = true;
      continue;
    }

    if (in_whitespace) {
      out.push_back('_');
      last_was_underscore = true;
      in_whitespace = false;
    }

    const auto is_valid = (std::isalnum(uch) != 0) || (ch == '_');
    if (!is_valid && options_.replace_invalid_chars) {
      if (!last_was_underscore || !options_.collapse_underscores) {
        out.push_back('_');
        last_was_underscore = true;
      }
      continue;
    }

    if (ch == '_' && options_.collapse_underscores && last_was_underscore) {
      continue;
    }

    out.push_back(ch);
    last_was_underscore = (ch == '_');
  }

  if (options_.collapse_underscores) {
    while (!out.empty() && out.front() == '_') {
      out.erase(out.begin());
    }
    while (!out.empty() && out.back() == '_') {
      out.pop_back();
    }
  }

  return out;
}

auto NormalizeNamingStrategy::PrefixFor(ImportNameKind kind) const
  -> std::string_view
{
  switch (kind) {
  case ImportNameKind::kMesh:
    return options_.mesh_prefix;
  case ImportNameKind::kMaterial:
    return options_.material_prefix;
  case ImportNameKind::kScene:
  case ImportNameKind::kSceneNode:
    return {};
  }
  return {};
}

} // namespace oxygen::content::import
