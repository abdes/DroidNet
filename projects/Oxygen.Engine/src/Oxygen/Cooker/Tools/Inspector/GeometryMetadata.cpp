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
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Clap/Fluent/CommandBuilder.h>
#include <Oxygen/Clap/Fluent/DSL.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/Loaders/GeometryLoader.h>
#include <Oxygen/Cooker/Import/Internal/GeometryDefaults.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/StableId.h>
#include <Oxygen/Data/SurfaceInventory.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Reader.h>

namespace oxygen::content::inspection {

namespace {
  //! A cooked surface's defaults; the loader rejects a cooked geometry that
  //! declares none.
  auto DefaultsOf(const data::Surface& surface) -> const data::SurfaceDefaults&
  {
    if (!surface.defaults.has_value()) {
      throw std::runtime_error("surface declares no defaults");
    }
    return *surface.defaults;
  }
} // namespace

auto BuildGeometryMetadataCommand(GeometryMetadataOptions& options)
  -> std::shared_ptr<clap::Command>
{
  auto root = clap::Option::Positional("cooked_root")
                .About("Loose cooked root directory or pak file")
                .Required()
                .WithValue<std::string>()
                .StoreTo(&options.cooked_root)
                .Build();
  auto output = clap::Option::WithKey("output")
                  .Long("output")
                  .About("Destination JSON geometry surface inventory")
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
    .About("Inspect native geometry surface identities and bindings.")
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
    if (output_path == root) {
      throw std::runtime_error(
        "Geometry metadata output must not replace its source pak.");
    }
    lc::Inspection inspection;
    inspection.Load(root);
    auto geometries = json::array();
    for (const auto& entry : inspection.Assets()) {
      const auto relative
        = std::filesystem::path(entry.descriptor_relpath).lexically_normal();
      if (!inspection.IsPak() && output_path == root / relative) {
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
        if (!inspection.IsPak()
          && (relative.empty() || relative.has_root_path()
            || *relative.begin() == "..")) {
          throw std::runtime_error(
            "Descriptor path must stay within its cooked root.");
        }
        const auto bytes = inspection.ReadAssetDescriptor(entry.key);
        if (bytes.size() != entry.descriptor_size) {
          throw std::runtime_error(
            "Geometry descriptor size does not match its index entry.");
        }
        serio::ReadOnlyMemoryStream stream(bytes);
        serio::Reader reader(stream);
        const LoaderContext context {
          .current_asset_key = entry.key,
          .desc_reader = &reader,
          .work_offline = true,
          .parse_only = true,
        };
        const auto geometry = loaders::LoadGeometryAsset(context);
        const auto& inventory = geometry->Surfaces();
        auto surfaces = json::array();
        for (const auto& surface : inventory.surfaces) {
          auto bindings = json::array();
          for (const auto& binding : surface.bindings) {
            bindings.push_back({
              { "lod_index", binding.lod_index.get() },
              { "submesh_index", binding.submesh_index.get() },
              {
                "default_material_key",
                nostd::to_string(binding.default_material_key),
              },
            });
          }
          const auto& defaults = DefaultsOf(surface);
          surfaces.push_back({
            { "surface_id", data::to_string(surface.surface_id) },
            { "display_name", surface.display_name },
            { "visible", defaults.visible },
            { "casts_shadows", defaults.casts_shadows },
            { "bindings", std::move(bindings) },
          });
        }
        const auto lod_policy = geometry->DefaultLodPolicy();
        if (!lod_policy.has_value()) {
          throw std::runtime_error("geometry declares no LOD policy");
        }
        geometries.push_back({
          {
            "geometry_asset_key",
            nostd::to_string(inventory.geometry_asset_key),
          },
          {
            "layout_revision",
            base::ToHexString(inventory.layout_revision),
          },
          {
            "lod_policy",
            import::internal::WriteLodPolicy(*lod_policy),
          },
          { "surfaces", std::move(surfaces) },
        });
      } catch (const std::exception& error) {
        throw std::runtime_error("invalid geometry '" + entry.virtual_path
          + "' (" + nostd::to_string(entry.key) + "): " + error.what());
      }
    }
    const auto report = json {
      { "schema_version", data::kSurfaceInventorySchemaVersion },
      { "geometries", std::move(geometries) },
    };
    std::ofstream output(output_path, std::ios::binary);
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
