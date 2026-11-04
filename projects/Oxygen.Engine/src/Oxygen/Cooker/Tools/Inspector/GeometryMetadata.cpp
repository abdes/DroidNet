//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <exception>
#include <expected>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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
#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Import/Internal/GeometryDefaults.h>
#include <Oxygen/Cooker/Tools/Common/ToolCommand.h>
#include <Oxygen/Cooker/Tools/Inspector/InspectorReport.h>
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

  //! The triangles the submesh `binding` names draws.
  auto TriangleCountOf(const data::GeometryAsset& geometry,
    const data::SurfaceBinding& binding) -> uint64_t
  {
    const auto& mesh = geometry.MeshAt(binding.lod_index);
    if (mesh == nullptr
      || binding.submesh_index.get() >= mesh->SubMeshes().size()) {
      throw std::runtime_error("surface binding names no submesh");
    }
    return mesh->SubMeshes()[binding.submesh_index.get()].TriangleCount();
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

auto RunGeometryMetadataReport(const GeometryMetadataOptions& options)
  -> toolkit::ToolResult
{
  using nlohmann::json;
  const auto target = PrepareReport({
    .cooked_root = options.cooked_root,
    .output = options.output,
  });
  if (!target) {
    return std::unexpected(target.error());
  }
  const auto content
    = cooked::CookedContent::Open(cooked::RealCookerStorage(), target->root);
  auto geometries = json::array();
  auto failures = std::vector<std::string> {};
  for (const auto& entry : content.Assets()) {
    if (entry.type != data::AssetType::kGeometry) {
      continue;
    }
    if (!options.virtual_path.empty()
      && entry.virtual_path != options.virtual_path) {
      continue;
    }
    auto row = json {
      { "geometry_asset_key", nostd::to_string(entry.key) },
      { "virtual_path", entry.virtual_path },
      { "complete", false },
      { "surfaces", json::array() },
    };
    try {
      const auto bytes = content.Descriptor(entry.key);
      if (bytes.size() != entry.descriptor_size.get()) {
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
            { "triangle_count", TriangleCountOf(*geometry, binding) },
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
      row.at("geometry_asset_key")
        = nostd::to_string(inventory.geometry_asset_key);
      row["layout_revision"] = base::ToHexString(inventory.layout_revision);
      row["lod_policy"] = import::internal::WriteLodPolicy(*lod_policy);
      row.at("surfaces") = std::move(surfaces);
      row.at("complete") = true;
    } catch (const std::exception& error) {
      row["diagnostic"] = error.what();
      failures.push_back(RowFailure("geometry", entry, error));
    }
    geometries.push_back(std::move(row));
  }
  const auto report = json {
    { "schema_version", kGeometryMetadataSchemaVersion },
    { "complete", failures.empty() },
    { "geometries", std::move(geometries) },
  };
  return PublishRowReport(target->output, report, failures);
}

} // namespace oxygen::content::inspection
