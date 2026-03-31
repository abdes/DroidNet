//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <cstdint>
#include <memory>
#include <string_view>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Core/Types/Scissors.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Renderer/Passes/DepthPrePass.h>
#include <Oxygen/Renderer/PreparedSceneFrame.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/Test/Fixtures/SyntheticSceneBuilder.h>
#include <Oxygen/Renderer/Types/PassMask.h>
#include <Oxygen/Renderer/Types/ViewConstants.h>

#include "DepthPrePassGpuTestFixture.h"

namespace {

using oxygen::NdcDepthRange;
using oxygen::Scissors;
using oxygen::ViewPort;
using oxygen::engine::DepthPrePass;
using oxygen::engine::PassMask;
using oxygen::engine::PassMaskBit;
using oxygen::engine::PreparedSceneFrame;
using oxygen::engine::testing::DepthPrePassGpuTestFixture;
using oxygen::engine::testing::SyntheticSceneBuilder;
using oxygen::engine::testing::TestVertex;
using oxygen::frame::SequenceNumber;
using oxygen::frame::Slot;

// -- Fixture for GPU execution tests that use empty scenes -------------------

class DepthPrePassClearTest : public DepthPrePassGpuTestFixture { };

NOLINT_TEST_F(DepthPrePassClearTest, ClearHonorsEffectiveDepthRectIntersection)
{
  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);

  auto depth_texture = CreateDepthTexture(4U, 4U, "clear.clipped");
  ASSERT_NE(depth_texture, nullptr);
  ClearDepthTexture(depth_texture, 0.25F, "clear.clipped.seed");

  auto pass
    = DepthPrePass(std::make_shared<DepthPrePass::Config>(DepthPrePass::Config {
      .depth_texture = depth_texture,
      .debug_name = "clear.clipped",
    }));
  pass.SetViewport(ViewPort {
    .top_left_x = 1.0F,
    .top_left_y = 0.0F,
    .width = 3.0F,
    .height = 4.0F,
    .min_depth = 0.0F,
    .max_depth = 1.0F,
  });
  pass.SetScissors(Scissors { .left = 0, .top = 1, .right = 4, .bottom = 3 });

  ExecuteDepthPass(pass, *renderer, depth_texture, SequenceNumber { 1U },
    "clear.clipped.execute");

  // Outside the intersection: should keep seed of 0.25
  EXPECT_FLOAT_EQ(
    ReadDepthTexel(depth_texture, 0U, 0U, "clear.outside.tl"), 0.25F);
  EXPECT_FLOAT_EQ(
    ReadDepthTexel(depth_texture, 0U, 3U, "clear.outside.bl"), 0.25F);
  EXPECT_FLOAT_EQ(
    ReadDepthTexel(depth_texture, 2U, 0U, "clear.outside.top"), 0.25F);

  // Inside the intersection: should be cleared to 0.0 (reverse-Z clear value)
  EXPECT_FLOAT_EQ(
    ReadDepthTexel(depth_texture, 1U, 1U, "clear.inside.a"), 0.0F);
  EXPECT_FLOAT_EQ(
    ReadDepthTexel(depth_texture, 3U, 2U, "clear.inside.b"), 0.0F);
}

// -- Fixture for GPU execution tests that render real geometry ----------------

class DepthPrePassGeometryTest : public DepthPrePassGpuTestFixture {
protected:
  static constexpr Slot kFrameSlot { 0U };
  static constexpr SequenceNumber kFrameSequence { 10U };

  static auto MakeOpaqueMainViewMask() -> PassMask
  {
    auto mask = PassMask {};
    mask.Set(PassMaskBit::kOpaque);
    mask.Set(PassMaskBit::kMainViewVisible);
    return mask;
  }

  static auto MakeMaskedMainViewMask() -> PassMask
  {
    auto mask = PassMask {};
    mask.Set(PassMaskBit::kMasked);
    mask.Set(PassMaskBit::kMainViewVisible);
    return mask;
  }

  static auto MakeTransparentMask() -> PassMask
  {
    auto mask = PassMask {};
    mask.Set(PassMaskBit::kTransparent);
    mask.Set(PassMaskBit::kMainViewVisible);
    return mask;
  }

  static auto MakeOpaqueNonMainViewMask() -> PassMask
  {
    auto mask = PassMask {};
    mask.Set(PassMaskBit::kOpaque);
    return mask;
  }

  static auto MakeDoubleSidedOpaqueMainViewMask() -> PassMask
  {
    auto mask = MakeOpaqueMainViewMask();
    mask.Set(PassMaskBit::kDoubleSided);
    return mask;
  }

  //! Full-screen triangle covering the entire NDC viewport at z=0.5.
  //! With identity view/proj and reverse-Z, z_ndc = z_world.
  //! The depth buffer should read 0.5 at every covered texel.
  static auto MakeFullScreenTriangle(const float z = 0.5F)
    -> std::array<TestVertex, 3>
  {
    return { {
      TestVertex {
        .position = { -1.0F, -3.0F, z },
        .normal = { 0.0F, 0.0F, 1.0F },
        .texcoord = { 0.0F, 2.0F },
        .tangent = { 1.0F, 0.0F, 0.0F },
        .bitangent = { 0.0F, 1.0F, 0.0F },
        .color = { 1.0F, 1.0F, 1.0F, 1.0F },
      },
      TestVertex {
        .position = { 3.0F, 1.0F, z },
        .normal = { 0.0F, 0.0F, 1.0F },
        .texcoord = { 2.0F, 0.0F },
        .tangent = { 1.0F, 0.0F, 0.0F },
        .bitangent = { 0.0F, 1.0F, 0.0F },
        .color = { 1.0F, 1.0F, 1.0F, 1.0F },
      },
      TestVertex {
        .position = { -1.0F, 1.0F, z },
        .normal = { 0.0F, 0.0F, 1.0F },
        .texcoord = { 0.0F, 0.0F },
        .tangent = { 1.0F, 0.0F, 0.0F },
        .bitangent = { 0.0F, 1.0F, 0.0F },
        .color = { 1.0F, 1.0F, 1.0F, 1.0F },
      },
    } };
  }

  static auto MakeFullScreenTriangleClockwise(const float z = 0.5F)
    -> std::array<TestVertex, 3>
  {
    const auto ccw = MakeFullScreenTriangle(z);
    return { { ccw[0], ccw[2], ccw[1] } };
  }
};

NOLINT_TEST_F(DepthPrePassGeometryTest, OpaqueTriangleWritesExpectedDepth)
{
  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);

  constexpr std::uint32_t kWidth = 8U;
  constexpr std::uint32_t kHeight = 8U;

  auto depth_texture = CreateDepthTexture(kWidth, kHeight, "geom.opaque");
  ASSERT_NE(depth_texture, nullptr);

  auto pass
    = DepthPrePass(std::make_shared<DepthPrePass::Config>(DepthPrePass::Config {
      .depth_texture = depth_texture,
      .debug_name = "geom.opaque",
    }));

  const auto resolved_view = MakeResolvedView(kWidth, kHeight);
  auto builder = SyntheticSceneBuilder(Backend(), *renderer, "geom.opaque");
  const auto tri = MakeFullScreenTriangle(0.5F);
  builder.AddTriangle(tri[0], tri[1], tri[2], MakeOpaqueMainViewMask());
  auto scene
    = builder.Build(kTestViewId, kFrameSlot, kFrameSequence, resolved_view);

  ExecuteDepthPassWithScene(pass, *renderer, depth_texture, resolved_view,
    scene.prepared_frame, scene.view_constants, kFrameSlot, kFrameSequence,
    "geom.opaque.execute");

  // With identity projection and reverse-Z (GreaterOrEqual), the clear is 0.0
  // and triangles at z=0.5 write 0.5 (which is > 0.0, so passes).
  const float center_depth = ReadDepthTexel(
    depth_texture, kWidth / 2, kHeight / 2, "geom.opaque.center");
  EXPECT_FLOAT_EQ(center_depth, 0.5F);
}

NOLINT_TEST_F(DepthPrePassGeometryTest, TransparentPartitionIsExcluded)
{
  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);

  constexpr std::uint32_t kWidth = 4U;
  constexpr std::uint32_t kHeight = 4U;

  auto depth_texture = CreateDepthTexture(kWidth, kHeight, "geom.transparent");
  ASSERT_NE(depth_texture, nullptr);

  auto pass
    = DepthPrePass(std::make_shared<DepthPrePass::Config>(DepthPrePass::Config {
      .depth_texture = depth_texture,
      .debug_name = "geom.transparent",
    }));

  // Add a transparent triangle — depth prepass should skip it entirely.
  const auto resolved_view = MakeResolvedView(kWidth, kHeight);
  auto builder
    = SyntheticSceneBuilder(Backend(), *renderer, "geom.transparent");
  const auto tri = MakeFullScreenTriangle(0.7F);
  builder.AddTriangle(tri[0], tri[1], tri[2], MakeTransparentMask());
  auto scene
    = builder.Build(kTestViewId, kFrameSlot, kFrameSequence, resolved_view);

  ExecuteDepthPassWithScene(pass, *renderer, depth_texture, resolved_view,
    scene.prepared_frame, scene.view_constants, kFrameSlot, kFrameSequence,
    "geom.transparent.execute");

  // Depth should remain at the clear value (0.0 reverse-Z) everywhere.
  const float center_depth = ReadDepthTexel(
    depth_texture, kWidth / 2, kHeight / 2, "geom.transparent.center");
  EXPECT_FLOAT_EQ(center_depth, 0.0F);
}

NOLINT_TEST_F(DepthPrePassGeometryTest, NonMainViewVisiblePartitionIsExcluded)
{
  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);

  constexpr std::uint32_t kWidth = 4U;
  constexpr std::uint32_t kHeight = 4U;

  auto depth_texture = CreateDepthTexture(kWidth, kHeight, "geom.non-main");
  ASSERT_NE(depth_texture, nullptr);

  auto pass
    = DepthPrePass(std::make_shared<DepthPrePass::Config>(DepthPrePass::Config {
      .depth_texture = depth_texture,
      .debug_name = "geom.non-main",
    }));

  const auto resolved_view = MakeResolvedView(kWidth, kHeight);
  auto builder = SyntheticSceneBuilder(Backend(), *renderer, "geom.non-main");
  const auto tri = MakeFullScreenTriangle(0.65F);
  builder.AddTriangle(tri[0], tri[1], tri[2], MakeOpaqueNonMainViewMask());
  auto scene
    = builder.Build(kTestViewId, kFrameSlot, kFrameSequence, resolved_view);

  ExecuteDepthPassWithScene(pass, *renderer, depth_texture, resolved_view,
    scene.prepared_frame, scene.view_constants, kFrameSlot, kFrameSequence,
    "geom.non-main.execute");

  const float center_depth = ReadDepthTexel(
    depth_texture, kWidth / 2, kHeight / 2, "geom.non-main.center");
  EXPECT_FLOAT_EQ(center_depth, 0.0F);
}

NOLINT_TEST_F(DepthPrePassGeometryTest, ZeroVertexDrawDoesNotCrash)
{
  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);

  constexpr std::uint32_t kWidth = 4U;
  constexpr std::uint32_t kHeight = 4U;

  auto depth_texture = CreateDepthTexture(kWidth, kHeight, "geom.zero-vertex");
  ASSERT_NE(depth_texture, nullptr);

  auto pass
    = DepthPrePass(std::make_shared<DepthPrePass::Config>(DepthPrePass::Config {
      .depth_texture = depth_texture,
      .debug_name = "geom.zero-vertex",
    }));

  // Build a scene with a proper triangle but also a degenerate draw (zero
  // indices) to verify the pass handles it gracefully.
  const auto resolved_view = MakeResolvedView(kWidth, kHeight);
  auto builder
    = SyntheticSceneBuilder(Backend(), *renderer, "geom.zero-vertex");
  const auto tri = MakeFullScreenTriangle(0.5F);
  builder.AddTriangle(tri[0], tri[1], tri[2], MakeOpaqueMainViewMask());
  // AddIndexedMesh with a single vertex and empty indices to exercise zero-draw
  builder.AddIndexedMesh({ TestVertex { .position = { 0.0F, 0.0F, 0.0F } } },
    { 0U }, // keep a valid single index so buffer creation succeeds
    MakeOpaqueMainViewMask());
  auto scene
    = builder.Build(kTestViewId, kFrameSlot, kFrameSequence, resolved_view);

  // Override the second draw's index_count to 0 to simulate a zero-vertex draw.
  scene.draw_metadata[1].index_count = 0U;
  scene.draw_metadata[1].vertex_count = 0U;

  ExecuteDepthPassWithScene(pass, *renderer, depth_texture, resolved_view,
    scene.prepared_frame, scene.view_constants, kFrameSlot, kFrameSequence,
    "geom.zero-vertex.execute");

  // The first triangle should still produce depth; no crash from the zero draw.
  const float center_depth = ReadDepthTexel(
    depth_texture, kWidth / 2, kHeight / 2, "geom.zero-vertex.center");
  EXPECT_FLOAT_EQ(center_depth, 0.5F);
}

NOLINT_TEST_F(DepthPrePassGeometryTest, MaskedPartitionSelectsAlphaTestPso)
{
  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);

  constexpr std::uint32_t kWidth = 4U;
  constexpr std::uint32_t kHeight = 4U;

  auto depth_texture = CreateDepthTexture(kWidth, kHeight, "geom.masked");
  ASSERT_NE(depth_texture, nullptr);

  auto pass
    = DepthPrePass(std::make_shared<DepthPrePass::Config>(DepthPrePass::Config {
      .depth_texture = depth_texture,
      .debug_name = "geom.masked",
    }));

  // A masked triangle should render through the alpha-test PSO path.
  // Without a real material/texture the PS will use the default alpha cutoff
  // (0.5) against the vertex color alpha (1.0), so it should still pass and
  // write depth.
  const auto resolved_view = MakeResolvedView(kWidth, kHeight);
  auto builder = SyntheticSceneBuilder(Backend(), *renderer, "geom.masked");
  const auto tri = MakeFullScreenTriangle(0.6F);
  builder.AddTriangle(tri[0], tri[1], tri[2], MakeMaskedMainViewMask());
  auto scene
    = builder.Build(kTestViewId, kFrameSlot, kFrameSequence, resolved_view);

  ExecuteDepthPassWithScene(pass, *renderer, depth_texture, resolved_view,
    scene.prepared_frame, scene.view_constants, kFrameSlot, kFrameSequence,
    "geom.masked.execute");

  const float center_depth = ReadDepthTexel(
    depth_texture, kWidth / 2, kHeight / 2, "geom.masked.center");
  // Masked pass should write depth for opaque-enough pixels.
  EXPECT_GT(center_depth, 0.0F);
}

NOLINT_TEST_F(DepthPrePassGeometryTest,
  DoubleSidedOpaqueTriangleWritesDepthWhenWoundClockwise)
{
  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);

  constexpr std::uint32_t kWidth = 8U;
  constexpr std::uint32_t kHeight = 8U;

  auto depth_texture = CreateDepthTexture(kWidth, kHeight, "geom.double-sided");
  ASSERT_NE(depth_texture, nullptr);

  auto pass
    = DepthPrePass(std::make_shared<DepthPrePass::Config>(DepthPrePass::Config {
      .depth_texture = depth_texture,
      .debug_name = "geom.double-sided",
    }));

  const auto resolved_view = MakeResolvedView(kWidth, kHeight);
  auto builder
    = SyntheticSceneBuilder(Backend(), *renderer, "geom.double-sided");
  const auto tri = MakeFullScreenTriangleClockwise(0.55F);
  builder.AddTriangle(
    tri[0], tri[1], tri[2], MakeDoubleSidedOpaqueMainViewMask());
  auto scene
    = builder.Build(kTestViewId, kFrameSlot, kFrameSequence, resolved_view);

  ExecuteDepthPassWithScene(pass, *renderer, depth_texture, resolved_view,
    scene.prepared_frame, scene.view_constants, kFrameSlot, kFrameSequence,
    "geom.double-sided.execute");

  const float center_depth = ReadDepthTexel(
    depth_texture, kWidth / 2, kHeight / 2, "geom.double-sided.center");
  EXPECT_FLOAT_EQ(center_depth, 0.55F);
}

} // namespace
