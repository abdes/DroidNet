//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include "GeometryMetadata.h"
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Clap/Fluent/CommandBuilder.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/Loaders/GeometryLoader.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialSlotId.h>
#include <Oxygen/Data/MaterialSlotInventory.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Reader.h>

namespace oxygen::content::inspection {

auto BuildGeometryMetadataCommand(GeometryMetadataOptions& options)
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
                  .About("Destination JSON geometry material-slot inventory")
                  .Required()
                  .WithValue<std::string>()
                  .StoreTo(&options.output)
                  .Build();
  auto virtual_path = clap::Option::WithKey("virtual-path")
                        .Long("virtual-path")
                        .About("Inspect only this native geometry virtual path")
                        .WithValue<std::string>()
                        .StoreTo(&options.virtual_path)
                        .Build();
  return clap::CommandBuilder("geometries")
    .About("Inspect native geometry material-slot identities and bindings.")
    .WithPositionalArguments(root)
    .WithOption(std::move(output))
    .WithOption(std::move(virtual_path));
}

auto RunGeometryMetadataReport(const GeometryMetadataOptions& options) -> int
{
  using nlohmann::json;
  try {
    const auto root
      = std::filesystem::absolute(options.cooked_root).lexically_normal();
    const auto output_path
      = std::filesystem::absolute(options.output).lexically_normal();
    if (output_path == root / "container.index.bin") {
      throw std::runtime_error(
        "Geometry metadata output must not replace its source index.");
    }
    lc::Inspection inspection;
    inspection.LoadFromRoot(root);
    auto geometries = json::array();
    for (const auto& entry : inspection.Assets()) {
      const auto relative
        = std::filesystem::path(entry.descriptor_relpath).lexically_normal();
      if (output_path == root / relative) {
        throw std::runtime_error(
          "Geometry metadata output must not replace a source descriptor.");
      }
      if (entry.asset_type
        != static_cast<uint8_t>(data::AssetType::kGeometry)) {
        continue;
      }
      if (!options.virtual_path.empty()
        && entry.virtual_path != options.virtual_path) {
        continue;
      }
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
            "Geometry descriptor size does not match its index entry.");
        }
        serio::Reader reader(stream);
        const LoaderContext context {
          .current_asset_key = entry.key,
          .desc_reader = &reader,
          .work_offline = true,
          .parse_only = true,
        };
        const auto geometry = loaders::LoadGeometryAsset(context);
        const auto& inventory = geometry->MaterialSlots();
        auto slots = json::array();
        for (const auto& slot : inventory.slots) {
          auto bindings = json::array();
          for (const auto& binding : slot.bindings) {
            bindings.push_back({
              { "lod_index", binding.lod_index.get() },
              { "submesh_index", binding.submesh_index.get() },
              {
                "default_material_key",
                nostd::to_string(binding.default_material_key),
              },
            });
          }
          slots.push_back({
            { "slot_id", data::to_string(slot.slot_id) },
            { "display_name", slot.display_name },
            { "bindings", std::move(bindings) },
          });
        }
        geometries.push_back({
          {
            "geometry_asset_key",
            nostd::to_string(inventory.geometry_asset_key),
          },
          {
            "layout_revision",
            fmt::format("{:02x}", fmt::join(inventory.layout_revision, "")),
          },
          { "slots", std::move(slots) },
        });
      } catch (const std::exception& error) {
        throw std::runtime_error("invalid geometry '" + entry.virtual_path
          + "' (" + nostd::to_string(entry.key) + "): " + error.what());
      }
    }
    const auto report = json {
      { "schema_version", data::kMaterialSlotInventorySchemaVersion },
      { "geometries", std::move(geometries) },
    };
    std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
    output << report.dump(2) << '\n';
    output.close();
    if (!output) {
      throw std::runtime_error("Could not write geometry metadata report.");
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "ERROR: " << error.what() << '\n';
    return 2;
  }
}

} // namespace oxygen::content::inspection
