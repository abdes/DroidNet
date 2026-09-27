//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Async/AsyncDemoTypes.h"
#include "Async/AsyncScene.h"
#include "DemoShell/Services/DefaultSceneLighting.h"
#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/ext/vector_double3.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/MaterialDomain.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Data/ShaderReference.h>
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Environment/Fog.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Environment/SkyLight.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneFlags.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/Types/Flags.h>
#include <Oxygen/Scene/Types/RenderablePolicies.h>

using oxygen::data::Mesh;
using oxygen::data::Vertex;
using oxygen::scene::DistancePolicy;
using oxygen::scene::PerspectiveCamera;

namespace {
constexpr glm::vec3 kSceneFocusPoint { 0.0F, 0.0F, 0.5F };
constexpr glm::vec3 kInitialCameraPosition { 16.0F, -24.0F, 15.0F };
constexpr float kGroundHalfExtent = 28.0F;
constexpr glm::vec3 kSunPosition { 0.0F, -16.0F, 18.0F };
constexpr float kGroundPlaneZ = -0.12F;
constexpr float kTwoSubmeshTrianglesHeight = 1.25F;
constexpr double kSphereMaxOrbitInclination = 0.35;

auto SetShadowParticipation(oxygen::scene::SceneNode& node,
  const bool casts_shadows, const bool receives_shadows) -> void
{
  if (auto flags_ref = node.GetFlags(); flags_ref.has_value()) {
    auto& flags = flags_ref->get();
    flags = flags.SetFlag(oxygen::scene::SceneNodeFlags::kCastsShadows,
      oxygen::scene::SceneFlag {}.SetEffectiveValueBit(casts_shadows));
    flags = flags.SetFlag(oxygen::scene::SceneNodeFlags::kReceivesShadows,
      oxygen::scene::SceneFlag {}.SetEffectiveValueBit(receives_shadows));
  }
}

struct MaterialSurface {
  float metalness { 0.0F };
  float roughness { 0.9F };
};

// Create an immutable procedural material.
auto MakeSolidColorMaterial(const char* name, const glm::vec4& rgba,
  oxygen::data::MaterialDomain domain = oxygen::data::MaterialDomain::kOpaque,
  bool double_sided = false, const MaterialSurface surface = {})
{
  // NOLINTBEGIN(*-magic-numbers)
  namespace d = oxygen::data;
  namespace pak = oxygen::data::pak;

  pak::render::MaterialAssetDesc desc {};
  desc.header.asset_type = static_cast<uint8_t>(
    oxygen::data::AssetType::kMaterial); // MaterialAsset (for tooling/debug)
  const auto name_view = std::string_view(name);
  auto destination = std::span(desc.header.name);
  const auto name_size = std::min(destination.size() - 1U, name_view.size());
  std::ranges::copy(name_view.substr(0, name_size), destination.begin());
  desc.header.version = 1;
  desc.header.streaming_priority = 255;
  desc.material_domain = static_cast<uint8_t>(domain);
  desc.flags = pak::render::kMaterialFlag_NoTextureSampling
    | (double_sided ? pak::render::kMaterialFlag_DoubleSided : 0U);
  desc.shader_stages = 0;
  desc.base_color[0] = rgba.r;
  desc.base_color[1] = rgba.g;
  desc.base_color[2] = rgba.b;
  desc.base_color[3] = rgba.a;
  desc.normal_scale = 1.0F;
  desc.metalness = d::Unorm16 { surface.metalness };
  desc.roughness = d::Unorm16 { surface.roughness };
  desc.ambient_occlusion = d::Unorm16 { 1.0F };
  // Leave texture indices at default invalid (no textures)
  const d::AssetKey asset_key = d::AssetKey::FromVirtualPath(
    "/Engine/Examples/Async/Materials/" + std::string(name) + ".omat");
  return std::make_shared<const d::MaterialAsset>(
    asset_key, desc, std::vector<d::ShaderReference> {});
  // NOLINTEND(*-magic-numbers)
};

//! Build a 2-LOD sphere GeometryAsset (high and low tessellation).
auto BuildSphereLodAsset() -> std::shared_ptr<oxygen::data::GeometryAsset>
{
  // NOLINTBEGIN(*-magic-numbers)
  using oxygen::data::MaterialAsset;
  using oxygen::data::MeshBuilder;
  using oxygen::data::pak::geometry::GeometryAssetDesc;
  using oxygen::data::pak::geometry::MeshViewDesc;

  // Semi-transparent material (transparent domain) with lower alpha to
  // accentuate blending against background.
  const auto glass = MakeSolidColorMaterial("Glass",
    { 0.2F, 0.6F, 0.9F, 0.35F }, oxygen::data::MaterialDomain::kAlphaBlended);

  // LOD 0: higher tessellation
  auto lod0_data = oxygen::data::MakeSphereMeshAsset(64, 64);
  CHECK_F(lod0_data.has_value());
  auto mesh0
    = MeshBuilder(0, "SphereLOD0")
        .WithVertices(lod0_data->first)
        .WithIndices(lod0_data->second)
        .BeginSubMesh("full", glass)
        .WithMeshView(MeshViewDesc {
          .first_index = 0,
          .index_count = static_cast<uint32_t>(lod0_data->second.size()),
          .first_vertex = 0,
          .vertex_count = static_cast<uint32_t>(lod0_data->first.size()),
        })
        .EndSubMesh()
        .Build();

  std::shared_ptr<Mesh> mesh1;
  {
    auto lod1_data = oxygen::data::MakeSphereMeshAsset(24, 24);
    CHECK_F(lod1_data.has_value());
    mesh1 = MeshBuilder(1, "SphereLOD1")
              .WithVertices(lod1_data->first)
              .WithIndices(lod1_data->second)
              .BeginSubMesh("full", glass)
              .WithMeshView(MeshViewDesc {
                .first_index = 0,
                .index_count = static_cast<uint32_t>(lod1_data->second.size()),
                .first_vertex = 0,
                .vertex_count = static_cast<uint32_t>(lod1_data->first.size()),
              })
              .EndSubMesh()
              .Build();
  }

  // Use LOD0 bounds for asset bounds
  GeometryAssetDesc geo_desc {};
  geo_desc.lod_count = 2;
  const glm::vec3 bb_min = mesh0->BoundingBoxMin();
  const glm::vec3 bb_max = mesh0->BoundingBoxMax();
  geo_desc.bounding_box_min[0] = bb_min.x;
  geo_desc.bounding_box_min[1] = bb_min.y;
  geo_desc.bounding_box_min[2] = bb_min.z;
  geo_desc.bounding_box_max[0] = bb_max.x;
  geo_desc.bounding_box_max[1] = bb_max.y;
  geo_desc.bounding_box_max[2] = bb_max.z;

  return std::make_shared<oxygen::data::GeometryAsset>(
    oxygen::data::AssetKey::FromVirtualPath(
      "/Engine/Examples/Async/Geometry/SphereLod.ogeo"),
    geo_desc,
    std::vector<std::shared_ptr<Mesh>> { std::move(mesh0), std::move(mesh1) });
}

//! Build a 1-LOD mesh with two submeshes (two triangles of a quad).
auto BuildTwoSubmeshQuadAsset() -> std::shared_ptr<oxygen::data::GeometryAsset>
{
  using oxygen::data::MaterialAsset;
  using oxygen::data::MeshBuilder;
  using oxygen::data::pak::geometry::GeometryAssetDesc;
  using oxygen::data::pak::geometry::MeshViewDesc;

  // Simple quad (XY plane), two triangles
  std::vector<Vertex> vertices;
  vertices.reserve(4);
  vertices.push_back(Vertex {
    .position = { -1, -1, 0 },
    .normal = { 0, 0, 1 },
    .texcoord = { 0, 1 },
    .tangent = { 1, 0, 0 },
    .bitangent = { 0, 1, 0 },
    .color = { 1, 1, 1, 1 },
  });
  vertices.push_back(Vertex {
    .position = { -1, 1, 0 },
    .normal = { 0, 0, 1 },
    .texcoord = { 0, 0 },
    .tangent = { 1, 0, 0 },
    .bitangent = { 0, 1, 0 },
    .color = { 1, 1, 1, 1 },
  });
  vertices.push_back(Vertex {
    .position = { 1, -1, 0 },
    .normal = { 0, 0, 1 },
    .texcoord = { 1, 1 },
    .tangent = { 1, 0, 0 },
    .bitangent = { 0, 1, 0 },
    .color = { 1, 1, 1, 1 },
  });
  vertices.push_back(Vertex {
    .position = { 1, 1, 0 },
    .normal = { 0, 0, 1 },
    .texcoord = { 1, 0 },
    .tangent = { 1, 0, 0 },
    .bitangent = { 0, 1, 0 },
    .color = { 1, 1, 1, 1 },
  });
  // Keep triangle winding consistent with the authored +Z normals. The
  // previous ordering faced -Z, which made the double-sided shading path treat
  // the visible side as a backface and flip the normal away from the light.
  std::vector<uint32_t> indices { 0, 2, 1, 2, 3, 1 };

  // Create two distinct solid-color materials
  const auto red = MakeSolidColorMaterial("Red", { 1.0F, 0.1F, 0.1F, 1.0F },
    oxygen::data::MaterialDomain::kOpaque, true);
  const auto green = MakeSolidColorMaterial("Green", { 0.1F, 1.0F, 0.1F, 1.0F },
    oxygen::data::MaterialDomain::kOpaque, true);

  auto mesh = MeshBuilder(0, "Quad2SM")
                .WithVertices(vertices)
                .WithIndices(indices)
                // Submesh 0: first triangle (opaque red)
                .BeginSubMesh("tri0", red)
                .WithMeshView(MeshViewDesc {
                  .first_index = 0,
                  .index_count = 3,
                  .first_vertex = 0,
                  .vertex_count = static_cast<uint32_t>(vertices.size()),
                })
                .EndSubMesh()
                // Submesh 1: second triangle (opaque green restored)
                .BeginSubMesh("tri1", green)
                .WithMeshView(MeshViewDesc {
                  .first_index = 3,
                  .index_count = 3,
                  .first_vertex = 0,
                  .vertex_count = static_cast<uint32_t>(vertices.size()),
                })
                .EndSubMesh()
                .Build();

  // Geometry asset with 1 LOD
  GeometryAssetDesc geo_desc {};
  geo_desc.lod_count = 1;
  const auto bb_min = mesh->BoundingBoxMin();
  const auto bb_max = mesh->BoundingBoxMax();
  geo_desc.bounding_box_min[0] = bb_min.x;
  geo_desc.bounding_box_min[1] = bb_min.y;
  geo_desc.bounding_box_min[2] = bb_min.z;
  geo_desc.bounding_box_max[0] = bb_max.x;
  geo_desc.bounding_box_max[1] = bb_max.y;
  geo_desc.bounding_box_max[2] = bb_max.z;
  return std::make_shared<oxygen::data::GeometryAsset>(
    oxygen::data::AssetKey::FromVirtualPath(
      "/Engine/Examples/Async/Geometry/Quad2SM.ogeo"),
    geo_desc, std::vector<std::shared_ptr<Mesh>> { std::move(mesh) });
  // NOLINTEND(*-magic-numbers)
}

auto BuildGroundPlaneAsset() -> std::shared_ptr<oxygen::data::GeometryAsset>
{
  using oxygen::data::MeshBuilder;
  using oxygen::data::pak::geometry::GeometryAssetDesc;
  using oxygen::data::pak::geometry::MeshViewDesc;

  std::vector<Vertex> vertices;
  vertices.reserve(4);
  vertices.push_back(Vertex {
    .position = { -1, -1, 0 },
    .normal = { 0, 0, 1 },
    .texcoord = { 0, 1 },
    .tangent = { 1, 0, 0 },
    .bitangent = { 0, 1, 0 },
    .color = { 1, 1, 1, 1 },
  });
  vertices.push_back(Vertex {
    .position = { -1, 1, 0 },
    .normal = { 0, 0, 1 },
    .texcoord = { 0, 0 },
    .tangent = { 1, 0, 0 },
    .bitangent = { 0, 1, 0 },
    .color = { 1, 1, 1, 1 },
  });
  vertices.push_back(Vertex {
    .position = { 1, -1, 0 },
    .normal = { 0, 0, 1 },
    .texcoord = { 1, 1 },
    .tangent = { 1, 0, 0 },
    .bitangent = { 0, 1, 0 },
    .color = { 1, 1, 1, 1 },
  });
  vertices.push_back(Vertex {
    .position = { 1, 1, 0 },
    .normal = { 0, 0, 1 },
    .texcoord = { 1, 0 },
    .tangent = { 1, 0, 0 },
    .bitangent = { 0, 1, 0 },
    .color = { 1, 1, 1, 1 },
  });

  std::vector<uint32_t> indices { 0, 2, 1, 2, 3, 1 };
  const auto material
    = MakeSolidColorMaterial("GroundMat", { 0.48F, 0.50F, 0.46F, 1.0F },
      oxygen::data::MaterialDomain::kOpaque, true, { .roughness = 0.92F });

  auto mesh = MeshBuilder(0, "GroundPlane")
                .WithVertices(vertices)
                .WithIndices(indices)
                .BeginSubMesh("surface", material)
                .WithMeshView(MeshViewDesc {
                  .first_index = 0,
                  .index_count = static_cast<uint32_t>(indices.size()),
                  .first_vertex = 0,
                  .vertex_count = static_cast<uint32_t>(vertices.size()),
                })
                .EndSubMesh()
                .Build();

  GeometryAssetDesc geo_desc {};
  geo_desc.lod_count = 1;
  const auto bb_min = mesh->BoundingBoxMin();
  const auto bb_max = mesh->BoundingBoxMax();
  geo_desc.bounding_box_min[0] = bb_min.x;
  geo_desc.bounding_box_min[1] = bb_min.y;
  geo_desc.bounding_box_min[2] = bb_min.z;
  geo_desc.bounding_box_max[0] = bb_max.x;
  geo_desc.bounding_box_max[1] = bb_max.y;
  geo_desc.bounding_box_max[2] = bb_max.z;

  return std::make_shared<oxygen::data::GeometryAsset>(
    oxygen::data::AssetKey::FromVirtualPath(
      "/Engine/Examples/Async/Geometry/GroundPlane.ogeo"),
    geo_desc, std::vector<std::shared_ptr<Mesh>> { std::move(mesh) });
}

using MeshBuffers = std::pair<std::vector<Vertex>, std::vector<std::uint32_t>>;

auto BuildPrimitive(const std::string& name, MeshBuffers buffers,
  const std::shared_ptr<const oxygen::data::MaterialAsset>& material)
  -> std::shared_ptr<oxygen::data::GeometryAsset>
{
  using oxygen::data::pak::geometry::MeshViewDesc;
  auto mesh
    = oxygen::data::MeshBuilder(0U, name)
        .WithVertices(buffers.first)
        .WithIndices(buffers.second)
        .BeginSubMesh("surface", material)
        .WithMeshView(MeshViewDesc {
          .first_index = 0U,
          .index_count = static_cast<std::uint32_t>(buffers.second.size()),
          .first_vertex = 0U,
          .vertex_count = static_cast<std::uint32_t>(buffers.first.size()),
        })
        .EndSubMesh()
        .Build();
  auto description = oxygen::data::pak::geometry::GeometryAssetDesc {};
  description.lod_count = 1U;
  const auto minimum = mesh->BoundingBoxMin();
  const auto maximum = mesh->BoundingBoxMax();
  description.bounding_box_min[0] = minimum.x;
  description.bounding_box_min[1] = minimum.y;
  description.bounding_box_min[2] = minimum.z;
  description.bounding_box_max[0] = maximum.x;
  description.bounding_box_max[1] = maximum.y;
  description.bounding_box_max[2] = maximum.z;
  return std::make_shared<oxygen::data::GeometryAsset>(
    oxygen::data::AssetKey::FromVirtualPath(
      "/Engine/Examples/Async/Geometry/" + name + ".ogeo"),
    description, std::vector<std::shared_ptr<Mesh>> { std::move(mesh) });
}

// Convert hue [0,1] to an RGB color (simple H->RGB approx)
auto ColorFromHue(double h) -> glm::vec3
{
  // NOLINTBEGIN(*-magic-numbers)
  // h in [0,1)
  const double hh = std::fmod(h, 1.0);
  const double r = std::abs((hh * 6.0) - 3.0) - 1.0;
  const double g = 2.0 - std::abs((hh * 6.0) - 2.0);
  const double b = 2.0 - std::abs((hh * 6.0) - 4.0);
  return { static_cast<float>(std::clamp(r, 0.0, 1.0)),
    static_cast<float>(std::clamp(g, 0.0, 1.0)),
    static_cast<float>(std::clamp(b, 0.0, 1.0)) };
  // NOLINTEND(*-magic-numbers)
}

// Orbit sphere around origin on XY plane with custom radius (Z-up).
auto AnimateSphereOrbit(oxygen::examples::async::SphereState& sphere,
  const double elapsed_seconds) -> void
{
  auto& sphere_node = sphere.node;
  const auto angle = sphere.base_angle + (sphere.speed * elapsed_seconds);
  const auto radius = sphere.radius;
  const auto inclination = sphere.inclination;
  const auto spin_angle
    = sphere.base_spin_angle + (sphere.spin_speed * elapsed_seconds);
  // Position in XY plane first (Z-up orbit, z=0)
  const double x = radius * std::cos(angle);
  const double y = radius * std::sin(angle);
  // Tilt the orbital plane by applying a rotation around the X axis
  const glm::dvec3 pos_local(x, y, 0.0);
  const double ci = std::cos(inclination);
  const double si = std::sin(inclination);
  // Rotation matrix for tilt around X: [1 0 0; 0 ci -si; 0 si ci]
  const glm::dvec3 pos_tilted(pos_local.x,
    (pos_local.y * ci) - (pos_local.z * si),
    (pos_local.y * si) + (pos_local.z * ci));
  const auto pos = sphere.orbit_center + glm::vec3(pos_tilted);

  if (!sphere_node.IsAlive()) {
    return;
  }

  // Set translation
  sphere_node.GetTransform().SetLocalPosition(pos);

  // Apply self-rotation (spin) around local Z axis
  const glm::quat spin_quat
    = glm::angleAxis(static_cast<float>(spin_angle), oxygen::space::move::Up);
  sphere_node.GetTransform().SetLocalRotation(spin_quat);
}

} // namespace

namespace oxygen::examples::async {

auto AsyncScene::Populate(scene::Scene& scene) -> void
{
  PopulateEnvironment(scene);
  constexpr glm::vec4 kBlueOverride { 0.2F, 0.3F, 1.0F, 1.0F };
  blue_override_ = MakeSolidColorMaterial(
    "BlueOverride", kBlueOverride, data::MaterialDomain::kOpaque, true);
  // NOLINTBEGIN(*-magic-numbers)
  // Create a LOD sphere and a multi-submesh quad
  auto sphere_geo = BuildSphereLodAsset();
  auto quad2sm_geo = BuildTwoSubmeshQuadAsset();
  auto ground_geo = BuildGroundPlaneAsset();

  constexpr std::size_t kMaterialGridWidth = 4U;
  constexpr std::size_t kOpaqueSphereCount
    = kMaterialGridWidth * kMaterialGridWidth;
  constexpr std::size_t kTransparentSphereCount = 4U;
  constexpr auto kNumSpheres = kOpaqueSphereCount + kTransparentSphereCount;
  constexpr float kMaterialSpacing = 5.0F;
  constexpr float kMaterialGridHalfWidth = 1.5F;
  constexpr float kMaterialHeight = 3.0F;
  constexpr float kTransparentHeight = 6.0F;
  constexpr double kMaterialOrbitRadius = 0.35;
  constexpr double kTransparentOrbitRadius = 16.0;
  constexpr double kTransparentOrbitSpeed = 0.25;
  spheres_.reserve(kNumSpheres);
  for (std::size_t i = 0; i < kNumSpheres; ++i) {
    const std::string name = std::string("Sphere_") + std::to_string(i);
    auto node = scene.CreateNode(name);
    node.GetRenderable().SetGeometry(sphere_geo);
    SetShadowParticipation(node, true, true);

    // Keep a full sphere diameter between the material-grid centers.
    if (node.IsAlive()) {
      node.GetTransform().SetLocalScale(glm::vec3(3.0F));
    }

    // Use the detailed mesh nearby and the coarse mesh along the drone path.
    {
      auto r = node.GetRenderable();
      DistancePolicy pol;
      pol.thresholds = { 24.0F };
      pol.hysteresis_ratio = 0.08F; // modest hysteresis to avoid flicker
      r.SetLodPolicy(std::move(pol));
    }

    // Fixed phases and material rows make lighting comparisons repeatable.
    constexpr auto two_pi = glm::two_pi<double>();
    const double fraction
      = static_cast<double>(i) / static_cast<double>(kNumSpheres);
    const bool is_transparent = i >= kOpaqueSphereCount;
    const auto material_row = i / kMaterialGridWidth;
    const auto material_column = i % kMaterialGridWidth;
    const double init_angle = is_transparent
      ? two_pi * static_cast<double>(i - kOpaqueSphereCount)
        / static_cast<double>(kTransparentSphereCount)
      : two_pi * fraction;
    const double speed
      = is_transparent ? kTransparentOrbitSpeed : 0.2 + fraction;
    const double radius
      = is_transparent ? kTransparentOrbitRadius : kMaterialOrbitRadius;

    // Opaque PBR samples and alpha-blended dielectrics have separate roles.
    auto r = node.GetRenderable();
    const std::string mat_name = std::string("SphereMat_") + std::to_string(i);
    const double hue = fraction;
    const auto rgb
      = is_transparent ? ColorFromHue(hue) : glm::vec3(0.85F, 0.57F, 0.21F);
    const float alpha = is_transparent ? 0.35F : 1.0F;
    const auto domain = is_transparent ? data::MaterialDomain::kAlphaBlended
                                       : data::MaterialDomain::kOpaque;
    const glm::vec4 color(rgb.x, rgb.y, rgb.z, alpha);
    const float roughness = is_transparent
      ? 0.2F
      : 0.08F + (0.84F * static_cast<float>(material_column) / 3.0F);
    const float metalness
      = is_transparent ? 0.0F : static_cast<float>(material_row) / 3.0F;
    const auto mat = MakeSolidColorMaterial(mat_name.c_str(), color, domain,
      false, { .metalness = metalness, .roughness = roughness });
    // Apply override for submesh index 0 across all LODs so switching LOD
    // retains the material override. Use EffectiveLodCount() to iterate.
    const auto lod_count = static_cast<std::size_t>(r.EffectiveLodCount());
    for (std::size_t lod = 0; lod < lod_count; ++lod) {
      r.SetMaterialOverride(lod, 0, mat);
    }

    SphereState s;
    s.node = node;
    s.orbit_center = is_transparent
      ? glm::vec3(0.0F, 0.0F, kTransparentHeight)
      : glm::vec3((static_cast<float>(material_column) - kMaterialGridHalfWidth)
            * kMaterialSpacing,
          (static_cast<float>(material_row) - kMaterialGridHalfWidth)
            * kMaterialSpacing,
          kMaterialHeight);
    s.base_angle = init_angle;
    s.speed = speed;
    s.radius = radius;
    s.inclination = is_transparent
      ? 0.0
      : kSphereMaxOrbitInclination * std::sin(init_angle);
    s.spin_speed = -2.0 + (4.0 * fraction);
    s.base_spin_angle = 0.0;
    AnimateSphereOrbit(s, 0.0);
    spheres_.push_back(std::move(s));
  }

  // Multi-submesh quad centered at origin facing +Z (already in XY plane)
  multisubmesh_ = scene.CreateNode("MultiSubmesh");
  multisubmesh_.GetRenderable().SetGeometry(quad2sm_geo);
  SetShadowParticipation(multisubmesh_, false, true);
  multisubmesh_.GetTransform().SetLocalPosition(
    glm::vec3(0.0F, 0.0F, kTwoSubmeshTrianglesHeight));
  multisubmesh_.GetTransform().SetLocalRotation(glm::quat(1, 0, 0, 0));

  auto ground = scene.CreateNode("AsyncGroundPlane");
  ground.GetRenderable().SetGeometry(ground_geo);
  SetShadowParticipation(ground, false, true);
  ground.GetTransform().SetLocalPosition(glm::vec3(0.0F, 0.0F, kGroundPlaneZ));
  ground.GetTransform().SetLocalScale(
    glm::vec3(kGroundHalfExtent, kGroundHalfExtent, 1.0F));

  const auto hero_material = MakeSolidColorMaterial("HeroCopper",
    { 0.95F, 0.64F, 0.54F, 1.0F }, data::MaterialDomain::kOpaque, false,
    { .metalness = 1.0F, .roughness = 0.18F });
  auto torus = data::MakeTorusMeshAsset();
  CHECK_F(torus.has_value());
  hero_ = scene.CreateNode("Copper torus");
  hero_.GetRenderable().SetGeometry(
    BuildPrimitive("Torus", std::move(torus).value(), hero_material));
  hero_.GetTransform().SetLocalPosition({ -11.0F, 0.0F, 8.0F });
  hero_.GetTransform().SetLocalScale(glm::vec3(5.0F));
  SetShadowParticipation(hero_, true, true);

  const auto slat_material
    = MakeSolidColorMaterial("ShadowScreen", { 0.24F, 0.27F, 0.30F, 1.0F });
  auto cube = data::MakeCubeMeshAsset();
  CHECK_F(cube.has_value());
  const auto slat_geometry
    = BuildPrimitive("Slat", std::move(cube).value(), slat_material);
  constexpr unsigned kSlatCount = 8U;
  for (unsigned index = 0U; index < kSlatCount; ++index) {
    auto slat = scene.CreateNode("Shadow slat " + std::to_string(index));
    slat.GetRenderable().SetGeometry(slat_geometry);
    slat.GetTransform().SetLocalPosition(
      { -7.0F + (2.0F * static_cast<float>(index)), 12.0F, 3.0F });
    slat.GetTransform().SetLocalScale({ 0.35F, 0.45F, 6.0F });
    SetShadowParticipation(slat, true, true);
  }

  // Create and register staged main camera so publish can hand it to DemoShell.
  main_camera_ = scene.CreateNode("MainCamera");
  {
    auto camera = std::make_unique<PerspectiveCamera>();
    constexpr float kVerticalFovDegrees = 45.0F;
    constexpr float kNearPlaneMeters = 0.1F;
    constexpr float kFarPlaneMeters = 600.0F;
    camera->SetFieldOfView(glm::radians(kVerticalFovDegrees));
    camera->SetNearPlane(kNearPlaneMeters);
    camera->SetFarPlane(kFarPlaneMeters);
    const bool attached = main_camera_.AttachCamera(std::move(camera));
    CHECK_F(attached, "Failed to attach PerspectiveCamera to MainCamera");
    auto tf = main_camera_.GetTransform();
    tf.SetLocalPosition(kInitialCameraPosition);
    tf.SetLocalRotation(glm::quatLookAtRH(
      glm::normalize(kSceneFocusPoint - kInitialCameraPosition),
      space::move::Up));
  }

  sun_light_ = EnsureDefaultSceneLighting(
    scene, { .sun_position = kSunPosition, .focus_point = kSceneFocusPoint });
  // NOLINTEND(*-magic-numbers)
}

auto AsyncScene::PopulateEnvironment(scene::Scene& scene) -> void
{
  scene.SetEnvironment(std::make_unique<scene::SceneEnvironment>());

  const auto environment = scene.GetEnvironment();
  if (environment == nullptr) {
    return;
  }

  auto& atmosphere
    = environment->AddSystem<scene::environment::SkyAtmosphere>();
  atmosphere.SetEnabled(true);
  atmosphere.SetTransformMode(scene::environment::SkyAtmosphereTransformMode::
      kPlanetTopAtAbsoluteWorldOrigin);
  atmosphere.SetRenderInMainPass(true);
  constexpr float kAerialPerspectiveStartMeters = 30.0F;
  constexpr float kAerialScatteringStrength = 0.45F;
  atmosphere.SetAerialPerspectiveStartDepthMeters(
    kAerialPerspectiveStartMeters);
  atmosphere.SetAerialScatteringStrength(kAerialScatteringStrength);

  auto& sky_light = environment->AddSystem<scene::environment::SkyLight>();
  sky_light.SetEnabled(true);
  sky_light.SetSource(scene::environment::SkyLightSource::kCapturedScene);
  sky_light.SetIntensityMul(1.0F);
  sky_light.SetTintRgb({ 1.0F, 1.0F, 1.0F });
  sky_light.SetDiffuseIntensity(1.0F);
  sky_light.SetSpecularIntensity(1.0F);
  sky_light.SetLowerHemisphereColor({ 0.0F, 0.0F, 0.0F });
  sky_light.SetVolumetricScatteringIntensity(1.0F);
  sky_light.SetAffectReflections(true);

  auto& fog = environment->AddSystem<scene::environment::Fog>();
  fog.SetEnabled(true);
  fog.SetEnableHeightFog(true);
  fog.SetEnableVolumetricFog(false);
  fog.SetRenderInMainPass(true);
  fog.SetVisibleInReflectionCaptures(true);
  fog.SetVisibleInRealTimeSkyCaptures(true);
  constexpr float kFogExtinctionPerMeter = 0.0007F;
  constexpr float kFogFalloffPerMeter = 0.08F;
  constexpr float kFogMaxOpacity = 0.65F;
  fog.SetExtinctionSigmaTPerMeter(kFogExtinctionPerMeter);
  fog.SetHeightFalloffPerMeter(kFogFalloffPerMeter);
  fog.SetHeightOffsetMeters(0.0F);
  fog.SetStartDistanceMeters(0.0F);
  fog.SetMaxOpacity(kFogMaxOpacity);
  fog.SetFogInscatteringLuminance({ 0.0F, 0.0F, 0.0F });
  fog.SetSkyAtmosphereAmbientContributionColorScale({ 1.0F, 1.0F, 1.0F });
  fog.SetDirectionalInscatteringLuminance({ 0.0F, 0.0F, 0.0F });
  fog.SetDirectionalInscatteringStartDistance(0.0F);
}

auto AsyncScene::Animate(const double elapsed_seconds) -> void
{
  // NOLINTBEGIN(*-magic-numbers)
  constexpr auto two_pi = glm::two_pi<double>();
  const auto anim_time_ = elapsed_seconds;
  for (auto& s : spheres_) {
    AnimateSphereOrbit(s, anim_time_);
  }

  if (hero_.IsAlive()) {
    constexpr double kHeroAngularSpeed = 0.3;
    const auto angle = static_cast<float>(elapsed_seconds * kHeroAngularSpeed);
    hero_.GetTransform().SetLocalRotation(
      glm::angleAxis(angle, oxygen::space::move::Right)
      * glm::angleAxis(angle, oxygen::space::move::Up));
  }
  if (multisubmesh_.IsAlive()) {
    constexpr double kQuadSpinSpeed = 0.6; // radians/sec
    const double quad_angle = std::fmod(anim_time_ * kQuadSpinSpeed, two_pi);
    const glm::quat quad_rot
      = glm::angleAxis(static_cast<float>(quad_angle), space::move::Up);
    multisubmesh_.GetTransform().SetLocalRotation(quad_rot);
  }

  // NOLINTEND(*-magic-numbers)
}

auto AsyncScene::UpdateMaterials(const double elapsed_seconds) -> void
{
  // NOLINTBEGIN(*-magic-numbers)
  // Toggle per-submesh visibility and material override over time
  if (multisubmesh_.IsAlive()) {
    auto r = multisubmesh_.GetRenderable();
    constexpr std::size_t lod = 0;

    // Every 2 seconds, toggle submesh 0 visibility
    int vis_phase = static_cast<int>(elapsed_seconds) / 2;
    if (vis_phase != last_vis_toggle_) {
      last_vis_toggle_ = vis_phase;
      const bool visible = (vis_phase % 2) == 0;
      r.SetSubmeshVisible(lod, 0, visible);
      LOG_F(2, "[MultiSubmesh] Submesh 0 visibility -> {}", visible);
    }

    // Every second, toggle an override on submesh 1 (use blue instead of
    // green)
    int ovr_phase = static_cast<int>(elapsed_seconds);
    if (ovr_phase != last_ovr_toggle_) {
      last_ovr_toggle_ = ovr_phase;
      const bool apply_override = (ovr_phase % 2) == 1;
      if (apply_override) {
        r.SetMaterialOverride(lod, 1, blue_override_);
      } else {
        r.ClearMaterialOverride(lod, 1);
      }
      LOG_F(2, "[MultiSubmesh] Submesh 1 override -> {}",
        apply_override ? "blue" : "clear");
    }
  }
  // NOLINTEND(*-magic-numbers)
}

} // namespace oxygen::examples::async
