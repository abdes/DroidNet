//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <unordered_set>
#include <utility>

#include <fmt/format.h>

#include <Oxygen/Content/Internal/PatchResolutionPolicy.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Data/PatchManifest.h>
#include <Oxygen/Data/SourceKey.h>

namespace oxygen::content::internal {

namespace {

  constexpr auto kCatalogDigestSize = size_t { 32U };
  constexpr auto kHexCharsPerByte = size_t { 2U };
  constexpr auto kDigestHashPrime = size_t { 131U };

  using CatalogDigest = std::array<uint8_t, kCatalogDigestSize>;

  auto DigestToHex(const CatalogDigest& digest) -> std::string
  {
    std::string out;
    out.reserve(digest.size() * kHexCharsPerByte);
    for (const auto byte : digest) {
      out += fmt::format("{:02x}", static_cast<unsigned int>(byte));
    }
    return out;
  }

  struct DigestHash final {
    auto operator()(const CatalogDigest& value) const noexcept -> size_t
    {
      size_t seed = 0;
      for (const auto byte : value) {
        seed = (seed * kDigestHashPrime) ^ static_cast<size_t>(byte);
      }
      return seed;
    }
  };

} // namespace

auto to_string(const KeyResolutionStatus status) noexcept -> std::string_view
{
  switch (status) {
  case KeyResolutionStatus::kNotFound:
    return "not_found";
  case KeyResolutionStatus::kFound:
    return "found";
  case KeyResolutionStatus::kTombstoned:
    return "tombstoned";
  }
  return "unknown";
}

auto ResolveAssetKeyByPrecedence(const std::span<const uint16_t> source_ids,
  const data::AssetKey& key, const KeyResolutionCallbacks& callbacks)
  -> KeyResolutionResult
{
  if (!callbacks.source_has_asset || !callbacks.source_tombstones_asset) {
    return {};
  }

  for (size_t source_index = source_ids.size(); source_index-- > 0;) {
    const auto source_id = source_ids[source_index];
    if (callbacks.source_tombstones_asset(source_id, key)) {
      return {
        .status = KeyResolutionStatus::kTombstoned,
        .source_id = source_id,
      };
    }
    if (callbacks.source_has_asset(source_id, key)) {
      return {
        .status = KeyResolutionStatus::kFound,
        .source_id = source_id,
      };
    }
  }

  return {};
}

auto ResolveVirtualPathByPrecedence(const std::span<const uint16_t> source_ids,
  const std::string_view virtual_path,
  const VirtualPathResolutionCallbacks& callbacks)
  -> VirtualPathResolutionResult
{
  if (!callbacks.resolve_virtual_path) {
    return {};
  }

  std::optional<data::AssetKey> winner_key {};
  std::optional<uint16_t> winner_source_id {};
  std::vector<VirtualPathCollision> collisions {};

  for (size_t source_index = source_ids.size(); source_index-- > 0;) {
    const auto source_id = source_ids[source_index];
    const auto candidate
      = callbacks.resolve_virtual_path(source_id, virtual_path);
    if (!candidate.has_value()) {
      continue;
    }
    if (!winner_key.has_value()) {
      winner_key = *candidate;
      winner_source_id = source_id;
      continue;
    }
    if (*candidate != *winner_key) {
      collisions.push_back({
        .winner_source_id = *winner_source_id,
        .masked_source_id = source_id,
        .winner_key = *winner_key,
        .masked_key = *candidate,
      });
    }
  }

  if (!winner_key.has_value()) {
    return {};
  }

  auto key_result = ResolveAssetKeyByPrecedence(
    source_ids, *winner_key, callbacks.key_resolution);
  if (key_result.status != KeyResolutionStatus::kFound) {
    return {
      .asset_key = std::nullopt,
      .key_result = key_result,
      .collisions = std::move(collisions),
    };
  }

  return {
    .asset_key = winner_key,
    .key_result = key_result,
    .collisions = std::move(collisions),
  };
}

auto to_string(const PatchCompatibilityCode code) noexcept -> std::string_view
{
  switch (code) {
  case PatchCompatibilityCode::kMissingBaseSourceKey:
    return "missing_base_source_key";
  case PatchCompatibilityCode::kUnexpectedBaseSourceKey:
    return "unexpected_base_source_key";
  case PatchCompatibilityCode::kMissingBaseContentVersion:
    return "missing_base_content_version";
  case PatchCompatibilityCode::kUnexpectedBaseContentVersion:
    return "unexpected_base_content_version";
  case PatchCompatibilityCode::kMissingBaseCatalogDigest:
    return "missing_base_catalog_digest";
  case PatchCompatibilityCode::kUnexpectedBaseCatalogDigest:
    return "unexpected_base_catalog_digest";
  }
  return "unknown";
}

auto ValidatePatchCompatibility(
  const std::span<const data::SourceKey> mounted_base_source_keys,
  const std::span<const data::PakCatalog> mounted_base_catalogs,
  const data::PatchManifest& manifest) -> PatchCompatibilityResult
{
  PatchCompatibilityResult result {};

  const auto& policy = manifest.compatibility_policy_snapshot;
  const auto& envelope = manifest.compatibility_envelope;

  std::unordered_set<data::SourceKey> mounted_source_keys;
  mounted_source_keys.insert(
    mounted_base_source_keys.begin(), mounted_base_source_keys.end());

  std::unordered_set<data::SourceKey> required_source_keys;
  required_source_keys.insert(envelope.required_base_source_keys.begin(),
    envelope.required_base_source_keys.end());

  if (policy.require_base_source_key_match || policy.require_exact_base_set) {
    for (const auto& required_source_key : required_source_keys) {
      if (!mounted_source_keys.contains(required_source_key)) {
        result.diagnostics.push_back({
          .code = PatchCompatibilityCode::kMissingBaseSourceKey,
          .message = fmt::format("Missing required base source key: {}",
            data::to_string(required_source_key)),
        });
      }
    }
  }

  if (policy.require_exact_base_set) {
    for (const auto& mounted_source_key : mounted_source_keys) {
      if (!required_source_keys.contains(mounted_source_key)) {
        result.diagnostics.push_back({
          .code = PatchCompatibilityCode::kUnexpectedBaseSourceKey,
          .message = fmt::format("Unexpected mounted base source key: {}",
            data::to_string(mounted_source_key)),
        });
      }
    }
  }

  std::unordered_set<uint16_t> mounted_content_versions;
  std::unordered_set<CatalogDigest, DigestHash> mounted_catalog_digests;
  for (const auto& catalog : mounted_base_catalogs) {
    mounted_content_versions.insert(catalog.content_version);
    mounted_catalog_digests.insert(catalog.catalog_digest);
  }

  if (policy.require_content_version_match) {
    std::unordered_set<uint16_t> required_content_versions;
    required_content_versions.insert(
      envelope.required_base_content_versions.begin(),
      envelope.required_base_content_versions.end());

    for (const auto content_version : required_content_versions) {
      if (!mounted_content_versions.contains(content_version)) {
        result.diagnostics.push_back({
          .code = PatchCompatibilityCode::kMissingBaseContentVersion,
          .message = fmt::format(
            "Missing required base content version: {}", content_version),
        });
      }
    }

    if (policy.require_exact_base_set) {
      for (const auto content_version : mounted_content_versions) {
        if (!required_content_versions.contains(content_version)) {
          result.diagnostics.push_back({
            .code = PatchCompatibilityCode::kUnexpectedBaseContentVersion,
            .message = fmt::format(
              "Unexpected mounted base content version: {}", content_version),
          });
        }
      }
    }
  }

  if (policy.require_catalog_digest_match) {
    std::unordered_set<CatalogDigest, DigestHash> required_catalog_digests;
    required_catalog_digests.insert(
      envelope.required_base_catalog_digests.begin(),
      envelope.required_base_catalog_digests.end());

    for (const auto& digest : required_catalog_digests) {
      if (!mounted_catalog_digests.contains(digest)) {
        result.diagnostics.push_back({
          .code = PatchCompatibilityCode::kMissingBaseCatalogDigest,
          .message = fmt::format(
            "Missing required base catalog digest: {}", DigestToHex(digest)),
        });
      }
    }

    if (policy.require_exact_base_set) {
      for (const auto& digest : mounted_catalog_digests) {
        if (!required_catalog_digests.contains(digest)) {
          result.diagnostics.push_back({
            .code = PatchCompatibilityCode::kUnexpectedBaseCatalogDigest,
            .message = fmt::format("Unexpected mounted base catalog digest: {}",
              DigestToHex(digest)),
          });
        }
      }
    }
  }

  result.compatible = result.diagnostics.empty();
  return result;
}

} // namespace oxygen::content::internal
