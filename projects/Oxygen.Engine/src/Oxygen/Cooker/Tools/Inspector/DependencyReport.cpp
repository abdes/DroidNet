//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "DependencyReport.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Clap/Fluent/CommandBuilder.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Content/DescriptorDependencies.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Reader.h>

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
        { "complete", false },
      };
      try {
        const auto relative
          = std::filesystem::path(entry.descriptor_relpath).lexically_normal();
        if (relative.empty() || relative.has_root_path()
          || *relative.begin() == "..") {
          throw std::runtime_error(
            "Descriptor path must stay within its cooked root.");
        }
        serio::FileStream<> stream(root / relative, std::ios::in);
        serio::Reader<serio::FileStream<>> reader(stream);
        const auto result = InspectDescriptorDependencies(
          reader, entry.key, static_cast<data::AssetType>(entry.asset_type));
        row["complete"] = result.complete;
        if (!result.explanation.empty()) {
          row["diagnostic"] = result.explanation;
        }
        for (const auto& dependency : result.assets) {
          row["dependencies"].push_back(nostd::to_string(dependency));
        }
      } catch (const std::exception& error) {
        row["diagnostic"] = error.what();
      }
      assets.push_back(std::move(row));
    }
    const nlohmann::json report = {
      { "schema", "oxygen.cooked-dependencies.v1" },
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
