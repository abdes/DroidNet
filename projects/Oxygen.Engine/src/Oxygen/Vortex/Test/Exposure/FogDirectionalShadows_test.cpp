//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>
#include <utility>

#include <glm/gtc/matrix_access.hpp>

#include <Oxygen/Core/Bindless/Generated.BindlessAbi.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ClearFlags.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Lighting/Types/DirectionalLightForwardData.h>
#include <Oxygen/Vortex/Lighting/Types/LightGridBuildStatus.h>
#include <Oxygen/Vortex/Shadows/Types/DirectionalShadowRecord.h>
#include <Oxygen/Vortex/Shadows/Types/LightShadowReference.h>
#include <Oxygen/Vortex/Shadows/Types/ShadowCascadeBinding.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>
#include <Oxygen/Vortex/Test/Fixtures/RendererPublicationProbe.h>
#include <Oxygen/Vortex/Types/LightingFrameBindings.h>
#include <Oxygen/Vortex/Types/ShadowFrameBindings.h>
#include <Oxygen/Vortex/Types/ViewConstants.h>
#include <Oxygen/Vortex/Types/ViewFrameBindings.h>

namespace oxygen::vortex::testing::exposure {

NOLINT_TEST_F(ExposureGpuTest, FogShadowsFollowAtmosphereSourceIdentity)
{
  namespace root = oxygen::bindless::generated::d3d12;
  using graphics::DescriptorVisibility;
  using graphics::ResourceStates;
  using graphics::ResourceViewType;
  auto& allocator = renderer_->GetGraphics()->GetDescriptorAllocator();
  auto& registry = Backend().GetResourceRegistry();
  const auto publish_records
    = [&]<typename T, std::size_t N>(const std::array<T, N>& records) -> auto {
    auto buffer = CreateRegisteredBuffer({
      .size_bytes = sizeof(records),
      .memory = graphics::BufferMemory::kUpload,
    });
    buffer->Update(records.data(), sizeof(records), 0U);
    auto handle = allocator.AllocateBindless(
      oxygen::bindless::generated::kGlobalSrvDomain,
      ResourceViewType::kStructuredBuffer_SRV);
    const auto slot = allocator.GetShaderVisibleIndex(handle);
    registry.RegisterView(*buffer, std::move(handle),
      graphics::BufferViewDescription {
        .view_type = ResourceViewType::kStructuredBuffer_SRV,
        .range = { 0U, sizeof(records) },
        .stride = sizeof(T),
      });
    return slot;
  };
  std::array<ShaderVisibleIndex, 2> surfaces {};
  for (std::uint32_t index = 0U; index < surfaces.size(); ++index) {
    auto texture = CreateRegisteredTexture({
      .width = 1U,
      .height = 1U,
      .format = Format::kDepth32,
      .texture_type = TextureType::kTexture2DArray,
      .is_shader_resource = true,
      .is_render_target = true,
      .initial_state = ResourceStates::kCommon,
    });
    auto srv = allocator.AllocateBindless(
      oxygen::bindless::generated::kGlobalSrvDomain,
      ResourceViewType::kTexture_SRV);
    surfaces.at(index) = allocator.GetShaderVisibleIndex(srv);
    registry.RegisterView(*texture, std::move(srv),
      graphics::TextureViewDescription {
        .view_type = ResourceViewType::kTexture_SRV,
        .format = Format::kR32Float,
        .dimension = TextureType::kTexture2DArray,
      });
    auto dsv = allocator.AllocateRaw(
      ResourceViewType::kTexture_DSV, DescriptorVisibility::kCpuOnly);
    const auto depth_view = registry.RegisterView(*texture, std::move(dsv),
      graphics::TextureViewDescription {
        .view_type = ResourceViewType::kTexture_DSV,
        .visibility = DescriptorVisibility::kCpuOnly,
        .format = Format::kDepth32,
        .dimension = TextureType::kTexture2DArray,
      });
    auto recorder = AcquireRecorder("Fog source shadow depths");
    EnsureTracked(*recorder, texture, ResourceStates::kCommon);
    recorder->RequireResourceState(*texture, ResourceStates::kDepthWrite);
    recorder->FlushBarriers();
    // Reversed Z: surface 0 is clear/lit; surface 1 occludes a receiver at 0.5.
    recorder->ClearDepthStencilView(*texture, depth_view,
      graphics::ClearFlags::kDepth, static_cast<float>(index), 0U);
    recorder->RequireResourceStateFinal(
      *texture, ResourceStates::kShaderResource);
  }
  auto output = CreateRegisteredTexture({
    .width = 1U,
    .height = 1U,
    .depth = 1U,
    .format = Format::kRGBA32Float,
    .texture_type = TextureType::kTexture3D,
    .is_shader_resource = true,
    .is_uav = true,
    .initial_state = ResourceStates::kCommon,
  });
  auto uav = allocator.AllocateRaw(
    ResourceViewType::kTexture_UAV, DescriptorVisibility::kShaderVisible);
  const auto output_slot = allocator.GetShaderVisibleIndex(uav);
  registry.RegisterView(*output, std::move(uav),
    graphics::TextureViewDescription {
      .view_type = ResourceViewType::kTexture_UAV,
      .format = Format::kRGBA32Float,
      .dimension = TextureType::kTexture3D,
    });
  const auto pipeline
    = graphics::ComputePipelineDesc::Builder {}
        .SetComputeShader(graphics::ShaderRequest {
          .stage = ShaderType::kCompute,
          .source_path = "Vortex/Services/Environment/VolumetricFog.hlsl",
          .entry_point = "VortexVolumetricFogCS",
        })
        .SetRootBindings(ExposureProbeRootBindings())
        .Build();

  // Secondary precedes an ordinary directional and Primary in the selection.
  const auto directionals = std::array {
    DirectionalLightForwardData {
      .atmosphere_light_slot = AtmosphereLightIndex { 1U },
      .selection_index = LightSelectionIndex { 0U },
    },
    DirectionalLightForwardData {
      .selection_index = LightSelectionIndex { 1U },
    },
    DirectionalLightForwardData {
      .atmosphere_light_slot = AtmosphereLightIndex { 0U },
      .selection_index = LightSelectionIndex { 2U },
    },
  };
  const auto references = std::array {
    LightShadowReference {
      .projection_kind = kShadowProjectionCascaded2D,
      .record_index = ShadowRecordIndex { 1U },
      .selection_index = LightSelectionIndex { 0U },
      .coverage_state = kShadowCoverageComplete,
    },
    LightShadowReference {},
    LightShadowReference {
      .projection_kind = kShadowProjectionCascaded2D,
      .record_index = ShadowRecordIndex { 0U },
      .selection_index = LightSelectionIndex { 2U },
      .coverage_state = kShadowCoverageComplete,
    },
  };
  const auto families = std::array {
    DirectionalShadowRecord {
      .selection_index = LightSelectionIndex { 2U },
      .first_cascade = ShadowCascadeIndex { 1U },
      .cascade_count = 1U,
    },
    DirectionalShadowRecord {
      .selection_index = LightSelectionIndex { 0U },
      .first_cascade = ShadowCascadeIndex { 0U },
      .cascade_count = 1U,
    },
  };
  auto lighting = LightingFrameBindings {};
  lighting.directional_records_srv = publish_records(directionals);
  lighting.directional_count = static_cast<std::uint32_t>(directionals.size());
  lighting.directional_shadow_map_srv = publish_records(references);
  lighting.build_status_srv = PublishFixtureData(
    LightGridBuildStatus { .state = kLightGridBuildValid });
  lighting.publication_state = kLightingPublicationRecorded;
  lighting.scene_generation = { 1U, 0U };
  auto shadows = ShadowFrameBindings {};
  shadows.directional_records_srv = publish_records(families);
  shadows.directional_record_count
    = static_cast<std::uint32_t>(families.size());
  shadows.cascade_record_count = 2U;
  shadows.view_status_srv = lighting.build_status_srv;
  shadows.scene_generation = lighting.scene_generation;

  // Test each source alone, both, swapped visibility, the disabled toggle and
  // an absent shadow reference. RGB separates the two sources numerically.
  struct Case {
    bool primary;
    bool secondary;
    bool secondary_occluded;
    bool shadows_enabled;
    bool secondary_requested;
  };
  constexpr std::array cases {
    Case {
      .primary = false,
      .secondary = true,
      .secondary_occluded = true,
      .shadows_enabled = true,
      .secondary_requested = true,
    },
    Case {
      .primary = true,
      .secondary = false,
      .secondary_occluded = true,
      .shadows_enabled = true,
      .secondary_requested = true,
    },
    Case {
      .primary = true,
      .secondary = true,
      .secondary_occluded = true,
      .shadows_enabled = true,
      .secondary_requested = true,
    },
    Case {
      .primary = true,
      .secondary = true,
      .secondary_occluded = false,
      .shadows_enabled = true,
      .secondary_requested = true,
    },
    Case {
      .primary = true,
      .secondary = true,
      .secondary_occluded = true,
      .shadows_enabled = false,
      .secondary_requested = true,
    },
    Case {
      .primary = false,
      .secondary = true,
      .secondary_occluded = true,
      .shadows_enabled = true,
      .secondary_requested = false,
    },
  };
  for (const auto& test : cases) {
    SCOPED_TRACE(::testing::PrintToString(test));
    std::array<ShadowCascadeBinding, 2> cascades {};
    for (std::uint32_t index = 0U; index < cascades.size(); ++index) {
      auto& cascade = cascades.at(index);
      // Constant projection keeps this a source-routing test, independent of
      // the camera ray used to integrate the homogeneous medium.
      cascade.light_view_projection = glm::mat4 { 0.0F };
      cascade.light_view_projection = glm::column(
        cascade.light_view_projection, 3, glm::vec4 { 0.0F, 0.0F, 0.5F, 1.0F });
      cascade.split_far = 32.0F;
      cascade.fade_begin = 32.0F;
      cascade.fade_end = 64.0F;
      cascade.surface_srv = surfaces.at(static_cast<std::size_t>(
        index == 0U ? test.secondary_occluded : !test.secondary_occluded));
      cascade.array_layer = ShadowArrayLayer { 0U };
      cascade.inverse_resolution = glm::vec2 { 1.0F };
    }
    shadows.cascade_records_srv = publish_records(cascades);
    auto case_references = references;
    if (!test.secondary_requested) {
      case_references.at(0) = LightShadowReference {};
    }
    lighting.directional_shadow_map_srv = publish_records(case_references);
    auto bindings = ViewFrameBindings {};
    bindings.lighting_frame_slot = PublishFixtureData(lighting);
    bindings.shadow_frame_slot = PublishFixtureData(shadows);
    auto view = ViewConstants::GpuData {};
    view.view_frame_bindings_bslot
      = BindlessViewFrameBindingsSlot { PublishFixtureData(bindings) };
    auto view_buffer = CreateUploadBuffer(
      SizeBytes { 256U }, graphics::BufferUsage::kConstant);
    view_buffer->Update(&view, sizeof(view), 0U);
    auto params = RendererPublicationProbe::FogPassConstants {};
    params.output_header = {
      .output_texture_uav = output_slot.get(),
      .output_width = 1U,
      .output_height = 1U,
      .output_depth = 1U,
    };
    params.grid = {
      .start_distance_m = 1.0F,
      .end_distance_m = 32.0F,
      .global_extinction_scale = 1.0F,
    };
    params.grid_z = {
      .grid_z_params = { 1.0F, 0.0F, 1.0F },
      .directional_shadows_enabled = test.shadows_enabled ? 1U : 0U,
    };
    params.height_fog0.primary_density = 1.0F;
    params.height_fog1.match_height_fog_factor = 1.0F;
    params.height_fog1.enabled = 1U;
    params.media0.albedo_rgb[0] = 1.0F;
    params.media0.albedo_rgb[1] = 1.0F;
    params.media0.albedo_rgb[2] = 1.0F;
    params.media1.static_lighting_scattering_intensity = 1.0F;
    params.light0_direction_enabled[2] = 1.0F;
    params.light0_direction_enabled[3] = test.primary ? 1.0F : 0.0F;
    params.light1_direction_enabled[2] = 1.0F;
    params.light1_direction_enabled[3] = test.secondary ? 1.0F : 0.0F;
    params.light0_illuminance_rgb[0] = 100000.0F;
    params.light1_illuminance_rgb[2] = 100000.0F;
    const auto constants = PublishFixtureData(params);
    {
      auto recorder
        = AcquireRecorder("Fog source identity production dispatch");
      EnsureTracked(*recorder, output, ResourceStates::kCommon);
      recorder->RequireResourceState(*output, ResourceStates::kUnorderedAccess);
      recorder->FlushBarriers();
      recorder->SetPipelineState(pipeline);
      recorder->SetComputeRootConstantBufferView(
        static_cast<unsigned>(root::RootParam::kViewConstants),
        view_buffer->GetGPUVirtualAddress());
      recorder->SetComputeRoot32BitConstant(
        static_cast<unsigned>(root::RootParam::kRootConstants), 0U, 0U);
      recorder->SetComputeRoot32BitConstant(
        static_cast<unsigned>(root::RootParam::kRootConstants), constants.get(),
        1U);
      recorder->Dispatch(1U, 1U, 1U);
    }
    const auto pixels = ReadFloatTexture(*output);
    ASSERT_EQ(pixels.size(), 1U);
    const double transmission = std::exp(-(std::numbers::sqrt2 - 1.0));
    const double lit
      = (1.0 - transmission) * 100000.0 * 2e-5 / (4.0 * std::numbers::pi);
    const bool primary_lit
      = test.primary && (!test.shadows_enabled || test.secondary_occluded);
    const bool secondary_lit = test.secondary
      && (!test.shadows_enabled || !test.secondary_occluded
        || !test.secondary_requested);
    EXPECT_NEAR(pixels.front().at(0), primary_lit ? lit : 0.0, 1e-5);
    EXPECT_NEAR(pixels.front().at(1), 0.0, 1e-5);
    EXPECT_NEAR(pixels.front().at(2), secondary_lit ? lit : 0.0, 1e-5);
    EXPECT_NEAR(pixels.front().at(3), transmission, 1e-5);
  }
}

} // namespace oxygen::vortex::testing::exposure
