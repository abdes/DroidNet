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
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/DepthPrepass/DepthPrepassMeshProcessor.h>
#include <Oxygen/Vortex/Shadows/Internal/CascadeShadowSetup.h>
#include <Oxygen/Vortex/Shadows/Internal/ConventionalShadowTargetAllocator.h>
#include <Oxygen/Vortex/Shadows/Internal/PointShadowSetup.h>
#include <Oxygen/Vortex/Shadows/Internal/SpotShadowSetup.h>
#include <Oxygen/Vortex/Shadows/Passes/ShadowDepthPass.h>
#include <Oxygen/Vortex/Shadows/ShadowService.h>
#include <Oxygen/Vortex/Shadows/Types/DirectionalShadowFrameData.h>
#include <Oxygen/Vortex/Shadows/Types/FrameShadowInputs.h>
#include <Oxygen/Vortex/Shadows/Types/PointShadowBinding.h>
#include <Oxygen/Vortex/Shadows/Types/ShadowCascadeBinding.h>
#include <Oxygen/Vortex/Shadows/Types/SpotShadowBinding.h>
#include <Oxygen/Vortex/Test/Fakes/Graphics.h>
#include <Oxygen/Vortex/Test/Fixtures/MeshRasterStateTest.h>
#include <Oxygen/Vortex/Types/FrameLightSelection.h>
#include <Oxygen/Vortex/Types/PassMask.h>
#include <Oxygen/Vortex/Types/ShadowFrameBindings.h>

namespace {

using oxygen::Graphics;
using oxygen::kInvalidShaderVisibleIndex;
using oxygen::RendererConfig;
using oxygen::vortex::DirectionalShadowFrameData;
using oxygen::vortex::FrameDirectionalCsmSplitMode;
using oxygen::vortex::FrameDirectionalLightSelection;
using oxygen::vortex::FrameLightSelection;
using oxygen::vortex::FrameLocalLightSelection;
using oxygen::vortex::kDirectionalLightShadowFlagCastsShadows;
using oxygen::vortex::kLocalLightFlagCastsShadows;
using oxygen::vortex::Renderer;
using oxygen::vortex::RendererCapabilityFamily;
using oxygen::vortex::ShadowCascadeBinding;
using oxygen::vortex::ShadowFrameBindings;
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

auto MakePerspectiveResolvedView() -> oxygen::ResolvedView
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
  params.near_plane = 0.1F;
  params.far_plane = 100.0F;
  params.proj_matrix = oxygen::MakeReversedZPerspectiveProjectionRH_ZO(
    glm::pi<float>() / 3.0F, 1.0F, params.near_plane, params.far_plane);
  return oxygen::ResolvedView(params);
}

NOLINT_TEST(ShadowServiceSurfaceTest,
  ShaderStructuredBufferLayoutsMatchNativeShadowPublication)
{
  using oxygen::vortex::PointShadowBinding;
  using oxygen::vortex::SpotShadowBinding;
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
  const auto& spot = layouts.at("VortexSpotShadowBinding");
  EXPECT_EQ(spot.size, sizeof(SpotShadowBinding));
  EXPECT_EQ(spot.offsets.at("sampling_metadata1"),
    offsetof(SpotShadowBinding, sampling_metadata1));
  const auto& point = layouts.at("VortexPointShadowBinding");
  EXPECT_EQ(point.size, sizeof(PointShadowBinding));
  EXPECT_EQ(point.offsets.at("position_and_inv_range"),
    offsetof(PointShadowBinding, position_and_inv_range));
  const auto& frame = layouts.at("VortexShadowFrameBindings");
  EXPECT_EQ(frame.size, sizeof(ShadowFrameBindings));
  const auto expected_offsets = std::array {
    std::pair {
      "conventional_shadow_surface_handle",
      offsetof(ShadowFrameBindings, conventional_shadow_surface_handle),
    },
    std::pair {
      "cascade_count",
      offsetof(ShadowFrameBindings, cascade_count),
    },
    std::pair {
      "technique_flags",
      offsetof(ShadowFrameBindings, technique_flags),
    },
    std::pair {
      "sampling_contract_flags",
      offsetof(ShadowFrameBindings, sampling_contract_flags),
    },
    std::pair {
      "light_direction_to_source",
      offsetof(ShadowFrameBindings, light_direction_to_source),
    },
    std::pair {
      "spot_shadow_surface_handle",
      offsetof(ShadowFrameBindings, spot_shadow_surface_handle),
    },
    std::pair {
      "spot_shadow_count",
      offsetof(ShadowFrameBindings, spot_shadow_count),
    },
    std::pair {
      "cascades",
      offsetof(ShadowFrameBindings, cascades),
    },
    std::pair {
      "spot_shadows",
      offsetof(ShadowFrameBindings, spot_shadows),
    },
    std::pair {
      "point_shadow_surface_handle",
      offsetof(ShadowFrameBindings, point_shadow_surface_handle),
    },
    std::pair {
      "point_shadow_count",
      offsetof(ShadowFrameBindings, point_shadow_count),
    },
    std::pair {
      "point_shadows",
      offsetof(ShadowFrameBindings, point_shadows),
    },
  };
  for (const auto& [name, offset] : expected_offsets) {
    EXPECT_EQ(frame.offsets.at(name), offset) << name;
  }
}

NOLINT_TEST(ShadowServiceSurfaceTest,
  ShadowFrameBindingsExposeDirectionalConventionalShadowContract)
{
  auto bindings = ShadowFrameBindings {};

  EXPECT_EQ(
    bindings.conventional_shadow_surface_handle, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.cascade_count, 0U);
  EXPECT_EQ(bindings.technique_flags, 0U);
  EXPECT_EQ(bindings.sampling_contract_flags, 0U);
  EXPECT_EQ(bindings.spot_shadow_surface_handle, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.spot_shadow_count, 0U);
  EXPECT_EQ(bindings.point_shadow_surface_handle, kInvalidShaderVisibleIndex);
  EXPECT_EQ(bindings.point_shadow_count, 0U);
  EXPECT_EQ(bindings.cascades.size(), ShadowFrameBindings::kMaxCascades);
  EXPECT_EQ(bindings.spot_shadows.size(), ShadowFrameBindings::kMaxSpotShadows);
  EXPECT_EQ(
    bindings.point_shadows.size(), ShadowFrameBindings::kMaxPointShadows);
}

NOLINT_TEST(ShadowServiceSurfaceTest,
  DirectionalShadowFrameDataStartsWithEmptyLocalLightShadowPayload)
{
  auto frame_data = DirectionalShadowFrameData {};
  frame_data.bindings.cascade_count = 2U;

  EXPECT_EQ(frame_data.backing_resolution.x, 0U);
  EXPECT_EQ(frame_data.backing_resolution.y, 0U);
  EXPECT_EQ(frame_data.storage_flags, 0U);
  EXPECT_EQ(frame_data.bindings.cascade_count, 2U);
  EXPECT_EQ(frame_data.bindings.spot_shadow_count, 0U);
  EXPECT_EQ(frame_data.bindings.point_shadow_count, 0U);
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

  const auto bindings = PointShadowSetup {}.BuildPointFrameBindings(
    view_input, std::span(local_lights), allocation);

  EXPECT_TRUE(bindings.HasPointConventionalShadow());
  EXPECT_EQ(bindings.point_shadow_surface_handle,
    (oxygen::ShaderVisibleIndex {
      13U,
    }));
  EXPECT_EQ(bindings.point_shadow_count, 1U);
  EXPECT_FLOAT_EQ(
    bindings.point_shadows.at(0).position_and_inv_range.w, 1.0F / 10.0F);
  EXPECT_FLOAT_EQ(bindings.point_shadows.at(0).sampling_metadata0.x, 0.0F);
  EXPECT_GT(bindings.point_shadows.at(0).sampling_metadata0.w, 0.0F);
  EXPECT_FLOAT_EQ(bindings.point_shadows.at(0).sampling_metadata1.x, 0.04F);
}

NOLINT_TEST(ShadowServiceSurfaceTest,
  FrameLightSelectionCarriesSharedDirectionalAuthorityForShadowService)
{
  auto selection = FrameLightSelection {};
  selection.selection_epoch = 19U;
  selection.directional_light = FrameDirectionalLightSelection {
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
  };

  if (!selection.directional_light.has_value()) {

    FAIL() << "Expected selection.directional_light to have a value";
  }
  EXPECT_EQ(selection.directional_light->cascade_count, 4U);
  EXPECT_NE(selection.directional_light->shadow_flags
      & kDirectionalLightShadowFlagCastsShadows,
    0U);
  EXPECT_EQ(selection.directional_light->cascade_split_mode,
    FrameDirectionalCsmSplitMode::kManualDistances);
  EXPECT_FLOAT_EQ(selection.directional_light->cascade_distances.at(3), 128.0F);
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
    graphics_->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());
    renderer_ = MakeRenderer(graphics_);
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
    auto draws = std::array<oxygen::vortex::DrawCommand, 5> {};
    for (std::size_t index = 0U; index < draws.size(); ++index) {
      metadata.at(index).flags.Set(oxygen::vortex::PassMaskBit::kShadowCaster);
      draws.at(index).draw_index = static_cast<std::uint32_t>(index);
      draws.at(index).index_count = metadata.at(index).vertex_count;
      draws.at(index).instance_count = 1U;
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
    const auto result = pass.RecordSlices(input, texture, slices, draws);
    ASSERT_EQ(result.rendered_cascade_count, 2U);
    ASSERT_EQ(result.rendered_draw_count, 10U);
    ASSERT_EQ(graphics_->draw_log_.draws.size(), 10U);
    for (std::size_t slice = 0U; slice < slices.size(); ++slice) {
      oxygen::vortex::testing::ExpectRasterStateDraws(
        std::span(graphics_->draw_log_.draws)
          .subspan(slice * draws.size(), draws.size()),
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

  const auto bindings = SpotShadowSetup {}.BuildSpotFrameBindings(
    view_input, std::span(local_lights), allocation);

  EXPECT_TRUE(bindings.HasSpotConventionalShadow());
  EXPECT_EQ(bindings.spot_shadow_surface_handle,
    (oxygen::ShaderVisibleIndex {
      9U,
    }));
  EXPECT_EQ(bindings.spot_shadow_count, 1U);
  EXPECT_FLOAT_EQ(
    bindings.spot_shadows.at(0).position_and_inv_range.w, 1.0F / 12.0F);
  EXPECT_GT(bindings.spot_shadows.at(0).direction_and_bias.w, 0.0F);
  EXPECT_FLOAT_EQ(bindings.spot_shadows.at(0).sampling_metadata1.w, 0.03F);
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
      .direction = direction,
      .outer_cone_half_angle_radians = 0.68F,
      .flags = kLocalLightFlagCastsShadows,
      .shadow_bias = 0.1F,
      .shadow_normal_bias = 0.02F,
    },
  };

  const auto bindings = SpotShadowSetup {}.BuildSpotFrameBindings(
    view_input, std::span(local_lights), allocation);

  ASSERT_EQ(bindings.spot_shadow_count, 1U);
  const auto& spot = bindings.spot_shadows.at(0);
  const auto caster_center = glm::vec3 {
    0.0F,
    0.0F,
    0.5F,
  };
  const auto axial_distance
    = glm::dot(caster_center - glm::vec3(spot.position_and_inv_range),
      glm::normalize(glm::vec3(spot.direction_and_bias)));
  const auto unbiased_linear_depth
    = 1.0F - (axial_distance * spot.position_and_inv_range.w);
  const auto max_depth_bias
    = spot.direction_and_bias.w + spot.sampling_metadata1.z;

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
  const auto directional_light = FrameDirectionalLightSelection {
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

  ASSERT_EQ(frame_data.bindings.cascade_count, 3U);
  EXPECT_FLOAT_EQ(frame_data.bindings.cascades.at(0).split_near, 0.1F);
  EXPECT_NEAR(frame_data.bindings.cascades.at(0).split_far, 12.475F, 0.0001F);
  EXPECT_FLOAT_EQ(frame_data.bindings.cascades.at(1).split_near, 10.0F);
  EXPECT_NEAR(frame_data.bindings.cascades.at(1).split_far, 22.5F, 0.0001F);
  EXPECT_FLOAT_EQ(frame_data.bindings.cascades.at(2).split_near, 20.0F);
  EXPECT_FLOAT_EQ(frame_data.bindings.cascades.at(2).split_far, 40.0F);
  EXPECT_NEAR(
    frame_data.bindings.cascades.at(0).transition_width, 2.475F, 0.0001F);
  EXPECT_NEAR(
    frame_data.bindings.cascades.at(1).transition_width, 2.5F, 0.0001F);
  EXPECT_FLOAT_EQ(frame_data.bindings.cascades.at(2).transition_width, 0.0F);
}

} // namespace
