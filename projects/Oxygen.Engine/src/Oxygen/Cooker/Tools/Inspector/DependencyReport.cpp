//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include "DependencyReport.h"
#include <nlohmann/json.hpp>

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Clap/Fluent/CommandBuilder.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/PakFormat_core.h>

namespace oxygen::content::inspection {

auto BuildDependencyCommand(DependencyReportOptions& options)
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
                  .About("Destination JSON report")
                  .Required()
                  .WithValue<std::string>()
                  .StoreTo(&options.output)
                  .Build();
  return clap::CommandBuilder("dependencies")
    .About(
      "Inspect direct asset dependencies without loading content resources.")
    .WithPositionalArguments(root)
    .WithOption(std::move(output));
}

auto RunDependencyReport(const DependencyReportOptions& options) -> int
{
  try {
    const auto root
      = std::filesystem::absolute(options.cooked_root).lexically_normal();
    lc::Inspection inspection;
    inspection.LoadFromRoot(root);
    nlohmann::json assets = nlohmann::json::array();
    for (const auto& entry : inspection.Assets()) {
      nlohmann::json row = {
        { "asset_key", nostd::to_string(entry.key) },
        { "asset_type", entry.asset_type },
        { "virtual_path", entry.virtual_path },
        { "dependencies", nlohmann::json::array() },
        { "complete", true },
      };
      row["key_references"] = nlohmann::json::array();
      row["resource_bindings"] = nlohmann::json::array();
      for (const auto& reference : entry.references.Keys()) {
        row["key_references"].push_back({
          { "asset_key", nostd::to_string(reference.key) },
          { "target_kind", nostd::to_underlying(reference.kind) },
          { "expected_asset_type",
            nostd::to_underlying(reference.expected_type) },
        });
        if (reference.kind == data::KeyReferenceKind::kAsset) {
          row["dependencies"].push_back(nostd::to_string(reference.key));
        }
      }
      for (const auto& binding : entry.references.Resources()) {
        const char* state = "resource";
        if (binding.kind == data::ResourceKind::kTexture) {
          if (binding.index == data::pak::core::kErrorTextureResourceIndex) {
            state = "error";
          } else if (binding.index == data::pak::core::kFallbackResourceIndex) {
            state = "fallback";
          }
        }
        row["resource_bindings"].push_back({
          { "kind", nostd::to_underlying(binding.kind) },
          { "index", binding.index.get() },
          { "state", state },
        });
      }
      assets.push_back(std::move(row));
    }
    const nlohmann::json report = {
      { "schema", "oxygen.cooked-dependencies.v2" },
      { "source_key", nostd::to_string(inspection.Guid()) },
      { "assets", std::move(assets) },
    };
    std::ofstream output(options.output, std::ios::binary | std::ios::trunc);
    output << report.dump(2);
    output.close();
    if (!output) {
      throw std::runtime_error("Could not write dependency report.");
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "ERROR: " << error.what() << '\n';
    return 2;
  }
}

} // namespace oxygen::content::inspection
