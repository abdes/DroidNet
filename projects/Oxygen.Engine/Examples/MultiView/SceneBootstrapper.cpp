//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Scene/Environment/Fog.h>
#include <Oxygen/Scene/Environment/LocalFogVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Environment/SkyLight.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Light/PointLight.h>
#include <Oxygen/Scene/Light/SpotLight.h>
#include <Oxygen/Scene/SceneFlags.h>
#include <Oxygen/Scene/Types/Flags.h>

#include "MultiView/SceneBootstrapper.h"

namespace oxygen::examples::multiview {
namespace {
  void SetShadowParticipation(scene::SceneNode& node, const bool casts_shadows,
    const bool receives_shadows)
  {
    if (auto flags_ref = node.GetFlags(); flags_ref.has_value()) {
      auto& flags = flags_ref->get();
      flags = flags.SetFlag(scene::SceneNodeFlags::kCastsShadows,
        scene::SceneFlag {}.SetEffectiveValueBit(casts_shadows));
      flags = flags.SetFlag(scene::SceneNodeFlags::kReceivesShadows,
        scene::SceneFlag {}.SetEffectiveValueBit(receives_shadows));
    }
  }

  auto MakeSolidColorMaterial(const char* name, const glm::vec4& rgba,
    data::MaterialDomain domain = data::MaterialDomain::kOpaque,
    bool emission_only = false, float emissive_scale = 4.0F)
    -> std::shared_ptr<const data::MaterialAsset>
  {
    using data::AssetKey;
    using data::AssetType;
    using data::MaterialAsset;
    using data::MaterialDomain;
    using data::ShaderReference;
    using data::Unorm16;
    namespace pak = data::pak;

    pak::render::MaterialAssetDesc desc {};
    desc.header.asset_type = static_cast<uint8_t>(AssetType::kMaterial);
    constexpr std::size_t maxn = sizeof(desc.header.name) - 1;
    const std::size_t n = (std::min)(maxn, std::strlen(name));
    std::memcpy(desc.header.name, name, n);
    desc.header.name[n] = '\0';
    desc.header.version = 1;
    desc.header.streaming_priority = 255;
    desc.material_domain = static_cast<uint8_t>(domain);
    desc.flags = pak::render::kMaterialFlag_NoTextureSampling;
    desc.shader_stages = 0;
    desc.base_color[0] = rgba.r;
    desc.base_color[1] = rgba.g;
    desc.base_color[2] = rgba.b;
    desc.base_color[3] = rgba.a;
    desc.normal_scale = 1.0F;
    desc.metalness = Unorm16 { 0.0F };
    desc.roughness = Unorm16 { 0.5F };
    desc.ambient_occlusion = Unorm16 { 1.0F };
    if (emission_only) {
      // Keep the lit shader path (including AP). Direct-light isolation is
      // established by backlighting the cards, not by unsupported material
      // knobs.
      for (unsigned channel = 0; channel < 3U; ++channel) {
        desc.emissive_factor[channel]
          = data::HalfFloat { emissive_scale * rgba[channel] };
        desc.base_color[channel] = 0.0F;
      }
    }

    const AssetKey asset_key = AssetKey::FromVirtualPath(
      "/Engine/Examples/MultiView/Materials/" + std::string(name) + ".omat");
    return std::make_shared<const MaterialAsset>(
      asset_key, desc, std::vector<ShaderReference> {});
  }

} // namespace

void SceneBootstrapper::BindToScene(observer_ptr<scene::Scene> scene)
{
  scene_ = scene;
  if (!scene_) {
    sphere_node_ = {};
    cube_node_ = {};
    cylinder_node_ = {};
    cone_node_ = {};
    key_light_node_ = {};
    fill_light_node_ = {};
    proof_sun_node_ = {};
    visual_local_fog_node_ = {};
    atmosphere_proof_phase_ = ~0U;
  }
}

auto SceneBootstrapper::EnsureSceneWithContent() -> observer_ptr<scene::Scene>
{
  auto* scene = scene_.get();
  if (scene == nullptr) {
    return observer_ptr<scene::Scene> { nullptr };
  }

  EnsureSphere(*scene);
  EnsureCube(*scene);
  EnsureCylinder(*scene);
  EnsureCone(*scene);
  EnsureGroundPlane(*scene);
  EnsureLighting(*scene);
  return scene_;
}

auto SceneBootstrapper::GetScene() const -> observer_ptr<scene::Scene>
{
  return scene_;
}

auto SceneBootstrapper::EnsureProofAtmosphere(const float sun_lux,
  const float scattering_strength, const bool backlit) -> void
{
  CHECK_NOTNULL_F(scene_.get());
  if (!scene_->GetEnvironment())
    scene_->SetEnvironment(std::make_unique<scene::SceneEnvironment>());
  auto* atmosphere = scene_->GetEnvironment()
                       ->TryGetSystem<scene::environment::SkyAtmosphere>()
                       .get();
  if (!atmosphere)
    atmosphere = &scene_->GetEnvironment()
                    ->AddSystem<scene::environment::SkyAtmosphere>();
  atmosphere->SetEnabled(true);
  atmosphere->SetAerialPerspectiveStartDepthMeters(0.0F);
  atmosphere->SetAerialScatteringStrength(scattering_strength);
  if (!proof_sun_node_.IsAlive()) {
    proof_sun_node_ = scene_->CreateNode("AtmosphereProofSun");
    auto sun = std::make_unique<scene::DirectionalLight>();
    sun->SetIntensityLux(sun_lux);
    sun->SetEnvironmentContribution(true);
    sun->SetIsSunLight(true);
    sun->SetAtmosphereLightSlot(scene::AtmosphereLightSlot::kPrimary);
    sun->Common().casts_shadows = false;
    CHECK_F(proof_sun_node_.AttachLight(std::move(sun)));
    const auto direction
      = glm::normalize(glm::vec3 { 0.0F, backlit ? -.5F : .5F, -1.0F });
    proof_sun_node_.GetTransform().SetLocalRotation(
      glm::angleAxis(std::acos(glm::dot(space::move::Forward, direction)),
        glm::normalize(glm::cross(space::move::Forward, direction))));
  }
}

auto SceneBootstrapper::ApplyLitAtmosphereProof() -> void
{
  // The cards isolate emission by backlighting. The lit-material fixture must
  // light the visible faces of the original meshes instead.
  EnsureProofAtmosphere(110000.0F, 1.0F, false);
}

auto SceneBootstrapper::ApplyMixedExposureProof() -> void
{
  ApplyConsumerVisualProof(VisualFogMode::kVolumetric);
  // The original sphere remains opaque and lit. The other existing shapes
  // exercise material domains together at comparable daylight radiance.
  cube_node_.GetRenderable().SetMaterialOverride(0, 0,
    MakeSolidColorMaterial("MixedExposureEmissive", { .7F, .65F, .5F, 1 },
      data::MaterialDomain::kOpaque, true, 4096));
  cylinder_node_.GetRenderable().SetMaterialOverride(0, 0,
    MakeSolidColorMaterial("MixedExposureTranslucent", { .3F, .4F, .9F, .5F },
      data::MaterialDomain::kAlphaBlended));
  cone_node_.GetRenderable().SetMaterialOverride(0, 0,
    MakeSolidColorMaterial("MixedExposureMasked", { .9F, .4F, .4F, .8F },
      data::MaterialDomain::kMasked));
}

auto SceneBootstrapper::ApplyConsumerVisualProof(const VisualFogMode fog_mode)
  -> void
{
  ApplyLitAtmosphereProof();
  auto& environment = *scene_->GetEnvironment();
  auto* sky_light
    = environment.TryGetSystem<scene::environment::SkyLight>().get();
  if (!sky_light)
    sky_light = &environment.AddSystem<scene::environment::SkyLight>();
  sky_light->SetEnabled(true);
  sky_light->SetDiffuseIntensity(1.0F);
  sky_light->SetSpecularIntensity(0.0F);
  sky_light->SetVolumetricScatteringIntensity(1.0F);
  // The existing atmosphere LUT supplies volumetric ambient independently of
  // captured-scene surface IBL, which this renderer does not yet provide.
  ground_plane_node_.GetTransform().SetLocalScale({ 2000.0F, 2000.0F, 0.1F });
  if (auto sun = proof_sun_node_.GetLightAs<scene::DirectionalLight>(); sun)
    sun->get().Common().casts_shadows = true;
  auto* fog = environment.TryGetSystem<scene::environment::Fog>().get();
  if (!fog)
    fog = &environment.AddSystem<scene::environment::Fog>();
  const bool volumetric = fog_mode == VisualFogMode::kVolumetric;
  fog->SetEnabled(volumetric);
  fog->SetEnableHeightFog(volumetric);
  fog->SetEnableVolumetricFog(volumetric);
  fog->SetExtinctionSigmaTPerMeter(0.06F);
  fog->SetHeightFalloffPerMeter(0.12F);
  fog->SetHeightOffsetMeters(0.0F);
  fog->SetStartDistanceMeters(0.0F);
  fog->SetFogInscatteringLuminance({ 300.0F, 450.0F, 650.0F });
  fog->SetVolumetricFogAlbedo({ 0.4F, 0.5F, 0.6F });
  fog->SetVolumetricFogEmissive({ 0.0F, 0.0F, 0.0F });
  fog->SetVolumetricFogDistance(40.0F);
  fog->SetVolumetricFogStartDistance(0.0F);
  fog->SetVolumetricFogNearFadeInDistance(1.0F);
  fog->SetVolumetricFogScatteringDistribution(0.1F);
  if (!visual_local_fog_node_.IsAlive()) {
    visual_local_fog_node_ = scene_->CreateNode("VisualReviewLocalFog");
    const auto node = visual_local_fog_node_.GetImpl();
    CHECK_F(node.has_value());
    node->get().AddComponent<scene::environment::LocalFogVolume>();
    visual_local_fog_node_.GetTransform().SetLocalPosition(
      { -1.75F, -0.25F, 0.3F });
    visual_local_fog_node_.GetTransform().SetLocalScale(
      { 0.45F, 0.45F, 0.3F });
  }
  auto& local = visual_local_fog_node_.GetImpl()
                  ->get()
                  .GetComponent<scene::environment::LocalFogVolume>();
  local.SetEnabled(fog_mode == VisualFogMode::kLocal);
  local.SetRadialFogExtinction(0.9F);
  local.SetHeightFogExtinction(0.45F);
  local.SetHeightFogFalloff(0.5F);
  local.SetHeightFogOffset(-0.5F);
  local.SetFogAlbedo({ 0.45F, 0.6F, 0.8F });
  local.SetFogEmissive({ 0.0F, 0.0F, 0.0F });
  local.SetFogPhaseG(0.1F);
}

auto SceneBootstrapper::ApplyAtmosphereProof(const std::uint64_t frame) -> void
{
  const std::uint32_t phase = frame < 44U ? 0U
    : frame < 48U                         ? 1U
    : frame < 52U                         ? 2U
    : frame < 56U                         ? 3U
                                          : 4U;
  EnsureProofAtmosphere(1000.0F, phase >= 3U ? 8.0F : 0.01F);
  if (phase == atmosphere_proof_phase_)
    return;
  const std::array nodes { sphere_node_, cube_node_, cylinder_node_, cone_node_,
    ground_plane_node_ };
  if (phase == 0U) {
    auto quad = data::MakeQuadMeshAsset(1.5F, 2.0F);
    CHECK_F(quad.has_value());
    auto mesh
      = data::MeshBuilder(0, "Atmosphere proof card")
          .WithVertices(quad->first)
          .WithIndices(quad->second)
          .BeginSubMesh("card",
            MakeSolidColorMaterial("AtmosphereProofCardBase", { 0, 0, 0, 1 }))
          .WithMeshView(data::pak::geometry::MeshViewDesc { .first_index = 0,
            .index_count = static_cast<std::uint32_t>(quad->second.size()),
            .first_vertex = 0,
            .vertex_count = static_cast<std::uint32_t>(quad->first.size()) })
          .EndSubMesh()
          .Build();
    data::pak::geometry::GeometryAssetDesc desc {};
    desc.lod_count = 1;
    desc.bounding_box_min[0] = -.75F;
    desc.bounding_box_min[2] = -1.0F;
    desc.bounding_box_max[0] = .75F;
    desc.bounding_box_max[2] = 1.0F;
    auto geometry = std::make_shared<data::GeometryAsset>(
      data::AssetKey::FromVirtualPath(
        "/Engine/Examples/MultiView/Geometry/AtmosphereProofCard.ogeo"),
      desc, std::vector<std::shared_ptr<data::Mesh>> { std::move(mesh) });
    for (std::size_t i = 0; i < nodes.size(); ++i) {
      auto node = nodes[i];
      node.GetRenderable().SetGeometry(geometry);
      node.GetTransform().SetLocalRotation(glm::quat { 1, 0, 0, 0 });
      node.GetTransform().SetLocalPosition(i < 4U
          ? glm::vec3 { -3.0F + 2.0F * static_cast<float>(i), 0, 0 }
          : glm::vec3 { 0, 2, 0 });
      node.GetTransform().SetLocalScale(
        i < 4U ? glm::vec3 { 1 } : glm::vec3 { 6, 1, 2 });
    }
  }
  const std::array colors { glm::vec4 { .2F, .7F, .3F, 1 },
    glm::vec4 { .7F, .7F, .7F, 1 }, glm::vec4 { .4F, .4F, .9F, 1 },
    glm::vec4 { .9F, .4F, .4F, 1 }, glm::vec4 { .18F, .18F, .18F, 1 } };
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    auto node = nodes[i];
    const bool forward = (phase == 1U && i < 4U) || (phase == 2U && i == 0U);
    auto color = colors[i];
    if (phase == 2U && i == 0U)
      color.a = .5F;
    const auto name
      = "AtmosphereProof" + std::to_string(phase) + "-" + std::to_string(i);
    auto material = MakeSolidColorMaterial(name.c_str(), color,
      forward ? data::MaterialDomain::kAlphaBlended
              : data::MaterialDomain::kOpaque,
      true);
    node.GetRenderable().SetMaterialOverride(0U, 0U, std::move(material));
    SetShadowParticipation(node, false, false);
  }
  atmosphere_proof_phase_ = phase;
  LOG_F(INFO,
    "Vortex.MultiView.AtmosphereProof frame={} phase={} (0=deferred, "
    "1=translucent-forward, 2=mixed, 3=opaque-reference, 4=opaque-forward)",
    frame, phase);
}

auto SceneBootstrapper::GetSphereNode() const -> scene::SceneNode
{
  return sphere_node_;
}

auto SceneBootstrapper::GetCubeNode() const -> scene::SceneNode
{
  return cube_node_;
}

auto SceneBootstrapper::GetCylinderNode() const -> scene::SceneNode
{
  return cylinder_node_;
}

auto SceneBootstrapper::GetConeNode() const -> scene::SceneNode
{
  return cone_node_;
}

auto SceneBootstrapper::GetGroundPlaneNode() const -> scene::SceneNode
{
  return ground_plane_node_;
}

auto SceneBootstrapper::EnsureSphere(scene::Scene& scene) -> void
{
  if (sphere_node_.IsAlive()) {
    return;
  }

  auto sphere_geom_data = data::MakeSphereMeshAsset(32, 32);
  if (!sphere_geom_data.has_value()) {
    LOG_F(WARNING,
      "[MultiView] SceneBootstrapper failed to create sphere mesh data.");
    return;
  }

  auto material
    = MakeSolidColorMaterial("SphereMaterial", { 0.2F, 0.7F, 0.3F, 1.0F });

  using data::MeshBuilder;
  using data::pak::geometry::GeometryAssetDesc;
  using data::pak::geometry::MeshViewDesc;

  auto mesh
    = MeshBuilder(0, "Sphere")
        .WithVertices(sphere_geom_data->first)
        .WithIndices(sphere_geom_data->second)
        .BeginSubMesh("full", material)
        .WithMeshView(MeshViewDesc {
          .first_index = 0,
          .index_count = static_cast<uint32_t>(sphere_geom_data->second.size()),
          .first_vertex = 0,
          .vertex_count = static_cast<uint32_t>(sphere_geom_data->first.size()),
        })
        .EndSubMesh()
        .Build();

  GeometryAssetDesc geo_desc {};
  geo_desc.lod_count = 1;
  const glm::vec3 bb_min = mesh->BoundingBoxMin();
  const glm::vec3 bb_max = mesh->BoundingBoxMax();
  geo_desc.bounding_box_min[0] = bb_min.x;
  geo_desc.bounding_box_min[1] = bb_min.y;
  geo_desc.bounding_box_min[2] = bb_min.z;
  geo_desc.bounding_box_max[0] = bb_max.x;
  geo_desc.bounding_box_max[1] = bb_max.y;
  geo_desc.bounding_box_max[2] = bb_max.z;

  auto geom_asset = std::make_shared<data::GeometryAsset>(
    data::AssetKey::FromVirtualPath(
      "/Engine/Examples/MultiView/Geometry/Sphere.ogeo"),
    geo_desc, std::vector<std::shared_ptr<data::Mesh>> { std::move(mesh) });

  sphere_node_ = scene.CreateNode("Sphere");
  sphere_node_.GetRenderable().SetGeometry(std::move(geom_asset));
  SetShadowParticipation(sphere_node_, true, true);
  sphere_node_.GetTransform().SetLocalPosition({ -2.0F, 1.0F, 0.0F });

  LOG_F(INFO,
    "[MultiView] SceneBootstrapper created sphere node (alive={}, "
    "geom_set={}).",
    sphere_node_.IsAlive(),
    sphere_node_.GetRenderable().GetGeometry() != nullptr);
}

auto SceneBootstrapper::EnsureCube(scene::Scene& scene) -> void
{
  if (cube_node_.IsAlive()) {
    return;
  }

  auto cube_geom_data = data::MakeCubeMeshAsset();
  if (!cube_geom_data.has_value()) {
    LOG_F(WARNING,
      "[MultiView] SceneBootstrapper failed to create cube mesh data.");
    return;
  }

  auto material
    = MakeSolidColorMaterial("CubeMaterial", { 0.7F, 0.7F, 0.7F, 1.0F });

  using data::MeshBuilder;
  using data::pak::geometry::GeometryAssetDesc;
  using data::pak::geometry::MeshViewDesc;

  auto mesh
    = MeshBuilder(0, "Cube")
        .WithVertices(cube_geom_data->first)
        .WithIndices(cube_geom_data->second)
        .BeginSubMesh("full", material)
        .WithMeshView(MeshViewDesc {
          .first_index = 0,
          .index_count = static_cast<uint32_t>(cube_geom_data->second.size()),
          .first_vertex = 0,
          .vertex_count = static_cast<uint32_t>(cube_geom_data->first.size()),
        })
        .EndSubMesh()
        .Build();

  GeometryAssetDesc geo_desc {};
  geo_desc.lod_count = 1;
  const glm::vec3 bb_min = mesh->BoundingBoxMin();
  const glm::vec3 bb_max = mesh->BoundingBoxMax();
  geo_desc.bounding_box_min[0] = bb_min.x;
  geo_desc.bounding_box_min[1] = bb_min.y;
  geo_desc.bounding_box_min[2] = bb_min.z;
  geo_desc.bounding_box_max[0] = bb_max.x;
  geo_desc.bounding_box_max[1] = bb_max.y;
  geo_desc.bounding_box_max[2] = bb_max.z;

  auto geom_asset = std::make_shared<data::GeometryAsset>(
    data::AssetKey::FromVirtualPath(
      "/Engine/Examples/MultiView/Geometry/Cube.ogeo"),
    geo_desc, std::vector<std::shared_ptr<data::Mesh>> { std::move(mesh) });

  cube_node_ = scene.CreateNode("Cube");
  cube_node_.GetRenderable().SetGeometry(std::move(geom_asset));
  SetShadowParticipation(cube_node_, true, true);
  // Place the cube to the right of the sphere
  cube_node_.GetTransform().SetLocalPosition({ 1.0F, -1.0F, 0.0F });

  LOG_F(INFO,
    "[MultiView] SceneBootstrapper created cube node (alive={}, geom_set={}).",
    cube_node_.IsAlive(), cube_node_.GetRenderable().GetGeometry() != nullptr);
}

auto SceneBootstrapper::EnsureCylinder(scene::Scene& scene) -> void
{
  if (cylinder_node_.IsAlive()) {
    return;
  }

  // Use a reasonable default — 16 segments, height 1.0, radius 0.5
  auto cyl_data = data::MakeCylinderMeshAsset(16U, 1.0F, 0.5F);
  if (!cyl_data.has_value()) {
    LOG_F(WARNING,
      "[MultiView] SceneBootstrapper failed to create cylinder mesh data.");
    return;
  }

  auto material
    = MakeSolidColorMaterial("CylinderMaterial", { 0.4F, 0.4F, 0.9F, 1.0F });

  using data::MeshBuilder;
  using data::pak::geometry::GeometryAssetDesc;
  using data::pak::geometry::MeshViewDesc;

  auto mesh = MeshBuilder(0, "Cylinder")
                .WithVertices(cyl_data->first)
                .WithIndices(cyl_data->second)
                .BeginSubMesh("full", material)
                .WithMeshView(MeshViewDesc {
                  .first_index = 0,
                  .index_count = static_cast<uint32_t>(cyl_data->second.size()),
                  .first_vertex = 0,
                  .vertex_count = static_cast<uint32_t>(cyl_data->first.size()),
                })
                .EndSubMesh()
                .Build();

  GeometryAssetDesc geo_desc {};
  geo_desc.lod_count = 1;
  const glm::vec3 bb_min = mesh->BoundingBoxMin();
  const glm::vec3 bb_max = mesh->BoundingBoxMax();
  geo_desc.bounding_box_min[0] = bb_min.x;
  geo_desc.bounding_box_min[1] = bb_min.y;
  geo_desc.bounding_box_min[2] = bb_min.z;
  geo_desc.bounding_box_max[0] = bb_max.x;
  geo_desc.bounding_box_max[1] = bb_max.y;
  geo_desc.bounding_box_max[2] = bb_max.z;

  auto geom_asset = std::make_shared<data::GeometryAsset>(
    data::AssetKey::FromVirtualPath(
      "/Engine/Examples/MultiView/Geometry/Cylinder.ogeo"),
    geo_desc, std::vector<std::shared_ptr<data::Mesh>> { std::move(mesh) });

  cylinder_node_ = scene.CreateNode("Cylinder");
  cylinder_node_.GetRenderable().SetGeometry(std::move(geom_asset));
  SetShadowParticipation(cylinder_node_, true, true);
  // Place cylinder centered between cube and sphere
  cylinder_node_.GetTransform().SetLocalPosition({ -0.5F, -0.5F, 0.0F });

  // Rotate the cylinder so it's easier to inspect in 3D from the main view.
  // Apply 30° pitch and 45° yaw (converted to radians) to give a clear 3D
  // perspective.
  cylinder_node_.GetTransform().SetLocalRotation(
    glm::quat(glm::vec3(glm::radians(30.0F), glm::radians(45.0F), 0.0F)));

  LOG_F(INFO,
    "[MultiView] SceneBootstrapper created cylinder node (alive={}, "
    "geom_set={}).",
    cylinder_node_.IsAlive(),
    cylinder_node_.GetRenderable().GetGeometry() != nullptr);
}

auto SceneBootstrapper::EnsureCone(scene::Scene& scene) -> void
{
  if (cone_node_.IsAlive()) {
    return;
  }

  // Use a reasonable default for the cone — 16 segments, height 1.0, radius 0.5
  auto cone_data = data::MakeConeMeshAsset(16U, 1.0F, 0.5F);
  if (!cone_data.has_value()) {
    LOG_F(WARNING,
      "[MultiView] SceneBootstrapper failed to create cone mesh data.");
    return;
  }

  auto material
    = MakeSolidColorMaterial("ConeMaterial", { 0.9F, 0.4F, 0.4F, 1.0F });

  using data::MeshBuilder;
  using data::pak::geometry::GeometryAssetDesc;
  using data::pak::geometry::MeshViewDesc;

  auto mesh
    = MeshBuilder(0, "Cone")
        .WithVertices(cone_data->first)
        .WithIndices(cone_data->second)
        .BeginSubMesh("full", material)
        .WithMeshView(MeshViewDesc {
          .first_index = 0,
          .index_count = static_cast<uint32_t>(cone_data->second.size()),
          .first_vertex = 0,
          .vertex_count = static_cast<uint32_t>(cone_data->first.size()),
        })
        .EndSubMesh()
        .Build();

  GeometryAssetDesc geo_desc {};
  geo_desc.lod_count = 1;
  const glm::vec3 bb_min = mesh->BoundingBoxMin();
  const glm::vec3 bb_max = mesh->BoundingBoxMax();
  geo_desc.bounding_box_min[0] = bb_min.x;
  geo_desc.bounding_box_min[1] = bb_min.y;
  geo_desc.bounding_box_min[2] = bb_min.z;
  geo_desc.bounding_box_max[0] = bb_max.x;
  geo_desc.bounding_box_max[1] = bb_max.y;
  geo_desc.bounding_box_max[2] = bb_max.z;

  auto geom_asset = std::make_shared<data::GeometryAsset>(
    data::AssetKey::FromVirtualPath(
      "/Engine/Examples/MultiView/Geometry/Cone.ogeo"),
    geo_desc, std::vector<std::shared_ptr<data::Mesh>> { std::move(mesh) });

  cone_node_ = scene.CreateNode("Cone");
  cone_node_.GetRenderable().SetGeometry(std::move(geom_asset));
  SetShadowParticipation(cone_node_, true, true);
  // Place cone to the left of the cylinder so it doesn't overlap
  cone_node_.GetTransform().SetLocalPosition({ -2.5F, -0.5F, 0.0F });

  // Rotate the cone so the base (bottom face) faces the main camera
  // MainView camera lives at +Z looking down -Z; rotate 90° pitch so the
  // base normal (-Y) maps to -Z (facing the camera). Add a small yaw for
  // better perspective.
  cone_node_.GetTransform().SetLocalRotation(
    glm::quat(glm::vec3(glm::radians(30.0F), glm::radians(20.0F), 0.0F)));

  LOG_F(INFO,
    "[MultiView] SceneBootstrapper created cone node (alive={}, geom_set={}).",
    cone_node_.IsAlive(), cone_node_.GetRenderable().GetGeometry() != nullptr);
}

auto SceneBootstrapper::EnsureGroundPlane(scene::Scene& scene) -> void
{
  if (ground_plane_node_.IsAlive()) {
    return;
  }

  auto cube_geom_data = data::MakeCubeMeshAsset();
  if (!cube_geom_data.has_value()) {
    return;
  }

  // 18% Gray ground
  auto material
    = MakeSolidColorMaterial("GroundMaterial", { 0.18F, 0.18F, 0.18F, 1.0F });

  using data::MeshBuilder;
  using data::pak::geometry::GeometryAssetDesc;
  using data::pak::geometry::MeshViewDesc;

  auto mesh
    = MeshBuilder(0, "Ground")
        .WithVertices(cube_geom_data->first)
        .WithIndices(cube_geom_data->second)
        .BeginSubMesh("full", material)
        .WithMeshView(MeshViewDesc {
          .first_index = 0,
          .index_count = static_cast<uint32_t>(cube_geom_data->second.size()),
          .first_vertex = 0,
          .vertex_count = static_cast<uint32_t>(cube_geom_data->first.size()),
        })
        .EndSubMesh()
        .Build();

  GeometryAssetDesc geo_desc {};
  geo_desc.lod_count = 1;
  const glm::vec3 bb_min = mesh->BoundingBoxMin();
  const glm::vec3 bb_max = mesh->BoundingBoxMax();
  geo_desc.bounding_box_min[0] = bb_min.x;
  geo_desc.bounding_box_min[1] = bb_min.y;
  geo_desc.bounding_box_min[2] = bb_min.z;
  geo_desc.bounding_box_max[0] = bb_max.x;
  geo_desc.bounding_box_max[1] = bb_max.y;
  geo_desc.bounding_box_max[2] = bb_max.z;

  auto geom_asset = std::make_shared<data::GeometryAsset>(
    data::AssetKey::FromVirtualPath(
      "/Engine/Examples/MultiView/Geometry/GroundPlane.ogeo"),
    geo_desc, std::vector<std::shared_ptr<data::Mesh>> { std::move(mesh) });

  ground_plane_node_ = scene.CreateNode("GroundPlane");
  ground_plane_node_.GetRenderable().SetGeometry(std::move(geom_asset));
  SetShadowParticipation(ground_plane_node_, false, true);
  // Flat scale for ground
  ground_plane_node_.GetTransform().SetLocalScale({ 10.0F, 10.0F, 0.1F });
  // Position it slightly below the objects (which are at Z=0)
  ground_plane_node_.GetTransform().SetLocalPosition({ 0.0F, 0.0F, -0.55F });
}

auto SceneBootstrapper::EnsureLighting(scene::Scene& scene) -> void
{
  // Key light: Spotlight from upper-front-right, aiming at center of scene
  if (!key_light_node_.IsAlive()) {
    key_light_node_ = scene.CreateNode("KeyLight");

    auto spot_light = std::make_unique<scene::SpotLight>();
    spot_light->Common().affects_world = spot_light_enabled_;
    spot_light->Common().casts_shadows = true;
    spot_light->Common().color_rgb
      = glm::vec3(1.0F, 0.98F, 0.95F); // Warm white
    spot_light->SetLuminousFluxLm(5000.0F);
    spot_light->SetRange(250.0F);
    spot_light->SetSourceRadius(0.4F);
    spot_light->SetInnerConeAngleRadians(glm::radians(35.0F)); // Inner cone
    spot_light->SetOuterConeAngleRadians(glm::radians(45.0F)); // Outer cone

    const bool attached = key_light_node_.AttachLight(std::move(spot_light));
    CHECK_F(attached, "Failed to attach SpotLight to KeyLight node");

    // Position above and in front of the scene
    key_light_node_.GetTransform().SetLocalPosition({ 3.0F, 3.0F, 3.0F });

    // Aim toward scene center (objects are roughly at origin)
    const glm::vec3 light_pos = { 3.0F, 3.0F, 3.0F };
    const glm::vec3 target = { -0.5F, 0.0F, 0.0F };
    const glm::vec3 direction = glm::normalize(target - light_pos);
    const glm::vec3 forward = space::move::Forward;
    const float cos_theta = glm::dot(forward, direction);

    if (cos_theta < 0.9999F && cos_theta > -0.9999F) {
      const glm::vec3 axis = glm::normalize(glm::cross(forward, direction));
      const float angle = std::acos(cos_theta);
      key_light_node_.GetTransform().SetLocalRotation(
        glm::angleAxis(angle, axis));
    }

    LOG_F(INFO,
      "[MultiView] SceneBootstrapper created key light (spotlight) at "
      "(3, 3, 3).");
  }

  // Fill light: Point light from the left side for ambient fill
  if (!fill_light_node_.IsAlive()) {
    fill_light_node_ = scene.CreateNode("FillLight");

    auto point_light = std::make_unique<scene::PointLight>();
    point_light->Common().affects_world = point_light_enabled_;
    point_light->Common().casts_shadows = true;
    point_light->Common().color_rgb
      = glm::vec3(0.7F, 0.85F, 1.0F); // Cool blue tint
    point_light->SetLuminousFluxLm(2000.0F);
    point_light->SetRange(300.0F);
    point_light->SetSourceRadius(0.2F);

    const bool attached = fill_light_node_.AttachLight(std::move(point_light));
    CHECK_F(attached, "Failed to attach PointLight to FillLight node");

    // Position to the left and slightly in front, lower than key
    fill_light_node_.GetTransform().SetLocalPosition({ -2.0F, 2.0F, 2.0F });

    LOG_F(INFO,
      "[MultiView] SceneBootstrapper created fill light (point) at "
      "(-2, 2, 2).");
  }
}

} // namespace oxygen::examples::multiview
