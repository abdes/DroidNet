//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "InventoryReport.h"
#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Clap/Fluent/CommandBuilder.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Content/LooseCookedIndex.h>
#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Cooked/Validate.h>
#include <Oxygen/Cooker/Tools/Common/ReportFile.h>
#include <Oxygen/Cooker/Tools/Common/ToolCommand.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakFormatVersions.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Serio/ByteReader.h>
#include <Oxygen/Serio/Integrity.h>
#include <Oxygen/Serio/NativeFile.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Serio/RegionStream.h>

namespace oxygen::content::inspection {

namespace {

  //! The descriptors of the root cooked in an asset format this engine no
  //! longer reads; each is an issue of its own file, so only the product that
  //! owns it needs cooking again.
  auto FindStaleDescriptors(const std::filesystem::path& root,
    const lc::LooseCookedIndex& index) -> std::vector<std::string>
  {
    auto stale = std::vector<std::string> {};
    for (const auto& key : index.GetAllAssetKeys()) {
      const auto relpath = index.FindDescriptorRelPath(key);
      const auto type = index.FindAssetType(key);
      if (!relpath.has_value() || !type.has_value()) {
        continue;
      }
      const auto descriptor_path = root / std::filesystem::path(*relpath);
      auto file = serio::NativeFile::Open(descriptor_path, {});
      if (!file) {
        throw std::system_error(
          file.error(), fmt::format("open '{}'", descriptor_path.string()));
      }
      auto stream = serio::OpenReadStream(*file);
      if (!stream) {
        throw std::system_error(
          stream.error(), fmt::format("size '{}'", descriptor_path.string()));
      }
      serio::Reader<serio::RegionReadStream<serio::NativeFile>> reader(*stream);
      auto pack = reader.ScopedAlignment(1);
      const auto blob = reader.ReadBlob(sizeof(data::pak::core::AssetHeader));
      if (!blob) {
        throw std::runtime_error(
          "failed to read descriptor header: " + std::string(*relpath));
      }
      const auto header = serio::CopyAt<data::pak::core::AssetHeader>(
        std::as_bytes(std::span { *blob }));
      if (!header) {
        throw std::runtime_error(
          "failed to read descriptor header: " + std::string(*relpath));
      }
      const auto current
        = data::pak::CurrentAssetVersion(static_cast<data::AssetType>(*type));
      if (!current.has_value() || header->version != *current) {
        stale.emplace_back(*relpath);
      }
    }
    return stale;
  }

  auto FileDigest(const std::filesystem::path& path) -> base::Sha256Digest
  {
    const auto digest = serio::ComputeFileSha256(path);
    if (!digest) {
      throw std::runtime_error(
        "Cannot hash '" + path.string() + "': " + digest.error().message());
    }
    return *digest;
  }

} // namespace

auto BuildInventoryReportCommand(InventoryReportOptions& options)
  -> std::shared_ptr<clap::Command>
{
  auto root = clap::Option::Positional("cooked_root")
                .About("Loose cooked root directory")
                .Required()
                .WithValue<std::string>()
                .StoreTo(&options.cooked_root)
                .Build();
  auto output = clap::Option::WithKey("output")
                  .Long("output")
                  .About("Destination JSON inventory, outside the cooked root")
                  .Required()
                  .WithValue<std::string>()
                  .StoreTo(&options.output)
                  .Build();
  return clap::CommandBuilder("inventory")
    .About(
      "Verify every cooked file and export the native integrity inventory.")
    .WithPositionalArguments(root)
    .WithOption(std::move(output));
}

auto RunInventoryReport(const InventoryReportOptions& options)
  -> toolkit::ToolResult
{
  using nlohmann::json;
  const auto root
    = std::filesystem::canonical(base::ToNativePath(options.cooked_root));
  const auto output = toolkit::ResolveReportPath(options.output);
  if (auto safe = toolkit::CheckToolReportDestination(
        output, { .inputs = {}, .cooked_roots = { root } });
    !safe) {
    return safe;
  }
  const auto index_path = root / oxygen::data::loose_cooked::kIndexFileName;
  const auto index_digest = FileDigest(index_path);
  const auto index = lc::LooseCookedIndex::LoadFromRoot(root);
  const auto failures = index.CheckContent(root, lc::IntegrityCheck::kFull);
  const auto stale = failures.empty() ? FindStaleDescriptors(root, index)
                                      : std::vector<std::string> {};
  auto content = std::optional<cooked::CookedContent> {};
  if (failures.empty()) {
    content.emplace(
      cooked::CookedContent::Open(cooked::RealCookerStorage(), root));
    if (stale.empty()) {
      cooked::Validate(*content, lc::IntegrityCheck::kMetadata);
    }
  }
  auto issues = json::array();
  for (const auto& relpath : stale) {
    issues.push_back(
      { { "relative_path", relpath }, { "reason", "format_version" } });
  }
  for (const auto& failure : failures) {
    const auto* const reason = [&] -> const char* {
      switch (failure.reason) {
      case lc::IntegrityFailure::kMissing:
        return "missing";
      case lc::IntegrityFailure::kUnexpected:
        return "unexpected";
      case lc::IntegrityFailure::kSizeMismatch:
        return "size_mismatch";
      case lc::IntegrityFailure::kDigestMismatch:
        return "digest_mismatch";
      case lc::IntegrityFailure::kLinkedPath:
        return "linked_path";
      case lc::IntegrityFailure::kNotRegular:
        return "not_regular";
      case lc::IntegrityFailure::kMalformedTable:
        return "malformed_table";
      case lc::IntegrityFailure::kResourceOutOfBounds:
        return "resource_out_of_bounds";
      case lc::IntegrityFailure::kResourceDigestMismatch:
        return "resource_digest_mismatch";
      }
      throw std::logic_error("Unknown inventory failure");
    }();
    auto issue = json {
      { "relative_path", failure.relative_path },
      { "reason", reason },
    };
    if (failure.resource_row.has_value()) {
      issue["resource_row"] = *failure.resource_row;
    }
    issues.push_back(std::move(issue));
  }
  const auto hex = [](const base::Sha256Digest& digest) -> std::string {
    return base::ToHexString(digest);
  };
  auto files = json::array();
  for (const auto& file : index.GetFileInventory()) {
    files.push_back({
      { "relative_path", file.relative_path },
      { "size", file.size },
      { "sha256", hex(file.sha256) },
      {
        "file_kind",
        file.kind.has_value() ? json(static_cast<uint16_t>(file.kind.value()))
                              : json(nullptr),
      },
    });
  }
  auto assets = json::array();
  for (const auto& key : index.GetAllAssetKeys()) {
    assets.push_back({
      { "asset_key", nostd::to_string(key) },
      { "asset_type", index.FindAssetType(key).value_or(0U) },
      { "virtual_path", std::string(index.FindVirtualPath(key).value_or("")) },
      {
        "descriptor_relative_path",
        std::string(index.FindDescriptorRelPath(key).value_or("")),
      },
    });
  }
  auto resources = json::array();
  for (const auto& file : index.GetFileInventory()) {
    if (file.kind != lc::LooseCookedIndex::FileKind::kTextureDescriptor) {
      continue;
    }
    auto resource_index = json(nullptr);
    if (content.has_value()) {
      const auto* const texture = content->FindNamedTexture(file.virtual_path);
      if (texture == nullptr) {
        throw std::runtime_error(
          "Texture descriptor is not a named texture: " + file.virtual_path);
      }
      resource_index = texture->row.get();
    }
    resources.push_back({
      { "kind", "texture" },
      { "virtual_path", file.virtual_path },
      { "descriptor_relative_path", file.relative_path },
      { "resource_index", std::move(resource_index) },
    });
  }
  if (FileDigest(index_path) != index_digest) {
    throw std::runtime_error("Loose cooked index changed during verification");
  }
  const auto report = json {
      { "schema_version", 1 }, { "source_key", nostd::to_string(index.Guid()) },
      { "index_size", std::filesystem::file_size(index_path) },
      { "index_sha256", hex(index_digest) },
      { "files", std::move(files) }, { "assets", std::move(assets) },
      { "resources", std::move(resources) },
      { "issues", std::move(issues) },
    }.dump();
  return toolkit::PublishToolReport(output, report);
}

} // namespace oxygen::content::inspection
