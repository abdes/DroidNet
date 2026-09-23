//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

#include <glm/ext/quaternion_float.hpp>

#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/ShaderReference.h>
#include <Oxygen/Data/Unorm16.h>
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Light/PointLight.h>
#include <Oxygen/Scene/Light/SpotLight.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Lighting/LightingService.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureLightingFixture.h>
#include <Oxygen/Vortex/Test/Fixtures/RendererPublicationProbe.h>
#include <Oxygen/Vortex/Test/Support/LightingWorkload.h>

namespace oxygen::vortex::testing {
namespace {
  class LightingWorkloadGpuTest : public exposure::ExposureLightingGpuTest {
  protected:
    auto SetUp() -> void override
    {
      initial_scene_capacity = LightingWorkloadOptions {}.light_count + 2U;
      ExposureLightingGpuTest::SetUp();
    }
  };

  NOLINT_TEST_F(
    LightingWorkloadGpuTest, PrimaryRecipePublishesAndRendersBothFamilies)
  {
    const auto workload = BuildLightingWorkload({});
    ASSERT_EQ(workload.lights.size(), 1024U);
    EXPECT_GE(
      CountGuaranteedVisibleContributors(workload, workload.views.front()),
      256U);
    for (const auto& source : workload.lights) {
      auto node = scene->CreateNode(
        "Workload light " + std::to_string(source.id.get()));
      node.GetTransform().SetLocalPosition({ source.position_ws.at(0),
        source.position_ws.at(1), source.position_ws.at(2) });
      const auto initialize = [&](auto& light) -> void {
        light.Common().affects_world = source.enabled;
        light.Common().casts_shadows = source.casts_shadows;
        light.Common().color_rgb = {
          source.color_rgb.at(0),
          source.color_rgb.at(1),
          source.color_rgb.at(2),
        };
        light.SetLuminousFluxLm(source.flux_lm);
        light.SetRange(source.range_m);
        light.SetSourceRadius(source.source_radius_m);
      };
      if (source.kind == WorkloadLightKind::kPoint) {
        auto light = std::make_unique<scene::PointLight>();
        initialize(*light);
        ASSERT_TRUE(node.AttachLight(std::move(light)));
      } else {
        auto light = std::make_unique<scene::SpotLight>();
        initialize(*light);
        light->SetInnerConeAngleRadians(source.inner_half_angle_radians);
        light->SetOuterConeAngleRadians(source.outer_half_angle_radians);
        ASSERT_TRUE(node.AttachLight(std::move(light)));
        const auto half = std::sqrt(0.5F);
        node.GetTransform().SetLocalRotation(
          glm::quat { half, half, 0.0F, 0.0F });
      }
    }
    const auto extent = workload.floor_half_extent_m;
    const auto positions = std::array {
      glm::vec3 { -extent, -extent, 0.0F },
      glm::vec3 { extent, -extent, 0.0F },
      glm::vec3 { extent, extent, 0.0F },
      glm::vec3 { -extent, extent, 0.0F },
    };
    auto vertices = std::vector<data::Vertex>(positions.size());
    for (std::size_t index = 0U; index < vertices.size(); ++index) {
      vertices.at(index) = {
        .position = positions.at(index),
        .normal = { 0, 0, 1 },
        .texcoord = { 0.5F, 0.5F },
        .tangent = { 1, 0, 0 },
        .bitangent = { 0, 1, 0 },
        .color = { 1, 1, 1, 1 },
      };
    }
    std::shared_ptr<data::Mesh> mesh
      = data::MeshBuilder()
          .WithVertices(vertices)
          .WithIndices(std::vector<std::uint32_t> { 0U, 1U, 2U, 0U, 2U, 3U })
          .BeginSubMesh("Workload floor", data::MaterialAsset::CreateDefault())
          .WithMeshView({
            .first_index = 0U,
            .index_count = 6U,
            .first_vertex = 0U,
            .vertex_count = 4U,
          })
          .EndSubMesh()
          .Build();
    auto geometry = data::pak::geometry::GeometryAssetDesc {};
    geometry.lod_count = 1U;
    geometry.bounding_box_min[0] = geometry.bounding_box_min[1] = -extent;
    geometry.bounding_box_max[0] = geometry.bounding_box_max[1] = extent;
    geometry.bounding_box_min[2] = geometry.bounding_box_max[2] = 0.0F;
    mesh_node.GetRenderable().SetGeometry(std::make_shared<data::GeometryAsset>(
      data::AssetKey::FromVirtualPath("/Test/Lighting/WorkloadFloor.ogeo"),
      geometry, std::vector<std::shared_ptr<data::Mesh>> { mesh }));
    auto material = data::pak::render::MaterialAssetDesc {};
    material.flags = data::pak::render::kMaterialFlag_DoubleSided
      | data::pak::render::kMaterialFlag_NoTextureSampling;
    material.base_color[0] = material.base_color[1] = material.base_color[2]
      = 0.5F;
    material.base_color[3] = 1.0F;
    material.roughness = data::Unorm16 { 0.5F };
    material.ambient_occlusion = data::Unorm16 { 1.0F };
    material.normal_scale = 1.0F;
    mesh_node.GetRenderable().SetMaterialOverride(0U, 0U,
      std::make_shared<data::MaterialAsset>(
        data::AssetKey::FromVirtualPath("/Test/Lighting/WorkloadFloor.omat"),
        material, std::vector<data::ShaderReference> {}));
    camera.GetTransform().SetLocalPosition({ 0.0F, 0.0F, 24.0F });
    auto lens = camera.GetCameraAs<scene::PerspectiveCamera>();
    ASSERT_TRUE(lens.has_value());
    lens->get().SetFieldOfView(std::numbers::pi_v<float> / 3.0F);
    lens->get().SetNearPlane(0.1F);
    lens->get().SetFarPlane(100.0F);
    // Same aspect/frustum and all 1,024 sources; a small correctness preview
    // keeps this out of the long official performance suite.
    constexpr std::uint32_t width = 64U;
    constexpr std::uint32_t height = 36U;
    view.viewport.width = static_cast<float>(width);
    view.viewport.height = static_cast<float>(height);
    lens->get().SetViewport(view.viewport);
    lens->get().SetAspectRatio(static_cast<float>(width) / height);
    auto output = CreateRegisteredTexture({
      .width = width,
      .height = height,
      .format = Format::kRGBA32Float,
      .is_render_target = true,
      .initial_state = graphics::ResourceStates::kCommon,
    });
    framebuffer = Backend().CreateFramebuffer(
      graphics::FramebufferDesc {}.AddColorAttachment(output));
    auto observed_counts = std::array<std::uint32_t, 3> {};
    probe->inspect = [&](const auto&, const auto&, unsigned) -> void {
      const auto* lighting
        = RendererPublicationProbe::GetLightingService(*renderer_);
      ASSERT_NE(lighting, nullptr);
      observed_counts = {
        lighting->GetLastGridBuildState().local_light_count,
        lighting->GetLastDeferredLightingState().point_light_count,
        lighting->GetLastDeferredLightingState().spot_light_count,
      };
    };
    for (const bool forward : { false, true }) {
      SCOPED_TRACE(forward);
      ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0.0F, 3U));
      EXPECT_EQ(observed_counts.at(0), 1024U);
      if (!forward) {
        EXPECT_EQ(observed_counts.at(1), 512U);
        EXPECT_EQ(observed_counts.at(2), 512U);
      }
      const auto image = ReadFloatTexture(*probe->color);
      ASSERT_EQ(image.size(), width * height);
      unsigned lit = 0U;
      for (const auto& pixel : image) {
        for (const auto value : pixel) {
          EXPECT_TRUE(std::isfinite(value));
        }
        lit += pixel.front() > 1.0e-5F ? 1U : 0U;
      }
      EXPECT_GT(lit, width * height * 9U / 10U);
      RecordProperty(
        forward ? "forward_lit_preview_pixels" : "deferred_lit_preview_pixels",
        lit);
    }
    RecordProperty("source_lights", workload.lights.size());
    RecordProperty("guaranteed_visible_contributors",
      CountGuaranteedVisibleContributors(workload, workload.views.front()));
  }
} // namespace
} // namespace oxygen::vortex::testing
