//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Pak/PakManifestWriter.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/SourceKey.h>

namespace {
namespace base = oxygen::base;
namespace data = oxygen::data;
namespace pak = oxygen::content::pak;
using nlohmann::ordered_json;

constexpr auto kDiffBasisIdentifier
  = std::string_view { "descriptor_plus_transitive_resources_v1" };
constexpr auto kHexCharsPerByte = size_t { 2U };
constexpr auto kHighNibbleShift = uint8_t { 4U };
constexpr auto kLowNibbleMask = uint8_t { 0x0FU };

auto AddDiagnostic(std::vector<pak::PakDiagnostic>& diagnostics,
  const pak::PakDiagnosticSeverity severity, const std::string_view code,
  const std::string_view message, const std::filesystem::path& path = {})
  -> void
{
  diagnostics.push_back(pak::PakDiagnostic {
    .severity = severity,
    .phase = pak::PakBuildPhase::kManifest,
    .code = std::string(code),
    .message = std::string(message),
    .asset_key = {},
    .resource_kind = {},
    .table_name = {},
    .path = path,
    .offset = {},
  });
}

auto HasManifestErrors(const std::vector<pak::PakDiagnostic>& diagnostics)
  -> bool
{
  return std::ranges::any_of(
    diagnostics, [](const pak::PakDiagnostic& diagnostic) {
      return diagnostic.severity == pak::PakDiagnosticSeverity::kError;
    });
}

auto ShouldEmitManifest(const pak::PakBuildRequest& request) -> bool
{
  return request.mode == pak::BuildMode::kPatch
    || (request.mode == pak::BuildMode::kFull
      && request.options.emit_manifest_in_full);
}

auto ToHex(const std::span<const uint8_t> bytes) -> std::string
{
  constexpr auto kHexDigits = std::string_view { "0123456789abcdef" };
  auto out = std::string {};
  out.reserve(bytes.size() * kHexCharsPerByte);
  for (const auto byte : bytes) {
    out.push_back(kHexDigits.at((byte >> kHighNibbleShift) & kLowNibbleMask));
    out.push_back(kHexDigits.at(byte & kLowNibbleMask));
  }
  return out;
}

auto SortAndUniqueSourceKeys(std::vector<data::SourceKey>& values) -> void
{
  std::ranges::sort(values);
  values.erase(std::ranges::unique(values).begin(), values.end());
}

auto SortAndUniqueVersions(std::vector<uint16_t>& values) -> void
{
  std::ranges::sort(values);
  values.erase(std::ranges::unique(values).begin(), values.end());
}

auto SortAndUniqueDigests(std::vector<std::array<uint8_t, 32>>& values) -> void
{
  std::ranges::sort(values);
  values.erase(std::ranges::unique(values).begin(), values.end());
}

auto SortAssetKeys(std::vector<data::AssetKey>& values) -> void
{
  std::ranges::sort(values);
}

auto ValidatePatchSetDisjointness(const data::PatchManifest& manifest,
  std::vector<pak::PakDiagnostic>& diagnostics) -> void
{
  auto ValidateNoDuplicates = [&diagnostics](const std::string_view set_name,
                                std::vector<data::AssetKey> values) -> void {
    SortAssetKeys(values);
    const auto duplicate_it = std::ranges::adjacent_find(values);
    if (duplicate_it != values.end()) {
      AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
        "pak.manifest.duplicate_asset_key",
        std::string("Manifest field '") + std::string(set_name)
          + "' contains duplicate asset keys.");
    }
  };

  ValidateNoDuplicates("created", manifest.created);
  ValidateNoDuplicates("replaced", manifest.replaced);
  ValidateNoDuplicates("deleted", manifest.deleted);

  auto created_keys = std::unordered_set<data::AssetKey>(
    manifest.created.begin(), manifest.created.end());
  auto replaced_keys = std::unordered_set<data::AssetKey>(
    manifest.replaced.begin(), manifest.replaced.end());
  auto deleted_keys = std::unordered_set<data::AssetKey>(
    manifest.deleted.begin(), manifest.deleted.end());

  const auto intersection_with = [](const auto& lhs, const auto& rhs) -> bool {
    return std::ranges::any_of(
      lhs, [&rhs](const auto& key) { return rhs.contains(key); });
  };

  if (intersection_with(created_keys, replaced_keys)
    || intersection_with(created_keys, deleted_keys)
    || intersection_with(replaced_keys, deleted_keys)) {
    AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
      "pak.manifest.patch_sets_not_disjoint",
      "Manifest fields 'created', 'replaced', and 'deleted' must be pairwise "
      "disjoint.");
  }
}

auto ValidateFullManifestCreatedSet(const pak::PakPlan& plan,
  const data::PatchManifest& manifest,
  std::vector<pak::PakDiagnostic>& diagnostics) -> void
{
  auto expected = std::vector<data::AssetKey> {};
  expected.reserve(plan.Assets().size());
  for (const auto& asset : plan.Assets()) {
    expected.push_back(asset.asset_key);
  }
  SortAssetKeys(expected);

  auto actual = manifest.created;
  SortAssetKeys(actual);

  if (expected != actual) {
    AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
      "pak.manifest.full_created_set_mismatch",
      "Full-mode manifest field 'created' must contain every emitted asset "
      "key.");
  }
}

auto ValidateManifestByMode(const pak::PakBuildRequest& request,
  const pak::PakPlan& plan, const data::PatchManifest& manifest,
  std::vector<pak::PakDiagnostic>& diagnostics) -> void
{
  ValidatePatchSetDisjointness(manifest, diagnostics);

  if (manifest.diff_basis_identifier != kDiffBasisIdentifier) {
    AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
      "pak.manifest.diff_basis_identifier_invalid",
      "Manifest field 'diff_basis_identifier' must equal "
      "'descriptor_plus_transitive_resources_v1'.");
  }

  if (manifest.patch_source_key.IsNil()) {
    AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
      "pak.manifest.patch_source_key_zero",
      "Manifest field 'patch_source_key' must be non-zero.");
  }

  if (request.mode == pak::BuildMode::kPatch) {
    const auto& envelope = manifest.compatibility_envelope;
    if (envelope.required_base_source_keys.empty()
      || envelope.required_base_content_versions.empty()
      || envelope.required_base_catalog_digests.empty()) {
      AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
        "pak.manifest.compatibility_envelope_incomplete",
        "Patch manifest compatibility_envelope must include non-empty "
        "'required_base_source_keys', 'required_base_content_versions', and "
        "'required_base_catalog_digests'.");
    }
  } else {
    if (!manifest.replaced.empty() || !manifest.deleted.empty()) {
      AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
        "pak.manifest.full_sets_invalid",
        "Full-mode manifest must keep fields 'replaced' and 'deleted' empty.");
    }

    const auto& envelope = manifest.compatibility_envelope;
    if (!envelope.required_base_source_keys.empty()
      || !envelope.required_base_content_versions.empty()
      || !envelope.required_base_catalog_digests.empty()) {
      AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
        "pak.manifest.full_base_requirements_not_empty",
        "Full-mode manifest compatibility_envelope base requirements must be "
        "empty.");
    }

    ValidateFullManifestCreatedSet(plan, manifest, diagnostics);
  }
}

auto ValidateManifestRequiredFields(const data::PatchManifest& manifest,
  std::vector<pak::PakDiagnostic>& diagnostics) -> void
{
  if (!manifest.patch_pak_digest.has_value()) {
    AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
      "pak.manifest.patch_pak_digest_missing",
      "Manifest field 'patch_pak_digest' must be populated.");
  }

  if (!manifest.patch_pak_crc32.has_value()) {
    AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
      "pak.manifest.patch_pak_crc32_missing",
      "Manifest field 'patch_pak_crc32' must be populated.");
  }
}

auto BuildManifestModel(const pak::PakBuildRequest& request,
  const pak::PakPlan& plan, const uint32_t pak_crc32) -> data::PatchManifest
{
  auto manifest = data::PatchManifest {};

  if (request.mode == pak::BuildMode::kPatch) {
    for (const auto& action : plan.PatchActions()) {
      switch (action.action) {
      case pak::PakPatchAction::kCreate:
        manifest.created.push_back(action.asset_key);
        break;
      case pak::PakPatchAction::kReplace:
        manifest.replaced.push_back(action.asset_key);
        break;
      case pak::PakPatchAction::kDelete:
        manifest.deleted.push_back(action.asset_key);
        break;
      case pak::PakPatchAction::kUnchanged:
        break;
      }
    }
  } else {
    manifest.created.reserve(plan.Assets().size());
    for (const auto& asset : plan.Assets()) {
      manifest.created.push_back(asset.asset_key);
    }
  }

  SortAssetKeys(manifest.created);
  SortAssetKeys(manifest.replaced);
  SortAssetKeys(manifest.deleted);

  manifest.compatibility_envelope.patch_content_version
    = request.content_version;

  if (request.mode == pak::BuildMode::kPatch) {
    for (const auto& base_catalog : request.base_catalogs) {
      manifest.compatibility_envelope.required_base_source_keys.push_back(
        base_catalog.source_key);
      manifest.compatibility_envelope.required_base_content_versions.push_back(
        base_catalog.content_version);
      manifest.compatibility_envelope.required_base_catalog_digests.push_back(
        base_catalog.catalog_digest);
    }

    SortAndUniqueSourceKeys(
      manifest.compatibility_envelope.required_base_source_keys);
    SortAndUniqueVersions(
      manifest.compatibility_envelope.required_base_content_versions);
    SortAndUniqueDigests(
      manifest.compatibility_envelope.required_base_catalog_digests);
  }

  manifest.compatibility_policy_snapshot
    = data::PatchCompatibilityPolicySnapshot {
        .require_exact_base_set = request.patch_compat.require_exact_base_set,
        .require_content_version_match
        = request.patch_compat.require_content_version_match,
        .require_base_source_key_match
        = request.patch_compat.require_base_source_key_match,
        .require_catalog_digest_match
        = request.patch_compat.require_catalog_digest_match,
      };
  manifest.diff_basis_identifier = std::string(kDiffBasisIdentifier);
  manifest.patch_source_key = request.source_key;
  manifest.patch_pak_crc32 = pak_crc32;

  return manifest;
}

auto ComputePatchPakDigest(const std::filesystem::path& pak_path,
  std::vector<pak::PakDiagnostic>& diagnostics)
  -> std::optional<std::array<uint8_t, 32>>
{
  try {
    return base::ComputeFileSha256(pak_path);
  } catch (const std::exception& ex) {
    AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
      "pak.manifest.patch_pak_digest_compute_failed",
      std::string("Failed to compute manifest field 'patch_pak_digest': ")
        + ex.what(),
      pak_path);
    return std::nullopt;
  }
}

auto SerializeManifestToJson(const data::PatchManifest& manifest) -> std::string
{
  auto created = ordered_json::array();
  for (const auto& key : manifest.created) {
    created.push_back(data::to_string(key));
  }

  auto replaced = ordered_json::array();
  for (const auto& key : manifest.replaced) {
    replaced.push_back(data::to_string(key));
  }

  auto deleted = ordered_json::array();
  for (const auto& key : manifest.deleted) {
    deleted.push_back(data::to_string(key));
  }

  auto required_base_source_keys = ordered_json::array();
  for (const auto& source_key :
    manifest.compatibility_envelope.required_base_source_keys) {
    required_base_source_keys.push_back(data::to_string(source_key));
  }

  auto required_base_catalog_digests = ordered_json::array();
  for (const auto& digest :
    manifest.compatibility_envelope.required_base_catalog_digests) {
    required_base_catalog_digests.push_back(ToHex(std::span { digest }));
  }

  auto required_base_content_versions = ordered_json::array();
  for (const auto version :
    manifest.compatibility_envelope.required_base_content_versions) {
    required_base_content_versions.push_back(version);
  }

  auto document = ordered_json {
    { "created", std::move(created) },
    { "replaced", std::move(replaced) },
    { "deleted", std::move(deleted) },
    { "compatibility_envelope",
      ordered_json {
        { "required_base_source_keys",
          std::move(required_base_source_keys) },
        { "required_base_content_versions",
          std::move(required_base_content_versions) },
        { "required_base_catalog_digests",
          std::move(required_base_catalog_digests) },
        { "patch_content_version",
          manifest.compatibility_envelope.patch_content_version },
      } },
    { "compatibility_policy_snapshot",
      ordered_json {
        { "require_exact_base_set",
          manifest.compatibility_policy_snapshot.require_exact_base_set },
        { "require_content_version_match",
          manifest.compatibility_policy_snapshot.require_content_version_match },
        { "require_base_source_key_match",
          manifest.compatibility_policy_snapshot.require_base_source_key_match },
        { "require_catalog_digest_match",
          manifest.compatibility_policy_snapshot.require_catalog_digest_match },
      } },
    { "diff_basis_identifier", manifest.diff_basis_identifier },
    { "patch_source_key", data::to_string(manifest.patch_source_key) },
    { "patch_pak_digest",
      manifest.patch_pak_digest.has_value()
        ? ordered_json(ToHex(std::span { *manifest.patch_pak_digest }))
        : ordered_json(nullptr) },
    { "patch_pak_crc32",
      manifest.patch_pak_crc32.has_value()
        ? ordered_json(*manifest.patch_pak_crc32)
        : ordered_json(nullptr) },
  };

  return document.dump(2);
}

auto WriteManifestFile(const std::filesystem::path& output_path,
  const std::string_view payload, std::vector<pak::PakDiagnostic>& diagnostics)
  -> void
{
  const auto parent = output_path.parent_path();
  if (!parent.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    if (ec) {
      AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
        "pak.manifest.create_parent_directory_failed",
        std::string("Failed to create manifest parent directory: ")
          + ec.message(),
        parent);
      return;
    }
  }

  auto stream = std::ofstream(output_path, std::ios::binary | std::ios::trunc);
  if (!stream.is_open()) {
    AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
      "pak.manifest.write_open_failed",
      "Failed to open output_manifest_path for writing.", output_path);
    return;
  }

  stream.write(payload.data(), static_cast<std::streamsize>(payload.size()));
  if (!stream.good()) {
    AddDiagnostic(diagnostics, pak::PakDiagnosticSeverity::kError,
      "pak.manifest.write_failed", "Failed to write manifest payload.",
      output_path);
  }
}

} // namespace

namespace oxygen::content::pak {

auto PakManifestWriter::Write(const PakBuildRequest& request,
  const PakPlan& plan, const uint32_t pak_crc32) const -> WriteResult
{
  auto output = WriteResult {};
  if (!ShouldEmitManifest(request)) {
    return output;
  }

  const auto start = std::chrono::steady_clock::now();

  if (request.output_manifest_path.empty()) {
    AddDiagnostic(output.diagnostics, PakDiagnosticSeverity::kError,
      "pak.manifest.output_manifest_path_empty",
      "Manifest emission requires a non-empty output_manifest_path.");
    output.manifest_duration
      = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start);
    return output;
  }

  auto manifest = BuildManifestModel(request, plan, pak_crc32);

  ValidateManifestByMode(request, plan, manifest, output.diagnostics);
  if (!HasManifestErrors(output.diagnostics)) {
    manifest.patch_pak_digest
      = ComputePatchPakDigest(request.output_pak_path, output.diagnostics);
    ValidateManifestRequiredFields(manifest, output.diagnostics);
  }

  if (!HasManifestErrors(output.diagnostics)) {
    const auto payload = SerializeManifestToJson(manifest);
    WriteManifestFile(
      request.output_manifest_path, payload, output.diagnostics);
  }

  if (!HasManifestErrors(output.diagnostics)) {
    output.manifest = std::move(manifest);
  }

  output.manifest_duration
    = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - start);

  return output;
}

} // namespace oxygen::content::pak
