//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstdint>
#include <cstring>
#include <utility>

#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/ReadbackManager.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>
#include <Oxygen/Vortex/Test/Fixtures/RendererPublicationProbe.h>
#include <Oxygen/Vortex/Types/Generated.ViewContracts.h>

namespace oxygen::vortex::testing::exposure {

namespace {

  using graphics::BufferMemory;
  using graphics::BufferUsage;
  using graphics::ComputePipelineDesc;
  using graphics::ResourceStates;
  using graphics::ResourceViewType;
  using graphics::ShaderRequest;

  //! What one probe dispatch saw through its b1 record.
  struct ViewRecordSample {
    std::uint32_t view_frame_bindings_slot;
    std::uint32_t scene_texture_frame_slot;
    std::uint32_t scene_color_srv;
    std::uint32_t frame_slot;
  };

  auto ProbeRequest() -> ShaderRequest
  {
    return {
      .stage = ShaderType::kCompute,
      .source_path = "Tests/ViewRecordProbe.hlsl",
      .entry_point = "CS",
    };
  }

  // Stage 9 must not publish SceneColor (SceneRenderer CHECKs this on the
  // CPU); Stage 10 republishes the view with it. A pass recorded between the
  // two must read the Stage 9 publication on the GPU, even though the list is
  // submitted after Stage 10 rewrote the view constants.
  NOLINT_TEST_F(
    ExposureGpuTest, PassRecordedMidViewReadsTheBindingsPublishedBeforeIt)
  {
    constexpr auto kStage10SceneColorSrv = ShaderVisibleIndex { 0x5C0FU };
    InstallProbeShader(OXYGEN_VIEW_RECORD_PROBE, ProbeRequest());

    auto stage10_textures = SceneTextureBindings {};
    stage10_textures.scene_color_srv = kStage10SceneColorSrv;
    const auto stage9_textures_slot
      = PublishFixtureData(SceneTextureBindings {});
    const auto stage10_textures_slot = PublishFixtureData(stage10_textures);
    auto stage9_view = ViewFrameBindings {};
    stage9_view.scene_texture_frame_slot = stage9_textures_slot;
    auto stage10_view = ViewFrameBindings {};
    stage10_view.scene_texture_frame_slot = stage10_textures_slot;
    const auto stage9_view_slot = PublishFixtureData(stage9_view);
    const auto stage10_view_slot = PublishFixtureData(stage10_view);

    constexpr auto kOutputBytes = 2U * sizeof(ViewRecordSample);
    auto output = CreateRegisteredBuffer({
      .size_bytes = kOutputBytes,
      .usage = BufferUsage::kStorage,
      .memory = BufferMemory::kDeviceLocal,
      .debug_name = "View record probe output",
    });
    auto& allocator = renderer_->GetGraphics()->GetDescriptorAllocator();
    auto output_handle = allocator.AllocateRaw(ResourceViewType::kRawBuffer_UAV,
      graphics::DescriptorVisibility::kShaderVisible);
    const auto output_uav = allocator.GetShaderVisibleIndex(output_handle);
    Backend().GetResourceRegistry().RegisterView(*output,
      std::move(output_handle),
      graphics::BufferViewDescription {
        .view_type = ResourceViewType::kRawBuffer_UAV,
        .range = { 0U, kOutputBytes },
        .stride = 0U,
      });
    const auto stage9_inputs = PublishFixtureData(
      std::array<std::uint32_t, 4> { output_uav.get(), 0U, 0U, 0U });
    const auto stage10_inputs = PublishFixtureData(
      std::array<std::uint32_t, 4> { output_uav.get(), 1U, 0U, 0U });

    const auto pipeline = ComputePipelineDesc::Builder {}
                            .SetComputeShader(ProbeRequest())
                            .SetRootBindings(ExposureProbeRootBindings())
                            .SetDebugName("View record probe")
                            .Build();
    auto readback
      = GetReadbackManager()->CreateBufferReadback("View record probe");
    {
      namespace root = bindless::generated::d3d12;
      const auto view_constants
        = static_cast<std::uint32_t>(root::RootParam::kViewConstants);
      const auto root_constants
        = static_cast<std::uint32_t>(root::RootParam::kRootConstants);
      auto recorder = AcquireRecorder("View record probe");
      EnsureTracked(*recorder, output, ResourceStates::kCommon);
      recorder->RequireResourceState(*output, ResourceStates::kUnorderedAccess);
      recorder->FlushBarriers();
      recorder->SetPipelineState(pipeline);
      recorder->SetComputeRoot32BitConstant(root_constants, 0U, 0U);

      ASSERT_TRUE(RendererPublicationProbe::WriteCurrentViewConstants(
        *renderer_, ctx_, stage9_view_slot));
      ASSERT_TRUE(ctx_.view_constants.IsValid());
      recorder->SetComputeRootConstantBufferView(
        view_constants, ctx_.view_constants.GpuAddress());
      recorder->SetComputeRoot32BitConstant(
        root_constants, stage9_inputs.get(), 1U);
      recorder->Dispatch(1U, 1U, 1U);

      ASSERT_TRUE(RendererPublicationProbe::WriteCurrentViewConstants(
        *renderer_, ctx_, stage10_view_slot));
      ASSERT_TRUE(ctx_.view_constants.IsValid());
      recorder->SetComputeRootConstantBufferView(
        view_constants, ctx_.view_constants.GpuAddress());
      recorder->SetComputeRoot32BitConstant(
        root_constants, stage10_inputs.get(), 1U);
      recorder->Dispatch(1U, 1U, 1U);

      ASSERT_TRUE(
        readback->EnqueueCopy(*recorder, *output, { 0U, kOutputBytes })
          .has_value());
    }
    const auto mapped = readback->MapNow();
    ASSERT_HAS_VALUE(mapped);
    auto samples = std::array<ViewRecordSample, 2> {};
    ASSERT_GE(mapped->Bytes().size(), sizeof(samples));
    std::memcpy(samples.data(), mapped->Bytes().data(), sizeof(samples));

    const auto frame_slot = ctx_.frame_slot.get();
    const auto& [before, after] = samples;
    EXPECT_EQ(before.view_frame_bindings_slot, stage9_view_slot.get());
    EXPECT_EQ(before.scene_texture_frame_slot, stage9_textures_slot.get());
    EXPECT_EQ(before.scene_color_srv, kInvalidShaderVisibleIndex.get());
    EXPECT_EQ(before.frame_slot, frame_slot);
    EXPECT_EQ(after.view_frame_bindings_slot, stage10_view_slot.get());
    EXPECT_EQ(after.scene_texture_frame_slot, stage10_textures_slot.get());
    EXPECT_EQ(after.scene_color_srv, kStage10SceneColorSrv.get());
    EXPECT_EQ(after.frame_slot, frame_slot);
  }

} // namespace

} // namespace oxygen::vortex::testing::exposure
