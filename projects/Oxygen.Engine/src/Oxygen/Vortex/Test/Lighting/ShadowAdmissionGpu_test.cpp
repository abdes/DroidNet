//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <d3d12.h>
#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/Bindless/Generated.BindlessAbi.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Core/Types/ByteUnits.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/GeometryIndices.h>
#include <Oxygen/Data/MaterialDomain.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecording.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/SubmissionCallback.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Light/LightCommon.h>
#include <Oxygen/Scene/Light/PointLight.h>
#include <Oxygen/Scene/Light/SpotLight.h>
#include <Oxygen/Scene/Types/Flags.h>
#include <Oxygen/Scene/Types/NodeHandle.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/Lighting/LightingService.h>
#include <Oxygen/Vortex/PostProcess/Passes/ExposurePass.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/SceneRenderer/ShadingMode.h>
#include <Oxygen/Vortex/Shadows/ShadowService.h>
#include <Oxygen/Vortex/Shadows/Types/CubeLocalShadowRecord.h>
#include <Oxygen/Vortex/Shadows/Types/ShadowContentLease.h>
#include <Oxygen/Vortex/Shadows/Types/ShadowFrameData.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureLightingFixture.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestTags.h>
#include <Oxygen/Vortex/Test/Fixtures/RendererPublicationProbe.h>
#include <Oxygen/Vortex/Test/Support/CpuAllocationCounter.h>
#include <Oxygen/Vortex/Types/ExposureStateData.h>
#include <Oxygen/Vortex/Types/ViewRenderStatus.h>

namespace oxygen::vortex::testing {
namespace {
  class ShadowAdmissionGpuTest : public exposure::ExposureLightingGpuTest {
  protected:
    auto AdditionalCapabilities() const -> CapabilitySet override
    {
      return RendererCapabilityFamily::kShadowing;
    }
    auto SetUp() -> void override
    {
      initial_scene_capacity = 32U;
      ExposureLightingGpuTest::SetUp();
    }
    auto AddPoint(unsigned index) -> scene::SceneNode
    {
      auto node = scene->CreateNode("Admission point " + std::to_string(index));
      auto light = std::make_unique<scene::PointLight>();
      light->SetRange(3.0F);
      light->SetLuminousFluxLm(1.0F);
      light->Common().casts_shadows = true;
      light->Common().shadow.resolution_hint
        = scene::ShadowResolutionHint::kLow;
      EXPECT_TRUE(node.AttachLight(std::move(light)));
      return node;
    }
  };

  class ShadowBudgetGpuTest : public ShadowAdmissionGpuTest {
  protected:
    auto ConfigureRenderer(RendererConfig& config) const -> void override
    {
      config.lighting_allocation_limit_bytes = 32ULL * 1024 * 1024;
      config.lighting_compact_index_limit_bytes = 8ULL * 1024 * 1024;
      config.lighting_driver_headroom_bytes = 0;
    }
  };

  //! The probe's comparison result for one sample: the bias sign decides it,
  //! except that an unbiased sample on the depth edge (`offset` 0) blends
  //! both sides, and other unbiased samples take their side's depth.
  struct CubeComparisonCase {
    float bias { 0.0F };
    float offset { 0.0F };
    unsigned cube { 0U };
  };

  auto ExpectedCubeComparison(const CubeComparisonCase& sample) -> float
  {
    const auto [bias, offset, cube] = sample;
    if (bias < 0.0F) {
      return 0.0F;
    }
    if (bias > 0.0F) {
      return 1.0F;
    }
    if (offset == 0.0F) {
      return 0.5F;
    }
    return (offset < 0.0F) == (cube == 0U) ? 1.0F : 0.0F;
  }

  NOLINT_TEST_F(ShadowAdmissionGpuTest,
    CubeComparisonFiltersVisibilityAndAppliesReversedDepthBias)
  {
    constexpr unsigned size = 8U;
    constexpr unsigned row_pitch = 256U;
    constexpr unsigned layer_bytes = row_pitch * size;
    auto texture = CreateRegisteredTexture({
      .width = size,
      .height = size,
      .array_size = 12U,
      .format = Format::kR32Float,
      .texture_type = TextureType::kTextureCubeArray,
      .is_shader_resource = true,
      .initial_state = graphics::ResourceStates::kCommon,
    });
    auto upload = CreateUploadBuffer(
      SizeBytes { static_cast<std::uint64_t>(layer_bytes) * 12U });
    for (unsigned layer = 0; layer < 12U; ++layer) {
      for (unsigned y = 0; y < size; ++y) {
        for (unsigned x = 0; x < size; ++x) {
          const float depth = ((x < size / 2U) == (layer < 6U)) ? 0.25F : 0.75F;
          upload->Update(&depth, sizeof(depth),
            (layer * layer_bytes) + (y * row_pitch) + (x * sizeof(float)));
        }
      }
    }
    SubmitCommands("Cube comparison fixture",
      [&](graphics::CommandRecorder& recorder) -> void {
        EnsureTracked(recorder, upload, graphics::ResourceStates::kGenericRead);
        EnsureTracked(recorder, texture, graphics::ResourceStates::kCommon);
        recorder.RequireResourceState(
          *texture, graphics::ResourceStates::kCopyDest);
        recorder.FlushBarriers();
        for (unsigned layer = 0; layer < 12U; ++layer) {
          recorder.CopyBufferToTexture(*upload,
            { .buffer_offset = static_cast<std::uint64_t>(layer) * layer_bytes,
              .buffer_row_pitch = row_pitch,
              .buffer_slice_pitch = layer_bytes,
              .dst_slice = { .width = size,
                .height = size,
                .depth = 1,
                .array_slice = layer, },
              .dst_subresources
              = { .base_array_slice = layer, .num_array_slices = 1U }, },
            *texture);
        }
        recorder.RequireResourceStateFinal(
          *texture, graphics::ResourceStates::kShaderResource);
      });
    WaitForQueueIdle();
    auto& allocator = renderer_->GetGraphics()->GetDescriptorAllocator();
    auto handle
      = allocator.AllocateRaw(graphics::ResourceViewType::kTexture_SRV,
        graphics::DescriptorVisibility::kShaderVisible);
    ASSERT_TRUE(handle.IsValid());
    const auto srv = allocator.GetShaderVisibleIndex(handle);
    const auto view_handle = Backend().GetResourceRegistry().RegisterView(
      *texture, std::move(handle),
      graphics::TextureViewDescription {
        .format = Format::kR32Float,
        .dimension = TextureType::kTextureCubeArray,
        .sub_resources = graphics::TextureSubResourceSet::EntireTexture(),
      });
    ASSERT_TRUE(view_handle->IsValid());
    auto inputs = std::vector<std::array<std::uint32_t, 12>> {};
    auto expected = std::vector<float> {};
    for (unsigned cube = 0; cube < 2U; ++cube) {
      for (unsigned face = 0; face < 6U; ++face) {
        for (const float s : { -0.5F, 0.0F, 0.5F }) {
          const auto directions = std::array {
            glm::vec3 { 1, 0, -s },
            glm::vec3 { -1, 0, s },
            glm::vec3 { s, 1, 0 },
            glm::vec3 { s, -1, 0 },
            glm::vec3 { s, 0, 1 },
            glm::vec3 { -s, 0, -1 },
          };
          for (const float bias : { -0.3F, 0.0F, 0.3F }) {
            const auto direction = directions.at(face);
            inputs.push_back({
              srv.get(),
              cube,
              1U,
              size,
              std::bit_cast<std::uint32_t>(direction.x),
              std::bit_cast<std::uint32_t>(direction.y),
              std::bit_cast<std::uint32_t>(direction.z),
              std::bit_cast<std::uint32_t>(0.5F),
              std::bit_cast<std::uint32_t>(bias),
              0,
              0,
              0,
            });
            expected.push_back(ExpectedCubeComparison(
              { .bias = bias, .offset = s, .cube = cube }));
          }
        }
      }
    }
    const auto result = RunToneProbe(std::as_bytes(std::span { inputs }),
      static_cast<unsigned>(inputs.size()), 65536U, false);
    ASSERT_EQ(result.size(), expected.size());
    for (std::size_t index = 0; index < result.size(); ++index) {
      EXPECT_NEAR(result.at(index).at(0), expected.at(index), 1.0e-6F) << index;
    }
    RecordProperty(
      "bilinear_comparison_cases", static_cast<int>(result.size()));
  }

  NOLINT_TEST_F(ShadowAdmissionGpuTest,
    CubeHardwarePcfUsesProjectedDepthAcrossFacesSeamsAndDescriptors)
  {
    std::vector<scene::NodeHandle> light_sources;
    for (unsigned index = 0; index < 3U; ++index) {
      auto light = AddPoint(index);
      light_sources.push_back(light.GetHandle());
      ASSERT_TRUE(
        light.EditLight<scene::PointLight>([index](auto& value) -> void {
          value.Common().shadow.bias = static_cast<float>(index) * 0.25F;
          if (index == 1U) {
            value.Common().shadow.resolution_hint
              = scene::ShadowResolutionHint::kHigh;
          }
        }));
    }
    SetSurface(data::MaterialDomain::kOpaque);
    const glm::vec3 extent { 1.0F, 1.3F, 1.6F };
    const auto extent_axes = std::array { extent.x, extent.y, extent.z };
    auto vertices = std::vector<data::Vertex> {};
    auto indices = std::vector<std::uint32_t> {};
    for (unsigned axis = 0; axis < 3U; ++axis) {
      for (const float sign : { -1.0F, 1.0F }) {
        const auto first = static_cast<std::uint32_t>(vertices.size());
        const unsigned u = (axis + 1U) % 3U;
        const unsigned v = (axis + 2U) % 3U;
        auto normal_axes = std::array<float, 3> {};
        normal_axes.at(axis) = -sign;
        const auto normal = std::bit_cast<glm::vec3>(normal_axes);
        for (const auto corner : std::array {
               glm::vec2 { -1, -1 },
               glm::vec2 { 1, -1 },
               glm::vec2 { 1, 1 },
               glm::vec2 { -1, 1 },
             }) {
          auto position = std::array<float, 3> {};
          position.at(axis) = extent_axes.at(axis) * sign;
          position.at(u) = extent_axes.at(u) * corner.x;
          position.at(v) = extent_axes.at(v) * corner.y;
          vertices.push_back({
            .position = std::bit_cast<glm::vec3>(position),
            .normal = normal,
            .texcoord = { 0.5F, 0.5F },
            .tangent = { 1, 0, 0 },
            .bitangent = { 0, 1, 0 },
            .color = { 1, 1, 1, 1 },
          });
        }
        const auto corners = sign < 0 ? std::array { 0U, 1U, 2U, 0U, 2U, 3U }
                                      : std::array { 0U, 2U, 1U, 0U, 3U, 2U };
        for (const auto corner : corners) {
          indices.push_back(first + corner);
        }
      }
    }
    std::shared_ptr<data::Mesh> mesh
      = data::MeshBuilder()
          .WithVertices(vertices)
          .WithIndices(indices)
          .BeginSubMesh("Box", data::MaterialAsset::CreateDefault())
          .WithMeshView({
            .first_index = 0,
            .index_count = 36,
            .first_vertex = 0,
            .vertex_count = 24,
          })
          .EndSubMesh()
          .Build();
    data::pak::geometry::GeometryAssetDesc desc {};
    desc.lod_count = 1U;
    std::ranges::transform(
      extent_axes, std::begin(desc.bounding_box_min), std::negate {});
    std::ranges::copy(extent_axes, std::begin(desc.bounding_box_max));
    mesh_node.GetRenderable().SetGeometry(std::make_shared<data::GeometryAsset>(
      data::AssetKey::FromVirtualPath("/Test/PointPcf/Box.ogeo"), desc,
      std::vector<std::shared_ptr<data::Mesh>> { mesh }));
    // Exercise ordinary one-sided winding as well as the cube orientation.
    mesh_node.GetRenderable().SetMaterialOverride(oxygen::data::LodIndex { 0 },
      oxygen::data::SubmeshIndex { 0 }, data::MaterialAsset::CreateDefault());
    auto records = std::vector<CubeLocalShadowRecord> {};
    std::vector<ShadowContentLease> retained_maps;
    probe->inspect = [&](const auto& ctx, const auto&, unsigned) -> void {
      auto* owner = RendererPublicationProbe::GetSceneRenderer(*renderer_);
      const auto data
        = RendererPublicationProbe::GetShadowService(*owner)->InspectShadowData(
          ctx.current_view.view_id);
      ASSERT_NE(data, nullptr);
      records = data->cube_local_records;
      retained_maps.clear();
      auto* shadows = RendererPublicationProbe::GetShadowService(*owner);
      for (const auto source : light_sources) {
        retained_maps.push_back(
          shadows->RetainLocalContent(ctx.current_view.view_id, source));
        ASSERT_TRUE(retained_maps.back());
      }
    };
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F, 3U));
    ASSERT_EQ(records.size(), 3U);
    // Physical grouping follows resolution, independently of selection order.
    std::vector<observer_ptr<const CubeLocalShadowRecord>> low_resolution;
    observer_ptr<const CubeLocalShadowRecord> high_resolution;
    for (const auto& record : records) {
      if (record.inverse_resolution.x == 1.0F / 512.0F) {
        low_resolution.emplace_back(&record);
      } else {
        high_resolution = observer_ptr { &record };
      }
    }
    ASSERT_EQ(low_resolution.size(), 2U);
    ASSERT_NE(high_resolution, nullptr);
    const auto& first_low = *low_resolution.front();
    const auto& second_low = *low_resolution.back();
    EXPECT_EQ(first_low.surface_srv, second_low.surface_srv);
    EXPECT_NE(first_low.first_array_layer, second_low.first_array_layer);
    EXPECT_NE(first_low.surface_srv, high_resolution->surface_srv);
    auto rays = std::vector<glm::vec3> {};
    for (int x = -1; x <= 1; ++x) {
      for (int y = -1; y <= 1; ++y) {
        for (int z = -1; z <= 1; ++z) {
          if (x != 0 || y != 0 || z != 0) {
            rays.emplace_back(x, y, z);
          }
        }
      }
    }
    rays.insert(rays.end(),
      {
        { 1, 0.21F, -0.37F },
        { -0.31F, 1, 0.47F },
        { 0.27F, -0.39F, 1 },
        { 1, 0.999F, 0 },
        { 0.999F, 1, 0 },
        { 0, 0.00001F, 1 },
        { 0, 0.00001F, -1 },
      });
    auto inputs = std::vector<std::array<std::uint32_t, 12>> {};
    auto expected = std::vector<float> {};
    auto expected_depth = std::vector<float> {};
    for (const auto ray : rays) {
      const auto direction = glm::normalize(ray);
      const auto abs_direction = glm::abs(direction);
      const auto hit = 1.0F
        / (std::max)({
          abs_direction.x / extent.x,
          abs_direction.y / extent.y,
          abs_direction.z / extent.z,
        });
      const auto axial
        = (std::max)({ abs_direction.x, abs_direction.y, abs_direction.z });
      for (const unsigned count : { 1U, 5U, 29U }) {
        for (const float scale : { 0.7F, 1.3F }) {
          // Interleave backing descriptors and nonzero cube indices per lane.
          for (const auto& record : records) {
            EXPECT_EQ(record.pcf_sample_count, 29U);
            const auto depth = [&](float distance) -> float {
              return record.near_plane_m * (record.far_plane_m - distance)
                / (distance * (record.far_plane_m - record.near_plane_m));
            };
            inputs.push_back({
              record.surface_srv.get(),
              record.first_array_layer.get() / 6U,
              count,
              static_cast<unsigned>(
                std::lround(1 / record.inverse_resolution.x)),
              std::bit_cast<std::uint32_t>(-direction.x),
              std::bit_cast<std::uint32_t>(-direction.y),
              std::bit_cast<std::uint32_t>(-direction.z),
              std::bit_cast<std::uint32_t>(depth(hit * axial * scale)),
              0,
              0,
              0,
              0,
            });
            expected.push_back(scale < 1 ? 1.0F : 0.0F);
            expected_depth.push_back(depth(hit * axial));
          }
        }
      }
    }
    const auto attach = [&](graphics::CommandRecorder& recorder) -> void {
      for (const auto& lease : retained_maps) {
        CHECK_F(
          lease.Attach(recorder, Backend().GetResourceRegistry()).has_value());
      }
    };
    const auto result = RunToneProbe(std::as_bytes(std::span { inputs }),
      static_cast<unsigned>(inputs.size()), 65536U, false, attach);
    ASSERT_EQ(result.size(), inputs.size());
    for (std::size_t index = 0; index < result.size(); ++index) {
      EXPECT_NEAR(result.at(index).at(0), expected.at(index), 1.0e-6F) << index;
      EXPECT_NEAR(result.at(index).at(1), expected_depth.at(index), 5.0e-4F)
        << index;
    }
    RecordProperty(
      "cube_hardware_pcf_samples", static_cast<int>(result.size()));
    // The cube permutation must retain alpha discard, and a material change
    // must invalidate the cached depth before either lighting path consumes it.
    for (const bool rejected : { true, false }) {
      retained_maps.clear();
      SetSurface(data::MaterialDomain::kMasked, 0.0F, rejected);
      ASSERT_NO_FATAL_FAILURE(RenderSurface(!rejected, 0.0F, 2U));
      for (size_t index = 0; index < inputs.size(); ++index) {
        const auto& record = records.at(index % records.size());
        auto& input = inputs.at(index);
        input.at(0) = record.surface_srv.get();
        input.at(1) = record.first_array_layer.get() / 6U;
        input.at(3)
          = static_cast<unsigned>(std::lround(1 / record.inverse_resolution.x));
      }
      const auto masked = RunToneProbe(std::as_bytes(std::span { inputs }),
        static_cast<unsigned>(inputs.size()), 65536U, false, attach);
      ASSERT_EQ(masked.size(), inputs.size());
      for (std::size_t index = 0; index < masked.size(); ++index) {
        EXPECT_NEAR(
          masked.at(index).at(0), rejected ? 1.0F : expected.at(index), 1.0e-6F)
          << index;
        EXPECT_NEAR(masked.at(index).at(1),
          rejected ? 0.0F : expected_depth.at(index), 5.0e-4F)
          << index;
      }
    }
  }

  NOLINT_TEST_F(ShadowAdmissionGpuTest,
    NonuniformCasterTransformMatchesBakedGeometryWithSlopeBias)
  {
    // Projected spots retain caster slope bias; cube hardware PCF applies
    // its bias on receivers and would no longer exercise this regression.
    auto light = scene->CreateNode("Normal regression spot");
    auto spot = std::make_unique<scene::SpotLight>();
    spot->SetRange(3.0F);
    spot->SetLuminousFluxLm(1.0F);
    spot->Common().casts_shadows = true;
    spot->Common().shadow.bias = 0.1F;
    spot->Common().shadow.resolution_hint = scene::ShadowResolutionHint::kLow;
    ASSERT_TRUE(light.AttachLight(std::move(spot)));
    ASSERT_TRUE(light.GetTransform().SetLocalRotation(
      glm::quat { 0.70710678F, 0.70710678F, 0, 0 }));
    SetSurface(data::MaterialDomain::kOpaque);
    ShadowContentLease retained_shadow;
    std::shared_ptr<const graphics::Texture> surface;
    std::uint32_t layer = 0U;
    std::uint32_t surface_srv = kInvalidShaderVisibleIndex.get();
    probe->inspect = [&](const auto& ctx, const auto&, unsigned) -> void {
      auto* owner = RendererPublicationProbe::GetSceneRenderer(*renderer_);
      auto* shadows = RendererPublicationProbe::GetShadowService(*owner);
      const auto data = shadows->InspectShadowData(ctx.current_view.view_id);
      ASSERT_NE(data, nullptr);
      ASSERT_EQ(data->projected_local_records.size(), 1U);
      const auto surfaces
        = shadows->InspectSpotShadowSurfaces(ctx.current_view.view_id);
      ASSERT_EQ(surfaces.size(), 1U);
      retained_shadow = shadows->RetainLocalContent(
        ctx.current_view.view_id, light.GetHandle());
      ASSERT_TRUE(retained_shadow);
      surface = retained_shadow.Texture();
      layer = data->projected_local_records.front().array_layer.get();
      surface_srv = data->projected_local_records.front().surface_srv.get();
    };
    std::array<float, 2> depths {};
    for (const bool baked : { false, true }) {
      SCOPED_TRACE(baked);
      // Both variants have the same world-space triangle. The baked normal is
      // perpendicular to its edges; the unbaked path must use inverse
      // transpose.
      const std::array positions {
        glm::vec3 { -0.5F, -0.5F, -0.5F },
        glm::vec3 { 0.5F, -0.5F, -1.5F },
        glm::vec3 { 0.0F, 0.5F, -1.0F },
      };
      const auto normal = baked ? glm::normalize(glm::vec3 { 0.5F, 0, 1 })
                                : glm::normalize(glm::vec3 { 1, 0, 1 });
      auto vertices = std::vector<data::Vertex> {};
      for (auto position : positions) {
        if (baked) {
          position.x *= 2.0F;
        }
        vertices.push_back({
          .position = position,
          .normal = normal,
          .texcoord = { 0.5F, 0.5F },
          .tangent = { 0, 1, 0 },
          .bitangent = { 1, 0, 0 },
          .color = { 1, 1, 1, 1 },
        });
      }
      std::shared_ptr<data::Mesh> mesh
        = data::MeshBuilder()
            .WithVertices(vertices)
            .WithIndices(std::vector<std::uint32_t> { 0, 1, 2 })
            .BeginSubMesh("Slope", data::MaterialAsset::CreateDefault())
            .WithMeshView({
              .first_index = 0,
              .index_count = 3,
              .first_vertex = 0,
              .vertex_count = 3,
            })
            .EndSubMesh()
            .Build();
      auto desc = data::pak::geometry::GeometryAssetDesc {};
      desc.lod_count = 1U;
      desc.bounding_box_min[0] = baked ? -1.0F : -0.5F;
      desc.bounding_box_max[0] = baked ? 1.0F : 0.5F;
      desc.bounding_box_min[1] = -0.5F;
      desc.bounding_box_max[1] = 0.5F;
      desc.bounding_box_min[2] = -1.5F;
      desc.bounding_box_max[2] = -0.5F;
      mesh_node.GetRenderable().SetGeometry(
        std::make_shared<data::GeometryAsset>(
          data::AssetKey::FromVirtualPath(baked
              ? "/Test/ShadowNormal/Baked.ogeo"
              : "/Test/ShadowNormal/Transformed.ogeo"),
          desc, std::vector<std::shared_ptr<data::Mesh>> { mesh }));
      ASSERT_TRUE(mesh_node.GetTransform().SetLocalScale(
        { baked ? 1.0F : 2.0F, 1.0F, 1.0F }));
      ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F, 3U));
      ASSERT_NE(surface, nullptr);
      const auto& texture = surface->GetDescriptor();
      const auto sample = std::array {
        surface_srv,
        texture.width / 2U,
        texture.height / 2U,
        layer,
      };
      constexpr auto depth_array_probe = 32768U;
      const auto readback = RunToneProbe(std::as_bytes(std::span { sample }),
        1U, depth_array_probe, false,
        [&](graphics::CommandRecorder& recorder) -> void {
          CHECK_F(
            retained_shadow.Attach(recorder, Backend().GetResourceRegistry())
              .has_value());
        });
      ASSERT_EQ(readback.size(), 1U);
      depths.at(static_cast<unsigned>(baked)) = readback.front().front();
      EXPECT_GT(depths.at(static_cast<unsigned>(baked)), 0.0F);
      EXPECT_LT(depths.at(static_cast<unsigned>(baked)), 1.0F);
    }
    EXPECT_NEAR(depths.front(), depths.back(), 1.0e-6F);
    surface.reset();
  }

  NOLINT_TEST_F(
    ShadowAdmissionGpuTest, FifthCubeAndNinthProjectedShadowRenderTogether)
  {
    for (unsigned index = 0U; index < 5U; ++index) {
      AddPoint(index);
    }
    for (unsigned index = 0U; index < 9U; ++index) {
      auto node = scene->CreateNode("Admission spot " + std::to_string(index));
      auto light = std::make_unique<scene::SpotLight>();
      light->SetRange(3.0F);
      light->SetLuminousFluxLm(1.0F);
      light->Common().casts_shadows = true;
      light->Common().shadow.resolution_hint
        = scene::ShadowResolutionHint::kLow;
      ASSERT_TRUE(node.AttachLight(std::move(light)));
      node.GetTransform().SetLocalRotation(
        glm::quat { 0.70710678F, 0.70710678F, 0, 0 });
    }
    unsigned inspected = 0U;
    auto first_surface = std::shared_ptr<const graphics::Texture> {};
    auto first_view = kInvalidViewId;
    probe->inspect = [&](const auto& ctx, const auto&, unsigned) -> void {
      auto* owner = RendererPublicationProbe::GetSceneRenderer(*renderer_);
      auto* shadows = RendererPublicationProbe::GetShadowService(*owner);
      const auto data = shadows->InspectShadowData(ctx.current_view.view_id);
      ASSERT_NE(data, nullptr);
      EXPECT_EQ(data->cube_local_records.size(), 5U);
      EXPECT_EQ(data->projected_local_records.size(), 9U);
      EXPECT_EQ(data->local_shadow_references.size(), 14U);
      const auto surfaces
        = shadows->InspectPointShadowSurfaces(ctx.current_view.view_id);
      ASSERT_FALSE(surfaces.empty());
      const auto* surface = surfaces.front().get();
      ASSERT_NE(surface, nullptr);
      if (!first_surface) {
        first_surface = surface->shared_from_this();
        first_view = ctx.current_view.view_id;
      } else if (ctx.current_view.view_id != first_view) {
        EXPECT_EQ(surface, first_surface.get());
      }
      ++inspected;
    };
    for (const bool forward : { false, true }) {
      surface_view_id = forward ? 101U : 100U;
      SetSurface(data::MaterialDomain::kOpaque);
      ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0.0F, 3U));
      EXPECT_GT(ReadFloatTexture(*probe->color).at(0).at(0), 1.0e-6F);
    }
    EXPECT_EQ(inspected, 6U);
    const auto memory = renderer_->GetLightingAllocationBudget()->Snapshot();
    EXPECT_GT(memory.allocated.get(), 0U);
    EXPECT_LE(memory.allocated, memory.limits.total);
    RecordProperty("lighting_allocation_bytes", memory.allocated.get());
  }

  NOLINT_TEST_F(
    ShadowAdmissionGpuTest, ContactDepthIsConditionalAndSelfDoesNotOcclude)
  {
    auto light_node = AddPoint(0U);
    bool expected_contact = false;
    probe->inspect = [&](const auto& ctx, const auto&, unsigned) -> void {
      auto* owner = RendererPublicationProbe::GetSceneRenderer(*renderer_);
      auto* shadows = RendererPublicationProbe::GetShadowService(*owner);
      const auto data = shadows->InspectShadowData(ctx.current_view.view_id);
      ASSERT_NE(data, nullptr);
      EXPECT_EQ(data->bindings.contact_enabled, expected_contact ? 1U : 0U);
      EXPECT_EQ(data->bindings.contact_depth_srv.IsValid(), expected_contact);
      if (expected_contact) {
        const auto index = data->bindings.contact_depth_srv.get();
        EXPECT_GE(index, bindless::generated::kTexturesShaderIndexBase);
        EXPECT_LT(index,
          bindless::generated::kTexturesShaderIndexBase
            + bindless::generated::kTexturesCapacity);
      }
    };
    for (const bool forward : { false, true }) {
      SetSurface(data::MaterialDomain::kOpaque);
      float baseline = 0.0F;
      for (const bool contact : { false, true, false }) {
        const auto current = light_node.GetLightAs<scene::PointLight>();
        ASSERT_HAS_VALUE(current);
        auto light = std::make_unique<scene::PointLight>(current->get());
        light->Common().shadow.contact_shadows = contact;
        ASSERT_TRUE(light_node.ReplaceLight(std::move(light)));
        expected_contact = contact;
        ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0.0F, 2U));
        const auto measured = ReadFloatTexture(*probe->color).at(0).at(0);
        ASSERT_GT(measured, 1.0e-6F);
        if (contact) {
          EXPECT_NEAR(measured, baseline, (baseline * 0.005F) + 2.0e-5F);
        } else {
          baseline = measured;
        }
      }
    }
    ASSERT_TRUE(light_node.EditLight<scene::PointLight>(
      [](auto& light) -> void { light.Common().affects_world = false; }));
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F, 1U));
    const auto empty
      = renderer_->InspectViewRenderStatus(ViewId { surface_view_id });
    ASSERT_HAS_VALUE(empty);
    EXPECT_TRUE(empty->IsCaptureEligible(frame::SequenceNumber { sequence }));
  }

  NOLINT_TEST_F(ShadowAdmissionGpuTest,
    ContactOccluderUsesCasterMaskAndReceiverGateInBothPaths)
  {
    constexpr unsigned extent = 129U;
    constexpr unsigned center = (extent / 2U * extent) + (extent / 2U);
    view.viewport.width = view.viewport.height = static_cast<float>(extent);
    const auto perspective = camera.GetCameraAs<scene::PerspectiveCamera>();
    ASSERT_HAS_VALUE(perspective);
    perspective->get().SetViewport(view.viewport);
    auto output = CreateRegisteredTexture({
      .width = extent,
      .height = extent,
      .format = Format::kRGBA32Float,
      .is_render_target = true,
      .initial_state = graphics::ResourceStates::kCommon,
    });
    framebuffer = Backend().CreateFramebuffer(
      graphics::FramebufferDesc {}.AddColorAttachment(output));
    auto light = AddPoint(0U);
    ASSERT_TRUE(light.GetTransform().SetLocalPosition({ 1.0F, 0.0F, 0.0F }));
    ASSERT_TRUE(light.EditLight<scene::PointLight>([](auto& candidate) -> void {
      // Keep the small blocker partly visible through the map filter so the
      // contact pass has a measurable contribution of its own.
      candidate.Common().shadow.bias = 1.0F;
    }));
    auto blocker = scene->CreateNode("Contact blocker");
    blocker.GetRenderable().SetGeometry(
      mesh_node.GetRenderable().GetGeometry());
    blocker.GetRenderable().SetMaterialOverride(oxygen::data::LodIndex { 0 },
      oxygen::data::SubmeshIndex { 0 }, MakeEmissiveMaterial(0.0F));
    ASSERT_TRUE(blocker.GetTransform().SetLocalScale({ 0.05F, 0.05F, 0.05F }));
    ASSERT_TRUE(
      blocker.GetTransform().SetLocalPosition({ 0.09F, 0.0F, -0.8606F }));
    auto mesh_flags = mesh_node.GetFlags();
    ASSERT_HAS_VALUE(mesh_flags);
    auto blocker_flags = blocker.GetFlags();
    ASSERT_HAS_VALUE(blocker_flags);
    mesh_flags->get().SetLocalValue(
      scene::SceneNodeFlags::kCastsShadows, false);
    expected_draws = 2U;
    std::shared_ptr<const graphics::Texture> contact_depth;
    probe->inspect = [&](const auto& ctx, const auto&, unsigned) -> void {
      const auto* owner
        = RendererPublicationProbe::GetSceneRenderer(*renderer_);
      contact_depth = RendererPublicationProbe::GetShadowService(*owner)
                        ->InspectContactShadowSurface(ctx.current_view.view_id);
    };
    for (const bool forward : { false, true }) {
      SetSurface(data::MaterialDomain::kOpaque);
      mesh_flags->get().SetLocalValue(
        scene::SceneNodeFlags::kReceivesShadows, true);
      blocker_flags->get().SetLocalValue(
        scene::SceneNodeFlags::kCastsShadows, false);
      ASSERT_TRUE(
        light.EditLight<scene::PointLight>([](auto& candidate) -> void {
          candidate.Common().shadow.contact_shadows = false;
        }));
      ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0.0F, 2U));
      const auto unshadowed = ReadFloatTexture(*probe->color).at(center).at(0);
      blocker_flags->get().SetLocalValue(
        scene::SceneNodeFlags::kCastsShadows, true);
      ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0.0F, 1U));
      const auto baseline = ReadFloatTexture(*probe->color).at(center).at(0);
      ASSERT_GT(baseline, 1.0e-6F);
      EXPECT_LE(baseline, unshadowed);
      ASSERT_TRUE(
        light.EditLight<scene::PointLight>([](auto& candidate) -> void {
          candidate.Common().shadow.contact_shadows = true;
        }));
      ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0.0F, 1U));
      ASSERT_NE(contact_depth, nullptr);
      EXPECT_EQ(contact_depth->GetDescriptor().format, Format::kDepth32);
      EXPECT_EQ(contact_depth->GetDescriptor().width, extent);
      EXPECT_EQ(contact_depth->GetDescriptor().height, extent);
      EXPECT_LT(
        ReadFloatTexture(*probe->color).at(center).at(0), baseline * 0.8F);
      blocker_flags->get().SetLocalValue(
        scene::SceneNodeFlags::kCastsShadows, false);
      ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0.0F, 1U));
      EXPECT_NEAR(ReadFloatTexture(*probe->color).at(center).at(0), unshadowed,
        unshadowed * 0.005F);
      blocker_flags->get().SetLocalValue(
        scene::SceneNodeFlags::kCastsShadows, true);
      mesh_flags->get().SetLocalValue(
        scene::SceneNodeFlags::kReceivesShadows, false);
      ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0.0F, 1U));
      EXPECT_NEAR(ReadFloatTexture(*probe->color).at(center).at(0), unshadowed,
        unshadowed * 0.005F);
    }
  }

  NOLINT_TEST_F(ShadowAdmissionGpuTest,
    OffscreenLightAndCasterStillShadowVisibleReceiverInBothPaths)
  {
    constexpr unsigned extent = 129U;
    constexpr unsigned center = (extent / 2U * extent) + (extent / 2U);
    view.viewport.width = view.viewport.height = static_cast<float>(extent);
    const auto perspective = camera.GetCameraAs<scene::PerspectiveCamera>();
    ASSERT_HAS_VALUE(perspective);
    perspective->get().SetViewport(view.viewport);
    auto output = CreateRegisteredTexture({
      .width = extent,
      .height = extent,
      .format = Format::kRGBA32Float,
      .is_render_target = true,
      .initial_state = graphics::ResourceStates::kCommon,
    });
    framebuffer = Backend().CreateFramebuffer(
      graphics::FramebufferDesc {}.AddColorAttachment(output));
    auto light = AddPoint(0U);
    ASSERT_TRUE(light.GetTransform().SetLocalPosition({ 2.0F, 0.0F, 0.0F }));
    auto blocker = scene->CreateNode("Offscreen shadow caster");
    blocker.GetRenderable().SetGeometry(
      mesh_node.GetRenderable().GetGeometry());
    blocker.GetRenderable().SetMaterialOverride(oxygen::data::LodIndex { 0 },
      oxygen::data::SubmeshIndex { 0 }, MakeEmissiveMaterial(0.0F));
    ASSERT_TRUE(blocker.GetTransform().SetLocalScale({ 0.2F, 0.2F, 0.2F }));
    ASSERT_TRUE(blocker.GetTransform().SetLocalPosition({ 1.0F, 0.0F, -0.3F }));
    auto mesh_flags = mesh_node.GetFlags();
    ASSERT_HAS_VALUE(mesh_flags);
    auto blocker_flags = blocker.GetFlags();
    ASSERT_HAS_VALUE(blocker_flags);
    mesh_flags->get().SetLocalValue(
      scene::SceneNodeFlags::kCastsShadows, false);
    expected_draws = 2U;
    bool is_forward = false;
    bool is_spot = false;
    probe->inspect = [&](const auto& ctx, const auto&, unsigned) -> void {
      const auto frustum = ctx.current_view.resolved_view->GetFrustum();
      EXPECT_FALSE(frustum.IntersectsSphere({ 2.0F, 0.0F, 0.0F }, 0.0F));
      EXPECT_TRUE(frustum.IntersectsSphere({ 2.0F, 0.0F, 0.0F }, 3.0F));
      // The entire blocker lies outside the receiver's view, but intersects
      // the light-to-receiver ray halfway between its endpoints.
      EXPECT_FALSE(frustum.IntersectsSphere({ 1.0F, 0.0F, -0.5F }, 0.57F));
      auto* owner = RendererPublicationProbe::GetSceneRenderer(*renderer_);
      const auto shadows
        = RendererPublicationProbe::GetShadowService(*owner)->InspectShadowData(
          ctx.current_view.view_id);
      ASSERT_NE(shadows, nullptr);
      EXPECT_EQ(shadows->cube_local_records.size(), is_spot ? 0U : 1U);
      EXPECT_EQ(shadows->projected_local_records.size(), is_spot ? 1U : 0U);
      if (!is_forward) {
        const auto& state = RendererPublicationProbe::GetLightingService(*owner)
                              ->GetLastDeferredLightingState();
        EXPECT_EQ(state.point_light_count, is_spot ? 0U : 1U);
        EXPECT_EQ(state.spot_light_count, is_spot ? 1U : 0U);
      }
    };
    for (const bool spot : { false, true }) {
      is_spot = spot;
      SCOPED_TRACE(spot);
      if (spot) {
        auto candidate = std::make_unique<scene::SpotLight>();
        candidate->SetLuminousFluxLm(1.0F);
        candidate->Common().casts_shadows = true;
        candidate->Common().shadow.resolution_hint
          = scene::ShadowResolutionHint::kLow;
        ASSERT_TRUE(light.ReplaceLight(std::move(candidate)));
        // Rotate local -Y onto the light-to-receiver vector (-2, 0, -1).
        ASSERT_TRUE(light.GetTransform().SetLocalRotation(
          glm::quat { 0.70710678F, 0.31622777F, 0.0F, -0.63245553F }));
      }
      for (const bool forward : { false, true }) {
        is_forward = forward;
        SetSurface(data::MaterialDomain::kOpaque);
        for (const float range : { 3.0F, 4096.0F }) {
          SCOPED_TRACE(range);
          if (spot) {
            ASSERT_TRUE(light.EditLight<scene::SpotLight>(
              [range](auto& candidate) -> void { candidate.SetRange(range); }));
          } else {
            ASSERT_TRUE(light.EditLight<scene::PointLight>(
              [range](auto& candidate) -> void { candidate.SetRange(range); }));
          }
          auto lit = 0.0F;
          for (const bool casts : { false, true, false }) {
            expected_draws = casts ? 2U : 1U;
            blocker_flags->get().SetLocalValue(
              scene::SceneNodeFlags::kCastsShadows, casts);
            ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0.0F, 3U));
            const auto measured
              = ReadFloatTexture(*probe->color).at(center).at(0);
            if (casts) {
              EXPECT_LT(measured, lit * 0.5F);
            } else if (lit == 0.0F) {
              lit = measured;
              ASSERT_GT(lit, 1.0e-6F);
            } else {
              EXPECT_NEAR(measured, lit, (lit * 0.005F) + 2.0e-5F);
            }
          }
        }
      }
    }
  }

  class BoundedShadowAdmissionGpuTest : public ShadowAdmissionGpuTest {
  protected:
    auto ConfigureRenderer(RendererConfig& config) const -> void override
    {
      config.lighting_allocation_limit_bytes = 64ULL * 1024ULL * 1024ULL;
      config.lighting_compact_index_limit_bytes = 8ULL * 1024ULL * 1024ULL;
    }
  };

  NOLINT_TEST_F(BoundedShadowAdmissionGpuTest,
    FailedGrowthRejectsWholeViewAndSmallerRequestRecovers)
  {
    for (unsigned index = 0U; index < 4U; ++index) {
      AddPoint(index);
    }
    SetSurface(data::MaterialDomain::kOpaque);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F, 3U));
    const auto before = renderer_->GetLightingAllocationBudget()->Snapshot();
    auto extras = std::vector<scene::SceneNode> {};
    for (unsigned index = 4U; index < 11U; ++index) {
      extras.push_back(AddPoint(index));
    }
    ASSERT_NO_FATAL_FAILURE(
      RenderSurface(false, 0.0F, 1U, ExpectedViewOutcome::kRejected));
    const auto failure
      = renderer_->InspectViewRenderStatus(ViewId { surface_view_id });
    ASSERT_HAS_VALUE(failure);
    EXPECT_EQ(failure->state, ViewRenderState::kFailed);
    EXPECT_EQ(failure->failure, ViewRenderFailure::kLighting);
    EXPECT_FALSE(
      failure->IsCaptureEligible(frame::SequenceNumber { sequence }));
    const auto rejected = renderer_->GetLightingAllocationBudget()->Snapshot();
    EXPECT_GT(rejected.rejected_requests, before.rejected_requests);
    EXPECT_GT(rejected.last_requested, rejected.last_available);
    EXPECT_LE(rejected.allocated, rejected.limits.total);
    for (auto& extra : extras) {
      auto disabled = std::make_unique<scene::PointLight>();
      disabled->Common().affects_world = false;
      ASSERT_TRUE(extra.ReplaceLight(std::move(disabled)));
    }
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0.0F, 1U));
    EXPECT_GT(ReadFloatTexture(*probe->color).at(0).at(0), 1.0e-6F);
    const auto recovery
      = renderer_->InspectViewRenderStatus(ViewId { surface_view_id });
    ASSERT_HAS_VALUE(recovery);
    EXPECT_TRUE(
      recovery->IsCaptureEligible(frame::SequenceNumber { sequence }));
    EXPECT_EQ(
      renderer_->GetLightingAllocationBudget()->Snapshot().rejected_requests,
      rejected.rejected_requests);
    RecordProperty("rejected_required_bytes", rejected.last_requested.get());
    RecordProperty("rejected_available_bytes", rejected.last_available.get());
  }
  NOLINT_TEST_F(ShadowAdmissionGpuTest,
    CompatibleViewsShareFiveCubeAndNineProjectedMapsInOneFrame)
  {
    for (unsigned index = 0; index < 5; ++index) {
      AddPoint(index);
    }
    for (unsigned index = 0; index < 9; ++index) {
      auto node = scene->CreateNode("Shared spot " + std::to_string(index));
      auto light = std::make_unique<scene::SpotLight>();
      light->SetRange(3);
      light->SetLuminousFluxLm(1);
      light->Common().casts_shadows = true;
      light->Common().shadow.resolution_hint
        = scene::ShadowResolutionHint::kLow;
      ASSERT_TRUE(node.AttachLight(std::move(light)));
      ASSERT_TRUE(node.GetTransform().SetLocalRotation(
        glm::quat { 0.70710678F, 0.70710678F, 0, 0 }));
    }
    auto output = CreateRegisteredTexture({
      .width = 1,
      .height = 1,
      .format = Format::kRGBA32Float,
      .is_render_target = true,
      .initial_state = graphics::ResourceStates::kCommon,
    });
    const std::array targets {
      framebuffer,
      Backend().CreateFramebuffer(
        graphics::FramebufferDesc {}.AddColorAttachment(output)),
    };
    const std::array handles {
      CompositionView::ViewStateHandle { 9101 },
      CompositionView::ViewStateHandle { 9102 },
    };
    const std::array intents { ViewId { 9101 }, ViewId { 9102 } };
    std::array<std::shared_ptr<const graphics::Texture>, 2> colors;
    std::array<postprocess::ExposurePass::FrameLease, 2> exposures;
    std::array<ShadowFrameData, 2> shadow_records;
    std::array<bool, 2> seen {};
    uint32_t writers = 0;
    probe->prepare = [](RenderContext&) -> void { };
    probe->inspect = [&](const RenderContext& ctx,
                       const SceneTextureExtractRef& color, unsigned) -> void {
      const auto index
        = ctx.current_view.view_state_handle == handles.front() ? 0U : 1U;
      auto* owner = RendererPublicationProbe::GetSceneRenderer(*renderer_);
      auto* shadows = RendererPublicationProbe::GetShadowService(*owner);
      const auto data = shadows->InspectShadowData(ctx.current_view.view_id);
      ASSERT_NE(data, nullptr);
      shadow_records.at(index) = *data;
      ASSERT_TRUE(color.valid);
      colors.at(index) = color.texture->shared_from_this();
      exposures.at(index) = color.exposure;
      seen.at(index) = true;
      EXPECT_EQ(shadows->GetLastRenderState().attached_map_uses, 14U);
      EXPECT_EQ(shadows->GetLastRenderState().attached_backing_uses, 2U);
      writers += shadows->GetLastRenderState().rendered_point_shadow_count
        + shadows->GetLastRenderState().rendered_spot_shadow_count;
    };
    const auto verbosity = loguru::g_global_verbosity;
    const auto restore_verbosity = ScopeGuard(
      [&] noexcept -> void { loguru::g_global_verbosity = verbosity; });
    loguru::g_global_verbosity = loguru::Verbosity_WARNING;
    settings.mode = engine::ExposureMode::kManual;
    settings.manual_ev = 0;
    for (unsigned mode = 0; mode < 3; ++mode) {
      const bool forward = mode == 1;
      SetSurface(mode == 2 ? data::MaterialDomain::kAlphaBlended
                           : data::MaterialDomain::kOpaque);
      // Warm the actual two-view family before comparing resident output.
      for (unsigned repeat = 0; repeat < 6; ++repeat) {
        SCOPED_TRACE((std::to_string(mode) + ":" + std::to_string(repeat)));
        writers = 0;
        seen = {};
        colors = {};
        scene->Update();
        scene->SyncObservers();
#if defined(_MSC_VER) && defined(_DEBUG)
        CpuAllocationCounter cpu_allocations;
#endif
        const auto slot = frame::Slot { sequence % 3 };
        Backend().BeginFrame(frame::SequenceNumber { ++sequence }, slot);
        frame.SetFrameSlot(slot, engine::internal::EngineTagFactory::Get());
        frame.SetFrameSequenceNumber(frame::SequenceNumber { sequence },
          engine::internal::EngineTagFactory::Get());
        renderer_->OnFrameStart(observer_ptr { &frame });
        for (unsigned publish = 0; publish < 2; ++publish) {
          const auto index = repeat % 2 != 0U ? 1U - publish : publish;
          auto input
            = CompositionView::ForScene(intents.at(index), view, camera);
          input.view_state_handle = handles.at(index);
          input.render_settings.exposure = settings;
          input.render_settings.exposure->manual_ev
            = static_cast<float>(index * 2);
          ASSERT_NE(
            renderer_->PublishRuntimeCompositionView(frame,
              { .composition_view = input,
                .render_target = observer_ptr { targets.at(index).get() },
                .composite_source = observer_ptr { targets.at(index).get() } },
              forward ? ShadingMode::kForward : ShadingMode::kDeferred),
            kInvalidViewId);
        }
        auto loop = co::testing::TestEventLoop {};
        // Run completes synchronously before the coroutine closure leaves
        // scope.
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
        co::Run(loop, [&] -> co::Co<void> {
          co_await renderer_->OnPreRender(observer_ptr { &frame });
          co_await renderer_->OnRender(observer_ptr { &frame });
        });
        renderer_->OnFrameEnd(observer_ptr { &frame });
        Backend().EndFrame(frame::SequenceNumber { sequence }, slot);
#if defined(_MSC_VER) && defined(_DEBUG)
        cpu_allocations.Stop();
        if (repeat >= 3) {
          const auto suffix
            = std::to_string(mode) + "_" + std::to_string(repeat);
          RecordProperty("render_thread_debug_allocations_" + suffix,
            cpu_allocations.allocations);
          RecordProperty("render_thread_debug_requested_bytes_" + suffix,
            cpu_allocations.requested_bytes);
        }
#endif
        WaitForQueueIdle();
        ASSERT_TRUE(seen.front() && seen.back());
        for (const auto& record : shadow_records) {
          EXPECT_EQ(record.cube_local_records.size(), 5U);
          EXPECT_EQ(record.projected_local_records.size(), 9U);
          EXPECT_EQ(record.local_shadow_references.size(), 14U);
        }
        const auto& [first_view, second_view] = shadow_records;
        for (size_t index = 0; index < 5; ++index) {
          const auto& first = first_view.cube_local_records.at(index);
          const auto& second = second_view.cube_local_records.at(index);
          EXPECT_EQ(first.surface_srv, second.surface_srv);
          EXPECT_EQ(first.first_array_layer, second.first_array_layer);
        }
        for (size_t index = 0; index < 9; ++index) {
          const auto& first = first_view.projected_local_records.at(index);
          const auto& second = second_view.projected_local_records.at(index);
          EXPECT_EQ(first.surface_srv, second.surface_srv);
          EXPECT_EQ(first.array_layer, second.array_layer);
        }
        EXPECT_LE(writers, 14U);
        if (repeat < 3) {
          continue;
        }
        EXPECT_EQ(writers, 0U);
        const auto& [first_exposure, second_exposure] = exposures;
        ASSERT_TRUE(first_exposure && second_exposure);
        const auto first_domain = Read<FrameExposureData>(
          *first_exposure->buffer, graphics::ResourceStates::kShaderResource);
        const auto second_domain = Read<FrameExposureData>(
          *second_exposure->buffer, graphics::ResourceStates::kShaderResource);
        ASSERT_GT(first_domain.pre_exposure, 0.0F);
        ASSERT_GT(second_domain.pre_exposure, 0.0F);
        const auto first = ReadFloatTexture(*colors.front(), true);
        const auto second = ReadFloatTexture(*colors.back(), true);
        ASSERT_EQ(first.size(), second.size());
        ASSERT_FALSE(first.empty());
        EXPECT_GT(first.front().front(), 0.0F);
        for (size_t pixel = 0; pixel < first.size(); ++pixel) {
          const auto& lhs = first.at(pixel);
          const auto& rhs = second.at(pixel);
          for (unsigned channel = 0; channel < 3; ++channel) {
            EXPECT_NEAR(lhs.at(channel) / first_domain.pre_exposure,
              rhs.at(channel) / second_domain.pre_exposure, 1.0e-5F);
          }
          EXPECT_EQ(lhs.at(3), rhs.at(3));
        }
      }
    }
  }

  NOLINT_TEST_F(ShadowAdmissionGpuTest,
    RetainedShadowReadbackSurvivesUnsubmittedFrameSlotRollovers)
  {
    auto light = scene->CreateNode("Retained capture spot");
    auto spot = std::make_unique<scene::SpotLight>();
    spot->SetRange(3);
    spot->SetLuminousFluxLm(1);
    spot->Common().casts_shadows = true;
    spot->Common().shadow.resolution_hint = scene::ShadowResolutionHint::kLow;
    ASSERT_TRUE(light.AttachLight(std::move(spot)));
    ASSERT_TRUE(light.GetTransform().SetLocalRotation(
      glm::quat { 0.70710678F, 0.70710678F, 0, 0 }));
    SetSurface(data::MaterialDomain::kOpaque);
    ShadowContentLease current;
    probe->inspect = [&](const auto& ctx, const auto&, unsigned) -> void {
      auto* owner = RendererPublicationProbe::GetSceneRenderer(*renderer_);
      current
        = RendererPublicationProbe::GetShadowService(*owner)
            ->RetainLocalContent(ctx.current_view.view_id, light.GetHandle());
      ASSERT_TRUE(current);
    };
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 3));
    auto retained = current;
    const auto capture = [&](const ShadowContentLease& lease,
                           const std::function<void()>& delay) -> float {
      const auto texture = lease.Texture();
      const auto& desc = texture->GetDescriptor();
      const auto row_pitch = ((uint64_t { desc.width } * sizeof(float)) + 255U)
        & ~uint64_t { 255 };
      auto readback = CreateRegisteredBuffer({
        .size_bytes = row_pitch * desc.height,
        .memory = graphics::BufferMemory::kReadBack,
      });
      auto recording = AcquireRecorder("Retained shadow readback",
        graphics::QueueRole::kGraphics, graphics::SubmissionPolicy::kExplicit);
      CHECK_F(lease.AttachReadback(*recording, Backend().GetResourceRegistry())
          .has_value());
      if (delay) {
        delay();
      }
      recording->BeginTrackingResourceState(
        *readback, graphics::ResourceStates::kCopyDest);
      recording->RequireResourceState(
        *texture, graphics::ResourceStates::kCopySource);
      recording->FlushBarriers();
      // The general readback facade intentionally rejects typeless depth.
      // Use the existing native copy primitive with an owned, aligned buffer.
      recording->CopyTextureToBuffer(*readback, *texture,
        {
          .buffer_offset = OffsetBytes { 0U },
          .buffer_row_pitch = SizeBytes { row_pitch },
          .buffer_slice_pitch = SizeBytes { row_pitch * desc.height },
          .texture_slice = { .array_slice = lease.FirstLayer() },
        });
      recording->RequireResourceStateFinal(
        *texture, graphics::ResourceStates::kShaderResource);
      CHECK_F(recording.SubmitWithReceipt().outcome
        == graphics::SubmissionOutcome::kSubmitted);
      WaitForQueueIdle();
      const auto offset = ((desc.height / 2U) * row_pitch)
        + ((desc.width / 2U) * sizeof(float));
      const auto bytes = std::span {
        static_cast<const std::byte*>(readback->Map()),
        static_cast<std::size_t>(row_pitch * desc.height),
      };
      float depth = 0;
      std::memcpy(&depth, bytes.subspan(offset).data(), sizeof(depth));
      readback->UnMap();
      return depth;
    };
    auto* scene_renderer
      = RendererPublicationProbe::GetSceneRenderer(*renderer_);
    auto* shadows = RendererPublicationProbe::GetShadowService(*scene_renderer);
    const auto initial_copies
      = shadows->InspectLocalSharing().copy_on_write_reader;
    const auto original = capture(retained, {});
    EXPECT_GT(original, 0.0F);
    const auto delayed = capture(retained, [&] -> void {
      CHECK_F(mesh_node.GetTransform().SetLocalPosition({ 0, 0, -0.3F }));
      RenderSurface(false, 0, frame::kFramesInFlight.get() + 2U);
    });
    EXPECT_FLOAT_EQ(delayed, original);
    ASSERT_TRUE(current);
    EXPECT_NE(current.Texture(),
      retained.Texture()); // unsubmitted exclusive backing interval
    const auto changed = capture(current, {});
    EXPECT_GT(std::abs(changed - original), 0.05F);
    const auto shadow_bytes
      = [&](const ShadowSharingDiagnostics& diagnostic) -> std::uint64_t {
      std::uint64_t bytes = 0;
      for (const auto& backing : diagnostic.backings) {
        const auto desc = backing.texture->GetNativeResource()
                            ->AsPointer<ID3D12Resource>()
                            ->GetDesc();
        bytes += Backend()
                   .GetCurrentDevice()
                   ->GetResourceAllocationInfo(0, 1, &desc)
                   .SizeInBytes;
      }
      return bytes;
    };
    std::uint64_t retained_bytes = 0;
    {
      const auto diagnostic = shadows->InspectLocalSharing();
      EXPECT_EQ(diagnostic.backings.size(), 2U);
      EXPECT_EQ(diagnostic.live_versions, 2U);
      EXPECT_EQ(diagnostic.copy_on_write_reader - initial_copies, 1U);
      retained_bytes = shadow_bytes(diagnostic);
      RecordProperty(
        "retained_copy_on_write_shadow_native_bytes", retained_bytes);
    }
    retained = {};
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 5));
    Backend().Flush();
    const auto released = shadows->InspectLocalSharing();
    EXPECT_EQ(released.backings.size(), 1U);
    EXPECT_EQ(released.live_versions, 1U);
    const auto resident_bytes = shadow_bytes(released);
    EXPECT_EQ(retained_bytes, 2U * resident_bytes);
    RecordProperty("post_release_shadow_native_bytes", resident_bytes);
  }

  NOLINT_TEST_F(ShadowBudgetGpuTest,
    TightBudgetUsesExactLayersAndChargesDiagnosticRetention)
  {
    auto light = AddPoint(0);
    SetSurface(data::MaterialDomain::kOpaque);
    std::shared_ptr<const graphics::Texture> retained;
    bool enabled = true;
    probe->inspect = [&](const auto& ctx, const auto&, unsigned) -> void {
      auto* owner = RendererPublicationProbe::GetSceneRenderer(*renderer_);
      auto* shadows = RendererPublicationProbe::GetShadowService(*owner);
      const auto surfaces
        = shadows->InspectPointShadowSurfaces(ctx.current_view.view_id);
      if (!enabled) {
        EXPECT_TRUE(surfaces.empty());
        return;
      }
      ASSERT_EQ(surfaces.size(), 1U);
      EXPECT_EQ(surfaces.front()->GetDescriptor().array_size, 6U);
      retained = surfaces.front();
    };
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 3));
    ASSERT_TRUE(retained);
    const auto budget = renderer_->GetLightingAllocationBudget();
    EXPECT_GT(budget->Snapshot().rejected_requests,
      0U); // spare capacity rejected, one map admitted
    auto native
      = retained->GetNativeResource()->AsPointer<ID3D12Resource>()->GetDesc();
    const auto native_bytes = Backend()
                                .GetCurrentDevice()
                                ->GetResourceAllocationInfo(0, 1, &native)
                                .SizeInBytes;
    ASSERT_TRUE(light.EditLight<scene::PointLight>(
      [](auto& value) -> void { value.Common().affects_world = false; }));
    enabled = false;
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 5));
    Backend().Flush();
    const auto charged = budget->Snapshot().allocated.get();
    EXPECT_GE(charged, native_bytes);
    auto* scene_renderer
      = RendererPublicationProbe::GetSceneRenderer(*renderer_);
    auto* shadows = RendererPublicationProbe::GetShadowService(*scene_renderer);
    {
      const auto diagnostic = shadows->InspectLocalSharing();
      EXPECT_EQ(diagnostic.aliases, 0U);
      EXPECT_EQ(diagnostic.live_versions, 0U);
      ASSERT_EQ(diagnostic.backings.size(), 1U);
      EXPECT_TRUE(diagnostic.backings.front().closing);
      EXPECT_EQ(diagnostic.backings.front().texture, retained);
      RecordProperty("pending_retirement_shadow_native_bytes", native_bytes);
    }
    retained.reset();
    EXPECT_EQ(charged - budget->Snapshot().allocated.get(), native_bytes);
    EXPECT_TRUE(shadows->InspectLocalSharing().backings.empty());
    RecordProperty("retained_shadow_native_bytes", native_bytes);
  }

} // namespace
} // namespace oxygen::vortex::testing
