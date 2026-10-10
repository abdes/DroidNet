//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <vector>

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/trigonometric.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Data/GeometryIndices.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Scene/Types/NodeHandle.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Hzb/HzbPyramidBuilder.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Hzb/HzbPyramidTexture.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Hzb/ScreenHzbModule.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/Internal/CullingViewHistory.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/Internal/DrawCullPass.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/Internal/IndirectListBuilder.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/OcclusionConfig.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/OcclusionModule.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/Types/DrawVisibility.h>
#include <Oxygen/Vortex/Test/Occlusion/OcclusionGpuTest.h>
#include <Oxygen/Vortex/Types/DrawCullRecord.h>
#include <Oxygen/Vortex/Types/DrawMetadata.h>
#include <Oxygen/Vortex/Upload/TransientStructuredBuffer.h>

namespace {

using oxygen::ResolvedView;
using oxygen::ViewId;
using oxygen::graphics::CommandRecorder;
using oxygen::graphics::ResourceStates;
using oxygen::graphics::Texture;
using oxygen::vortex::DrawCullFlagBits;
using oxygen::vortex::DrawCullRecord;
using oxygen::vortex::DrawMetadata;
using oxygen::vortex::DrawVisibilityBit;
using oxygen::vortex::DrawVisibilityPredicate;
using oxygen::vortex::DrawVisibilityProducts;
using oxygen::vortex::HzbPyramidBuilder;
using oxygen::vortex::HzbPyramidTexture;
using oxygen::vortex::OcclusionConfig;
using oxygen::vortex::OcclusionModule;
using oxygen::vortex::PreparedSceneFrame;
using oxygen::vortex::SceneTextures;
using oxygen::vortex::SceneTexturesConfig;
using oxygen::vortex::ScreenHzbModule;
using oxygen::vortex::ToUnderlying;
using oxygen::vortex::occlusion::internal::CullingViewHistory;
using oxygen::vortex::occlusion::internal::DrawCullDepth;
using oxygen::vortex::occlusion::internal::DrawCullHistory;
using oxygen::vortex::occlusion::internal::DrawCullInputs;
using oxygen::vortex::occlusion::internal::DrawCullPass;
using oxygen::vortex::occlusion::internal::IndirectDrawCandidate;
using oxygen::vortex::occlusion::internal::IndirectDrawCommand;
using oxygen::vortex::occlusion::internal::IndirectDrawList;
using oxygen::vortex::occlusion::internal::IndirectListBuilder;
using oxygen::vortex::occlusion::internal::OcclusionPyramidBinding;

constexpr std::uint32_t kExtent = 64U;
constexpr auto kCommandUints
  = sizeof(IndirectDrawCommand) / sizeof(std::uint32_t);
constexpr float kNear = 0.1F;

constexpr auto kInFrustum = ToUnderlying(DrawVisibilityBit::kInFrustum);
constexpr auto kPhase1 = ToUnderlying(DrawVisibilityBit::kPhase1Drawn);
constexpr auto kPhase2 = ToUnderlying(DrawVisibilityBit::kPhase2Drawn);
constexpr auto kVisible = ToUnderlying(DrawVisibilityBit::kVisible);
constexpr auto kDrawnInPhase1 = kInFrustum | kPhase1 | kVisible;
constexpr auto kDrawnInPhase2 = kInFrustum | kPhase2 | kVisible;
//! Drawn in phase 1 from history, then found occluded.
constexpr auto kDrawnButOccluded = kInFrustum | kPhase1;
constexpr auto kOccluded = kInFrustum;

//! The view-space x (or y) over distance of pixel column (or row) `pixel`
//! for the 90-degree test projection.
constexpr auto Slope(const float pixel) -> float
{
  return (pixel / (static_cast<float>(kExtent) * 0.5F)) - 1.0F;
}

//! A view-space box from distance `front` to `back` whose front face
//! projects to the pixel rect (x0, x1, y0, y1).
auto PixelBox(const glm::vec4& rect, const float front, const float back)
  -> DrawCullRecord
{
  // Pixel y grows down; view y grows up.
  const auto min
    = glm::vec3 { Slope(rect.x) * front, -Slope(rect.w) * front, -back };
  const auto max
    = glm::vec3 { Slope(rect.y) * front, -Slope(rect.z) * front, -front };
  return DrawCullRecord {
    .box_center = (min + max) * 0.5F,
    .box_extent = (max - min) * 0.5F,
  };
}

//! A draw identity: node, LOD and mesh view.
auto Source(const std::uint32_t node, const std::uint32_t lod = 0U,
  const std::uint32_t mesh_view = 0U) -> PreparedSceneFrame::DrawSource
{
  return PreparedSceneFrame::DrawSource {
    .node = oxygen::scene::NodeHandle { node, 1U },
    .lod_index = oxygen::data::LodIndex { lod },
    .submesh_index = oxygen::data::SubmeshIndex {},
    .mesh_view_index = oxygen::data::MeshViewIndex { mesh_view },
  };
}

//! One draw of a test scene.
struct TestDraw {
  DrawCullRecord record;
  PreparedSceneFrame::DrawSource source;
  //! Index into the scene's world matrices.
  std::uint32_t transform { 0U };
};

//! A pixel rect of phase 1 depth at a view distance.
struct DepthRect {
  glm::vec4 rect { 0.0F };
  float distance { 1.0F };
};

//! The test camera's infinite reversed-Z depth of a view distance.
auto CameraDepth(const float distance) -> float { return kNear / distance; }

//! The test cascade spans view distances 0 to 10, stored as 1 to 0.
constexpr float kCascadeDepthRange = 10.0F;

auto CascadeDepth(const float distance) -> float
{
  return 1.0F - (distance / kCascadeDepthRange);
}

//! The test spot light stores 1 - distance / range.
constexpr float kSpotInverseRange = 1.0F / 20.0F;

auto SpotDepth(const float distance) -> float
{
  return 1.0F - (distance * kSpotInverseRange);
}

class TwoPhaseOcclusionGpuTest
  : public oxygen::vortex::testing::occlusion::OcclusionGpuTest {
protected:
  //! Infinite reversed-Z perspective with a 90-degree field of view:
  //! clip = (x, y, near, -z).
  static auto Projection() -> glm::mat4
  {
    return glm::mat4 {
      glm::vec4 { 1.0F, 0.0F, 0.0F, 0.0F },
      glm::vec4 { 0.0F, 1.0F, 0.0F, 0.0F },
      glm::vec4 { 0.0F, 0.0F, 0.0F, -1.0F },
      glm::vec4 { 0.0F, 0.0F, kNear, 0.0F },
    };
  }

  auto SetUp() -> void override
  {
    OcclusionGpuTest::SetUp();
    auto params = ResolvedView::Params {};
    params.view_config.viewport = {
      .width = static_cast<float>(kExtent),
      .height = static_cast<float>(kExtent),
    };
    params.view_config.scissor = {
      .right = static_cast<std::int32_t>(kExtent),
      .bottom = static_cast<std::int32_t>(kExtent),
    };
    params.proj_matrix = Projection();
    params.stable_proj_matrix = Projection();
    params.near_plane = kNear;
    view_ = std::make_unique<ResolvedView>(params);
    scene_textures_ = std::make_unique<SceneTextures>(Backend(),
      SceneTexturesConfig {
        .extent = { kExtent, kExtent },
      });
    hzb_
      = std::make_unique<ScreenHzbModule>(*renderer_, SceneTexturesConfig {});
    cull_ = std::make_unique<DrawCullPass>(*renderer_, "Culling view test");
    pyramid_builder_ = std::make_unique<HzbPyramidBuilder>(
      *renderer_, "Culling view test pyramid");
    pyramid_ = std::make_unique<HzbPyramidTexture>("Culling view test pyramid");
    slot_words_.emplace(oxygen::observer_ptr { renderer_->GetGraphics().get() },
      renderer_->GetStagingProvider(),
      static_cast<std::uint32_t>(sizeof(std::uint32_t)),
      oxygen::observer_ptr { &renderer_->GetInlineTransfersCoordinator() },
      "Culling view test slots");
  }

  auto TearDown() -> void override
  {
    slot_words_.reset();
    pyramid_.reset();
    pyramid_builder_.reset();
    cull_.reset();
    hzb_.reset();
    scene_textures_.reset();
    OcclusionGpuTest::TearDown();
  }

  //! Phase 1 depth: `rects` at their distances, encoded by `encode`, and
  //! nothing elsewhere.
  auto MakeDepth(const std::span<const DepthRect> rects,
    float (*encode)(float distance) = &CameraDepth) -> std::shared_ptr<Texture>
  {
    auto texels
      = std::vector<float>(static_cast<std::size_t>(kExtent) * kExtent, 0.0F);
    for (const auto& [rect, distance] : rects) {
      for (auto y = static_cast<std::uint32_t>(rect.z);
        y < static_cast<std::uint32_t>(rect.w); ++y) {
        for (auto x = static_cast<std::uint32_t>(rect.x);
          x < static_cast<std::uint32_t>(rect.y); ++x) {
          texels.at((static_cast<std::size_t>(y) * kExtent) + x)
            = encode(distance);
        }
      }
    }
    auto desc = oxygen::graphics::TextureDesc {};
    desc.width = kExtent;
    desc.height = kExtent;
    desc.format = oxygen::Format::kR32Float;
    desc.texture_type = oxygen::TextureType::kTexture2D;
    desc.debug_name = "Occlusion test depth";
    desc.is_shader_resource = true;
    desc.initial_state = ResourceStates::kCommon;
    auto texture = CreateRegisteredTexture(desc);
    WriteTexels(*texture, std::as_bytes(std::span(texels)), sizeof(float),
      ResourceStates::kShaderResource);
    return texture;
  }

  //! Uploads `draws` as this frame's prepared scene.
  auto PrepareScene(const std::span<const TestDraw> draws,
    const std::span<const glm::mat4> worlds) -> void
  {
    metadata_.assign(draws.size(), DrawMetadata {});
    records_.clear();
    sources_.clear();
    for (const auto& [metadata, draw] : std::views::zip(metadata_, draws)) {
      metadata.transform_index = draw.transform;
      metadata.instance_count = 1U;
      metadata.vertex_count = 3U;
      records_.push_back(draw.record);
      sources_.push_back(draw.source);
    }
    const auto [metadata_buffer, metadata_srv] = MakeUploadBuffer(
      std::span<const DrawMetadata>(metadata_), "Occlusion test metadata");
    const auto [records_buffer, records_srv] = MakeUploadBuffer(
      std::span<const DrawCullRecord>(records_), "Occlusion test records");
    const auto [worlds_buffer, worlds_srv]
      = MakeUploadBuffer(worlds, "Occlusion test worlds");
    scene_buffers_ = { metadata_buffer, records_buffer, worlds_buffer };

    frame_ = PreparedSceneFrame {};
    frame_.draw_metadata_bytes = std::as_bytes(std::span(metadata_));
    frame_.draw_sources = sources_;
    frame_.bindless_draw_metadata_slot = metadata_srv;
    frame_.bindless_draw_cull_records_slot = records_srv;
    frame_.bindless_worlds_slot = worlds_srv;
  }

  auto PrepareScene(const std::span<const TestDraw> draws) -> void
  {
    const auto identity = std::array { glm::mat4 { 1.0F } };
    PrepareScene(draws, identity);
  }

  //! Renders one frame of `view_id` with `depth` as its phase 1 depth and
  //! returns each draw's visibility bits.
  auto RunFrame(OcclusionModule& module, const ViewId view_id,
    const Texture& depth, const bool camera_cut = false)
    -> std::vector<std::uint32_t>
  {
    NextFrame();
    ctx_.current_view.view_id = view_id;
    ctx_.current_view.prepared_frame
      = oxygen::observer_ptr<const PreparedSceneFrame> { &frame_ };
    ctx_.current_view.resolved_view
      = oxygen::observer_ptr<const ResolvedView> { view_.get() };
    ctx_.current_view.history_discontinuity = camera_cut;
    SubmitCommands(
      "Occlusion test frame", [&](CommandRecorder& recorder) -> void {
        module.BuildPhase1(ctx_, recorder, *scene_textures_);
        ASSERT_TRUE(module.NeedsPhase2(ctx_));
        const auto pyramid = hzb_->BuildOcclusionPyramid(ctx_, recorder,
          HzbPyramidBuilder::Source {
            .depth = oxygen::observer_ptr { &depth },
            .width = kExtent,
            .height = kExtent,
          });
        ASSERT_TRUE(pyramid.has_value());
        module.BuildPhase2(ctx_, recorder, pyramid);
      });
    WaitForQueueIdle();
    const auto& visibility = ctx_.current_view.draw_visibility;
    EXPECT_TRUE(visibility.IsValid());
    EXPECT_TRUE(visibility.phase2);
    if (!visibility.IsValid()) {
      return {};
    }
    return ReadUints(*visibility.buffer, 0U, metadata_.size());
  }

  static auto Enabled() -> OcclusionConfig
  {
    return OcclusionConfig { .enabled = true };
  }

  //! Culls a shadow-like view in both phases, as the shadow depth pass does,
  //! with `depth` as its phase 1 depth.
  auto RunCullingView(CullingViewHistory& history,
    const glm::mat4& view_projection, const DrawCullDepth& depth_encoding,
    const Texture& depth) -> std::vector<std::uint32_t>
  {
    NextFrame();
    auto inputs = DrawCullInputs {
      .frame_sequence = ctx_.frame_sequence,
      .frame_slot = ctx_.frame_slot,
      .prepared_frame
      = oxygen::observer_ptr<const PreparedSceneFrame> { &frame_ },
      .view_projection = view_projection,
      .depth = depth_encoding,
      .viewport = view_->Viewport(),
      .scissors = view_->Scissor(),
      .occlusion_enabled = true,
      .depth_bias = oxygen::vortex::kDefaultOcclusionDepthBias,
    };
    auto visibility = DrawVisibilityProducts {};
    SubmitCommands(
      "Culling view frame", [&](CommandRecorder& recorder) -> void {
        slot_words_->OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
        const auto binding
          = history.Prepare(recorder, renderer_->GetGraphics(), *slot_words_,
            sources_, static_cast<std::uint32_t>(sources_.size()), false);
        ASSERT_TRUE(binding.has_value());
        inputs.history = binding.value_or(DrawCullHistory {});
        const auto phase1 = cull_->RunPhase1(recorder, inputs);
        ASSERT_TRUE(
          pyramid_->Ensure(renderer_->GetGraphics(), kExtent, kExtent));
        ASSERT_TRUE(pyramid_builder_->Build(
          HzbPyramidBuilder::BuildFrame {
            .sequence = ctx_.frame_sequence,
            .slot = ctx_.frame_slot,
            .view_id = ViewId { 77U },
          },
          recorder,
          HzbPyramidBuilder::Source {
            .depth = oxygen::observer_ptr { &depth },
            .array_slice = 0U,
            .origin_x = 0U,
            .origin_y = 0U,
            .width = kExtent,
            .height = kExtent,
          },
          HzbPyramidBuilder::Targets {
            .closest = nullptr,
            .furthest = oxygen::observer_ptr { pyramid_->GetTexture().get() },
          }));
        visibility = cull_->RunPhase2(recorder, inputs, phase1,
        OcclusionPyramidBinding {
          .texture = oxygen::observer_ptr<const Texture> {
            pyramid_->GetTexture().get(),
          },
          .srv = pyramid_->GetSrv(),
          .origin_x = 0U,
          .origin_y = 0U,
          .width = kExtent,
          .height = kExtent,
        });
        history.SetWritten(visibility.phase2);
      });
    WaitForQueueIdle();
    EXPECT_TRUE(visibility.phase2);
    if (!visibility.IsValid()) {
      return {};
    }
    return ReadUints(*visibility.buffer, 0U, metadata_.size());
  }

  std::unique_ptr<ResolvedView> view_;
  std::unique_ptr<SceneTextures> scene_textures_;
  std::unique_ptr<ScreenHzbModule> hzb_;
  std::unique_ptr<DrawCullPass> cull_;
  std::unique_ptr<HzbPyramidBuilder> pyramid_builder_;
  std::unique_ptr<HzbPyramidTexture> pyramid_;
  std::optional<oxygen::vortex::upload::TransientStructuredBuffer> slot_words_;
  std::vector<DrawMetadata> metadata_;
  std::vector<DrawCullRecord> records_;
  std::vector<PreparedSceneFrame::DrawSource> sources_;
  std::array<std::shared_ptr<oxygen::graphics::Buffer>, 3> scene_buffers_;
  PreparedSceneFrame frame_;
};

// An occluder covering x in [8, 56) at distance 2, and nothing elsewhere.
constexpr auto kWall = DepthRect {
  .rect = { 8.0F, 56.0F, 0.0F, 64.0F },
  .distance = 2.0F,
};
auto BehindWall() -> DrawCullRecord
{
  return PixelBox({ 20.0F, 40.0F, 20.0F, 40.0F }, 10.0F, 11.0F);
}

auto BesideWall() -> DrawCullRecord
{
  return PixelBox({ 57.0F, 63.0F, 20.0F, 40.0F }, 10.0F, 11.0F);
}

//! A draw beside the wall that seeds a view's history before the draws under
//! test arrive without history.
auto Anchor() -> TestDraw
{
  return TestDraw { .record = BesideWall(), .source = Source(99U) };
}

//! A box whose history says hidden is culled in the frame it is tested; a
//! box drawn last frame is drawn in phase 1 once more, and leaves the next.
NOLINT_TEST_F(TwoPhaseOcclusionGpuTest, OccludedBoxesLeaveTheDrawnSet)
{
  auto module = OcclusionModule(*renderer_, Enabled());
  const auto view = ViewId { 1U };
  const auto open = MakeDepth({});
  const auto walled = MakeDepth(std::array { kWall });
  const auto box = TestDraw { .record = BehindWall(), .source = Source(1U) };

  // The first frame of a view draws everything in phase 1.
  PrepareScene(std::array { box });
  EXPECT_EQ(RunFrame(module, view, *open), std::vector { kDrawnInPhase1 });
  EXPECT_EQ(RunFrame(module, view, *open), std::vector { kDrawnInPhase1 });
  EXPECT_EQ(RunFrame(module, view, *walled), std::vector { kDrawnButOccluded });
  EXPECT_EQ(RunFrame(module, view, *walled), std::vector { kOccluded });

  // A new box behind the wall has no history and is culled at once.
  const auto newcomer
    = TestDraw { .record = BehindWall(), .source = Source(2U) };
  PrepareScene(std::array { box, newcomer });
  EXPECT_EQ(
    RunFrame(module, view, *walled), (std::vector { kOccluded, kOccluded }));
}

//! When the occluder moves away, the box it hid is drawn in phase 2 of the
//! same frame.
NOLINT_TEST_F(TwoPhaseOcclusionGpuTest, RemovedOccluderRevealsInTheSameFrame)
{
  auto module = OcclusionModule(*renderer_, Enabled());
  const auto view = ViewId { 1U };
  const auto walled = MakeDepth(std::array { kWall });
  const auto open = MakeDepth({});
  PrepareScene(
    std::array { TestDraw { .record = BehindWall(), .source = Source(1U) } });

  (void)RunFrame(module, view, *walled);
  EXPECT_EQ(RunFrame(module, view, *walled), std::vector { kOccluded });
  EXPECT_EQ(RunFrame(module, view, *open), std::vector { kDrawnInPhase2 });
  EXPECT_EQ(RunFrame(module, view, *open), std::vector { kDrawnInPhase1 });
}

//! Boxes the wall does not hide stay drawn: beside it, through a hole in a
//! masked wall, and crossing the near plane.
NOLINT_TEST_F(TwoPhaseOcclusionGpuTest, UnhiddenBoxesAreNeverCulled)
{
  auto module = OcclusionModule(*renderer_, Enabled());
  const auto view = ViewId { 1U };
  // A masked wall: its alpha test left a hole at [24, 32) x [44, 52).
  const auto masked = MakeDepth(std::array {
    DepthRect { .rect = { 8.0F, 56.0F, 0.0F, 44.0F }, .distance = 2.0F },
    DepthRect { .rect = { 8.0F, 24.0F, 44.0F, 52.0F }, .distance = 2.0F },
    DepthRect { .rect = { 32.0F, 56.0F, 44.0F, 52.0F }, .distance = 2.0F },
    DepthRect { .rect = { 8.0F, 56.0F, 52.0F, 64.0F }, .distance = 2.0F },
  });
  auto straddler = PixelBox({ 28.0F, 36.0F, 28.0F, 36.0F }, 1.0F, 20.0F);
  straddler.box_center.z = 0.0F;
  const auto draws = std::array {
    TestDraw { .record = BesideWall(), .source = Source(1U) },
    TestDraw {
      .record = PixelBox({ 26.0F, 30.0F, 46.0F, 50.0F }, 10.0F, 11.0F),
      .source = Source(2U),
    },
    TestDraw { .record = straddler, .source = Source(3U) },
  };
  PrepareScene(std::array { draws.front() });
  (void)RunFrame(module, view, *masked);

  // The new boxes have no history, so phase 2 tests them.
  PrepareScene(draws);
  EXPECT_EQ(RunFrame(module, view, *masked),
    (std::vector { kDrawnInPhase1, kDrawnInPhase2, kDrawnInPhase2 }));
}

//! A thin plank leaning into the depth behind the wall's edge: its oriented
//! box stays behind the wall, while its world AABB reaches past the edge.
NOLINT_TEST_F(TwoPhaseOcclusionGpuTest, OrientedBoxesCullTighterThanAabbs)
{
  auto module = OcclusionModule(*renderer_, Enabled());
  const auto view = ViewId { 1U };
  const auto walled = MakeDepth(std::array { kWall });
  const auto plank = DrawCullRecord {
    .box_extent = { 4.0F, 0.2F, 0.2F },
  };
  // The plank's near end points at the view axis, its far end away from it.
  const auto lean = glm::rotate(
    glm::mat4 { 1.0F }, glm::radians(80.0F), glm::vec3 { 0.0F, 1.0F, 0.0F });
  const auto worlds = std::array {
    glm::mat4 { 1.0F },
    glm::translate(glm::mat4 { 1.0F }, glm::vec3 { 4.0F, 0.0F, -10.0F }) * lean,
    glm::translate(glm::mat4 { 1.0F }, glm::vec3 { 6.5F, 0.0F, -10.0F }) * lean,
  };
  // The world AABB of the plank at transform 1.
  const auto aabb = DrawCullRecord {
    .box_center = { 4.0F, 0.0F, -10.0F },
    .box_extent = { 0.89F, 0.2F, 3.97F },
    .flags = ToUnderlying(DrawCullFlagBits::kWorldSpaceBox),
  };
  const auto draws = std::array {
    Anchor(),
    TestDraw { .record = plank, .source = Source(1U), .transform = 1U },
    TestDraw { .record = plank, .source = Source(2U), .transform = 2U },
    TestDraw { .record = aabb, .source = Source(3U) },
  };
  PrepareScene(std::span(draws).first(1U), worlds);
  (void)RunFrame(module, view, *walled);

  PrepareScene(draws, worlds);
  EXPECT_EQ(RunFrame(module, view, *walled),
    (std::vector {
      kDrawnInPhase1, kOccluded, kDrawnInPhase2, kDrawnInPhase2 }));
}

//! A LOD switch starts a new history, and a camera cut resets all of it;
//! neither drops a visible draw.
NOLINT_TEST_F(TwoPhaseOcclusionGpuTest, LodSwitchAndCameraCutKeepVisibleDraws)
{
  auto module = OcclusionModule(*renderer_, Enabled());
  const auto view = ViewId { 1U };
  const auto walled = MakeDepth(std::array { kWall });
  const auto lod0
    = TestDraw { .record = BesideWall(), .source = Source(1U, 0U) };
  const auto lod1
    = TestDraw { .record = BesideWall(), .source = Source(1U, 1U) };
  const auto hidden = TestDraw { .record = BehindWall(), .source = Source(2U) };

  PrepareScene(std::array { lod0, hidden });
  (void)RunFrame(module, view, *walled);
  EXPECT_EQ(RunFrame(module, view, *walled),
    (std::vector { kDrawnInPhase1, kOccluded }));

  PrepareScene(std::array { lod1, hidden });
  EXPECT_EQ(RunFrame(module, view, *walled),
    (std::vector { kDrawnInPhase2, kOccluded }));

  EXPECT_EQ(RunFrame(module, view, *walled, true),
    (std::vector { kDrawnInPhase1, kDrawnButOccluded }));
}

//! A large mesh split into mesh views, half behind the wall, draws only its
//! unoccluded views.
NOLINT_TEST_F(TwoPhaseOcclusionGpuTest, ChunkedMeshDrawsOnlyUnoccludedViews)
{
  auto module = OcclusionModule(*renderer_, Enabled());
  const auto view = ViewId { 1U };
  // A wall over the left half only.
  const auto half_wall = MakeDepth(std::array {
    DepthRect { .rect = { 0.0F, 33.0F, 0.0F, 64.0F }, .distance = 2.0F },
  });
  auto chunks = std::vector { Anchor() };
  for (std::uint32_t chunk = 0U; chunk < 4U; ++chunk) {
    const auto x0 = 10.0F + (12.0F * static_cast<float>(chunk));
    chunks.push_back(TestDraw {
      .record = PixelBox({ x0, x0 + 10.0F, 20.0F, 40.0F }, 10.0F, 11.0F),
      .source = Source(1U, 0U, chunk),
    });
  }
  PrepareScene(std::span(chunks).first(1U));
  (void)RunFrame(module, view, *half_wall);

  PrepareScene(chunks);
  EXPECT_EQ(RunFrame(module, view, *half_wall),
    (std::vector {
      kDrawnInPhase1, kOccluded, kOccluded, kDrawnInPhase2, kDrawnInPhase2 }));

  // The counters of the frames read back so far reach diagnostics.
  (void)RunFrame(module, view, *half_wall);
  const auto stats = module.GetStats(view);
  if (!stats.has_value()) {
    FAIL() << "Expected read-back counters";
  }
  EXPECT_TRUE(stats.value().occlusion_enabled);
  EXPECT_EQ(stats.value().counters.draw_count, 5U);
  EXPECT_EQ(stats.value().counters.in_frustum_count, 5U);
  EXPECT_EQ(stats.value().counters.phase1_drawn_count, 1U);
  EXPECT_EQ(stats.value().counters.phase2_drawn_count, 2U);
  EXPECT_EQ(stats.value().counters.occluded_count, 2U);
}

//! Two views with different occluders keep independent visibility.
NOLINT_TEST_F(TwoPhaseOcclusionGpuTest, ViewsKeepIndependentVisibility)
{
  auto module = OcclusionModule(*renderer_, Enabled());
  const auto pane = ViewId { 1U };
  const auto preview = ViewId { 2U };
  const auto walled = MakeDepth(std::array { kWall });
  const auto open = MakeDepth({});
  PrepareScene(
    std::array { TestDraw { .record = BehindWall(), .source = Source(1U) } });

  for (std::uint32_t frame = 0U; frame < 2U; ++frame) {
    (void)RunFrame(module, pane, *walled);
    (void)RunFrame(module, preview, *open);
  }
  EXPECT_EQ(RunFrame(module, pane, *walled), std::vector { kOccluded });
  EXPECT_EQ(RunFrame(module, preview, *open), std::vector { kDrawnInPhase1 });

  module.RemoveViewState(pane);
  EXPECT_EQ(RunFrame(module, pane, *walled), std::vector { kDrawnButOccluded });
}

//! A list with the drawn predicate holds exactly the final set, in order.
NOLINT_TEST_F(TwoPhaseOcclusionGpuTest, DrawnListsHoldExactlyTheFinalSet)
{
  auto module = OcclusionModule(*renderer_, Enabled());
  const auto view = ViewId { 1U };
  const auto walled = MakeDepth(std::array { kWall });
  auto draws = std::vector<TestDraw> {};
  for (std::uint32_t node = 0U; node < 8U; ++node) {
    draws.push_back(TestDraw {
      .record = node % 2U == 0U ? BehindWall() : BesideWall(),
      .source = Source(node),
    });
  }
  PrepareScene(std::span(draws).first(4U));
  (void)RunFrame(module, view, *walled);
  PrepareScene(draws);
  const auto bits = RunFrame(module, view, *walled);

  auto builder = IndirectListBuilder(*renderer_, "Occlusion test list");
  auto candidates = std::vector<IndirectDrawCandidate> {};
  auto expected = std::vector<std::uint32_t> {};
  for (std::uint32_t draw = 0U; draw < draws.size(); ++draw) {
    candidates.push_back(IndirectDrawCandidate {
      .draw_index = draw,
      .vertex_count = 3U,
    });
    if ((bits.at(draw) & (kPhase1 | kPhase2)) != 0U) {
      expected.insert(expected.end(), { 3U, 1U, 0U, draw });
    }
  }
  const auto visibility = ctx_.current_view.draw_visibility;
  const auto list = SubmitCommands(
    "Occlusion test list", [&](CommandRecorder& recorder) -> IndirectDrawList {
      return builder.Build(recorder, ctx_.frame_sequence, ctx_.frame_slot,
        candidates, visibility, DrawVisibilityPredicate::Drawn());
    });
  WaitForQueueIdle();
  ASSERT_EQ(list.segments.size(), 1U);
  const auto count = ReadUints(*list.counts, 0U, 1U).front();
  ASSERT_EQ(count * kCommandUints, expected.size());
  EXPECT_EQ(ReadUints(*list.arguments, 0U, expected.size()), expected);
}

//! Shadow views store depth differently: an orthographic cascade stores
//! device depth, a spot map linear distance along the light axis. Each culls a
//! box hidden behind a wall in its own encoding, and keeps one beside it.
NOLINT_TEST_F(TwoPhaseOcclusionGpuTest, ShadowEncodingsCullHiddenBoxes)
{
  // View x and y in [-1, 1], distance 0 to 10 stored as 1 to 0.
  const auto cascade = glm::mat4 {
    glm::vec4 { 1.0F, 0.0F, 0.0F, 0.0F },
    glm::vec4 { 0.0F, 1.0F, 0.0F, 0.0F },
    glm::vec4 { 0.0F, 0.0F, 1.0F / kCascadeDepthRange, 0.0F },
    glm::vec4 { 0.0F, 0.0F, 1.0F, 1.0F },
  };
  // Orthographic boxes span their pixel rect at every distance.
  const auto cascade_box = [](const glm::vec4& rect, const float front,
                             const float back) -> DrawCullRecord {
    auto box = PixelBox(rect, 1.0F, 1.0F);
    box.box_center.z = -(front + back) * 0.5F;
    box.box_extent.z = (back - front) * 0.5F;
    return box;
  };
  struct Case {
    const char* name {};
    glm::mat4 view_projection {};
    DrawCullDepth depth;
    float (*encode)(float) {};
    DrawCullRecord hidden;
    DrawCullRecord beside;
  };
  const auto cases = std::array {
    Case {
      .name = "cascade",
      .view_projection = cascade,
      .depth = DrawCullDepth::ForProjection(cascade),
      .encode = &CascadeDepth,
      .hidden = cascade_box({ 20.0F, 40.0F, 20.0F, 40.0F }, 5.0F, 6.0F),
      .beside = cascade_box({ 57.0F, 63.0F, 20.0F, 40.0F }, 5.0F, 6.0F),
    },
    Case {
      .name = "spot",
      .view_projection = Projection(),
      .depth = DrawCullDepth::AxialLinear(kSpotInverseRange),
      .encode = &SpotDepth,
      .hidden = BehindWall(),
      .beside = BesideWall(),
    },
  };
  for (const auto& test_case : cases) {
    SCOPED_TRACE(test_case.name);
    auto history = CullingViewHistory { "Culling view test" };
    const auto walled = MakeDepth(std::array { kWall }, test_case.encode);
    const auto anchor
      = TestDraw { .record = test_case.beside, .source = Source(1U) };
    PrepareScene(std::array { anchor });
    (void)RunCullingView(
      history, test_case.view_projection, test_case.depth, *walled);

    PrepareScene(std::array {
      anchor,
      TestDraw { .record = test_case.hidden, .source = Source(2U) },
    });
    EXPECT_EQ(RunCullingView(
                history, test_case.view_projection, test_case.depth, *walled),
      (std::vector { kDrawnInPhase1, kOccluded }));
  }
}

} // namespace
