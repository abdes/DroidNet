//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Base/Span.h>
#include <Oxygen/Content/Test/Fixtures/LooseCookedTestWriter.h>
#include <Oxygen/Cooker/Pak/PakBuildReport.h>
#include <Oxygen/Cooker/Pak/PakBuildRequest.h>
#include <Oxygen/Cooker/Test/Support/Diagnostics.h>
#include <Oxygen/Cooker/Test/Support/FileIo.h>
#include <Oxygen/Cooker/Test/Support/TempDir.h>
#include <Oxygen/Cooker/Test/Support/TestValues.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/CookedSource.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_physics.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Writer.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::pak::test {

namespace data = oxygen::data;
namespace lc = oxygen::data::loose_cooked;

struct AssetSpec final {
  data::AssetKey key {};
  data::AssetType asset_type = data::AssetType::kUnknown;
  std::string descriptor_relpath;
  std::string virtual_path;
  uint64_t descriptor_size = 0U;
  std::array<uint8_t, lc::kSha256Size> descriptor_sha {};
  std::vector<std::byte> descriptor_payload {};
  data::AssetReferences references;
};

struct FileSpec final {
  lc::FileKind kind = lc::FileKind::kUnknown;
  std::string relpath;
  std::vector<std::byte> payload;
};

//! Per-test unique temp directory; thin naming adapter over `TempDirTest`.
class TempDirFixture : public oxygen::cooker::test::TempDirTest {
protected:
  [[nodiscard]] auto Root() const -> const std::filesystem::path&
  {
    return TempDir();
  }

  [[nodiscard]] auto Path(std::string_view leaf) const -> std::filesystem::path
  {
    return TempDir() / std::filesystem::path(leaf);
  }
};

//! Shared helpers, re-exported so Pak tests keep spelling them `paktest::X`.
using oxygen::cooker::test::HasDiagnosticCode;
using oxygen::cooker::test::MakeAssetKey;

[[nodiscard]] inline auto MakeSourceKey(const uint8_t seed) -> data::SourceKey
{
  auto bytes = std::array<uint8_t, data::SourceKey::kSizeBytes> {};
  for (auto i = size_t { 0U }; i < bytes.size(); ++i) {
    bytes.at(i) = static_cast<uint8_t>(seed + static_cast<uint8_t>(i));
  }
  bytes.at(6) = static_cast<uint8_t>((bytes.at(6) & 0x0FU) | 0x70U);
  bytes.at(8) = static_cast<uint8_t>((bytes.at(8) & 0x3FU) | 0x80U);
  return data::SourceKey::FromBytes(bytes).value();
}

[[nodiscard]] inline auto MakeDigest(const uint8_t seed)
  -> std::array<uint8_t, lc::kSha256Size>
{
  auto digest = std::array<uint8_t, lc::kSha256Size> {};
  digest.fill(seed);
  return digest;
}

[[nodiscard]] inline auto HasError(std::span<const PakDiagnostic> diagnostics)
  -> bool
{
  return std::ranges::any_of(diagnostics, [](const PakDiagnostic& diagnostic) {
    return diagnostic.severity == PakDiagnosticSeverity::kError;
  });
}

//! A base catalog with the given entries and a valid digest.
[[nodiscard]] inline auto MakeBaseCatalog(
  std::span<const data::PakCatalogEntry> entries) -> data::PakCatalog
{
  constexpr auto kBaseCatalogSourceSeed = uint8_t { 0xC1U };
  constexpr auto kBaseCatalogContentVersion = uint16_t { 9U };

  auto catalog = data::PakCatalog {
    .source_key = MakeSourceKey(kBaseCatalogSourceSeed),
    .content_version = kBaseCatalogContentVersion,
    .catalog_digest = {},
    .entries
    = std::vector<data::PakCatalogEntry>(entries.begin(), entries.end()),
  };
  catalog.catalog_digest = catalog.ComputeDigest().value();
  return catalog;
}

//! Stores the SHA-256 of `bytes` (with the hash field zeroed) in its header.
inline auto EnableDescriptorHash(std::vector<std::byte>& bytes) -> void
{
  const auto digest = oxygen::base::ComputeSha256(bytes);
  auto field = std::span(bytes).subspan(
    offsetof(data::pak::core::AssetHeader, content_hash), digest.size());
  std::memcpy(field.data(), digest.data(), digest.size());
}

//! The physics sidecar paired with `scene`, with a valid descriptor hash.
[[nodiscard]] inline auto MakePhysicsSidecarSpec(
  const uint8_t seed, const AssetSpec& scene) -> AssetSpec
{
  auto descriptor = data::pak::physics::PhysicsSceneAssetDesc {};
  descriptor.header.asset_type
    = static_cast<uint8_t>(data::AssetType::kPhysicsScene);
  descriptor.header.version = data::pak::physics::kPhysicsSceneAssetVersion;
  descriptor.target_scene_key = scene.key;
  const auto digest = oxygen::base::ComputeSha256(scene.descriptor_payload);
  std::ranges::copy(digest, std::begin(descriptor.target_scene_content_hash));
  const auto view = std::as_bytes(std::span { &descriptor, 1U });
  auto bytes = std::vector<std::byte>(view.begin(), view.end());
  EnableDescriptorHash(bytes);
  return AssetSpec {
    .key = MakeAssetKey(seed),
    .asset_type = data::AssetType::kPhysicsScene,
    .descriptor_relpath = "Physics.opscene",
    .virtual_path = "/Game/Physics" + std::to_string(seed) + ".opscene",
    .descriptor_size = bytes.size(),
    .descriptor_sha = oxygen::base::ComputeSha256(bytes),
    .descriptor_payload = std::move(bytes),
  };
}

//! The source identity `WriteLooseIndex` records for a given seed.
[[nodiscard]] inline auto LooseSourceKey(const uint8_t seed) -> data::SourceKey
{
  auto bytes = std::array<uint8_t, data::SourceKey::kSizeBytes> {};
  for (auto i = size_t { 0U }; i < bytes.size(); ++i) {
    bytes.at(i) = static_cast<uint8_t>(seed + static_cast<uint8_t>(i + 1U));
  }
  bytes.at(6) = static_cast<uint8_t>((bytes.at(6) & 0x0FU) | 0x70U);
  bytes.at(8) = static_cast<uint8_t>((bytes.at(8) & 0x3FU) | 0x80U);
  return data::SourceKey::FromBytes(bytes).value();
}

//! Writes a loose cooked root through the shared forging writer.
/*!
 An asset's recorded `descriptor_size` and `descriptor_sha` may deliberately
 disagree with its payload; an all-zero digest records the computed one.
*/
[[nodiscard]] inline auto WriteLooseIndex(const std::filesystem::path& root,
  std::span<const AssetSpec> assets, std::span<const FileSpec> files,
  const uint8_t guid_seed) -> bool
{
  auto writer = content::testing::LooseCookedTestWriter(root);
  writer.SetSourceKey(LooseSourceKey(guid_seed));
  for (const auto& file : files) {
    writer.WriteFile(file.kind, file.relpath, file.payload);
  }
  for (const auto& asset : assets) {
    writer.WriteAssetDescriptor(asset.key, asset.asset_type, asset.virtual_path,
      asset.descriptor_relpath, asset.descriptor_payload, asset.references,
      content::testing::DescriptorRecordOverride {
        .size = asset.descriptor_size, .sha256 = asset.descriptor_sha });
  }
  static_cast<void>(writer.Finish());
  return true;
}

//! Options for `MakeFullRequest` / `MakePatchRequest`.
struct FullRequestOptions final {
  std::vector<data::CookedSource> sources {};
  uint16_t content_version = 1U;
  data::SourceKey source_key = MakeSourceKey(0x7DU);
  bool embed_browse_index = false;
  bool compute_crc32 = true;
};

//! A deterministic full-build request without manifest or base catalogs.
[[nodiscard]] inline auto MakeFullRequest(
  const std::filesystem::path& output_pak_path, FullRequestOptions options = {})
  -> PakBuildRequest
{
  return PakBuildRequest {
    .mode = BuildMode::kFull,
    .sources = std::move(options.sources),
    .output_pak_path = output_pak_path,
    .output_manifest_path = {},
    .content_version = options.content_version,
    .source_key = options.source_key,
    .base_catalogs = {},

    .options = {
      .deterministic = true,
      .embed_browse_index = options.embed_browse_index,
      .emit_manifest_in_full = false,
      .compute_crc32 = options.compute_crc32,
      .fail_on_warnings = false,
    },
  };
}

//! A deterministic patch request; the manifest sits next to the pak.
[[nodiscard]] inline auto MakePatchRequest(
  const std::filesystem::path& output_pak_path,
  std::vector<data::PakCatalog> base_catalogs, FullRequestOptions options = {})
  -> PakBuildRequest
{
  auto request = MakeFullRequest(output_pak_path, std::move(options));
  request.mode = BuildMode::kPatch;
  request.output_manifest_path = output_pak_path;
  request.output_manifest_path.replace_extension(".manifest");
  request.base_catalogs = std::move(base_catalogs);
  return request;
}

} // namespace oxygen::content::pak::test
