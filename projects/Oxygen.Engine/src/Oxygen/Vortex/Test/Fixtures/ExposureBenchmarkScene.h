//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/HalfFloat.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Data/Unorm16.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Environment/Fog.h>
#include <Oxygen/Scene/Environment/LocalFogVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Environment/SkyLight.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Light/PointLight.h>
#include <Oxygen/Scene/Light/SpotLight.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneFlags.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/Types/Flags.h>

namespace oxygen::vortex::testing {

struct MixedExposureBenchmarkScene {
  //! Sphere, cube, cylinder, cone, ground; one submesh per surface.
  std::array<scene::SceneNode, 5> surfaces;
  //! Optional roof, back, left, right, front-left and front-right walls.
  std::array<scene::SceneNode, 6> enclosure;
  scene::SceneNode main_camera;
  scene::SceneNode secondary_camera;
  scene::SceneNode sun;
  scene::SceneNode key_light;
  scene::SceneNode fill_light;
};

//! Populate the existing MultiView mixed exposure recipe in a native fixture.
/*!
 This is the concrete recipe from Examples/MultiView/SceneBootstrapper.cpp
 and MainModule::UpdateCameras, with the same procedural asset identities.
 Call once after removing unrelated fixture surfaces. The scene may contain
 postprocessing, but must not already contain the recipe's atmosphere, SkyLight
 or fog systems. The caller owns camera viewport/aspect, exposure, renderer
 quality/history settings, simulation time and rendering path.
 The optional enclosure adds a roof and five walls with a front opening
 spanning x = 1..3.5 at y = -3; the caller owns any indoor/outdoor camera path.
*/
[[nodiscard]] inline auto PopulateMixedExposureBenchmarkScene(
  scene::Scene& scene, const bool with_enclosure = false)
  -> MixedExposureBenchmarkScene
{
  auto result = MixedExposureBenchmarkScene {};
  const auto material
    = [](const std::string_view name, const glm::vec4 rgba,
        const data::MaterialDomain domain = data::MaterialDomain::kOpaque,
        const bool emission_only = false,
        const float emissive_scale
        = 4.0F) -> std::shared_ptr<const oxygen::data::MaterialAsset> {
    auto desc = data::pak::render::MaterialAssetDesc {};
    desc.header.asset_type
      = static_cast<std::uint8_t>(data::AssetType::kMaterial);
    const auto count = std::min(name.size(), sizeof(desc.header.name) - 1U);
    std::memcpy(std::data(desc.header.name), name.data(), count);
    desc.header.name[count] = '\0';
    desc.header.version = 1U;
    desc.header.streaming_priority = 255U;
    desc.material_domain = static_cast<std::uint8_t>(domain);
    desc.flags = data::pak::render::kMaterialFlag_NoTextureSampling;
    desc.shader_stages = 0U;
    std::ranges::copy(
      std::array {
        rgba.x,
        rgba.y,
        rgba.z,
        rgba.w,
      },
      std::begin(desc.base_color));
    desc.normal_scale = 1.0F;
    desc.metalness = data::Unorm16 {
      0.0F,
    };
    desc.roughness = data::Unorm16 {
      .5F,
    };
    desc.ambient_occlusion = data::Unorm16 {
      1.0F,
    };
    if (emission_only) {
      std::ranges::transform(
        std::array {
          rgba.x,
          rgba.y,
          rgba.z,
        },
        std::begin(desc.emissive_factor),
        [emissive_scale](const float channel) -> data::HalfFloat {
          return data::HalfFloat {
            emissive_scale * channel,
          };
        });
      std::fill_n(std::begin(desc.base_color), 3, 0.0F);
    }
    return std::make_shared<const data::MaterialAsset>(
      data::AssetKey::FromVirtualPath("/Engine/Examples/MultiView/Materials/"
        + std::string { name, } + ".omat"),
      desc, std::vector<data::ShaderReference> {});
  };
  const auto surface
    = [&](const std::string_view name, const auto& mesh_data,
        std::shared_ptr<const data::MaterialAsset> base,
        const glm::vec3 position, const bool casts_shadows = true) -> auto {
    CHECK_F(
      mesh_data.has_value(), "Cannot create mixed benchmark mesh {}", name);
    auto mesh
      = data::MeshBuilder(
        0, name == "GroundPlane" ? "Ground" : std::string { name, })
          .WithVertices(mesh_data->first)
          .WithIndices(mesh_data->second)
          .BeginSubMesh("full", std::move(base))
          .WithMeshView({ .first_index = 0U,
            .index_count = static_cast<std::uint32_t>(mesh_data->second.size()),
            .first_vertex = 0U,
            .vertex_count
            = static_cast<std::uint32_t>(mesh_data->first.size()), })
          .EndSubMesh()
          .Build();
    CHECK_NOTNULL_F(mesh.get());
    auto desc = data::pak::geometry::GeometryAssetDesc {};
    desc.lod_count = 1U;
    const auto minimum = mesh->BoundingBoxMin();
    const auto maximum = mesh->BoundingBoxMax();
    std::ranges::copy(
      std::array {
        minimum.x,
        minimum.y,
        minimum.z,
      },
      std::begin(desc.bounding_box_min));
    std::ranges::copy(
      std::array {
        maximum.x,
        maximum.y,
        maximum.z,
      },
      std::begin(desc.bounding_box_max));
    auto node = scene.CreateNode(std::string {
      name,
    });
    node.GetRenderable().SetGeometry(std::make_shared<data::GeometryAsset>(
      data::AssetKey::FromVirtualPath("/Engine/Examples/MultiView/Geometry/"
        + std::string { name, } + ".ogeo"),
      desc, std::vector<std::shared_ptr<data::Mesh>> { std::move(mesh) }));
    const auto flags_ref = node.GetFlags();
    CHECK_F(flags_ref.has_value());
    auto& flags = flags_ref->get();
    flags = flags.SetFlag(scene::SceneNodeFlags::kCastsShadows,
      scene::SceneFlag {}.SetEffectiveValueBit(casts_shadows));
    flags = flags.SetFlag(scene::SceneNodeFlags::kReceivesShadows,
      scene::SceneFlag {}.SetEffectiveValueBit(true));
    node.GetTransform().SetLocalPosition(position);
    return node;
  };
  result.surfaces.at(0) = surface("Sphere", data::MakeSphereMeshAsset(32U, 32U),
    material("SphereMaterial",
      {
        .2F,
        .7F,
        .3F,
        1.0F,
      }),
    {
      -2.0F,
      1.0F,
      0.0F,
    });
  result.surfaces.at(1) = surface("Cube", data::MakeCubeMeshAsset(),
    material("CubeMaterial",
      {
        .7F,
        .7F,
        .7F,
        1.0F,
      }),
    {
      1.0F,
      -1.0F,
      0.0F,
    });
  result.surfaces.at(2)
    = surface("Cylinder", data::MakeCylinderMeshAsset(16U, 1.0F, .5F),
      material("CylinderMaterial",
        {
          .4F,
          .4F,
          .9F,
          1.0F,
        }),
      {
        -.5F,
        -.5F,
        0.0F,
      });
  result.surfaces.at(2).GetTransform().SetLocalRotation(
    glm::quat(glm::vec3(glm::radians(30.0F), glm::radians(45.0F), 0.0F)));
  result.surfaces.at(3)
    = surface("Cone", data::MakeConeMeshAsset(16U, 1.0F, .5F),
      material("ConeMaterial",
        {
          .9F,
          .4F,
          .4F,
          1.0F,
        }),
      {
        -2.5F,
        -.5F,
        0.0F,
      });
  result.surfaces.at(3).GetTransform().SetLocalRotation(
    glm::quat(glm::vec3(glm::radians(30.0F), glm::radians(20.0F), 0.0F)));
  result.surfaces.at(4) = surface("GroundPlane", data::MakeCubeMeshAsset(),
    material("GroundMaterial",
      {
        .18F,
        .18F,
        .18F,
        1.0F,
      }),
    {
      0.0F,
      0.0F,
      -.55F,
    },
    false);
  result.surfaces.at(4).GetTransform().SetLocalScale({
    2000.0F,
    2000.0F,
    .1F,
  });
  result.surfaces.at(1).GetRenderable().SetMaterialOverride(0, 0,
    material("MixedExposureEmissive",
      {
        .7F,
        .65F,
        .5F,
        1.0F,
      },
      data::MaterialDomain::kOpaque, true, 4096.0F));
  result.surfaces.at(2).GetRenderable().SetMaterialOverride(0, 0,
    material("MixedExposureTranslucent",
      {
        .3F,
        .4F,
        .9F,
        .5F,
      },
      data::MaterialDomain::kAlphaBlended));
  result.surfaces.at(3).GetRenderable().SetMaterialOverride(0, 0,
    material("MixedExposureMasked",
      {
        .9F,
        .4F,
        .4F,
        .8F,
      },
      data::MaterialDomain::kMasked));

  if (with_enclosure) {
    const auto enclosure_material = material("ExposureBenchmarkEnclosure",
      {
        .18F,
        .18F,
        .18F,
        1.0F,
      });
    const std::array names {
      "ExposureBenchmarkRoof",
      "ExposureBenchmarkBack",
      "ExposureBenchmarkLeft",
      "ExposureBenchmarkRight",
      "ExposureBenchmarkFrontLeft",
      "ExposureBenchmarkFrontRight",
    };
    const std::array positions {
      glm::vec3 {
        0.0F,
        .5F,
        3.1F,
      },
      glm::vec3 {
        0.0F,
        4.0F,
        1.3F,
      },
      glm::vec3 {
        -4.0F,
        .5F,
        1.3F,
      },
      glm::vec3 {
        4.0F,
        .5F,
        1.3F,
      },
      glm::vec3 {
        -1.5F,
        -3.0F,
        1.3F,
      },
      glm::vec3 {
        3.75F,
        -3.0F,
        1.3F,
      },
    };
    const std::array scales {
      glm::vec3 {
        8.0F,
        7.0F,
        .2F,
      },
      glm::vec3 {
        8.0F,
        .2F,
        3.6F,
      },
      glm::vec3 {
        .2F,
        7.0F,
        3.6F,
      },
      glm::vec3 {
        .2F,
        7.0F,
        3.6F,
      },
      glm::vec3 {
        5.0F,
        .2F,
        3.6F,
      },
      glm::vec3 {
        .5F,
        .2F,
        3.6F,
      },
    };
    for (std::size_t index = 0U; index < result.enclosure.size(); ++index) {
      result.enclosure.at(index) = surface(names.at(index),
        data::MakeCubeMeshAsset(), enclosure_material, positions.at(index));
      result.enclosure.at(index).GetTransform().SetLocalScale(scales.at(index));
    }
  }

  result.key_light = scene.CreateNode("KeyLight");
  auto spot = std::make_unique<scene::SpotLight>();
  spot->Common().affects_world = true;
  spot->Common().casts_shadows = true;
  spot->Common().color_rgb = {
    1.0F,
    .98F,
    .95F,
  };
  spot->SetLuminousFluxLm(5000.0F);
  spot->SetRange(250.0F);
  spot->SetSourceRadius(.4F);
  spot->SetInnerConeAngleRadians(glm::radians(35.0F));
  spot->SetOuterConeAngleRadians(glm::radians(45.0F));
  CHECK_F(result.key_light.AttachLight(std::move(spot)));
  result.key_light.GetTransform().SetLocalPosition({
    3.0F,
    3.0F,
    3.0F,
  });
  const auto key_direction = glm::normalize(glm::vec3 {
                                              -.5F,
                                              0.0F,
                                              0.0F,
                                            }
    - glm::vec3 {
      3.0F,
      3.0F,
      3.0F,
    });
  result.key_light.GetTransform().SetLocalRotation(
    glm::angleAxis(std::acos(glm::dot(space::move::Forward, key_direction)),
      glm::normalize(glm::cross(space::move::Forward, key_direction))));

  result.fill_light = scene.CreateNode("FillLight");
  auto point = std::make_unique<scene::PointLight>();
  point->Common().affects_world = true;
  point->Common().casts_shadows = true;
  point->Common().color_rgb = {
    .7F,
    .85F,
    1.0F,
  };
  point->SetLuminousFluxLm(2000.0F);
  point->SetRange(300.0F);
  point->SetSourceRadius(.2F);
  CHECK_F(result.fill_light.AttachLight(std::move(point)));
  result.fill_light.GetTransform().SetLocalPosition({
    -2.0F,
    2.0F,
    2.0F,
  });

  result.sun = scene.CreateNode("AtmosphereProofSun");
  auto sun = std::make_unique<scene::DirectionalLight>();
  sun->SetIntensityLux(110000.0F);
  sun->SetEnvironmentContribution(true);
  sun->SetIsSunLight(true);
  sun->SetAtmosphereLightSlot(scene::AtmosphereLightSlot::kPrimary);
  sun->Common().casts_shadows = true;
  CHECK_F(result.sun.AttachLight(std::move(sun)));
  const auto sun_direction = glm::normalize(glm::vec3 {
    0.0F,
    .5F,
    -1.0F,
  });
  result.sun.GetTransform().SetLocalRotation(
    glm::angleAxis(std::acos(glm::dot(space::move::Forward, sun_direction)),
      glm::normalize(glm::cross(space::move::Forward, sun_direction))));

  if (!scene.GetEnvironment()) {
    scene.SetEnvironment(std::make_unique<scene::SceneEnvironment>());
  }
  auto& environment = *scene.GetEnvironment();
  auto& atmosphere = environment.AddSystem<scene::environment::SkyAtmosphere>();
  atmosphere.SetEnabled(true);
  atmosphere.SetAerialPerspectiveStartDepthMeters(0.0F);
  atmosphere.SetAerialScatteringStrength(1.0F);
  auto& sky_light = environment.AddSystem<scene::environment::SkyLight>();
  sky_light.SetEnabled(true);
  sky_light.SetDiffuseIntensity(1.0F);
  sky_light.SetSpecularIntensity(0.0F);
  sky_light.SetVolumetricScatteringIntensity(1.0F);
  auto& fog = environment.AddSystem<scene::environment::Fog>();
  fog.SetEnabled(true);
  fog.SetEnableHeightFog(true);
  fog.SetEnableVolumetricFog(true);
  fog.SetExtinctionSigmaTPerMeter(.06F);
  fog.SetHeightFalloffPerMeter(.12F);
  fog.SetHeightOffsetMeters(0.0F);
  fog.SetStartDistanceMeters(0.0F);
  fog.SetFogInscatteringLuminance({
    300.0F,
    450.0F,
    650.0F,
  });
  fog.SetVolumetricFogAlbedo({
    .4F,
    .5F,
    .6F,
  });
  fog.SetVolumetricFogEmissive({
    0.0F,
    0.0F,
    0.0F,
  });
  fog.SetVolumetricFogDistance(40.0F);
  fog.SetVolumetricFogStartDistance(0.0F);
  fog.SetVolumetricFogNearFadeInDistance(1.0F);
  fog.SetVolumetricFogScatteringDistribution(.1F);

  auto local_node = scene.CreateNode("VisualReviewLocalFog");
  auto local_impl = local_node.GetImpl();
  CHECK_F(local_impl.has_value());
  auto& local
    = local_impl->get().AddComponent<scene::environment::LocalFogVolume>();
  local_node.GetTransform().SetLocalPosition({
    -1.75F,
    -.25F,
    .3F,
  });
  local_node.GetTransform().SetLocalScale({
    .45F,
    .45F,
    .3F,
  });
  local.SetEnabled(false);
  local.SetRadialFogExtinction(.9F);
  local.SetHeightFogExtinction(.45F);
  local.SetHeightFogFalloff(.5F);
  local.SetHeightFogOffset(-.5F);
  local.SetFogAlbedo({
    .45F,
    .6F,
    .8F,
  });
  local.SetFogEmissive({
    0.0F,
    0.0F,
    0.0F,
  });
  local.SetFogPhaseG(.1F);

  const auto camera = [&](const char* name, const glm::vec3 position,
                        const float near_plane) -> scene::SceneNode {
    auto node = scene.CreateNode(name);
    auto lens = std::make_unique<scene::PerspectiveCamera>();
    lens->SetFieldOfView(glm::radians(45.0F));
    lens->SetNearPlane(near_plane);
    lens->SetFarPlane(100.0F);
    CHECK_F(node.AttachCamera(std::move(lens)));
    node.GetTransform().SetLocalPosition(position);
    const auto view = glm::lookAt(position,
      glm::vec3 {
        -.75F,
        0.0F,
        0.0F,
      },
      space::move::Up);
    node.GetTransform().SetLocalRotation(glm::quat_cast(glm::inverse(view)));
    return node;
  };
  result.main_camera = camera("MainCamera",
    {
      4.0F,
      -6.0F,
      4.0F,
    },
    .1F);
  result.secondary_camera = camera("PipCamera",
    {
      -5.0F,
      .4F,
      4.0F,
    },
    .05F);
  return result;
}

} // namespace oxygen::vortex::testing
