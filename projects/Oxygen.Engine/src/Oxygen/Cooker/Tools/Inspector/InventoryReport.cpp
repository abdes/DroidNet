//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#include "InventoryReport.h"
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Clap/Fluent/CommandBuilder.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Content/LooseCookedIndex.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Cooker/Loose/Types.h>
#include <Oxygen/Cooker/Loose/Validation.h>
#include <Oxygen/Serio/AtomicFile.h>

namespace oxygen::content::inspection {

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

auto RunInventoryReport(const InventoryReportOptions& options) -> int
{
  using nlohmann::json;
  try {
    const auto root
      = std::filesystem::canonical(base::ToNativePath(options.cooked_root));
    const auto output
      = std::filesystem::weakly_canonical(base::ToNativePath(options.output));
    const auto relative = output.lexically_relative(root);
    if (!relative.empty() && *relative.begin() != "..") {
      throw std::runtime_error(
        "Inventory report output must be outside the cooked root");
    }
    const auto index_path = root / "container.index.bin";
    const auto index_digest = base::ComputeFileSha256(index_path);
    const auto index = lc::LooseCookedIndex::LoadFromRoot(root);
    const auto failures = index.CheckContent(root, lc::IntegrityCheck::kFull);
    if (failures.empty()) {
      lc::ValidateRoot(root, lc::IntegrityCheck::kMetadata);
    }
    auto issues = json::array();
    for (const auto& failure : failures) {
      const auto reason = [&] {
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
        }
        throw std::logic_error("Unknown inventory failure");
      }();
      issues.push_back(
        { { "relative_path", failure.relative_path }, { "reason", reason } });
    }
    const auto hex = [](const base::Sha256Digest& digest) {
      return fmt::format("{:02x}", fmt::join(digest, ""));
    };
    auto files = json::array();
    for (const auto& file : index.GetFileInventory()) {
      files.push_back({ { "relative_path", file.relative_path },
        { "size", file.size }, { "sha256", hex(file.sha256) },
        { "file_kind",
          file.kind.has_value() ? json(static_cast<uint16_t>(file.kind.value()))
                                : json(nullptr) } });
    }
    lc::Inspection inspection;
    inspection.LoadFromRoot(root);
    auto assets = json::array();
    for (const auto& asset : inspection.Assets()) {
      assets.push_back({ { "asset_key", nostd::to_string(asset.key) },
        { "asset_type", asset.asset_type },
        { "virtual_path", asset.virtual_path },
        { "descriptor_relative_path", asset.descriptor_relpath } });
    }
    auto resources = json::array();
    for (const auto& file : inspection.Files()) {
      if (file.kind != lc::FileKind::kAuxiliary
        || std::filesystem::path(file.relpath).extension() != ".otex") {
        continue;
      }
      auto resource_index = json(nullptr);
      if (failures.empty()) {
        resource_index = static_cast<uint32_t>(
          inspection.ReadTextureDescriptor(file.relpath).index);
      }
      resources.push_back(
        { { "kind", "texture" }, { "descriptor_relative_path", file.relpath },
          { "resource_index", std::move(resource_index) } });
    }
    if (base::ComputeFileSha256(index_path) != index_digest) {
      throw std::runtime_error(
        "Loose cooked index changed during verification");
    }
    const auto report = json {
      { "schema_version", 1 }, { "source_key", nostd::to_string(index.Guid()) },
      { "index_size", std::filesystem::file_size(index_path) },
      { "index_sha256", hex(index_digest) },
      { "files", std::move(files) }, { "assets", std::move(assets) },
      { "resources", std::move(resources) },
      { "issues", std::move(issues) },
    }.dump();
    const auto result
      = serio::WriteFileAtomically(output, std::as_bytes(std::span(report)));
    if (!result) {
      throw std::system_error(
        result.error(), "Write integrity inventory report");
    }
    if (result->durability_error) {
      throw std::system_error(
        result->durability_error, "Synchronize integrity inventory report");
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "ERROR: " << error.what() << '\n';
    return 2;
  }
}

} // namespace oxygen::content::inspection
