//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <numbers>
#include <regex>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include <glm/ext/scalar_constants.hpp>
#include <glm/geometric.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Core/Types/ViewHelpers.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Scene/Light/LightCommon.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Lighting/Types/LightingPreparationFailure.h>
#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/DepthPrepass/DepthPrepassMeshProcessor.h>
#include <Oxygen/Vortex/Shadows/Internal/CascadeShadowSetup.h>
#include <Oxygen/Vortex/Shadows/Internal/ConventionalShadowTargetAllocator.h>
#include <Oxygen/Vortex/Shadows/Internal/LocalShadowProjection.h>
#include <Oxygen/Vortex/Shadows/Internal/LocalShadowQuality.h>
#include <Oxygen/Vortex/Shadows/Internal/PointShadowSetup.h>
#include <Oxygen/Vortex/Shadows/Internal/ShadowCasterCulling.h>
#include <Oxygen/Vortex/Shadows/Internal/ShadowEligibility.h>
#include <Oxygen/Vortex/Shadows/Internal/ShadowReferenceBuilder.h>
#include <Oxygen/Vortex/Shadows/Internal/SpotShadowSetup.h>
#include <Oxygen/Vortex/Shadows/Passes/ShadowDepthPass.h>
#include <Oxygen/Vortex/Shadows/ShadowService.h>
#include <Oxygen/Vortex/Shadows/Types/CubeLocalShadowRecord.h>
#include <Oxygen/Vortex/Shadows/Types/FrameShadowInputs.h>
#include <Oxygen/Vortex/Shadows/Types/LightShadowReference.h>
#include <Oxygen/Vortex/Shadows/Types/ProjectedLocalShadowRecord.h>
#include <Oxygen/Vortex/Shadows/Types/ShadowCascadeBinding.h>
#include <Oxygen/Vortex/Shadows/Types/ShadowFrameData.h>
#include <Oxygen/Vortex/Test/Fakes/Graphics.h>
#include <Oxygen/Vortex/Test/Fixtures/MeshRasterStateTest.h>
#include <Oxygen/Vortex/Types/FrameLightSelection.h>
#include <Oxygen/Vortex/Types/LightingFrameBindings.h>
#include <Oxygen/Vortex/Types/LightingIndices.h>
#include <Oxygen/Vortex/Types/PassMask.h>
#include <Oxygen/Vortex/Types/ShadowFrameBindings.h>

namespace {

using oxygen::Graphics;
using oxygen::kInvalidShaderVisibleIndex;
using oxygen::RendererConfig;
using oxygen::vortex::CubeLocalShadowRecord;
using oxygen::vortex::FrameDirectionalCsmSplitMode;
using oxygen::vortex::FrameDirectionalLightSelection;
using oxygen::vortex::FrameLightSelection;
using oxygen::vortex::FrameLocalLightSelection;
using oxygen::vortex::kDirectionalLightShadowFlagCastsShadows;
using oxygen::vortex::kInvalidShadowRecordIndex;
using oxygen::vortex::kLocalLightFlagCastsShadows;
using oxygen::vortex::kShadowCoverageNoInfluence;
using oxygen::vortex::kShadowCoverageNoRequest;
using oxygen::vortex::kShadowProjectionLocalCube;
using oxygen::vortex::kShadowProjectionLocalProjected2D;
using oxygen::vortex::LightingPreparationError;
using oxygen::vortex::LightSelectionIndex;
using oxygen::vortex::LocalLightKind;
using oxygen::vortex::ProjectedLocalShadowRecord;
using oxygen::vortex::Renderer;
using oxygen::vortex::RendererCapabilityFamily;
using oxygen::vortex::ShadowArrayLayer;
using oxygen::vortex::ShadowCascadeBinding;
using oxygen::vortex::ShadowFrameBindings;
using oxygen::vortex::ShadowFrameData;
using oxygen::vortex::ShadowRecordIndex;
using oxygen::vortex::ShadowService;
using oxygen::vortex::shadows::internal::CascadeShadowSetup;
using oxygen::vortex::shadows::internal::ConventionalShadowTargetAllocator;
using oxygen::vortex::shadows::internal::PointShadowSetup;
using oxygen::vortex::shadows::internal::SpotShadowSetup;
using oxygen::vortex::testing::FakeGraphics;

struct ShaderStructLayout {
  std::size_t size = 0;
  std::unordered_map<std::string, std::size_t> offsets;
};

// StructuredBuffer members use natural scalar layout, without cbuffer register
// padding. Read the shader declarations independently of the native structures.
auto ReadShadowShaderLayouts()
  -> std::unordered_map<std::string, ShaderStructLayout>
{
  const auto path = std::filesystem::path {
    OXYGEN_D3D12_VORTEX_SHADER_SOURCE_DIR,
  } / "Contracts/Shadows/ShadowFrameBindings.hlsli";
  auto stream = std::ifstream(path);
  if (!stream) {
    throw std::runtime_error(
      "Cannot read shadow shader contract: " + path.string());
  }
  auto cascade_stream
    = std::ifstream(path.parent_path() / "ShadowCascadeBinding.hlsli");
  if (!cascade_stream) {
    throw std::runtime_error("Cannot read cascade shader contract");
  }
  auto source = std::string {
    std::istreambuf_iterator<char>(cascade_stream),
    std::istreambuf_iterator<char>(),
  };
  for (const auto* filename :
    { "ProjectedLocalShadowRecord.hlsli", "CubeLocalShadowRecord.hlsli" }) {
    auto local_stream = std::ifstream(path.parent_path() / filename);
    if (!local_stream) {
      throw std::runtime_error("Cannot read local shadow shader contract");
    }
    source.append(std::istreambuf_iterator<char>(local_stream),
      std::istreambuf_iterator<char>());
  }
  source.append(
    std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
  auto sizes = std::unordered_map<std::string, std::size_t> {
    {
      "uint",
      4U,
    },
    { "uint2", 8U },
    {
      "float",
      4U,
    },
    {
      "float2",
      8U,
    },
    {
      "float3",
      12U,
    },
    {
      "float4",
      16U,
    },
    {
      "float4x4",
      64U,
    },
  };
  auto layouts = std::unordered_map<std::string, ShaderStructLayout> {};
  const auto structures = std::regex(R"(struct\s+(\w+)\s*\{([^}]*)\})");
  const auto fields = std::regex(R"(\b(\w+)\s+(\w+)(?:\[(\d+)\])?\s*;)");
  for (auto type
    = std::sregex_iterator(source.begin(), source.end(), structures);
    type != std::sregex_iterator(); ++type) {
    const auto name = type->str(1);
    const auto body = type->str(2);
    auto& layout = layouts.try_emplace(name).first->second;
    for (auto field = std::sregex_iterator(body.begin(), body.end(), fields);
      field != std::sregex_iterator(); ++field) {
      layout.offsets.emplace(field->str(2), layout.size);
      const auto array_count = field->str(3);
      const auto count = array_count.empty() ? 1U : std::stoul(array_count);
      layout.size += sizes.at(field->str(1)) * count;
    }
    sizes.emplace(name, layout.size);
  }
  return layouts;
}

auto DestroyRenderer(Renderer* renderer) -> void
{
  if (renderer != nullptr) {
    renderer->OnShutdown();
    std::default_delete<Renderer> {}(renderer);
  }
}

auto MakeRenderer(const std::shared_ptr<FakeGraphics>& graphics)
  -> std::shared_ptr<Renderer>
{
  auto config = RendererConfig {};
  config.upload_queue_key
    = graphics->QueueKeyFor(oxygen::graphics::QueueRole::kGraphics).get();
  constexpr auto kCapabilities = RendererCapabilityFamily::kScenePreparation
    | RendererCapabilityFamily::kDeferredShading
    | RendererCapabilityFamily::kLightingData;
  return {
    new Renderer(
      std::weak_ptr<Graphics>(graphics), std::move(config), kCapabilities),
    DestroyRenderer,
  };
}

auto MakePerspectiveResolvedView(const float near_plane = 0.1F)
  -> oxygen::ResolvedView
{
  auto params = oxygen::ResolvedView::Params {};
  params.view_config.viewport = oxygen::ViewPort {
    .top_left_x = 0.0F,
    .top_left_y = 0.0F,
    .width = 128.0F,
    .height = 128.0F,
    .min_depth = 0.0F,
    .max_depth = 1.0F,
  };
  params.view_config.reverse_z = true;
  params.view_matrix = glm::mat4(1.0F);
  params.near_plane = near_plane;
  params.far_plane = 100.0F;
  params.proj_matrix = oxygen::MakeReversedZPerspectiveProjectionRH_ZO(
    glm::pi<float>() / 3.0F, 1.0F, params.near_plane, params.far_plane);
  return oxygen::ResolvedView(params);
}

NOLINT_TEST(ShadowServiceSurfaceTest,
  ShaderStructuredBufferLayoutsMatchNativeShadowPublication)
{
  using oxygen::vortex::CubeLocalShadowRecord;
  using oxygen::vortex::ProjectedLocalShadowRecord;
  const auto layouts = ReadShadowShaderLayouts();
  const auto& cascade = layouts.at("VortexShadowCascadeBinding");
  EXPECT_EQ(cascade.size, sizeof(ShadowCascadeBinding));
  EXPECT_EQ(cascade.offsets.at("surface_srv"),
    offsetof(ShadowCascadeBinding, surface_srv));
  EXPECT_EQ(cascade.offsets.at("array_layer"),
    offsetof(ShadowCascadeBinding, array_layer));
  EXPECT_EQ(cascade.offsets.at("inverse_resolution"),
    offsetof(ShadowCascadeBinding, inverse_resolution));
  EXPECT_EQ(
    cascade.offsets.at("fade_end"), offsetof(ShadowCascadeBinding, fade_end));
  const auto& spot = layouts.at("ProjectedLocalShadowRecord");
  EXPECT_EQ(spot.size, sizeof(ProjectedLocalShadowRecord));
  EXPECT_EQ(spot.offsets.at("selection_index"),
    offsetof(ProjectedLocalShadowRecord, selection_index));
  const auto& point = layouts.at("CubeLocalShadowRecord");
  EXPECT_EQ(point.size, sizeof(CubeLocalShadowRecord));
  EXPECT_EQ(point.offsets.at("shadow_origin_ws"),
    offsetof(CubeLocalShadowRecord, shadow_origin_ws));
  const auto& frame = layouts.at("VortexShadowFrameBindings");
  EXPECT_EQ(frame.size, sizeof(ShadowFrameBindings));
  const auto expected_offsets = std::array {
    std::pair {
      "directional_records_srv",
      offsetof(ShadowFrameBindings, directional_records_srv),
    },
    std::pair {
      "directional_record_count",
      offsetof(ShadowFrameBindings, directional_record_count),
    },
    std::pair {
      "projected_local_records_srv",
      offsetof(ShadowFrameBindings, projected_local_records_srv),
    },
    std::pair {
      "projected_local_record_count",
      offsetof(ShadowFrameBindings, projected_local_record_count),
    },
    std::pair {
      "cube_local_records_srv",
      offsetof(ShadowFrameBindings, cube_local_records_srv),
    },
    std::pair {
      "cube_local_record_count",
      offsetof(ShadowFrameBindings, cube_local_record_count),
    },
    std::pair {
      "cascade_records_srv",
      offsetof(ShadowFrameBindings, cascade_records_srv),
    },
    std::pair {
      "cascade_record_count",
      offsetof(ShadowFrameBindings, cascade_record_count),
    },
    std::pair {
      "contact_depth_srv",
      offsetof(ShadowFrameBindings, contact_depth_srv),
    },
    std::pair {
      "view_status_srv",
      offsetof(ShadowFrameBindings, view_status_srv),
    },
    std::pair {
      "contact_enabled",
      offsetof(ShadowFrameBindings, contact_enabled),
    },
    std::pair {
      "sampling_flags",
      offsetof(ShadowFrameBindings, sampling_flags),
    },
    std::pair {
      "contact_content_origin_px",
      offsetof(ShadowFrameBindings, contact_content_origin_px),
    },
    std::pair {
      "contact_content_extent_px",
      offsetof(ShadowFrameBindings, contact_content_extent_px),
    },
    std::pair {
      "scene_generation",
      offsetof(ShadowFrameBindings, scene_generation),
    },
    std::pair {
      "selection_revision",
      offsetof(ShadowFrameBindings, selection_revision),
    },
    std::pair {
      "frame_sequence",
      offsetof(ShadowFrameBindings, frame_sequence),
    },
    std::pair {
      "view_generation",
      offsetof(ShadowFrameBindings, view_generation),
    },
    std::pair {
      "contact_texture_extent_px",
      offsetof(ShadowFrameBindings, contact_texture_extent_px),
    },
    std::pair {
      "reserved",
      offsetof(ShadowFrameBindings, reserved),
    },
  };
  for (const auto& [name, offset] : expected_offsets) {
    EXPECT_EQ(frame.offsets.at(name), offset) << name;
  }
}

NOLINT_TEST(
  ShadowServiceSurfaceTest, ShadowFrameBindingsStartWithNoPublishedArrays)
{
  const auto bindings = ShadowFrameBindings {};
  EXPECT_EQ(bindings.directional_records_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.projected_local_records_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.cube_local_records_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.cascade_records_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.view_status_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.contact_depth_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.directional_record_count, 0U);
  EXPECT_EQ(bindings.projected_local_record_count, 0U);
  EXPECT_EQ(bindings.cube_local_record_count, 0U);
  EXPECT_EQ(bindings.cascade_record_count, 0U);
  EXPECT_EQ(bindings.contact_enabled, 0U);
  EXPECT_EQ(bindings.sampling_flags, 0U);
}

NOLINT_TEST(
  ShadowServiceSurfaceTest, ShadowFrameDataOwnsIndependentRecordArrays)
{
  auto data = ShadowFrameData {};
  data.cascades.resize(2U);
  EXPECT_EQ(data.cascades.size(), 2U);
  EXPECT_TRUE(data.directional_records.empty());
  EXPECT_TRUE(data.projected_local_records.empty());
  EXPECT_TRUE(data.cube_local_records.empty());
  EXPECT_EQ(data.bindings.cascade_records_srv, kInvalidShaderVisibleIndex);
  EXPECT_EQ(data.bindings.cascade_record_count, 0U);
}

auto AllLightIndices(std::span<const FrameLocalLightSelection> lights)
  -> std::vector<ConventionalShadowTargetAllocator::LocalSelection>
{
  auto indices
    = std::vector<ConventionalShadowTargetAllocator::LocalSelection> {};
  auto offsets = std::array<std::uint32_t, 2> {};
  for (std::uint32_t index = 0U; index < lights.size(); ++index) {
    if (oxygen::vortex::shadows::internal::HasLocalShadowInfluence(
          lights[index])) {
      const auto cube
        = oxygen::vortex::shadows::internal::UsesCubeLocalShadow(lights[index]);
      indices.push_back(
        { LightSelectionIndex { index }, { .offset = offsets[cube]++ } });
    }
  }
  return indices;
}

NOLINT_TEST(ShadowServiceSurfaceTest,
  PointShadowSetupPublishesOnlyShadowCastingPointLightsInSelectionOrder)
{
  auto resolved_view = MakePerspectiveResolvedView();
  auto view_input = oxygen::vortex::PreparedViewShadowInput {};
  view_input.view_id = oxygen::ViewId {
    6U,
  };
  view_input.resolved_view = oxygen::observer_ptr<const oxygen::ResolvedView> {
    &resolved_view,
  };
  const auto allocation = ConventionalShadowTargetAllocator::PointAllocation {
    .surface_srv = oxygen::ShaderVisibleIndex { 13U, },
    .resolution = glm::uvec2 { 1024U, 1024U, },
    .shadow_count = 2U,
  };
  const auto local_lights = std::array {
    FrameLocalLightSelection {
      .kind = oxygen::vortex::LocalLightKind::kSpot,
      .flags = kLocalLightFlagCastsShadows,
    },
    FrameLocalLightSelection {
      .kind = oxygen::vortex::LocalLightKind::kPoint,
      .position = glm::vec3 { 0.0F, 3.0F, 2.0F, },
      .range = 10.0F,
      .luminous_flux_lm = 100.0F,
      .flags = kLocalLightFlagCastsShadows,
      .shadow_bias = 0.5F,
      .shadow_normal_bias = 0.04F,
    },
    FrameLocalLightSelection {
      .kind = oxygen::vortex::LocalLightKind::kPoint,
      .position = glm::vec3 { 2.0F, 0.0F, 3.0F, },
      .range = 8.0F,
      .flags = 0U,
    },
  };

  const auto bindings = PointShadowSetup {}.BuildPointRecords(view_input,
    std::span(local_lights), AllLightIndices(local_lights), allocation);

  EXPECT_EQ(bindings.size(), 1U);
  EXPECT_FLOAT_EQ(bindings.at(0).far_plane_m, 10.0F);
  EXPECT_EQ(
    bindings.at(0).first_array_layer, oxygen::vortex::ShadowArrayLayer { 0U });
  EXPECT_EQ(
    bindings.at(0).selection_index, oxygen::vortex::LightSelectionIndex { 1U });
  EXPECT_EQ(bindings.at(0).surface_srv, allocation.surface_srv);
  EXPECT_GT(bindings.at(0).depth_bias, 0.0F);
  EXPECT_FLOAT_EQ(bindings.at(0).normal_bias_m, 0.04F);
}

NOLINT_TEST(ShadowServiceSurfaceTest,
  OrdinarySpotsUseProjectedCoverageAndHemispheresRetainCubes)
{
  auto resolved_view = MakePerspectiveResolvedView();
  auto view_input = oxygen::vortex::PreparedViewShadowInput {};
  view_input.resolved_view
    = oxygen::observer_ptr<const oxygen::ResolvedView> { &resolved_view };
  const auto lights = std::array {
    FrameLocalLightSelection {
      .source_node = {},
      .kind = oxygen::vortex::LocalLightKind::kSpot,
      .range = 3.0F,
      .luminous_flux_lm = 100.0F,
      .source_radius = 0.5F,
      .flags = kLocalLightFlagCastsShadows,
    },
    FrameLocalLightSelection {
      .source_node = {},
      .kind = oxygen::vortex::LocalLightKind::kSpot,
      .range = 4.0F,
      .luminous_flux_lm = 100.0F,
      .outer_cone_half_angle_radians = std::numbers::pi_v<float> / 2.0F,
      .flags = kLocalLightFlagCastsShadows,
    },
    FrameLocalLightSelection {
      .source_node = {},
      .kind = oxygen::vortex::LocalLightKind::kPoint,
      .range = 5.0F,
      .luminous_flux_lm = 100.0F,
      .source_radius = 0.25F,
      .flags = kLocalLightFlagCastsShadows,
    },
    FrameLocalLightSelection {
      .source_node = {},
      .kind = oxygen::vortex::LocalLightKind::kSpot,
      .range = 6.0F,
      .luminous_flux_lm = 100.0F,
      .outer_cone_half_angle_radians = 0.4F,
      .flags = kLocalLightFlagCastsShadows,
    },
    FrameLocalLightSelection {
      .source_node = {},
      .kind = oxygen::vortex::LocalLightKind::kPoint,
      .range = 0.0F,
      .source_radius = 1.0F,
      .flags = kLocalLightFlagCastsShadows,
    },
  };
  const auto cube = PointShadowSetup {}.BuildPointRecords(view_input, lights,
    AllLightIndices(lights),
    {
      .surface = nullptr,
      .surface_srv = oxygen::ShaderVisibleIndex { 13U },
      .resolution = { 1024U, 1024U },
      .shadow_count = 2U,
    });
  const auto projected = SpotShadowSetup {}.BuildSpotRecords(view_input, lights,
    AllLightIndices(lights),
    {
      .surface = nullptr,
      .surface_srv = oxygen::ShaderVisibleIndex { 14U },
      .resolution = { 1024U, 1024U },
      .shadow_count = 2U,
    });
  ASSERT_EQ(cube.size(), 2U);
  ASSERT_EQ(projected.size(), 2U);
  EXPECT_EQ(projected.at(0).selection_index,
    oxygen::vortex::LightSelectionIndex { 0U });
  EXPECT_EQ(projected.at(1).selection_index,
    oxygen::vortex::LightSelectionIndex { 3U });
  for (std::size_t index = 0; index < cube.size(); ++index) {
    const auto& record = cube.at(index);
    EXPECT_EQ(record.selection_index.get(), index + 1U);
    EXPECT_EQ(record.first_array_layer.get(), index * 6U);
    EXPECT_FLOAT_EQ(record.far_plane_m, lights.at(index + 1U).range);
    // Every axial support endpoint is inside the corresponding cube face.
    for (std::size_t face = 0; face < 6U; ++face) {
      const auto directions = std::array {
        glm::vec3 { 1, 0, 0 },
        glm::vec3 { -1, 0, 0 },
        glm::vec3 { 0, 1, 0 },
        glm::vec3 { 0, -1, 0 },
        glm::vec3 { 0, 0, 1 },
        glm::vec3 { 0, 0, -1 },
      };
      const auto point
        = glm::vec4 { -directions.at(face) * (0.999F * record.far_plane_m),
            1.0F };
      const auto clip = record.face_light_view_projection.at(face) * point;
      EXPECT_GT(clip.w, 0.0F);
      EXPECT_LE(std::abs(clip.x), clip.w);
      EXPECT_LE(std::abs(clip.y), clip.w);
      EXPECT_GE(clip.z, 0.0F);
      EXPECT_LE(clip.z, clip.w);
    }
  }
}

NOLINT_TEST(ShadowServiceSurfaceTest,
  CubeProjectionMatchesNativeAddressingAndMetricBiasAcrossRanges)
{
  // Keep the shortest test light's sphere inside the view's near plane.
  auto resolved_view = MakePerspectiveResolvedView(0.001F);
  oxygen::vortex::PreparedViewShadowInput input {};
  input.resolved_view
    = oxygen::observer_ptr<const oxygen::ResolvedView> { &resolved_view };
  for (const float range : { 0.05F, 3.0F, 4096.0F }) {
    const auto lights = std::array { FrameLocalLightSelection {
      .kind = LocalLightKind::kPoint,
      .position = { 0, 0, 0 },
      .range = range,
      .luminous_flux_lm = 100.0F,
      .flags = kLocalLightFlagCastsShadows,
      .shadow_bias = 0.5F,
    } };
    const auto records = PointShadowSetup {}.BuildPointRecords(input, lights,
      AllLightIndices(lights),
      { .surface_srv = oxygen::ShaderVisibleIndex { 13U },
        .resolution = { 1024U, 1024U },
        .shadow_count = 1U });
    ASSERT_EQ(records.size(), 1U);
    const auto& record = records.front();
    // Hardware cube convention expressed independently as sample directions
    // for a fixed asymmetric (s,t) on each face. t points down in the image.
    constexpr float s = 0.31F;
    constexpr float t = -0.47F;
    const auto directions = std::array {
      glm::vec3 { 1, -t, -s },
      glm::vec3 { -1, -t, s },
      glm::vec3 { s, 1, t },
      glm::vec3 { s, -1, -t },
      glm::vec3 { s, -t, 1 },
      glm::vec3 { -s, -t, -1 },
    };
    for (unsigned face = 0; face < 6U; ++face) {
      for (const float axial : { record.near_plane_m, range * 0.25F, range }) {
        const auto clip = record.face_light_view_projection[face]
          * glm::vec4(-directions[face] * axial, 1);
        ASSERT_GT(clip.w, 0.0F);
        EXPECT_NEAR(clip.x / clip.w, s, 1.0e-6F);
        EXPECT_NEAR(-clip.y / clip.w, t, 1.0e-6F);
        const auto expected = record.near_plane_m * (range - axial)
          / (axial * (range - record.near_plane_m));
        EXPECT_NEAR(clip.z / clip.w, expected, 1.0e-6F);
      }
    }
    // Resolution 1024 and authored bias 0.5 halve UE's normalized 0.02
    // coefficient. The result stays invariant under a change of world units.
    EXPECT_NEAR(record.depth_bias / record.near_plane_m, 0.01F, 1.0e-7F);
  }
}

NOLINT_TEST(ShadowServiceSurfaceTest,
  FrameLightSelectionCarriesSharedDirectionalAuthorityForShadowService)
{
  auto selection = FrameLightSelection {};
  selection.selection_epoch = 19U;
  selection.directional_lights = { FrameDirectionalLightSelection{ .source_node = {},
    .direction = glm::vec3 { 0.0F, -1.0F, 0.0F, },
    .color = glm::vec3 { 1.0F, 0.95F, 0.8F, },
    .illuminance_lux = 1400.0F,
    .shadow_flags = kDirectionalLightShadowFlagCastsShadows,
    .cascade_count = 4U,
    .cascade_split_mode = FrameDirectionalCsmSplitMode::kManualDistances,
    .max_shadow_distance = 128.0F,
    .cascade_distances = { 16.0F, 32.0F, 64.0F, 128.0F },
    .transition_fraction = 0.2F,
    .distance_fadeout_fraction = 0.15F,
    .shadow_bias = 0.001F,
    .shadow_normal_bias = 0.03F,
  }, };

  if (selection.directional_lights.empty()) {

    FAIL() << "Expected a selected directional light";
  }
  EXPECT_EQ(selection.directional_lights.front().cascade_count, 4U);
  EXPECT_NE(selection.directional_lights.front().shadow_flags
      & kDirectionalLightShadowFlagCastsShadows,
    0U);
  EXPECT_EQ(selection.directional_lights.front().cascade_split_mode,
    FrameDirectionalCsmSplitMode::kManualDistances);
  EXPECT_FLOAT_EQ(
    selection.directional_lights.front().cascade_distances.at(3), 128.0F);
  EXPECT_TRUE(selection.local_lights.empty());
}

NOLINT_TEST(
  ShadowServiceSurfaceTest, ShadowServiceIsANonPlaceholderSubsystemSurface)
{
  EXPECT_TRUE((std::is_class_v<ShadowService>));
  EXPECT_TRUE((std::is_destructible_v<ShadowService>));
  EXPECT_TRUE((std::is_standard_layout_v<ShadowCascadeBinding>));
}

class ShadowServiceBehaviorTest : public ::testing::Test {
protected:
  void SetUp() override
  {
    graphics_ = std::make_shared<FakeGraphics>();
    graphics_->CreateCommandQueues(*MakeQueueStrategy());
    renderer_ = MakeRenderer(graphics_);
  }

  virtual auto MakeQueueStrategy() const
    -> std::unique_ptr<oxygen::graphics::QueuesStrategy>
  {
    return std::make_unique<oxygen::graphics::SingleQueueStrategy>();
  }

  auto ProduceMap(ConventionalShadowTargetAllocator& allocator,
    oxygen::ViewId view, oxygen::scene::NodeHandle source, uint32_t resolution)
    -> std::shared_ptr<oxygen::vortex::shadows::internal::ShadowMapOwner>
  {
    using namespace oxygen::vortex::shadows::internal;
    auto light = FrameLocalLightSelection { .source_node = source,
      .range = 5,
      .luminous_flux_lm = 100,
      .flags = kLocalLightFlagCastsShadows };
    oxygen::vortex::PreparedViewShadowInput input { .view_id = view,
      .scene_generation = 1,
      .shadow_dependencies_available = true };
    LocalShadowRequest request;
    PrepareLocalShadowRequest(
      input, light, LightSelectionIndex { 0 }, resolution, 1, request);
    auto acquired = allocator.AcquireLocalMap(view, request);
    if (!acquired.reused) {
      auto recording = graphics_->AcquireCommandRecorder(
        graphics_->QueueKeyFor(oxygen::graphics::QueueRole::kGraphics),
        "Allocator writer", oxygen::graphics::SubmissionPolicy::kExplicit);
      AttachShadowUse(acquired.owner->version, ShadowUseMode::kWrite,
        *recording, graphics_->GetResourceRegistry());
      recording->RequireResourceStateFinal(
        *acquired.owner->version->slot->backing->texture,
        oxygen::graphics::ResourceStates::kShaderResource);
      if (!recording.Submit()) {
        throw std::runtime_error("Test writer rejected");
      }
    }
    acquired.Commit();
    return acquired.owner;
  }

  std::shared_ptr<FakeGraphics> graphics_;
  std::shared_ptr<Renderer> renderer_;
};

NOLINT_TEST_F(
  ShadowServiceBehaviorTest, ShadowServiceStartsWithEmptyPublicationAndNoVsm)
{
  auto service = ShadowService(*renderer_);

  EXPECT_FALSE(service.HasVsm());
  EXPECT_EQ(service.InspectShadowData(oxygen::ViewId {
              11U,
            }),
    nullptr);
}

NOLINT_TEST_F(ShadowServiceBehaviorTest,
  PublishesSeparateRecordArraysWithMatchingGenerations)
{
  auto service = ShadowService(*renderer_);
  const auto sequence = oxygen::frame::SequenceNumber { 0x100000003ULL };
  service.OnFrameStart(sequence, oxygen::frame::Slot { 0U });
  auto resolved_view = MakePerspectiveResolvedView();
  auto selection = FrameLightSelection {};
  selection.scene_generation = 0x200000005ULL;
  selection.selection_epoch = 0x300000007ULL;
  selection.directional_lights = { FrameDirectionalLightSelection {} };
  selection.directional_lights.front().shadow_flags
    = kDirectionalLightShadowFlagCastsShadows;
  selection.directional_lights.front().cascade_count = 2U;
  selection.directional_lights.front().illuminance_lux = 100.0F;
  selection.local_lights = {
    FrameLocalLightSelection {
      .kind = oxygen::vortex::LocalLightKind::kSpot,
      .range = 12.0F,
      .luminous_flux_lm = 100.0F,
      .flags = kLocalLightFlagCastsShadows,
    },
    FrameLocalLightSelection {
      .kind = oxygen::vortex::LocalLightKind::kPoint,
      .range = 10.0F,
      .luminous_flux_lm = 100.0F,
      .flags = kLocalLightFlagCastsShadows,
    },
  };
  selection.local_lights[0].source_node = oxygen::scene::NodeHandle { 1U, 1U };
  selection.local_lights[1].source_node = oxygen::scene::NodeHandle { 2U, 1U };
  const auto lighting = oxygen::vortex::LightingFrameBindings {
    .build_status_srv = oxygen::ShaderVisibleIndex { 19U },
    .view_generation = { 11U, 4U },
  };
  const auto views = std::array {
    oxygen::vortex::PreparedViewShadowInput {
      .view_id = oxygen::ViewId { 17U },
      .lighting_bindings = &lighting,
      .prepared_scene = {},
      .resolved_view = oxygen::observer_ptr { &resolved_view },
      .view_constants = {},
      .composition_view = {},
    },
  };
  service.RenderShadowDepths(
    { .frame_light_set = &selection, .active_views = views });
  const auto* data = service.InspectShadowData(views.front().view_id);
  ASSERT_NE(data, nullptr);
  EXPECT_TRUE(service.ResolveShadowFrameSlot(views.front().view_id).IsValid());
  EXPECT_TRUE(data->bindings.directional_records_srv.IsValid());
  EXPECT_TRUE(data->bindings.cascade_records_srv.IsValid());
  EXPECT_TRUE(data->bindings.projected_local_records_srv.IsValid());
  EXPECT_TRUE(data->bindings.cube_local_records_srv.IsValid());
  EXPECT_EQ(data->bindings.directional_record_count, 1U);
  EXPECT_EQ(data->bindings.cascade_record_count, 2U);
  EXPECT_EQ(data->bindings.projected_local_record_count, 1U);
  EXPECT_EQ(data->bindings.cube_local_record_count, 1U);
  EXPECT_EQ(
    data->bindings.scene_generation, (std::array<std::uint32_t, 2> { 5U, 2U }));
  EXPECT_EQ(data->bindings.selection_revision,
    (std::array<std::uint32_t, 2> { 7U, 3U }));
  EXPECT_EQ(
    data->bindings.frame_sequence, (std::array<std::uint32_t, 2> { 3U, 1U }));
  EXPECT_EQ(data->bindings.view_generation, lighting.view_generation);
  EXPECT_EQ(data->bindings.view_status_srv, lighting.build_status_srv);
  EXPECT_EQ(data->projected_local_records.front().selection_index,
    oxygen::vortex::LightSelectionIndex { 0U });
  EXPECT_EQ(data->cube_local_records.front().selection_index,
    oxygen::vortex::LightSelectionIndex { 1U });
  EXPECT_EQ(data->directional_records.front().first_cascade,
    oxygen::vortex::ShadowCascadeIndex { 0U });
}

NOLINT_TEST_F(ShadowServiceBehaviorTest,
  DirectionalFamiliesKeepDistinctSurfacesAndFilteredSelectionIndices)
{
  auto service = ShadowService(*renderer_);
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 1U }, oxygen::frame::Slot { 0U });
  auto resolved_view = MakePerspectiveResolvedView();
  auto selection = FrameLightSelection {};
  selection.directional_lights = {
    FrameDirectionalLightSelection {},
    FrameDirectionalLightSelection {
      .source_node = {},
      .direction = { 0.0F, -1.0F, -1.0F },
      .illuminance_lux = 100.0F,
      .atmosphere_light_slot = 1U,
      .shadow_flags = kDirectionalLightShadowFlagCastsShadows,
      .cascade_count = 2U,
      .shadow_resolution_hint = oxygen::scene::ShadowResolutionHint::kLow,
    },
    FrameDirectionalLightSelection {
      .source_node = {},
      .direction = { 1.0F, 0.0F, -1.0F },
      .illuminance_lux = 100.0F,
      .atmosphere_light_slot = 0U,
      .shadow_flags = kDirectionalLightShadowFlagCastsShadows,
      .cascade_count = 3U,
      .shadow_resolution_hint = oxygen::scene::ShadowResolutionHint::kMedium,
    },
  };
  auto input = oxygen::vortex::PreparedViewShadowInput {};
  input.view_id = oxygen::ViewId { 17U };
  input.resolved_view = oxygen::observer_ptr { &resolved_view };
  const auto views = std::array { input };
  service.RenderShadowDepths(
    { .frame_light_set = &selection, .active_views = views });
  const auto* data = service.InspectShadowData(input.view_id);
  ASSERT_NE(data, nullptr);
  ASSERT_EQ(data->directional_records.size(), 2U);
  EXPECT_EQ(data->directional_records.at(0).selection_index,
    oxygen::vortex::LightSelectionIndex { 1U });
  EXPECT_EQ(data->directional_records.at(1).selection_index,
    oxygen::vortex::LightSelectionIndex { 2U });
  EXPECT_EQ(data->directional_records.at(0).first_cascade,
    oxygen::vortex::ShadowCascadeIndex { 0U });
  EXPECT_EQ(data->directional_records.at(1).first_cascade,
    oxygen::vortex::ShadowCascadeIndex { 2U });
  ASSERT_EQ(data->cascades.size(), 5U);
  EXPECT_NE(data->cascades.at(0).surface_srv, data->cascades.at(2).surface_srv);
  const auto surfaces = service.InspectDirectionalShadowSurfaces(input.view_id);
  ASSERT_EQ(surfaces.size(), 2U);
  EXPECT_NE(surfaces.front(), surfaces.back());
  EXPECT_EQ(surfaces.front()->GetDescriptor().array_size, 2U);
  EXPECT_EQ(surfaces.back()->GetDescriptor().array_size, 3U);
  EXPECT_NE(surfaces.front()->GetDescriptor().width,
    surfaces.back()->GetDescriptor().width);
  EXPECT_EQ(service.GetLastRenderState().rendered_cascade_count, 5U);
}

NOLINT_TEST_F(
  ShadowServiceBehaviorTest, FailedRecordAllocationDoesNotPublishHeader)
{
  graphics_->SetFailMap(true);
  auto service = ShadowService(*renderer_);
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 1U }, oxygen::frame::Slot { 0U });
  auto resolved_view = MakePerspectiveResolvedView();
  auto selection = FrameLightSelection {};
  selection.directional_lights = { FrameDirectionalLightSelection {} };
  selection.directional_lights.front().shadow_flags
    = kDirectionalLightShadowFlagCastsShadows;
  selection.directional_lights.front().cascade_count = 2U;
  const auto views = std::array {
    oxygen::vortex::PreparedViewShadowInput {
      .view_id = oxygen::ViewId { 17U },
      .prepared_scene = {},
      .resolved_view = oxygen::observer_ptr { &resolved_view },
      .view_constants = {},
      .composition_view = {},
    },
  };
  service.RenderShadowDepths(
    { .frame_light_set = &selection, .active_views = views });
  EXPECT_EQ(service.InspectShadowData(views.front().view_id), nullptr);
  EXPECT_EQ(service.ResolveShadowFrameSlot(views.front().view_id),
    kInvalidShaderVisibleIndex);
  EXPECT_EQ(service.GetLastRenderState().published_view_count, 0U);
}

NOLINT_TEST_F(ShadowServiceBehaviorTest,
  ShadowDepthBindsSidednessAndHandednessForEveryDrawAndSlice)
{
  using oxygen::vortex::shadows::ShadowDepthPass;
  auto pass = ShadowDepthPass(*renderer_);
  pass.OnFrameStart(
    oxygen::frame::SequenceNumber {
      1U,
    },
    oxygen::frame::Slot {
      0U,
    });
  auto texture_desc = oxygen::graphics::TextureDesc {};
  texture_desc.width = 64U;
  texture_desc.height = 64U;
  texture_desc.array_size = 2U;
  texture_desc.format = oxygen::Format::kDepth32Stencil8;
  texture_desc.texture_type = oxygen::TextureType::kTexture2DArray;
  texture_desc.debug_name = "RasterState.ShadowDepth";
  texture_desc.is_shader_resource = true;
  texture_desc.is_render_target = true;
  texture_desc.is_typeless = true;
  const auto texture = graphics_->CreateTexture(texture_desc);
  graphics_->GetResourceRegistry().Register(texture);
  const auto view_constants = graphics_->CreateBuffer({
    .size_bytes = 1024U,
    .usage = oxygen::graphics::BufferUsage::kConstant,
    .memory = oxygen::graphics::BufferMemory::kUpload,
    .debug_name = "RasterState.ShadowViewConstants",
  });
  const auto slices = std::array {
    ShadowDepthPass::DepthSlice {
      .target_slice = 0U,
    },
    ShadowDepthPass::DepthSlice {
      .target_slice = 1U,
    },
  };
  for (const auto kind : {
         oxygen::vortex::PassMaskBit::kOpaque,
         oxygen::vortex::PassMaskBit::kMasked,
       }) {
    auto metadata = oxygen::vortex::testing::MakeRasterStateDraws(kind);
    for (auto& draw : metadata) {
      draw.flags.Set(oxygen::vortex::PassMaskBit::kShadowCaster);
    }
    auto frame = oxygen::vortex::PreparedSceneFrame {};
    frame.draw_metadata_bytes = std::as_bytes(std::span(metadata));
    auto input = oxygen::vortex::PreparedViewShadowInput {};
    input.view_id = oxygen::ViewId {
      1U,
    };
    input.prepared_scene
      = oxygen::observer_ptr<const oxygen::vortex::PreparedSceneFrame> {
          &frame,
        };
    input.view_constants
      = oxygen::observer_ptr<const oxygen::graphics::Buffer> {
          view_constants.get(),
        };
    graphics_->draw_log_.draws.clear();
    const auto result = pass.RecordSlices(input, texture, slices);
    ASSERT_EQ(result.rendered_cascade_count, 2U);
    ASSERT_EQ(result.rendered_draw_count, 10U);
    ASSERT_EQ(graphics_->draw_log_.draws.size(), 10U);
    for (std::size_t slice = 0U; slice < slices.size(); ++slice) {
      oxygen::vortex::testing::ExpectRasterStateDraws(
        std::span(graphics_->draw_log_.draws)
          .subspan(slice * metadata.size(), metadata.size()),
        "Vortex.ShadowDepth.");
    }
  }
}

NOLINT_TEST(ShadowServiceSurfaceTest,
  SpotShadowSetupPublishesOnlyShadowCastingSpotLightsInSelectionOrder)
{
  auto resolved_view = MakePerspectiveResolvedView();
  auto view_input = oxygen::vortex::PreparedViewShadowInput {};
  view_input.view_id = oxygen::ViewId {
    4U,
  };
  view_input.resolved_view = oxygen::observer_ptr<const oxygen::ResolvedView> {
    &resolved_view,
  };
  const auto allocation = ConventionalShadowTargetAllocator::SpotAllocation {
    .surface_srv = oxygen::ShaderVisibleIndex { 9U, },
    .resolution = glm::uvec2 { 1024U, 1024U, },
    .shadow_count = 2U,
  };
  const auto local_lights = std::array {
    FrameLocalLightSelection {
      .kind = oxygen::vortex::LocalLightKind::kPoint,
      .flags = kLocalLightFlagCastsShadows,
    },
    FrameLocalLightSelection {
      .kind = oxygen::vortex::LocalLightKind::kSpot,
      .position = glm::vec3 { 0.0F, 4.0F, 3.0F, },
      .range = 12.0F,
      .luminous_flux_lm = 100.0F,
      .direction = glm::vec3 { 0.0F, -1.0F, -0.5F, },
      .outer_cone_half_angle_radians = std::acos(0.75F),
      .flags = kLocalLightFlagCastsShadows,
      .shadow_bias = 0.5F,
      .shadow_normal_bias = 0.03F,
    },
    FrameLocalLightSelection {
      .kind = oxygen::vortex::LocalLightKind::kSpot,
      .position = glm::vec3 { 2.0F, 0.0F, 5.0F, },
      .range = 8.0F,
      .direction = glm::vec3 { -1.0F, 0.0F, -0.25F, },
      .outer_cone_half_angle_radians = std::acos(0.6F),
      .flags = 0U,
    },
  };

  const auto bindings = SpotShadowSetup {}.BuildSpotRecords(view_input,
    std::span(local_lights), AllLightIndices(local_lights), allocation);

  EXPECT_EQ(bindings.size(), 1U);
  EXPECT_EQ(
    bindings.at(0).selection_index, oxygen::vortex::LightSelectionIndex { 1U });
  EXPECT_EQ(
    bindings.at(0).array_layer, oxygen::vortex::ShadowArrayLayer { 0U });
  EXPECT_EQ(bindings.at(0).surface_srv, allocation.surface_srv);
  EXPECT_FLOAT_EQ(bindings.at(0).far_plane_m, 12.0F);
  EXPECT_GT(bindings.at(0).depth_bias, 0.0F);
  EXPECT_FLOAT_EQ(bindings.at(0).normal_bias_m, 0.03F);
}

NOLINT_TEST(ShadowServiceSurfaceTest,
  SpotShadowLinearDepthLeavesValidationCastersInsideDepthRangeAfterBias)
{
  auto resolved_view = MakePerspectiveResolvedView();
  auto view_input = oxygen::vortex::PreparedViewShadowInput {};
  view_input.view_id = oxygen::ViewId {
    5U,
  };
  view_input.resolved_view = oxygen::observer_ptr<const oxygen::ResolvedView> {
    &resolved_view,
  };
  const auto allocation = ConventionalShadowTargetAllocator::SpotAllocation {
    .surface_srv = oxygen::ShaderVisibleIndex { 11U, },
    .resolution = glm::uvec2 { 2048U, 2048U, },
    .shadow_count = 1U,
  };
  const auto direction = glm::normalize(glm::vec3 {
    0.506013870F,
    0.574625850F,
    -0.643237948F,
  });
  const auto local_lights = std::array {
    FrameLocalLightSelection {
      .kind = oxygen::vortex::LocalLightKind::kSpot,
      .position = glm::vec3 { -2.8F, -3.2F, 4.2F, },
      .range = 9.0F,
      .luminous_flux_lm = 100.0F,
      .direction = direction,
      .outer_cone_half_angle_radians = 0.68F,
      .flags = kLocalLightFlagCastsShadows,
      .shadow_bias = 0.1F,
      .shadow_normal_bias = 0.02F,
    },
  };

  const auto bindings = SpotShadowSetup {}.BuildSpotRecords(view_input,
    std::span(local_lights), AllLightIndices(local_lights), allocation);

  ASSERT_EQ(bindings.size(), 1U);
  const auto& spot = bindings.at(0);
  const auto caster_center = glm::vec3 {
    0.0F,
    0.0F,
    0.5F,
  };
  const auto axial_distance = glm::dot(caster_center - spot.shadow_origin_ws,
    glm::normalize(local_lights.at(0).direction));
  const auto projected
    = spot.light_view_projection * glm::vec4(caster_center, 1.0F);
  EXPECT_NEAR(projected.w, axial_distance, 1.0e-5F);
  const auto unbiased_linear_depth = 1.0F - (axial_distance / spot.far_plane_m);
  const auto max_depth_bias = spot.depth_bias * 4.0F;

  EXPECT_GT(unbiased_linear_depth - max_depth_bias, 0.25F);
}

NOLINT_TEST(ShadowServiceSurfaceTest,
  DirectionalCascadeCoverageExtendsNonLastSplitsForTransitionOverlap)
{
  auto resolved_view = MakePerspectiveResolvedView();
  auto view_input = oxygen::vortex::PreparedViewShadowInput {};
  view_input.view_id = oxygen::ViewId {
    3U,
  };
  view_input.resolved_view = oxygen::observer_ptr<const oxygen::ResolvedView> {
    &resolved_view,
  };
  const auto allocation
    = ConventionalShadowTargetAllocator::DirectionalAllocation {
        .surface_srv = oxygen::ShaderVisibleIndex { 7U, },
        .resolution = glm::uvec2 { 2048U, 2048U, },
        .cascade_count = 3U,
      };
  const auto directional_light = FrameDirectionalLightSelection { .source_node = {},
    .direction = glm::vec3 { 0.0F, -1.0F, -1.0F, },
    .shadow_flags = kDirectionalLightShadowFlagCastsShadows,
    .cascade_count = 3U,
    .cascade_split_mode = FrameDirectionalCsmSplitMode::kManualDistances,
    .max_shadow_distance = 40.0F,
    .cascade_distances = { 10.0F, 20.0F, 40.0F, 40.0F },
    .transition_fraction = 0.25F,
    .distance_fadeout_fraction = 0.1F,
  };

  const auto frame_data = CascadeShadowSetup {}.BuildDirectionalFrameData(
    view_input, directional_light, allocation);

  ASSERT_EQ(frame_data.cascades.size(), 3U);
  EXPECT_FLOAT_EQ(frame_data.cascades.at(0).split_near, 0.1F);
  EXPECT_NEAR(frame_data.cascades.at(0).split_far, 12.475F, 0.0001F);
  EXPECT_FLOAT_EQ(frame_data.cascades.at(1).split_near, 10.0F);
  EXPECT_NEAR(frame_data.cascades.at(1).split_far, 22.5F, 0.0001F);
  EXPECT_FLOAT_EQ(frame_data.cascades.at(2).split_near, 20.0F);
  EXPECT_FLOAT_EQ(frame_data.cascades.at(2).split_far, 40.0F);
  EXPECT_NEAR(frame_data.cascades.at(0).transition_width, 2.475F, 0.0001F);
  EXPECT_NEAR(frame_data.cascades.at(1).transition_width, 2.5F, 0.0001F);
  EXPECT_FLOAT_EQ(frame_data.cascades.at(2).transition_width, 0.0F);
}

NOLINT_TEST(
  ShadowServiceSurfaceTest, ReferencesFollowRecordsRatherThanLightKindCounters)
{
  auto selection = FrameLightSelection {};
  selection.local_lights = {
    FrameLocalLightSelection {
      .kind = LocalLightKind::kSpot,
      .range = 10.0F,
      .luminous_flux_lm = 100.0F,
      .flags = kLocalLightFlagCastsShadows,
    },
    FrameLocalLightSelection {},
    FrameLocalLightSelection {
      .kind = LocalLightKind::kPoint,
      .range = 10.0F,
      .luminous_flux_lm = 100.0F,
      .flags = kLocalLightFlagCastsShadows,
    },
    FrameLocalLightSelection {
      .kind = LocalLightKind::kSpot,
      .range = 10.0F,
      .luminous_flux_lm = 100.0F,
      .flags = kLocalLightFlagCastsShadows,
    },
  };
  auto data = ShadowFrameData {};
  // A spot uses a cube; cube record order deliberately differs from selection.
  data.cube_local_records = {
    CubeLocalShadowRecord {
      .surface_srv = oxygen::ShaderVisibleIndex { 19U },
      .first_array_layer = ShadowArrayLayer { 0U },
      .selection_index = LightSelectionIndex { 2U },
    },
    CubeLocalShadowRecord {
      .surface_srv = oxygen::ShaderVisibleIndex { 23U },
      .first_array_layer = ShadowArrayLayer { 0U },
      .selection_index = LightSelectionIndex { 0U },
    },
  };
  data.projected_local_records = {
    ProjectedLocalShadowRecord {
      .surface_srv = oxygen::ShaderVisibleIndex { 29U },
      .array_layer = ShadowArrayLayer { 0U },
      .selection_index = LightSelectionIndex { 3U },
    },
  };
  ASSERT_TRUE(
    oxygen::vortex::shadows::internal::BuildShadowReferences(selection, data));
  ASSERT_EQ(data.local_shadow_references.size(), 4U);
  EXPECT_EQ(data.local_shadow_references.at(0).projection_kind,
    kShadowProjectionLocalCube);
  EXPECT_EQ(
    data.local_shadow_references.at(0).record_index, ShadowRecordIndex { 1U });
  EXPECT_EQ(data.local_shadow_references.at(1).coverage_state,
    kShadowCoverageNoRequest);
  EXPECT_EQ(data.local_shadow_references.at(1).selection_index,
    LightSelectionIndex { 1U });
  EXPECT_EQ(
    data.local_shadow_references.at(1).record_index, kInvalidShadowRecordIndex);
  EXPECT_EQ(
    data.local_shadow_references.at(2).record_index, ShadowRecordIndex { 0U });
  EXPECT_EQ(data.local_shadow_references.at(3).projection_kind,
    kShadowProjectionLocalProjected2D);
  EXPECT_EQ(
    data.local_shadow_references.at(3).record_index, ShadowRecordIndex { 0U });

  data.local_shadow_references.clear();
  data.projected_local_records.front().selection_index
    = LightSelectionIndex { 0U };
  const auto duplicate
    = oxygen::vortex::shadows::internal::BuildShadowReferences(selection, data);
  ASSERT_FALSE(duplicate);
  EXPECT_EQ(duplicate.error().selection_index, LightSelectionIndex { 0U });
  EXPECT_TRUE(data.local_shadow_references.empty());
}

NOLINT_TEST(
  ShadowServiceSurfaceTest, MissingRequiredShadowFailsButZeroInfluenceDoesNot)
{
  auto selection = FrameLightSelection {};
  selection.local_lights = {
    FrameLocalLightSelection {
      .range = 10.0F,
      .luminous_flux_lm = 100.0F,
      .flags = kLocalLightFlagCastsShadows,
    },
  };
  auto data = ShadowFrameData {};
  const auto missing
    = oxygen::vortex::shadows::internal::BuildShadowReferences(selection, data);
  ASSERT_FALSE(missing);
  EXPECT_EQ(missing.error().error, LightingPreparationError::kMissingShadow);
  EXPECT_EQ(missing.error().selection_index, LightSelectionIndex { 0U });
  EXPECT_TRUE(data.local_shadow_references.empty());
  selection.local_lights.front().range = 0.0F;
  ASSERT_TRUE(
    oxygen::vortex::shadows::internal::BuildShadowReferences(selection, data));
  EXPECT_EQ(data.local_shadow_references.front().coverage_state,
    kShadowCoverageNoInfluence);
  EXPECT_EQ(data.local_shadow_references.front().record_index,
    kInvalidShadowRecordIndex);
  selection.local_lights.front().range = 10.0F;
  selection.local_lights.front().luminous_flux_lm = 0.0F;
  ASSERT_TRUE(
    oxygen::vortex::shadows::internal::BuildShadowReferences(selection, data));
  EXPECT_EQ(data.local_shadow_references.front().coverage_state,
    kShadowCoverageNoInfluence);
}

NOLINT_TEST_F(
  ShadowServiceBehaviorTest, FifthPointMapPublishesCompleteSelection)
{
  auto service = ShadowService(*renderer_);
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 1U }, oxygen::frame::Slot { 0U });
  auto resolved_view = MakePerspectiveResolvedView();
  auto selection = FrameLightSelection {};
  selection.local_lights.resize(5U,
    FrameLocalLightSelection {
      .range = 10.0F,
      .luminous_flux_lm = 100.0F,
      .flags = kLocalLightFlagCastsShadows,
    });
  for (unsigned i = 0U; i < selection.local_lights.size(); ++i) {
    selection.local_lights[i].source_node
      = oxygen::scene::NodeHandle { i + 1U, 1U };
  }
  auto input = oxygen::vortex::PreparedViewShadowInput {};
  input.view_id = oxygen::ViewId { 17U };
  input.resolved_view = oxygen::observer_ptr { &resolved_view };
  const auto views = std::array { input };
  service.RenderShadowDepths(
    { .frame_light_set = &selection, .active_views = views });
  const auto* data = service.InspectShadowData(input.view_id);
  ASSERT_NE(data, nullptr);
  EXPECT_EQ(data->cube_local_records.size(), 5U);
  EXPECT_EQ(data->local_shadow_references.size(), 5U);
  EXPECT_TRUE(service.ResolveShadowFrameSlot(input.view_id).IsValid());
  EXPECT_EQ(service.GetLastRenderState().published_view_count, 1U);
}

NOLINT_TEST(ShadowCasterCullingTest, ProjectionRejectsUnrelatedCasters)
{
  auto metadata = std::array<oxygen::vortex::DrawMetadata, 4> {};
  for (auto& draw : metadata) {
    draw.flags.Set(oxygen::vortex::PassMaskBit::kShadowCaster);
    draw.vertex_count = 3U;
    draw.instance_count = 1U;
  }
  const auto bounds = std::array {
    glm::vec4 { 0.0F, 0.0F, -2.0F, 0.25F },
    glm::vec4 { 0.0F, 0.0F, 2.0F, 0.25F },
    glm::vec4 { 20.0F, 0.0F, -2.0F, 0.25F },
    glm::vec4 { 0.0F, 0.0F, -8.0F, 0.25F },
  };
  auto frame = oxygen::vortex::PreparedSceneFrame {};
  frame.draw_metadata_bytes = std::as_bytes(std::span(metadata));
  frame.draw_bounding_spheres = bounds;
  const auto projection = oxygen::MakeReversedZPerspectiveProjectionRH_ZO(
    glm::half_pi<float>(), 1.0F, 0.1F, 10.0F);
  auto culling = oxygen::vortex::shadows::internal::ShadowCasterCulling {};
  culling.BuildDrawCommands(
    frame, projection, glm::vec4 { 0.0F, 0.0F, 0.0F, 1.0F / 5.0F });
  ASSERT_EQ(culling.GetDrawCommands().size(), 1U);
  EXPECT_EQ(culling.GetDrawCommands().front().draw_index, 0U);
  EXPECT_EQ(culling.GetCandidateCount(), 4U);
}

NOLINT_TEST(ShadowCasterCullingTest, KeepsShadowOnlyAndUnknownBounds)
{
  auto metadata = std::array<oxygen::vortex::DrawMetadata, 3> {};
  for (auto& draw : metadata) {
    draw.flags.Set(oxygen::vortex::PassMaskBit::kShadowCaster);
    draw.vertex_count = 3U;
  }
  metadata.back().flags.Unset(oxygen::vortex::PassMaskBit::kShadowCaster);
  const auto bounds = std::array {
    glm::vec4 { 0.0F, 0.0F, -2.0F, 0.25F },
  };
  auto frame = oxygen::vortex::PreparedSceneFrame {};
  frame.draw_metadata_bytes = std::as_bytes(std::span(metadata));
  frame.draw_bounding_spheres = bounds;
  auto culling = oxygen::vortex::shadows::internal::ShadowCasterCulling {};
  culling.BuildDrawCommands(frame,
    oxygen::MakeReversedZPerspectiveProjectionRH_ZO(
      glm::half_pi<float>(), 1.0F, 0.1F, 10.0F));
  ASSERT_EQ(culling.GetDrawCommands().size(), 2U);
  EXPECT_EQ(culling.GetDrawCommands()[0].draw_index, 0U);
  EXPECT_EQ(culling.GetDrawCommands()[1].draw_index, 1U);
}

NOLINT_TEST(ShadowServiceSurfaceTest, EligibilityAndReferencesAgreeForView)
{
  auto view = MakePerspectiveResolvedView();
  auto selection = FrameLightSelection {};
  selection.local_lights = {
    FrameLocalLightSelection {
      .position = { 100.0F, 0.0F, -2.0F },
      .range = 1.0F,
      .luminous_flux_lm = 100.0F,
      .flags = kLocalLightFlagCastsShadows,
    },
    FrameLocalLightSelection {
      .range = 10.0F,
      .color = glm::vec3 { 0.0F },
      .luminous_flux_lm = 100.0F,
      .flags = kLocalLightFlagCastsShadows,
    },
    FrameLocalLightSelection {
      .range = 10.0F,
      .flags = kLocalLightFlagCastsShadows,
    },
  };
  auto data = ShadowFrameData {};
  ASSERT_TRUE(oxygen::vortex::shadows::internal::BuildShadowReferences(
    selection, data, &view));
  for (std::size_t i = 0; i < selection.local_lights.size(); ++i) {
    EXPECT_FALSE(oxygen::vortex::shadows::internal::HasLocalShadowInfluence(
      selection.local_lights[i], &view));
    EXPECT_EQ(data.local_shadow_references[i].coverage_state,
      kShadowCoverageNoInfluence);
  }
  // The source is behind the eye, but its influence reaches visible receivers.
  auto& light = selection.local_lights.front();
  light.position = { 0.0F, 0.0F, 1.0F };
  light.range = 3.0F;
  EXPECT_TRUE(
    oxygen::vortex::shadows::internal::HasLocalShadowInfluence(light, &view));
}

NOLINT_TEST_F(ShadowServiceBehaviorTest,
  LocalMapsPreserveResolutionAndSourceIdentityAcrossSelectionOrder)
{
  auto service = ShadowService(*renderer_);
  auto view = MakePerspectiveResolvedView();
  auto selection = FrameLightSelection {};
  selection.scene_generation = 12U;
  selection.local_lights = {
    FrameLocalLightSelection {
      .source_node = oxygen::scene::NodeHandle { 8U, 1U },
      .range = 10.0F,
      .luminous_flux_lm = 100.0F,
      .flags = kLocalLightFlagCastsShadows,
      .shadow_resolution_hint = oxygen::scene::ShadowResolutionHint::kLow,
    },
    FrameLocalLightSelection {
      .source_node = oxygen::scene::NodeHandle { 3U, 1U },
      .range = 10.0F,
      .luminous_flux_lm = 100.0F,
      .flags = kLocalLightFlagCastsShadows,
      .shadow_resolution_hint = oxygen::scene::ShadowResolutionHint::kHigh,
    },
  };
  auto input = oxygen::vortex::PreparedViewShadowInput {};
  input.view_id = oxygen::ViewId { 22U };
  input.resolved_view = oxygen::observer_ptr { &view };
  const auto inputs = std::array { input };
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 1U }, oxygen::frame::Slot { 0U });
  service.RenderShadowDepths(
    { .frame_light_set = &selection, .active_views = inputs });
  auto surfaces = service.InspectPointShadowSurfaces(input.view_id);
  ASSERT_EQ(surfaces.size(), 2U);
  EXPECT_EQ(surfaces[0]->GetDescriptor().width, 512U);
  EXPECT_EQ(surfaces[1]->GetDescriptor().width, 2048U);
  EXPECT_EQ(surfaces[0]->GetDescriptor().format, oxygen::Format::kDepth32);
  EXPECT_EQ(surfaces[1]->GetDescriptor().format, oxygen::Format::kDepth32);
  const auto first
    = service.InspectShadowData(input.view_id)->cube_local_records;
  std::swap(selection.local_lights[0], selection.local_lights[1]);
  service.OnFrameStart(
    oxygen::frame::SequenceNumber { 2U }, oxygen::frame::Slot { 1U });
  service.RenderShadowDepths(
    { .frame_light_set = &selection, .active_views = inputs });
  const auto& second
    = service.InspectShadowData(input.view_id)->cube_local_records;
  ASSERT_EQ(second.size(), 2U);
  EXPECT_EQ(second[0].surface_srv, first[1].surface_srv);
  EXPECT_EQ(second[1].surface_srv, first[0].surface_srv);
  EXPECT_EQ(second[0].selection_index, LightSelectionIndex { 0U });
  EXPECT_EQ(second[1].selection_index, LightSelectionIndex { 1U });
}

NOLINT_TEST_F(
  ShadowServiceBehaviorTest, CacheDetectsEnteringCasterWithoutReuseSubmission)
{
  auto service = ShadowService(*renderer_);
  auto view = MakePerspectiveResolvedView();
  auto selection = FrameLightSelection {};
  selection.scene_generation = 19U;
  selection.local_lights = { FrameLocalLightSelection {
    .source_node = oxygen::scene::NodeHandle { 3U, 1U },
    .range = 5.0F,
    .luminous_flux_lm = 100.0F,
    .flags = kLocalLightFlagCastsShadows,
  } };
  selection.local_lights.push_back(selection.local_lights.front());
  selection.local_lights.back().source_node
    = oxygen::scene::NodeHandle { 4U, 1U };
  selection.local_lights.back().position.x = 5.0F;
  auto metadata = std::array<oxygen::vortex::DrawMetadata, 1> {};
  metadata[0].flags.Set(oxygen::vortex::PassMaskBit::kShadowCaster);
  metadata[0].flags.Set(oxygen::vortex::PassMaskBit::kMasked);
  metadata[0].vertex_count = 3U;
  auto bounds = std::array { glm::vec4 { -4.0F, 0.0F, -20.0F, 0.25F } };
  auto world = std::array<float, 32> {};
  world[0] = world[5] = world[10] = world[15] = 1.0F;
  world[12] = -4.0F;
  world[14] = -20.0F;
  auto items = std::array<oxygen::vortex::sceneprep::RenderItemData, 1> {};
  items[0].node_handle = oxygen::scene::NodeHandle { 7U, 1U };
  auto frame = oxygen::vortex::PreparedSceneFrame {};
  frame.draw_metadata_bytes = std::as_bytes(std::span(metadata));
  frame.draw_bounding_spheres = bounds;
  frame.world_matrices = world;
  frame.render_items = items;
  auto sources
    = std::array { oxygen::vortex::ShadowCasterSource { .draw = metadata[0],
      .bounds = bounds[0],
      .node = items[0].node_handle,
      .geometry_content_revision = 1U } };
  frame.shadow_caster_sources = sources;
  auto materials = std::array<oxygen::vortex::MaterialShadingConstants, 2> {};
  auto texture_revisions = std::array<std::uint64_t, 2> {};
  materials[0].base_color_texture_index = oxygen::ShaderVisibleIndex { 5U };
  texture_revisions[0] = 1U; // This fixture samples a known masked texture.
  frame.shadow_materials = materials;
  frame.shadow_texture_revisions = texture_revisions;
  const auto constants = graphics_->CreateBuffer({
    .size_bytes = 1024U,
    .usage = oxygen::graphics::BufferUsage::kConstant,
    .memory = oxygen::graphics::BufferMemory::kUpload,
  });
  const auto inputs = std::array { oxygen::vortex::PreparedViewShadowInput {
    .view_id = oxygen::ViewId { 24U },
    .prepared_scene = oxygen::observer_ptr { &frame },
    .resolved_view = oxygen::observer_ptr { &view },
    .view_constants = oxygen::observer_ptr { constants.get() },
  } };
  auto* queue = static_cast<oxygen::vortex::testing::FakeCommandQueue*>(
    graphics_->GetCommandQueue(oxygen::graphics::QueueRole::kGraphics).get());
  queue->SetAutoComplete(false);
  const auto render = [&](const std::uint32_t number) {
    service.OnFrameStart(oxygen::frame::SequenceNumber { number },
      oxygen::frame::Slot { number % 3U });
    service.RenderShadowDepths(
      { .frame_light_set = &selection, .active_views = inputs });
  };
  render(1U);
  EXPECT_EQ(service.GetLastRenderState().rendered_point_shadow_count, 2U);
  EXPECT_EQ(service.GetLastRenderState().rendered_draw_count, 0U);
  const auto submitted = queue->submitted_batches;
  EXPECT_EQ(submitted, 2U);
  {
    const auto diagnostic = service.InspectLocalSharing();
    EXPECT_EQ(diagnostic.aliases, 2U);
    EXPECT_EQ(diagnostic.live_versions, 2U);
    EXPECT_EQ(diagnostic.cache_misses, 2U);
    EXPECT_EQ(diagnostic.first_allocations, 2U);
    ASSERT_EQ(diagnostic.backings.size(), 1U);
    EXPECT_FALSE(diagnostic.backings.front().closing);
  }
  render(2U);
  EXPECT_EQ(service.GetLastRenderState().rendered_point_shadow_count, 0U);
  EXPECT_EQ(queue->submitted_batches, submitted);
  EXPECT_EQ(service.InspectLocalSharing().cache_hits, 2U);
  EXPECT_EQ(queue->InspectSubmissionCounters().completion_signals, 2U);
  EXPECT_EQ(queue->InspectSubmissionCounters().dependency_waits, 0U);
  EXPECT_TRUE(queue->submitted_actions
      .empty()); // no shadow self-waits or reserved signals
  sources[0].bounds.z = bounds[0].z = world[14] = -2.0F;
  render(3U);
  EXPECT_EQ(service.GetLastRenderState().rendered_point_shadow_count, 1U);
  EXPECT_GT(service.GetLastRenderState().rendered_draw_count, 0U);
  // Non-caster transforms, unrelated materials/textures and source ordering
  // must not invalidate either map, including maps sharing one surface.
  world[28] = 100.0F;
  materials[1].alpha_cutoff = 0.8F;
  texture_revisions[1] = 1U;
  std::swap(selection.local_lights[0], selection.local_lights[1]);
  render(4U);
  EXPECT_EQ(service.GetLastRenderState().rendered_point_shadow_count, 0U);
  texture_revisions[0] = 2U;
  graphics_->SetFailSubmission(true);
  render(5U);
  EXPECT_EQ(service.InspectShadowData(inputs[0].view_id), nullptr);
  graphics_->SetFailSubmission(false);
  render(6U);
  EXPECT_EQ(service.GetLastRenderState().rendered_point_shadow_count, 1U);
  EXPECT_NE(service.InspectShadowData(inputs[0].view_id), nullptr);
  sources[0].bounds.z = bounds[0].z = world[14] = -20.0F;
  render(7U);
  EXPECT_EQ(service.GetLastRenderState().rendered_point_shadow_count, 1U);
  render(8U);
  EXPECT_EQ(service.GetLastRenderState().rendered_point_shadow_count, 0U);
  auto added = selection.local_lights.back();
  added.source_node = oxygen::scene::NodeHandle { 1U, 1U };
  selection.local_lights.insert(selection.local_lights.begin(), added);
  render(9U);
  EXPECT_EQ(service.GetLastRenderState().rendered_point_shadow_count, 1U);
  selection.local_lights.erase(selection.local_lights.begin());
  render(10U);
  EXPECT_EQ(service.GetLastRenderState().rendered_point_shadow_count, 0U);
  selection.local_lights.front().position.x = 10000.0F;
  render(11U);
  EXPECT_EQ(service.GetLastRenderState().rendered_point_shadow_count, 0U);
  selection.local_lights.front().position.x = 5.0F;
  render(12U);
  EXPECT_EQ(service.GetLastRenderState().rendered_point_shadow_count, 0U);
  // Both maps leave camera coverage while caster content is replaced in place.
  sources[0].bounds.z = bounds[0].z = world[14] = -2.0F;
  render(13U);
  EXPECT_EQ(service.GetLastRenderState().rendered_point_shadow_count, 1U);
  render(14U);
  EXPECT_EQ(service.GetLastRenderState().rendered_point_shadow_count, 0U);
  auto away_params = oxygen::ResolvedView::Params {};
  away_params.view_config.viewport = view.Viewport();
  away_params.proj_matrix = view.StableProjectionMatrix();
  away_params.view_matrix[3][0] = -10000.0F;
  away_params.near_plane = 0.1F;
  away_params.far_plane = 100.0F;
  view = oxygen::ResolvedView(away_params);
  render(15U);
  EXPECT_TRUE(service.InspectPointShadowSurfaces(inputs[0].view_id).empty());
  ++sources[0].geometry_content_revision;
  render(16U);
  view = MakePerspectiveResolvedView();
  render(17U);
  EXPECT_EQ(service.GetLastRenderState().rendered_point_shadow_count, 1U);
  render(18U);
  EXPECT_EQ(service.GetLastRenderState().rendered_point_shadow_count, 0U);
  sources[0].geometry_content_revision = 0U;
  render(19U);
  EXPECT_EQ(service.GetLastRenderState().rendered_point_shadow_count, 1U);
  render(20U);
  EXPECT_EQ(service.GetLastRenderState().rendered_point_shadow_count, 1U);
  sources[0].geometry_content_revision = 2U;
  frame.preparation_revision = 1U;
  selection.selection_epoch = 1U;
  render(21U);
  EXPECT_EQ(service.GetLastRenderState().rendered_point_shadow_count, 1U);
  render(21U);
  EXPECT_EQ(service.GetLastRenderState().rendered_point_shadow_count, 0U);
  world[12] -= 0.01F;
  ++frame.preparation_revision; // Same frame, rebuilt offscreen snapshot.
  render(21U);
  EXPECT_EQ(service.GetLastRenderState().rendered_point_shadow_count, 1U);
}

NOLINT_TEST(ShadowQualityTest, BucketsHysteresisAndFadeFollowProjectedSize)
{
  using oxygen::vortex::shadows::internal::SelectLocalShadowQuality;
  EXPECT_EQ(SelectLocalShadowQuality(31.0F, 1024U).resolution, 0U);
  EXPECT_FLOAT_EQ(SelectLocalShadowQuality(32.0F, 1024U).strength, 0.0F);
  EXPECT_FLOAT_EQ(SelectLocalShadowQuality(48.0F, 1024U).strength, 0.5F);
  EXPECT_FLOAT_EQ(SelectLocalShadowQuality(64.0F, 1024U).strength, 1.0F);
  EXPECT_EQ(SelectLocalShadowQuality(900.0F, 1024U).resolution, 512U);
  EXPECT_EQ(SelectLocalShadowQuality(900.0F, 1024U, 1024U).resolution, 1024U);
  EXPECT_EQ(SelectLocalShadowQuality(767.0F, 1024U, 1024U).resolution, 512U);
  EXPECT_EQ(SelectLocalShadowQuality(10000.0F, 512U, 1024U).resolution, 512U);
}

NOLINT_TEST(ShadowQualityTest, QualityOmissionDoesNotBecomeMissingShadow)
{
  auto selection = FrameLightSelection {};
  selection.local_lights = { FrameLocalLightSelection {
    .range = 10.0F,
    .luminous_flux_lm = 100.0F,
    .flags = kLocalLightFlagCastsShadows,
  } };
  auto data = ShadowFrameData {};
  data.local_quality_omissions = { LightSelectionIndex { 0U } };
  ASSERT_TRUE(
    oxygen::vortex::shadows::internal::BuildShadowReferences(selection, data));
  EXPECT_EQ(data.local_shadow_references[0].coverage_state,
    oxygen::vortex::kShadowCoverageQualityOmitted);
  EXPECT_EQ(
    data.local_shadow_references[0].record_index, kInvalidShadowRecordIndex);
  data.local_quality_omissions.clear();
  EXPECT_FALSE(
    oxygen::vortex::shadows::internal::BuildShadowReferences(selection, data));
}

NOLINT_TEST_F(
  ShadowServiceBehaviorTest, NewChunkDoesNotReplaceExistingPointArray)
{
  auto allocator = ConventionalShadowTargetAllocator(*renderer_);
  allocator.OnFrameStart(
    oxygen::frame::SequenceNumber { 1 }, oxygen::frame::Slot { 0 });
  const auto view = oxygen::ViewId { 23 };
  const auto first
    = ProduceMap(allocator, view, oxygen::scene::NodeHandle { 1, 1 }, 1024);
  const auto second
    = ProduceMap(allocator, view, oxygen::scene::NodeHandle { 2, 1 }, 1024);
  const auto third
    = ProduceMap(allocator, view, oxygen::scene::NodeHandle { 3, 1 }, 1024);
  EXPECT_EQ(first->version->slot->backing, second->version->slot->backing);
  EXPECT_NE(first->version->slot->backing, third->version->slot->backing);
  EXPECT_EQ(
    first->version->slot->backing->texture->GetDescriptor().array_size, 12U);
  EXPECT_EQ(
    third->version->slot->backing->texture->GetDescriptor().array_size, 12U);
  const auto reused
    = ProduceMap(allocator, view, oxygen::scene::NodeHandle { 1, 1 }, 1024);
  EXPECT_EQ(reused, first);
}

NOLINT_TEST_F(ShadowServiceBehaviorTest,
  NexusSlotsRetireBeforeActualGpuCompletionAndRecycleAfterIt)
{
  auto allocator = ConventionalShadowTargetAllocator(*renderer_);
  const auto view = oxygen::ViewId { 71 };
  auto* queue = static_cast<oxygen::vortex::testing::FakeCommandQueue*>(
    graphics_->GetCommandQueue(oxygen::graphics::QueueRole::kGraphics).get());
  queue->SetAutoComplete(false);
  allocator.OnFrameStart(
    oxygen::frame::SequenceNumber { 1 }, oxygen::frame::Slot { 0 });
  auto first
    = ProduceMap(allocator, view, oxygen::scene::NodeHandle { 10, 1 }, 512);
  auto survivor
    = ProduceMap(allocator, view, oxygen::scene::NodeHandle { 11, 1 }, 512);
  const auto first_handle = first->version->slot->handle;
  const auto first_offset = first->version->slot->offset;
  const auto receipt = *first->version->producer;
  const auto pool = first->version->slot->pool.lock();
  const auto backing = first->version->slot->backing;
  std::array lights { FrameLocalLightSelection {
    .source_node = oxygen::scene::NodeHandle { 11, 1 },
    .range = 5,
    .luminous_flux_lm = 100,
    .flags = kLocalLightFlagCastsShadows } };
  allocator.RetainLocalSources(view, 1, lights);
  first.reset();
  EXPECT_FALSE(pool->reuse.IsHandleCurrent(first_handle));
  EXPECT_EQ(pool->reuse.GetTelemetrySnapshot().pending_count, 1U);
  const auto fresh
    = ProduceMap(allocator, view, oxygen::scene::NodeHandle { 12, 1 }, 512);
  EXPECT_NE(fresh->version->slot->handle.index, first_handle.index);
  queue->CompletePrivateThrough(receipt.Value());
  graphics_->PollCompletedUses();
  EXPECT_EQ(pool->reuse.GetTelemetrySnapshot().pending_count, 0U);
  const auto recycled
    = ProduceMap(allocator, view, oxygen::scene::NodeHandle { 13, 1 }, 512);
  EXPECT_EQ(recycled->version->slot->handle.index, first_handle.index);
  EXPECT_NE(
    recycled->version->slot->handle.generation, first_handle.generation);
  EXPECT_EQ(recycled->version->slot->offset, first_offset);
  EXPECT_EQ(recycled->version->slot->backing, backing);
  const auto survivor_again
    = ProduceMap(allocator, view, oxygen::scene::NodeHandle { 11, 1 }, 512);
  EXPECT_EQ(survivor_again, survivor);
  graphics_->Flush();
}

NOLINT_TEST(ShadowQualityTest, OrthographicAxisMotionPreservesResolutionAndFade)
{
  auto params = oxygen::ResolvedView::Params {};
  params.view_config.viewport.width = params.view_config.viewport.height
    = 128.0F;
  params.proj_matrix = oxygen::MakeReversedZOrthographicProjectionRH_ZO(
    -2.0F, 2.0F, -2.0F, 2.0F, -100.0F, 100.0F);
  params.near_plane = -100.0F;
  params.far_plane = 100.0F;
  auto light = FrameLocalLightSelection {};
  light.range = 1.0F;
  params.camera_position = glm::vec3 { 0.0F };
  const auto inside = oxygen::ResolvedView(params);
  params.camera_position = glm::vec3 { 0.0F, 0.0F, 20.0F };
  params.view_matrix[3][2] = -20.0F;
  const auto outside = oxygen::ResolvedView(params);
  const auto a = oxygen::vortex::shadows::internal::EvaluateLocalShadowQuality(
    light, &inside, 1024U);
  const auto b = oxygen::vortex::shadows::internal::EvaluateLocalShadowQuality(
    light, &outside, 1024U);
  EXPECT_EQ(a.resolution, b.resolution);
  EXPECT_FLOAT_EQ(a.strength, b.strength);
  EXPECT_GT(a.strength, 0.0F);
  EXPECT_LT(a.strength, 1.0F);
  EXPECT_LT(a.resolution, 1024U);
}

NOLINT_TEST_F(ShadowServiceBehaviorTest,
  EmptyLocalSelectionRetiresOwnersWhileDirectionalViewStaysActive)
{
  auto service = ShadowService(*renderer_);
  auto view = MakePerspectiveResolvedView();
  auto selection = FrameLightSelection {};
  selection.scene_generation = 41U;
  selection.directional_lights
    = { FrameDirectionalLightSelection { .illuminance_lux = 100.0F,
      .shadow_flags = kDirectionalLightShadowFlagCastsShadows,
      .cascade_count = 1U,
      .shadow_resolution_hint = oxygen::scene::ShadowResolutionHint::kLow } };
  selection.local_lights = {
    FrameLocalLightSelection {
      .source_node = oxygen::scene::NodeHandle { 1U, 1U },
      .range = 5.0F,
      .luminous_flux_lm = 100.0F,
      .flags = kLocalLightFlagCastsShadows },
    FrameLocalLightSelection {
      .source_node = oxygen::scene::NodeHandle { 2U, 1U },
      .kind = LocalLightKind::kSpot,
      .range = 5.0F,
      .luminous_flux_lm = 100.0F,
      .flags = kLocalLightFlagCastsShadows },
  };
  const auto views = std::array { oxygen::vortex::PreparedViewShadowInput {
    .view_id = oxygen::ViewId { 92U },
    .resolved_view = oxygen::observer_ptr { &view } } };
  const auto render = [&](unsigned number) {
    const auto slot = oxygen::frame::Slot { (number - 1U) % 3U };
    graphics_->PollCompletedUses();
    graphics_->GetDeferredReclaimer().OnBeginFrame(slot);
    service.OnFrameStart(oxygen::frame::SequenceNumber { number }, slot);
    service.RenderShadowDepths(
      { .frame_light_set = &selection, .active_views = views });
    EXPECT_NE(service.InspectShadowData(views[0].view_id), nullptr);
  };
  render(1U);
  ASSERT_EQ(service.InspectPointShadowSurfaces(views[0].view_id).size(), 1U);
  ASSERT_EQ(service.InspectSpotShadowSurfaces(views[0].view_id).size(), 1U);
  const auto point = std::weak_ptr(
    service.InspectPointShadowSurfaces(views[0].view_id).front());
  const auto spot = std::weak_ptr(
    service.InspectSpotShadowSurfaces(views[0].view_id).front());
  const auto sun = std::weak_ptr(
    service.InspectDirectionalShadowSurfaces(views[0].view_id).front());
  selection.local_lights.clear();
  render(2U);
  EXPECT_FALSE(point.expired());
  EXPECT_FALSE(spot.expired());
  for (unsigned frame = 3U; frame <= 9U; ++frame) {
    render(frame);
  }
  EXPECT_TRUE(point.expired());
  EXPECT_TRUE(spot.expired());
  EXPECT_FALSE(sun.expired());
  EXPECT_EQ(service.GetLastRenderState().rendered_cascade_count, 1U);
}

NOLINT_TEST_F(ShadowServiceBehaviorTest,
  ResolutionChangesRedrawWarmedPointAndSpotCachesThenReuse)
{
  auto service = ShadowService(*renderer_);
  auto view = MakePerspectiveResolvedView();
  auto scene = oxygen::vortex::PreparedSceneFrame {};
  auto selection = FrameLightSelection {};
  selection.scene_generation = 42U;
  selection.local_lights = {
    FrameLocalLightSelection {
      .source_node = oxygen::scene::NodeHandle { 1U, 1U },
      .range = 5.0F,
      .luminous_flux_lm = 100.0F,
      .flags = kLocalLightFlagCastsShadows },
    FrameLocalLightSelection {
      .source_node = oxygen::scene::NodeHandle { 2U, 1U },
      .kind = LocalLightKind::kSpot,
      .range = 5.0F,
      .luminous_flux_lm = 100.0F,
      .flags = kLocalLightFlagCastsShadows },
  };
  selection.local_lights[1].direction = { 0.0F, 0.0F, -1.0F };
  auto metadata = std::array<oxygen::vortex::DrawMetadata, 1> {};
  metadata[0].flags.Set(oxygen::vortex::PassMaskBit::kShadowCaster);
  metadata[0].vertex_count = 3U;
  auto bounds = std::array { glm::vec4 { 0.0F, 0.0F, -1.0F, 0.1F } };
  auto world = std::array<float, 16> {};
  world[0] = world[5] = world[10] = world[15] = 1.0F;
  world[14] = -1.0F;
  auto sources
    = std::array { oxygen::vortex::ShadowCasterSource { .draw = metadata[0],
      .bounds = bounds[0],
      .geometry_content_revision = 1U } };
  scene.draw_metadata_bytes = std::as_bytes(std::span(metadata));
  scene.draw_bounding_spheres = bounds;
  scene.shadow_caster_sources = sources;
  scene.world_matrices = world;
  const auto constants = graphics_->CreateBuffer({ .size_bytes = 1024U,
    .usage = oxygen::graphics::BufferUsage::kConstant,
    .memory = oxygen::graphics::BufferMemory::kUpload });
  const auto views = std::array { oxygen::vortex::PreparedViewShadowInput {
    .view_id = oxygen::ViewId { 93U },
    .prepared_scene = oxygen::observer_ptr { &scene },
    .resolved_view = oxygen::observer_ptr { &view },
    .view_constants = oxygen::observer_ptr { constants.get() } } };
  auto frame = 0U;
  for (auto hint : { oxygen::scene::ShadowResolutionHint::kMedium,
         oxygen::scene::ShadowResolutionHint::kLow,
         oxygen::scene::ShadowResolutionHint::kMedium }) {
    for (auto& light : selection.local_lights) {
      light.shadow_resolution_hint = hint;
    }
    for (unsigned repeat = 0U; repeat < 2U; ++repeat) {
      const auto slot = oxygen::frame::Slot { frame % 3U };
      graphics_->PollCompletedUses();
      graphics_->GetDeferredReclaimer().OnBeginFrame(slot);
      service.OnFrameStart(oxygen::frame::SequenceNumber { ++frame }, slot);
      service.RenderShadowDepths(
        { .frame_light_set = &selection, .active_views = views });
      const auto* data = service.InspectShadowData(views[0].view_id);
      ASSERT_NE(data, nullptr);
      ASSERT_EQ(data->cube_local_records.size(), 1U);
      ASSERT_EQ(data->projected_local_records.size(), 1U);
      const auto resolution
        = hint == oxygen::scene::ShadowResolutionHint::kLow ? 512U : 1024U;
      EXPECT_FLOAT_EQ(
        data->cube_local_records[0].inverse_resolution.x, 1.0F / resolution);
      EXPECT_FLOAT_EQ(data->projected_local_records[0].inverse_resolution.x,
        1.0F / resolution);
      EXPECT_EQ(service.GetLastRenderState().rendered_point_shadow_count,
        repeat == 0U ? 1U : 0U);
      EXPECT_EQ(service.GetLastRenderState().rendered_spot_shadow_count,
        repeat == 0U ? 1U : 0U);
      EXPECT_EQ(
        service.GetLastRenderState().rendered_draw_count > 0U, repeat == 0U);
    }
  }
}

class ShadowTestQueues final : public oxygen::graphics::QueuesStrategy {
public:
  auto Clone() const
    -> std::unique_ptr<oxygen::graphics::QueuesStrategy> override
  {
    return std::make_unique<ShadowTestQueues>();
  }
  auto KeyFor(oxygen::graphics::QueueRole role) const
    -> oxygen::graphics::QueueKey override
  {
    return oxygen::graphics::QueueKey {
      role == oxygen::graphics::QueueRole::kCompute ? "shadow-compute"
                                                    : "universal"
    };
  }
  auto Specifications() const
    -> std::vector<oxygen::graphics::QueueSpecification> override
  {
    using namespace oxygen::graphics;
    return { { KeyFor(QueueRole::kGraphics), QueueRole::kGraphics,
               QueueAllocationPreference::kDedicated,
               QueueSharingPreference::kShared },
      { KeyFor(QueueRole::kCompute), QueueRole::kCompute,
        QueueAllocationPreference::kDedicated,
        QueueSharingPreference::kShared } };
  }
};

class ShadowSharingTest : public ShadowServiceBehaviorTest {
protected:
  auto MakeQueueStrategy() const
    -> std::unique_ptr<oxygen::graphics::QueuesStrategy> override
  {
    return std::make_unique<ShadowTestQueues>();
  }
  void SetUp() override
  {
    ShadowServiceBehaviorTest::SetUp();
    selection.scene_generation = 77;
    selection.selection_epoch = 1;
    selection.local_lights = { FrameLocalLightSelection {
      .source_node = oxygen::scene::NodeHandle { 9, 1 },
      .range = 5,
      .luminous_flux_lm = 100,
      .flags = kLocalLightFlagCastsShadows,
      .shadow_resolution_hint = oxygen::scene::ShadowResolutionHint::kLow } };
    for (size_t i = 0; i < scenes.size(); ++i) {
      sources[i].node = oxygen::scene::NodeHandle { 7, 1 };
      sources[i].geometry_content_revision = 1;
      sources[i].bounds = glm::vec4(0, 0, -1, 0.1F);
      scenes[i].preparation_revision = i + 1;
      scenes[i].world_matrices = world;
      scenes[i].shadow_caster_sources = { &sources[i], 1 };
      inputs[i]
        = { .view_id = oxygen::ViewId { static_cast<uint32_t>(101 + i) },
            .prepared_scene = oxygen::observer_ptr { &scenes[i] },
            .resolved_view = oxygen::observer_ptr { &view } };
    }
    service = std::make_unique<ShadowService>(*renderer_);
  }
  void Render(uint64_t number,
    std::span<const oxygen::vortex::PreparedViewShadowInput> active = {})
  {
    if (number != frame) {
      graphics_->PollCompletedUses();
      service->OnFrameStart(oxygen::frame::SequenceNumber { number },
        oxygen::frame::Slot { static_cast<uint32_t>(number % 3) });
      frame = number;
    }
    service->RenderShadowDepths({ .frame_light_set = &selection,
      .active_views = active.empty() ? std::span(inputs) : active,
      .preparation_views = inputs });
  }
  auto Record(size_t view_index) const -> CubeLocalShadowRecord
  {
    const auto* data = service->InspectShadowData(inputs[view_index].view_id);
    if (!data || data->cube_local_records.empty()) {
      throw std::runtime_error("Missing test publication");
    }
    return data->cube_local_records.front();
  }
  void MutateCasters()
  {
    for (size_t i = 0; i < scenes.size(); ++i) {
      ++sources[i].geometry_content_revision;
      scenes[i].preparation_revision += 2;
    }
  }
  oxygen::ResolvedView view = MakePerspectiveResolvedView();
  std::array<float, 16> world { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0,
    1 };
  std::array<oxygen::vortex::ShadowCasterSource, 2> sources;
  std::array<oxygen::vortex::PreparedSceneFrame, 2> scenes;
  std::array<oxygen::vortex::PreparedViewShadowInput, 2> inputs;
  FrameLightSelection selection;
  uint64_t frame { 0 };
  std::unique_ptr<ShadowService> service;
};

NOLINT_TEST_F(
  ShadowSharingTest, CompatibleViewsRenderOnceAndWarmHitsSubmitNothing)
{
  Render(1);
  EXPECT_EQ(service->GetLastRenderState().rendered_point_shadow_count, 1U);
  EXPECT_EQ(Record(0).surface_srv, Record(1).surface_srv);
  EXPECT_EQ(Record(0).first_array_layer, Record(1).first_array_layer);
  auto* queue = static_cast<oxygen::vortex::testing::FakeCommandQueue*>(
    graphics_->GetCommandQueue(oxygen::graphics::QueueRole::kGraphics).get());
  const auto submissions = queue->submitted_batches;
  Render(2);
  EXPECT_EQ(service->GetLastRenderState().rendered_point_shadow_count, 0U);
  EXPECT_EQ(queue->submitted_batches, submissions);
}
NOLINT_TEST_F(
  ShadowSharingTest, FullFamilyPreparationAllowsOneInPlaceDynamicUpdate)
{
  Render(1);
  const auto original = Record(0);
  MutateCasters();
  Render(2, std::span(inputs).first(1));
  EXPECT_EQ(service->GetLastRenderState().rendered_point_shadow_count, 1U);
  EXPECT_EQ(Record(0).surface_srv, original.surface_srv);
  EXPECT_EQ(Record(0).first_array_layer, original.first_array_layer);
  Render(2, std::span(inputs).subspan(1));
  EXPECT_EQ(service->GetLastRenderState().rendered_point_shadow_count, 0U);
  EXPECT_EQ(Record(0).surface_srv, Record(1).surface_srv);
  EXPECT_EQ(Record(0).first_array_layer, Record(1).first_array_layer);
  EXPECT_NE(service->InspectReadSet(inputs[0].view_id), nullptr);
}
NOLINT_TEST_F(ShadowSharingTest,
  RetainedContentForcesCopyOnWriteAndSurvivesPublicationExpiry)
{
  Render(1);
  auto retained = service->RetainLocalContent(
    inputs[0].view_id, selection.local_lights[0].source_node);
  ASSERT_TRUE(retained);
  const auto original_layer = retained.FirstLayer();
  const auto original_texture = retained.Texture();
  MutateCasters();
  Render(2);
  const auto current = service->RetainLocalContent(
    inputs[0].view_id, selection.local_lights[0].source_node);
  ASSERT_TRUE(current);
  EXPECT_TRUE(current.Texture() != original_texture
    || current.FirstLayer() != original_layer);
  EXPECT_EQ(retained.FirstLayer(), original_layer);
  auto recording = graphics_->AcquireCommandRecorder(
    graphics_->QueueKeyFor(oxygen::graphics::QueueRole::kGraphics),
    "Retained old content", oxygen::graphics::SubmissionPolicy::kExplicit);
  EXPECT_TRUE(retained.Attach(*recording, graphics_->GetResourceRegistry()));
  EXPECT_TRUE(recording.Submit());
}
NOLINT_TEST_F(
  ShadowSharingTest, UnsubmittedReaderPreventsWritingAnyLayerOfItsBacking)
{
  Render(1);
  const auto old_texture
    = service->InspectPointShadowSurfaces(inputs[0].view_id).front();
  auto recording = graphics_->AcquireCommandRecorder(
    graphics_->QueueKeyFor(oxygen::graphics::QueueRole::kGraphics),
    "Delayed reader", oxygen::graphics::SubmissionPolicy::kExplicit);
  service->AttachLocalReads(inputs[0].view_id,
    oxygen::frame::SequenceNumber { 1 }, scenes[0].preparation_revision,
    *recording);
  MutateCasters();
  Render(2);
  EXPECT_NE(service->InspectPointShadowSurfaces(inputs[0].view_id).front(),
    old_texture);
  EXPECT_FALSE(recording.Submit()); // frame publication expired;
                                    // retained-content captures use a lease
  graphics_->Flush();
}
NOLINT_TEST_F(ShadowSharingTest, ExpiredFrameReadSetRejectsNewAttachment)
{
  Render(1);
  const auto expired = service->InspectReadSet(inputs[0].view_id);
  ASSERT_NE(expired, nullptr);
  Render(2);
  auto recording = graphics_->AcquireCommandRecorder(
    graphics_->QueueKeyFor(oxygen::graphics::QueueRole::kGraphics),
    "Expired frame", oxygen::graphics::SubmissionPolicy::kExplicit);
  EXPECT_EQ(expired
              ->Attach(oxygen::frame::SequenceNumber { 1 },
                scenes[0].preparation_revision, *recording,
                graphics_->GetResourceRegistry())
              .error(),
    oxygen::vortex::ShadowUseError::kClosed);
  recording.Discard();
}
NOLINT_TEST_F(ShadowSharingTest, DifferentLodsRemainSeparateWhileEqualLodsShare)
{
  sources[1].lod_index = 1;
  Render(1);
  EXPECT_EQ(service->GetLastRenderState().rendered_point_shadow_count, 2U);
  EXPECT_TRUE(Record(0).surface_srv != Record(1).surface_srv
    || Record(0).first_array_layer != Record(1).first_array_layer);
  sources[1].lod_index = 0;
  scenes[1].preparation_revision += 2;
  Render(2);
  EXPECT_EQ(service->GetLastRenderState().rendered_point_shadow_count, 0U);
  EXPECT_EQ(Record(0).surface_srv, Record(1).surface_srv);
  EXPECT_EQ(Record(0).first_array_layer, Record(1).first_array_layer);
}
NOLINT_TEST_F(
  ShadowSharingTest, FailedFirstWriterDoesNotPublishAndSecondViewCanRetry)
{
  graphics_->SetFailSubmission(true);
  Render(1, std::span(inputs).first(1));
  EXPECT_EQ(service->InspectShadowData(inputs[0].view_id), nullptr);
  graphics_->SetFailSubmission(false);
  Render(1, std::span(inputs).subspan(1));
  EXPECT_NE(service->InspectShadowData(inputs[1].view_id), nullptr);
  EXPECT_EQ(service->GetLastRenderState().rendered_point_shadow_count, 1U);
}
NOLINT_TEST_F(ShadowSharingTest,
  ExclusiveReadbackBlocksReadersUntilSubmissionThenOrdersThem)
{
  Render(1);
  auto retained = service->RetainLocalContent(
    inputs[0].view_id, selection.local_lights[0].source_node);
  ASSERT_TRUE(retained);
  const auto key
    = graphics_->QueueKeyFor(oxygen::graphics::QueueRole::kGraphics);
  auto copy = graphics_->AcquireCommandRecorder(
    key, "Exclusive copy", oxygen::graphics::SubmissionPolicy::kExplicit);
  ASSERT_TRUE(retained.AttachReadback(*copy, graphics_->GetResourceRegistry()));
  auto blocked = graphics_->AcquireCommandRecorder(
    key, "Premature reader", oxygen::graphics::SubmissionPolicy::kExplicit);
  EXPECT_EQ(retained.Attach(*blocked, graphics_->GetResourceRegistry()).error(),
    oxygen::vortex::ShadowUseError::kNotReady);
  blocked.Discard();
  const auto result = copy.SubmitWithReceipt();
  ASSERT_TRUE(result.receipt);
  auto reader = graphics_->AcquireCommandRecorder(
    key, "Ordered reader", oxygen::graphics::SubmissionPolicy::kExplicit);
  ASSERT_TRUE(retained.Attach(*reader, graphics_->GetResourceRegistry()));
  const auto dependencies
    = reader->GetCommandListForInspection()->Uses().Dependencies();
  ASSERT_EQ(dependencies.size(), 1U);
  EXPECT_EQ(dependencies.front(), *result.receipt);
  EXPECT_TRUE(reader.Submit());
}

NOLINT_TEST_F(
  ShadowSharingTest, WriterOrdersSubmittedCrossQueueReaderBeforeInPlaceUpdate)
{
  Render(1);
  const auto original = Record(0);
  auto lease = service->RetainLocalContent(
    inputs[0].view_id, selection.local_lights[0].source_node);
  auto* compute = static_cast<oxygen::vortex::testing::FakeCommandQueue*>(
    graphics_->GetCommandQueue(oxygen::graphics::QueueRole::kCompute).get());
  auto* graphics = static_cast<oxygen::vortex::testing::FakeCommandQueue*>(
    graphics_->GetCommandQueue(oxygen::graphics::QueueRole::kGraphics).get());
  compute->SetAutoComplete(false);
  auto reader = graphics_->AcquireCommandRecorder(
    graphics_->QueueKeyFor(oxygen::graphics::QueueRole::kCompute),
    "Cross-queue reader", oxygen::graphics::SubmissionPolicy::kExplicit);
  ASSERT_TRUE(lease.Attach(*reader, graphics_->GetResourceRegistry()));
  const auto result = reader.SubmitWithReceipt();
  ASSERT_TRUE(result.receipt);
  lease = {};
  graphics->waited_dependencies.clear();
  MutateCasters();
  Render(2);
  EXPECT_EQ(Record(0).surface_srv, original.surface_srv);
  EXPECT_EQ(Record(0).first_array_layer, original.first_array_layer);
  EXPECT_NE(std::ranges::find(graphics->waited_dependencies, *result.receipt),
    graphics->waited_dependencies.end());
  EXPECT_EQ(compute->QueryCompletion(*result.receipt),
    oxygen::graphics::CompletionStatus::kPending);
  compute->CompletePrivateThrough(result.receipt->Value());
  graphics_->PollCompletedUses();
}
NOLINT_TEST_F(ShadowSharingTest,
  PartiallyOverlappingViewsAllocateTheUnionWithViewLocalReferences)
{
  std::array<oxygen::ResolvedView, 2> resolved { view, view };
  for (size_t i = 0; i < resolved.size(); ++i) {
    oxygen::ResolvedView::Params params;
    params.view_config.viewport.width = params.view_config.viewport.height
      = 128;
    params.proj_matrix = oxygen::MakeReversedZOrthographicProjectionRH_ZO(
      -1, 1, -1, 1, 0.1F, 100);
    const auto x = i == 0 ? -1.0F : 1.0F;
    params.view_matrix[3][0] = -x;
    params.camera_position = glm::vec3(x, 0, 0);
    params.near_plane = 0.1F;
    params.far_plane = 100;
    resolved[i] = oxygen::ResolvedView(params);
    inputs[i].resolved_view = oxygen::observer_ptr { &resolved[i] };
  }
  const auto prototype = selection.local_lights.front();
  selection.local_lights.assign(3, prototype);
  for (uint32_t i = 0; i < 3; ++i) {
    auto& light = selection.local_lights[i];
    light.source_node = oxygen::scene::NodeHandle { 20 + i, 1 };
    light.position = glm::vec3(-2.0F + 2.0F * i, 0, -3);
    light.range = 0.75F;
  }
  Render(1);
  EXPECT_EQ(service->GetLastRenderState().rendered_point_shadow_count, 3U);
  const auto& a = *service->InspectShadowData(inputs[0].view_id);
  const auto& b = *service->InspectShadowData(inputs[1].view_id);
  ASSERT_EQ(a.cube_local_records.size(), 2U);
  ASSERT_EQ(b.cube_local_records.size(), 2U);
  const auto find_shared
    = [](const auto& data) -> const CubeLocalShadowRecord& {
    return *std::ranges::find(data.cube_local_records,
      LightSelectionIndex { 1 }, &CubeLocalShadowRecord::selection_index);
  };
  EXPECT_EQ(find_shared(a).surface_srv, find_shared(b).surface_srv);
  EXPECT_EQ(find_shared(a).first_array_layer, find_shared(b).first_array_layer);
  EXPECT_EQ(a.local_shadow_references[2].coverage_state,
    oxygen::vortex::kShadowCoverageNoInfluence);
  EXPECT_EQ(b.local_shadow_references[0].coverage_state,
    oxygen::vortex::kShadowCoverageNoInfluence);
}
NOLINT_TEST_F(
  ShadowSharingTest, UnknownCasterContinuityRemainsIndependentAndRedraws)
{
  sources[1].geometry_content_revision = 0;
  Render(1);
  EXPECT_EQ(service->GetLastRenderState().rendered_point_shadow_count, 2U);
  EXPECT_TRUE(Record(0).surface_srv != Record(1).surface_srv
    || Record(0).first_array_layer != Record(1).first_array_layer);
  Render(2);
  EXPECT_EQ(service->GetLastRenderState().rendered_point_shadow_count, 1U);
}

NOLINT_TEST_F(
  ShadowSharingTest, FrameEndRejectsAlreadyAttachedButUnsubmittedFrameBindings)
{
  Render(1);
  auto recording = graphics_->AcquireCommandRecorder(
    graphics_->QueueKeyFor(oxygen::graphics::QueueRole::kGraphics),
    "Late frame-ring reader", oxygen::graphics::SubmissionPolicy::kExplicit);
  service->AttachLocalReads(inputs[0].view_id,
    oxygen::frame::SequenceNumber { 1 }, scenes[0].preparation_revision,
    *recording);
  service->CloseFramePublications();
  EXPECT_FALSE(recording.Submit());
  EXPECT_FALSE(graphics_->GetBackendLifetime()->IsFaulted());
}

} // namespace
