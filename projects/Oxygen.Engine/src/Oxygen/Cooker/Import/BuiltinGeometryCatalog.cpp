//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <fmt/format.h>
#include <fmt/ranges.h>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Cooker/Import/BuiltinGeometryCatalog.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/BuiltinGeometry.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/MaterialSlotId.h>

namespace {

using nlohmann::json;
constexpr auto kDefaultMaterialName = "OxygenEditor_Default";

auto AuthoringCategoryName(
  const oxygen::data::BuiltinGeometryAuthoringCategory category)
  -> std::string_view
{
  using Category = oxygen::data::BuiltinGeometryAuthoringCategory;
  switch (category) {
  case Category::kStandard:
    return "standard";
  case Category::kAdvanced:
    return "advanced";
  case Category::kInternal:
    return "internal";
  }
  throw std::logic_error("Unknown built-in geometry authoring category.");
}

auto DefaultMaterialDescriptor() -> json
{
  const auto material = oxygen::data::MaterialAsset::CreateDefault();
  auto color = json::array();
  for (const auto channel : material->GetBaseColor()) {
    color.push_back(channel);
  }
  return {
    { "$schema", "oxygen.material-descriptor.v1" },
    { "name", kDefaultMaterialName },
    { "domain", "opaque" },
    { "alpha_mode", "opaque" },
    {
      "parameters",
      {
        {
          "base_color",
          std::move(color),
        },
        { "metalness", material->GetMetalness() },
        { "roughness", material->GetRoughness() },
        { "normal_scale", material->GetNormalScale() },
        { "ambient_occlusion", material->GetAmbientOcclusion() },
        { "alpha_cutoff", material->GetAlphaCutoff() },
        { "double_sided", material->IsDoubleSided() },
      },
    },
  };
}

auto GeometryDescriptor(const oxygen::data::BuiltinGeometryIdentity& identity,
  const oxygen::data::GeometryAsset& geometry, const std::string& material_path)
  -> json
{
  const auto minimum = geometry.BoundingBoxMin();
  const auto maximum = geometry.BoundingBoxMax();
  const auto bounds = json {
    { "min", json::array({ minimum.x, minimum.y, minimum.z }) },
    { "max", json::array({ maximum.x, maximum.y, maximum.z }) },
  };
  return {
    { "$schema", "oxygen.geometry-descriptor.v1" },
    { "name", identity.descriptor_name },
    { "bounds", bounds },
    {
      "lods",
      json::array({
        {
          { "name", "LOD0" },
          { "mesh_type", "procedural" },
          { "bounds", bounds },
          {
            "procedural",
            {
              { "generator", identity.generator },
              { "mesh_name", identity.name },
              { "params", json::object() },
            },
          },
          {
            "submeshes",
            json::array({
              {
                { "name", "Main" },
                {
                  "slot_id",
                  to_string(geometry.MaterialSlots().slots.front().slot_id),
                },
                { "material_ref", material_path },
                {
                  "views",
                  json::array({ json { { "view_ref", "__all__" } } }),
                },
              },
            }),
          },
        },
      }),
    },
  };
}

auto MaterialInventory(const oxygen::data::GeometryAsset& geometry) -> json
{
  const auto& inventory = geometry.MaterialSlots();
  auto slots = json::array();
  for (const auto& slot : inventory.slots) {
    auto bindings = json::array();
    for (const auto& binding : slot.bindings) {
      bindings.push_back({
        { "lod_index", binding.lod_index },
        { "submesh_index", binding.submesh_index },
        { "default_material_key", to_string(binding.default_material_key) },
      });
    }
    slots.push_back({
      { "slot_id", to_string(slot.slot_id) },
      { "display_name", slot.display_name },
      { "bindings", std::move(bindings) },
    });
  }
  return {
    { "geometry_asset_key", to_string(inventory.geometry_asset_key) },
    {
      "layout_revision",
      fmt::format("{:02x}", fmt::join(inventory.layout_revision, "")),
    },
    { "slots", std::move(slots) },
  };
}

} // namespace

namespace oxygen::content::import {

auto ExportBuiltinGeometryCatalog(const std::string_view mount_name)
  -> std::string
{
  if (mount_name.empty() || mount_name == "." || mount_name == ".."
    || mount_name.find_first_of("/\\") != std::string_view::npos) {
    throw std::invalid_argument(
      "A built-in catalog requires one virtual mount name.");
  }
  const auto mount = "/" + std::string(mount_name);
  const auto material_path
    = mount + "/Materials/" + kDefaultMaterialName + ".omat";
  auto entries = json::array();
  for (const auto name : data::GetBuiltinGeometryNames()) {
    const auto uri
      = "asset:///Engine/Generated/BasicShapes/" + std::string(name);
    const auto identity = data::ResolveBuiltinGeometryIdentity(uri);
    const auto geometry = data::ResolveBuiltinGeometry(uri);
    if (!identity || !geometry) {
      throw std::runtime_error("Built-in generation failed for " + uri);
    }
    entries.push_back({
      { "name", identity->name },
      { "canonical_name", identity->generator },
      {
        "authoring_category",
        AuthoringCategoryName(identity->authoring_category),
      },
      { "asset_uri", identity->asset_uri },
      {
        "virtual_path",
        mount + "/Geometry/" + identity->descriptor_name + ".ogeo",
      },
      { "descriptor", GeometryDescriptor(*identity, *geometry, material_path) },
      { "material_slot_inventory", MaterialInventory(*geometry) },
    });
  }

  return json {
    { "schema", "oxygen.builtin-geometry-catalog.v3" },
    { "mount", mount_name },
    {
      "default_material",
      {
        { "virtual_path", material_path },
        { "descriptor", DefaultMaterialDescriptor() },
      },
    },
    { "geometries", std::move(entries) },
  }
    .dump(2);
}

} // namespace oxygen::content::import
