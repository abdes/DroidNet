//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "SceneMetadata.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Clap/Fluent/CommandBuilder.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/Loaders/SceneLoader.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Reader.h>

namespace oxygen::content::inspection {
namespace {

  auto FlagSource(const data::pak::world::NodeRecord& node, const uint32_t mask,
    const std::string_view local_on, const std::string_view local_off)
    -> std::string_view
  {
    if ((node.inherited_flags & mask) != 0U) {
      return "inherit";
    }
    return (node.node_flags & mask) != 0U ? local_on : local_off;
  }

} // namespace

auto BuildSceneMetadataCommand(SceneMetadataOptions& options)
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
                  .About("Destination JSON scene metadata report")
                  .Required()
                  .WithValue<std::string>()
                  .StoreTo(&options.output)
                  .Build();
  return clap::CommandBuilder("scenes")
    .About("Inspect native scene versions, node identities and authored flag "
           "sources.")
    .WithPositionalArguments(root)
    .WithOption(std::move(output));
}

auto RunSceneMetadataReport(const SceneMetadataOptions& options) -> int
{
  using nlohmann::json;
  namespace world = data::pak::world;
  try {
    const auto root
      = std::filesystem::absolute(options.cooked_root).lexically_normal();
    const auto output_path
      = std::filesystem::absolute(options.output).lexically_normal();
    if (output_path == root / "container.index.bin") {
      throw std::runtime_error(
        "Scene metadata output must not replace its source index.");
    }
    lc::Inspection inspection;
    inspection.LoadFromRoot(root);
    auto scenes = json::array();
    bool complete = true;
    for (const auto& entry : inspection.Assets()) {
      const auto relative
        = std::filesystem::path(entry.descriptor_relpath).lexically_normal();
      if (output_path == root / relative) {
        throw std::runtime_error(
          "Scene metadata output must not replace a source descriptor.");
      }
      if (entry.asset_type != static_cast<uint8_t>(data::AssetType::kScene)) {
        continue;
      }
      auto row = json {
        { "asset_key", nostd::to_string(entry.key) },
        { "virtual_path", entry.virtual_path },
        { "descriptor_version", nullptr },
        { "nodes", json::array() },
        { "complete", false },
      };
      try {
        if (relative.empty() || relative.has_root_path()
          || *relative.begin() == "..") {
          throw std::runtime_error(
            "Descriptor path must stay within its cooked root.");
        }
        serio::FileStream<> stream(root / relative, std::ios::in);
        const auto size = stream.Size();
        if (!size || *size != entry.descriptor_size) {
          throw std::runtime_error(
            "Scene descriptor size does not match its index entry.");
        }
        serio::Reader reader(stream);
        const LoaderContext context {
          .current_asset_key = entry.key,
          .desc_reader = &reader,
          .work_offline = true,
          .parse_only = true,
        };
        const auto scene = loaders::LoadSceneAsset(context);
        row.at("descriptor_version") = scene->GetHeader().version;
        auto index = size_t { 0 };
        for (const auto& node : scene->GetNodes()) {
          row.at("nodes").push_back({
            { "index", index++ },
            { "node_id", nostd::to_string(node.node_id) },
            { "name", scene->GetNodeName(node) },
            { "parent_index", node.parent_index },
            { "flags",
              {
                { "visible",
                  FlagSource(
                    node, world::kSceneNodeFlag_Visible, "shown", "hidden") },
                { "casts_shadows",
                  FlagSource(
                    node, world::kSceneNodeFlag_CastsShadows, "on", "off") },
                { "receives_shadows",
                  FlagSource(
                    node, world::kSceneNodeFlag_ReceivesShadows, "on", "off") },
              } },
          });
        }
        row.at("complete") = true;
      } catch (const std::exception& error) {
        complete = false;
        row.emplace("diagnostic", error.what());
        std::cerr << "ERROR: scene '" << entry.virtual_path << "' ("
                  << nostd::to_string(entry.key) << "): " << error.what()
                  << '\n';
      }
      scenes.push_back(std::move(row));
    }
    const auto report = json {
      { "schema", "oxygen.cooked-scenes.v1" },
      { "source_key", nostd::to_string(inspection.Guid()) },
      { "complete", complete },
      { "scenes", std::move(scenes) },
    };
    std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
    output << report.dump(2) << '\n';
    output.close();
    if (!output) {
      throw std::runtime_error("Could not write scene metadata report.");
    }
    return complete ? 0 : 2;
  } catch (const std::exception& error) {
    std::cerr << "ERROR: " << error.what() << '\n';
    return 2;
  }
}

} // namespace oxygen::content::inspection
