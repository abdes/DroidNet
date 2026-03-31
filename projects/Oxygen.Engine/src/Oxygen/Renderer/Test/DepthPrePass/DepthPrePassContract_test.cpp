//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <cstdint>
#include <memory>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Renderer/Passes/DepthPrePass.h>
#include <Oxygen/Renderer/PreparedSceneFrame.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/Test/Fixtures/RendererOffscreenGpuTestFixture.h>
#include <Oxygen/Renderer/Test/Fixtures/SyntheticSceneBuilder.h>
#include <Oxygen/Renderer/Types/PassMask.h>
#include <Oxygen/Renderer/Types/ViewConstants.h>

#include "DepthPrePassGpuTestFixture.h"

namespace {

using oxygen::NdcDepthRange;
using oxygen::engine::DepthPrePass;
using oxygen::engine::PassMask;
using oxygen::engine::PassMaskBit;
using oxygen::engine::PreparedSceneFrame;
using oxygen::engine::testing::DepthPrePassGpuTestFixture;
using oxygen::engine::testing::RunPass;
using oxygen::engine::testing::SyntheticSceneBuilder;
using oxygen::engine::testing::TestVertex;
using oxygen::frame::SequenceNumber;
using oxygen::frame::Slot;

// -- Fixture for contract / integration tests --------------------------------

class DepthPrePassContractTest : public DepthPrePassGpuTestFixture {
protected:
  static constexpr Slot kFrameSlot { 0U };
  static constexpr SequenceNumber kFrameSequence { 20U };

  static auto MakeOpaqueMainViewMask() -> PassMask
  {
    auto mask = PassMask {};
    mask.Set(PassMaskBit::kOpaque);
    mask.Set(PassMaskBit::kMainViewVisible);
    return mask;
  }

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
};

NOLINT_TEST_F(DepthPrePassContractTest,
  ExecutionPublishesCanonicalDepthProductsForDownstreamPasses)
{
  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);

  auto depth_texture = CreateDepthTexture(6U, 5U, "contract.products.texture");
  ASSERT_NE(depth_texture, nullptr);

  auto pass
    = DepthPrePass(std::make_shared<DepthPrePass::Config>(DepthPrePass::Config {
      .depth_texture = depth_texture,
      .debug_name = "contract.products",
    }));

  auto prepared_frame = PreparedSceneFrame {};
  auto offscreen = renderer->BeginOffscreenFrame(
    { .frame_slot = Slot { 0U }, .frame_sequence = SequenceNumber { 7U } });
  offscreen.SetCurrentView(kTestViewId,
    MakeResolvedView(depth_texture->GetDescriptor().width,
      depth_texture->GetDescriptor().height, true, NdcDepthRange::ZeroToOne),
    prepared_frame);
  auto& render_context = offscreen.GetRenderContext();

  {
    auto recorder = AcquireRecorder("contract.products.execute");
    ASSERT_NE(recorder, nullptr);
    EnsureTracked(
      *recorder, depth_texture, oxygen::graphics::ResourceStates::kCommon);
    RunPass(pass, render_context, *recorder);
  }
  WaitForQueueIdle();

  const auto output = pass.GetOutput();
  ASSERT_NE(output.depth_texture, nullptr);
  EXPECT_EQ(output.depth_texture, depth_texture.get());
  EXPECT_TRUE(output.canonical_srv_index.IsValid());
  EXPECT_EQ(output.width, 6U);
  EXPECT_EQ(output.height, 5U);
  EXPECT_EQ(output.ndc_depth_range, NdcDepthRange::ZeroToOne);
  EXPECT_TRUE(output.reverse_z);
  EXPECT_TRUE(output.has_depth_texture);
  EXPECT_TRUE(output.has_canonical_srv);
  EXPECT_TRUE(output.is_complete);
}

NOLINT_TEST_F(
  DepthPrePassContractTest, CanonicalSrvIndexIsStableAcrossReexecution)
{
  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);

  auto depth_texture = CreateDepthTexture(4U, 4U, "contract.stable-srv");
  ASSERT_NE(depth_texture, nullptr);

  auto pass
    = DepthPrePass(std::make_shared<DepthPrePass::Config>(DepthPrePass::Config {
      .depth_texture = depth_texture,
      .debug_name = "contract.stable-srv",
    }));

  // First execution
  ExecuteDepthPass(pass, *renderer, depth_texture, SequenceNumber { 1U },
    "contract.stable-srv.exec1");
  const auto output1 = pass.GetOutput();
  ASSERT_TRUE(output1.canonical_srv_index.IsValid());

  // Second execution (same pass, same texture)
  ExecuteDepthPass(pass, *renderer, depth_texture, SequenceNumber { 2U },
    "contract.stable-srv.exec2");
  const auto output2 = pass.GetOutput();
  ASSERT_TRUE(output2.canonical_srv_index.IsValid());

  EXPECT_EQ(output1.canonical_srv_index, output2.canonical_srv_index);
}

NOLINT_TEST_F(DepthPrePassContractTest, GeometryExecutionProducesCompleteOutput)
{
  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);

  constexpr std::uint32_t kWidth = 8U;
  constexpr std::uint32_t kHeight = 8U;

  auto depth_texture = CreateDepthTexture(kWidth, kHeight, "contract.complete");
  ASSERT_NE(depth_texture, nullptr);

  auto pass
    = DepthPrePass(std::make_shared<DepthPrePass::Config>(DepthPrePass::Config {
      .depth_texture = depth_texture,
      .debug_name = "contract.complete",
    }));

  auto builder
    = SyntheticSceneBuilder(Backend(), *renderer, "contract.complete");
  const auto tri = MakeFullScreenTriangle(0.5F);
  const auto resolved_view
    = MakeResolvedView(kWidth, kHeight, true, NdcDepthRange::ZeroToOne);
  builder.AddTriangle(tri[0], tri[1], tri[2], MakeOpaqueMainViewMask());
  auto scene
    = builder.Build(kTestViewId, kFrameSlot, kFrameSequence, resolved_view);

  ExecuteDepthPassWithScene(pass, *renderer, depth_texture, resolved_view,
    scene.prepared_frame, scene.view_constants, kFrameSlot, kFrameSequence,
    "contract.complete.execute");

  const auto output = pass.GetOutput();
  EXPECT_TRUE(output.is_complete);
  EXPECT_TRUE(output.has_canonical_srv);
  EXPECT_TRUE(output.canonical_srv_index.IsValid());
  EXPECT_EQ(output.ndc_depth_range, NdcDepthRange::ZeroToOne);
  EXPECT_TRUE(output.reverse_z);

  // Verify depth was actually written by the geometry.
  const float center_depth = ReadDepthTexel(
    depth_texture, kWidth / 2, kHeight / 2, "contract.complete.center");
  EXPECT_FLOAT_EQ(center_depth, 0.5F);
}

NOLINT_TEST_F(DepthPrePassContractTest,
  MissingPartitionsStillPublishesCompleteOutputButSkipsGeometry)
{
  auto renderer = MakeRenderer();
  ASSERT_NE(renderer, nullptr);

  constexpr std::uint32_t kWidth = 8U;
  constexpr std::uint32_t kHeight = 8U;

  auto depth_texture
    = CreateDepthTexture(kWidth, kHeight, "contract.no-partitions");
  ASSERT_NE(depth_texture, nullptr);

  auto pass
    = DepthPrePass(std::make_shared<DepthPrePass::Config>(DepthPrePass::Config {
      .depth_texture = depth_texture,
      .debug_name = "contract.no-partitions",
    }));

  auto builder
    = SyntheticSceneBuilder(Backend(), *renderer, "contract.no-partitions");
  const auto tri = MakeFullScreenTriangle(0.5F);
  const auto resolved_view
    = MakeResolvedView(kWidth, kHeight, true, NdcDepthRange::ZeroToOne);
  builder.AddTriangle(tri[0], tri[1], tri[2], MakeOpaqueMainViewMask());
  auto scene
    = builder.Build(kTestViewId, kFrameSlot, kFrameSequence, resolved_view);
  scene.partitions.clear();
  scene.prepared_frame.partitions = {};

  ExecuteDepthPassWithScene(pass, *renderer, depth_texture, resolved_view,
    scene.prepared_frame, scene.view_constants, kFrameSlot, kFrameSequence,
    "contract.no-partitions.execute");

  const auto output = pass.GetOutput();
  EXPECT_TRUE(output.is_complete);
  EXPECT_TRUE(output.has_canonical_srv);
  EXPECT_TRUE(output.canonical_srv_index.IsValid());

  const float center_depth = ReadDepthTexel(
    depth_texture, kWidth / 2, kHeight / 2, "contract.no-partitions.center");
  EXPECT_FLOAT_EQ(center_depth, 0.0F);
}

} // namespace
