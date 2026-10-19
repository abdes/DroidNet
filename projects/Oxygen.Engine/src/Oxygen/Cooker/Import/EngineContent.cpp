//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <ios>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/Import/EngineContent.h>
#include <Oxygen/Core/Types/DescriptorExtensions.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/MaterialSlotId.h>
#include <Oxygen/Data/ProceduralMeshes.h>

namespace {

using nlohmann::json;
using oxygen::content::import::EngineShape;

constexpr auto kMount = std::string_view { "/Engine" };
constexpr auto kBasicShapes = std::string_view { "Generated/BasicShapes" };
constexpr auto kInternal = std::string_view { "Internal" };
constexpr auto kMaterials = std::string_view { "Generated/Materials" };
constexpr auto kDefaultMaterialName = std::string_view { "Default" };

constexpr auto kShapes = std::array {
  EngineShape { .name = "Cube", .folder = kBasicShapes },
  EngineShape { .name = "SubdividedCube", .folder = kBasicShapes },
  EngineShape { .name = "Sphere", .folder = kBasicShapes },
  EngineShape { .name = "Capsule", .folder = kBasicShapes },
  EngineShape { .name = "IcoSphere", .folder = kBasicShapes },
  EngineShape { .name = "Plane", .folder = kBasicShapes },
  EngineShape { .name = "Cylinder", .folder = kBasicShapes },
  EngineShape { .name = "Cone", .folder = kBasicShapes },
  EngineShape { .name = "Torus", .folder = kBasicShapes },
  EngineShape { .name = "Quad", .folder = kBasicShapes },
  EngineShape { .name = "ArrowGizmo", .folder = kInternal },
};

// Every job cooks into the `/Engine` mount.
auto Layout() -> json { return { { "virtual_mount_root", kMount } }; }

// A job's asset is `/Engine/<folder>/<name><extension>`, and is placed there.
auto EngineVirtualPath(const std::string_view folder,
  const std::string_view name, const std::string_view extension) -> std::string
{
  return std::string(kMount) + "/" + std::string(folder) + "/"
    + std::string(name) + std::string(extension);
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
        { "base_color", std::move(color) },
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

auto GeometryDescriptor(const EngineShape& shape) -> json
{
  auto mesh_name = std::string(shape.name);
  std::ranges::transform(
    mesh_name, mesh_name.begin(), [](const char value) -> char {
      return value >= 'A' && value <= 'Z'
        ? static_cast<char>(value + ('a' - 'A'))
        : value;
    });
  const auto mesh
    = oxygen::data::GenerateMesh(std::string(shape.name) + "/" + mesh_name, {});
  if (!mesh || mesh->SubMeshes().empty()) {
    throw std::runtime_error(
      "Engine shape generation failed for " + std::string(shape.name));
  }

  const auto minimum = mesh->BoundingBoxMin();
  const auto maximum = mesh->BoundingBoxMax();
  const auto bounds = json {
    { "min", json::array({ minimum.x, minimum.y, minimum.z }) },
    { "max", json::array({ maximum.x, maximum.y, maximum.z }) },
  };
  return {
    { "$schema", "oxygen.geometry-descriptor.v1" },
    { "name", shape.name },
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
              { "generator", shape.name },
              { "mesh_name", shape.name },
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
                  to_string(mesh->SubMeshes().front().SlotId()),
                },
                {
                  "material_ref",
                  oxygen::data::MaterialAsset::kDefaultVirtualPath,
                },
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

auto WriteJson(const std::filesystem::path& path, const json& value) -> void
{
  std::filesystem::create_directories(path.parent_path());
  auto stream = std::ofstream(path, std::ios::binary);
  stream << value.dump(2) << '\n';
  stream.close();
  if (!stream) {
    throw std::runtime_error("Cannot write " + path.generic_string());
  }
}

auto Manifest(const std::filesystem::path& cooked_root, json jobs) -> json
{
  return {
    { "version", 1 },
    { "output", cooked_root.generic_string() },
    { "jobs", std::move(jobs) },
  };
}

} // namespace

namespace oxygen::content::import {

auto GetEngineShapes() noexcept -> std::span<const EngineShape>
{
  return kShapes;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) - public API order
auto WriteEngineContentSources(const std::filesystem::path& directory,
  const std::filesystem::path& cooked_root) -> EngineContentManifests
{
  const auto material_source = directory / kMaterials
    / (std::string(kDefaultMaterialName) + ".omat.json");
  WriteJson(material_source, DefaultMaterialDescriptor());
  auto material_jobs = json::array({
    {
      { "type", "material-descriptor" },
      { "source", material_source.generic_string() },
      {
        "virtual_path",
        EngineVirtualPath(kMaterials, kDefaultMaterialName,
          oxygen::data::kMaterialDescriptorExtension),
      },
      { "name", kDefaultMaterialName },
      { "layout", Layout() },
    },
  });

  auto geometry_jobs = json::array();
  for (const auto& shape : kShapes) {
    const auto source
      = directory / shape.folder / (std::string(shape.name) + ".ogeo.json");
    WriteJson(source, GeometryDescriptor(shape));
    geometry_jobs.push_back({
      { "type", "geometry-descriptor" },
      { "source", source.generic_string() },
      {
        "virtual_path",
        EngineVirtualPath(
          shape.folder, shape.name, oxygen::data::kGeometryDescriptorExtension),
      },
      { "name", shape.name },
      { "layout", Layout() },
    });
  }

  auto manifests = EngineContentManifests {
    .materials = directory / "materials.import-manifest.json",
    .geometry = directory / "geometry.import-manifest.json",
  };
  WriteJson(
    manifests.materials, Manifest(cooked_root, std::move(material_jobs)));
  WriteJson(
    manifests.geometry, Manifest(cooked_root, std::move(geometry_jobs)));
  return manifests;
}

} // namespace oxygen::content::import
