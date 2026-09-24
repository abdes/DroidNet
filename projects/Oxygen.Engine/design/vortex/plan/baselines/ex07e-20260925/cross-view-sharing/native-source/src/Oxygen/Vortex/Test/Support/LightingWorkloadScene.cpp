//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
#include <array>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <string>

#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/Scene/Camera/Orthographic.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Light/PointLight.h>
#include <Oxygen/Scene/Light/SpotLight.h>
#include <Oxygen/Vortex/Test/Support/LightingWorkloadScene.h>

namespace oxygen::vortex::testing {
namespace {
  auto MakePlane(const float extent, const std::string& name)
    -> std::shared_ptr<data::GeometryAsset>
  {
    const auto positions = std::array {
      glm::vec3 { -extent, -extent, 0 },
      glm::vec3 { extent, -extent, 0 },
      glm::vec3 { extent, extent, 0 },
      glm::vec3 { -extent, extent, 0 },
    };
    auto vertices = std::vector<data::Vertex>(positions.size());
    for (std::size_t index = 0; index < vertices.size(); ++index) {
      vertices.at(index) = { .position = positions.at(index),
        .normal = { 0, 0, 1 },
        .texcoord = { 0.5F, 0.5F },
        .tangent = { 1, 0, 0 },
        .bitangent = { 0, 1, 0 },
        .color = { 1, 1, 1, 1 } };
    }
    std::shared_ptr<data::Mesh> mesh
      = data::MeshBuilder()
          .WithVertices(vertices)
          .WithIndices(std::vector<std::uint32_t> { 0, 1, 2, 0, 2, 3 })
          .BeginSubMesh(name, data::MaterialAsset::CreateDefault())
          .WithMeshView({ .first_index = 0,
            .index_count = 6,
            .first_vertex = 0,
            .vertex_count = 4 })
          .EndSubMesh()
          .Build();
    auto desc = data::pak::geometry::GeometryAssetDesc {};
    desc.lod_count = 1;
    desc.bounding_box_min[0] = desc.bounding_box_min[1] = -extent;
    desc.bounding_box_max[0] = desc.bounding_box_max[1] = extent;
    return std::make_shared<data::GeometryAsset>(
      data::AssetKey::FromVirtualPath("/Test/Lighting/" + name + ".ogeo"), desc,
      std::vector<std::shared_ptr<data::Mesh>> { mesh });
  }

  auto MakeMaterial() -> std::shared_ptr<data::MaterialAsset>
  {
    auto desc = data::pak::render::MaterialAssetDesc {};
    desc.flags = data::pak::render::kMaterialFlag_DoubleSided
      | data::pak::render::kMaterialFlag_NoTextureSampling;
    desc.base_color[0] = desc.base_color[1] = desc.base_color[2] = 0.5F;
    desc.base_color[3] = 1.0F;
    desc.roughness = data::Unorm16 { 0.5F };
    desc.ambient_occlusion = data::Unorm16 { 1.0F };
    desc.normal_scale = 1.0F;
    return std::make_shared<data::MaterialAsset>(
      data::AssetKey::FromVirtualPath("/Test/Lighting/Workload.omat"), desc,
      std::vector<data::ShaderReference> {});
  }
} // namespace

auto CreateLightingWorkloadScene(const LightingWorkload& workload)
  -> LightingWorkloadScene
{
  auto result = LightingWorkloadScene {};
  result.scene = std::make_shared<scene::Scene>(
    "Many-light baseline", workload.lights.size() * 2U + 4U);
  result.scene->SetEnvironment(std::make_unique<scene::SceneEnvironment>());
  auto& post = result.scene->GetEnvironment()
                 ->AddSystem<scene::environment::PostProcessVolume>();
  auto exposure = scene::ExposureSettings {};
  exposure.mode = engine::ExposureMode::kManual;
  exposure.manual_ev = 0;
  exposure.key = 12.5F;
  post.SetExposureSettings(exposure);
  post.SetToneMapper(engine::ToneMapper::kNone);
  post.SetDisplayGamma(1);
  post.SetBloomIntensity(0);
  const auto material = MakeMaterial();
  result.floor = result.scene->CreateNode("Neutral receiver floor");
  result.floor.GetRenderable().SetGeometry(
    MakePlane(workload.floor_half_extent_m, "WorkloadFloor"));
  result.floor.GetRenderable().SetMaterialOverride(0, 0, material);
  const auto caster_geometry = MakePlane(0.2F, "WorkloadOccluder");
  for (const auto& source : workload.lights) {
    auto node
      = result.scene->CreateNode("Light " + std::to_string(source.id.get()));
    node.GetTransform().SetLocalPosition({ source.position_ws.at(0),
      source.position_ws.at(1), source.position_ws.at(2) });
    const auto initialize = [&](auto& light) {
      light.Common().affects_world = source.enabled;
      light.Common().casts_shadows = source.casts_shadows;
      light.Common().color_rgb = { source.color_rgb.at(0),
        source.color_rgb.at(1), source.color_rgb.at(2) };
      light.SetLuminousFluxLm(source.flux_lm);
      light.SetRange(source.range_m);
      light.SetSourceRadius(source.source_radius_m);
    };
    if (source.kind == WorkloadLightKind::kPoint) {
      auto light = std::make_unique<scene::PointLight>();
      initialize(*light);
      if (!node.AttachLight(std::move(light))) {
        throw std::runtime_error("Cannot attach workload point light");
      }
    } else {
      auto light = std::make_unique<scene::SpotLight>();
      initialize(*light);
      light->SetInnerConeAngleRadians(source.inner_half_angle_radians);
      light->SetOuterConeAngleRadians(source.outer_half_angle_radians);
      if (!node.AttachLight(std::move(light))) {
        throw std::runtime_error("Cannot attach workload spot light");
      }
      const auto half = std::sqrt(0.5F);
      node.GetTransform().SetLocalRotation(glm::quat { half, half, 0, 0 });
    }
    result.lights.push_back(node);
    if (source.casts_shadows) {
      auto caster = result.scene->CreateNode(
        "Occluder " + std::to_string(source.id.get()));
      caster.GetRenderable().SetGeometry(caster_geometry);
      caster.GetRenderable().SetMaterialOverride(0, 0, material);
      caster.GetTransform().SetLocalPosition(
        { source.position_ws.at(0) + 0.25F, source.position_ws.at(1), 0.5F });
      result.casters.push_back(caster);
    }
  }
  for (const auto& view : workload.views) {
    auto camera
      = result.scene->CreateNode("Camera " + std::to_string(view.id.get()));
    camera.GetTransform().SetLocalPosition(
      { view.eye_ws.at(0), view.eye_ws.at(1), view.eye_ws.at(2) });
    const auto viewport = ViewPort { .top_left_x = view.content_origin_px.at(0),
      .top_left_y = view.content_origin_px.at(1),
      .width = static_cast<float>(view.width),
      .height = static_cast<float>(view.height) };
    const auto aspect = static_cast<float>(view.width) / view.height;
    if (view.projection == WorkloadProjection::kPerspective) {
      auto lens = std::make_unique<scene::PerspectiveCamera>();
      lens->SetViewport(viewport);
      lens->SetAspectRatio(aspect);
      lens->SetFieldOfView(std::numbers::pi_v<float> / 3.0F);
      lens->SetNearPlane(0.1F);
      lens->SetFarPlane(100.0F);
      if (!camera.AttachCamera(std::move(lens))) {
        throw std::runtime_error("Cannot attach perspective workload camera");
      }
    } else {
      auto lens = std::make_unique<scene::OrthographicCamera>();
      const auto half_height = view.eye_ws.at(2) / std::numbers::sqrt3_v<float>;
      lens->SetExtents(-half_height * aspect, half_height * aspect,
        -half_height, half_height, 0.1F, 100.0F);
      lens->SetViewport(viewport);
      if (!camera.AttachCamera(std::move(lens))) {
        throw std::runtime_error("Cannot attach orthographic workload camera");
      }
    }
    result.cameras.push_back(camera);
  }
  result.scene->Update();
  result.scene->SyncObservers();
  return result;
}

auto LightingWorkloadScene::ApplyMotion(const LightingWorkload& workload)
  -> void
{
  if (workload.lights.size() != lights.size()) {
    throw std::invalid_argument("Motion cannot change the workload population");
  }
  for (std::size_t index = 0; index < lights.size(); ++index) {
    const auto& p = workload.lights.at(index).position_ws;
    lights.at(index).GetTransform().SetLocalPosition(
      { p.at(0), p.at(1), p.at(2) });
  }
  scene->Update();
  scene->SyncObservers();
}
} // namespace oxygen::vortex::testing
