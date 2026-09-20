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
#include <cstring>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include <Oxygen/Core/EngineTag.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Direct3D12/CommandList.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Vortex/PostProcess/Passes/TonemapPass.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureLightingFixture.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestTags.h>
#include <Oxygen/Vortex/Test/Fixtures/RendererPublicationProbe.h>

namespace oxygen::vortex::testing::exposure {

using graphics::DescriptorVisibility;
using graphics::Framebuffer;
using graphics::FramebufferDesc;
using graphics::ResourceStates;
using graphics::ResourceViewType;
using graphics::Texture;
using graphics::TextureSubResourceSet;
using graphics::TextureViewDescription;

NOLINT_TEST_F(ExposureLightingGpuTest,
  QueuedDepthAliasesRetainSnapshotAcrossViewResizeAndRetirement)
{
  verify_manual_p = false;
  probe->prepare = [](RenderContext&) -> void { };
  SceneRenderer* owner = nullptr;
  std::array<SceneTextureExtractRef, 2> latest;
  const Texture* live_depth = nullptr;
  probe->inspect = [&](const RenderContext&, const SceneTextureExtractRef&,
                     unsigned) -> void {
    owner
      = vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_);
    ASSERT_NE(owner, nullptr);
    const auto& extracts = owner->GetSceneTextureExtracts();
    latest = {
      extracts.resolved_scene_depth,
      extracts.prev_scene_depth,
    };
    live_depth = &owner->GetSceneTextures().GetSceneDepth();
  };
  const auto resize_output = [&](unsigned width, unsigned height) -> void {
    view.viewport = {
      .width = static_cast<float>(width),
      .height = static_cast<float>(height),
    };
    auto& lens = camera.GetCameraAs<scene::PerspectiveCamera>()->get();
    lens.SetViewport(view.viewport);
    lens.SetAspectRatio(static_cast<float>(width) / static_cast<float>(height));
    auto output = CreateRegisteredTexture({
      .width = width,
      .height = height,
      .format = Format::kRGBA32Float,
      .is_render_target = true,
      .initial_state = ResourceStates::kCommon,
    });
    framebuffer = Backend().CreateFramebuffer(
      FramebufferDesc {}.AddColorAttachment(output));
  };

  struct DepthCopy {
    std::shared_ptr<graphics::Buffer> readback;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint {};
    UINT64 row_bytes {};
  };
  const auto copy_depth = [&](graphics::CommandRecorder& recorder,
                            const Texture& depth) -> DepthCopy {
    // Generic texture readback rejects typeless depth/stencil resources. Copy
    // just the native depth plane into the fixture's ordinary readback buffer.
    auto* source = depth.GetNativeResource()->AsPointer<ID3D12Resource>();
    const auto desc = source->GetDesc();
    DepthCopy copy {};
    UINT rows = 0U;
    UINT64 bytes = 0U;
    Backend().GetCurrentDevice()->GetCopyableFootprints(
      &desc, 0U, 1U, 0U, &copy.footprint, &rows, &copy.row_bytes, &bytes);
    CHECK_EQ_F(rows, depth.GetDescriptor().height);
    CHECK_F(bytes > 0U && bytes != (std::numeric_limits<UINT64>::max)());
    copy.readback = CreateReadbackBuffer(
      SizeBytes {
        bytes,
      },
      "Queued depth");
    if (!recorder.IsResourceTracked(depth)) {
      CHECK_F(recorder.AdoptKnownResourceState(depth));
    }
    recorder.RequireResourceState(depth, ResourceStates::kCopySource);
    EnsureTracked(recorder, copy.readback, ResourceStates::kCopyDest);
    recorder.FlushBarriers();
    D3D12_TEXTURE_COPY_LOCATION source_location {};
    source_location.pResource = source;
    source_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    source_location.SubresourceIndex = 0U;
    D3D12_TEXTURE_COPY_LOCATION destination {};
    destination.pResource
      = copy.readback->GetNativeResource()->AsPointer<ID3D12Resource>();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint = copy.footprint;
    const auto recording = recorder.GetCommandListForInspection();
    const auto* native_recording
      // The recorder comes from the fixture D3D12 backend; RTTI is disabled.
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
      = static_cast<const graphics::d3d12::CommandList*>(recording.get());
    native_recording->GetCommandList()->CopyTextureRegion(
      &destination, 0U, 0U, 0U, &source_location, nullptr);
    return copy;
  };
  const auto depth_words
    = [](const DepthCopy& copy) -> std::vector<std::uint32_t> {
    const auto width = copy.footprint.Footprint.Width;
    const auto height = copy.footprint.Footprint.Height;
    CHECK_GT_F(width, 0U);
    CHECK_GT_F(height, 0U);
    CHECK_EQ_F(copy.row_bytes % width, 0U);
    const auto texel_stride = copy.row_bytes / width;
    CHECK_GE_F(texel_stride, sizeof(std::uint32_t));
    const auto* bytes = static_cast<const std::byte*>(copy.readback->Map());
    CHECK_NOTNULL_F(bytes);
    const auto mapped_bytes = std::span {
      bytes,
      copy.readback->GetDescriptor().size_bytes,
    };
    std::vector<std::uint32_t> words(static_cast<std::size_t>(width) * height);
    for (unsigned y = 0U; y < height; ++y) {
      for (unsigned x = 0U; x < width; ++x) {
        std::memcpy(&words.at((y * width) + x),
          mapped_bytes
            .subspan(copy.footprint.Offset
                + (static_cast<std::size_t>(y)
                  * copy.footprint.Footprint.RowPitch)
                + (x * texel_stride),
              sizeof(std::uint32_t))
            .data(),
          sizeof(words.front()));
      }
    }
    copy.readback->UnMap();
    return words;
  };
  const auto read_depth
    = [&](const Texture& depth) -> std::vector<std::uint32_t> {
    DepthCopy copy {};
    {
      auto recorder = AcquireRecorder("Depth snapshot reference");
      copy = copy_depth(*recorder, depth);
      recorder->RequireResourceStateFinal(
        depth, ResourceStates::kShaderResource);
    }
    WaitForQueueIdle();
    return depth_words(copy);
  };

  resize_output(8U, 8U);
  SetSurface(data::MaterialDomain::kOpaque, .25F);
  ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0));
  for (const auto& depth : latest) {
    ASSERT_TRUE(depth.valid);
    ASSERT_NE(depth.texture, nullptr);
    ASSERT_NE(depth.retained_texture, nullptr);
    EXPECT_NE(depth.texture, live_depth);
  }
  ASSERT_EQ(latest.at(0).texture, latest.at(1).texture);
  EXPECT_FALSE(
    latest.at(0).retained_texture.owner_before(latest.at(1).retained_texture));
  EXPECT_FALSE(
    latest.at(1).retained_texture.owner_before(latest.at(0).retained_texture));
  auto retained = latest;
  const auto reference = read_depth(*retained.at(0).texture);
  ASSERT_EQ(reference.size(), 64U);
  for (const auto bits : reference) {
    const auto depth = std::bit_cast<float>(bits);
    ASSERT_TRUE(std::isfinite(depth));
    ASSERT_GT(depth, 0.0F);
    ASSERT_LT(depth, 1.0F);
  }
  std::weak_ptr<const Texture> extract_lifetime
    = retained.at(0).retained_texture;
  std::weak_ptr<const Texture> resource_lifetime
    = retained.at(0).texture->shared_from_this();

  surface_view_id = 101U;
  mesh_node.GetTransform().SetLocalPosition({
    0.0F,
    0.0F,
    -1.0F,
  });
  ASSERT_NO_FATAL_FAILURE(RenderSurface(true, 0, 1U));
  ASSERT_NE(latest.at(0).texture, retained.at(0).texture);
  EXPECT_NE(read_depth(*latest.at(0).texture), reference);
  surface_view_id = 100U;
  resize_output(16U, 8U);
  ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 1U));
  ASSERT_EQ(latest.at(0).texture->GetDescriptor().width, 16U);
  ASSERT_EQ(retained.at(0).texture->GetDescriptor().width, 8U);
  owner->RemoveViewState(
    ViewId {
      100U,
    },
    CompositionView::ViewStateHandle {
      100U,
    });
  owner->RemoveViewState(
    ViewId {
      101U,
    },
    CompositionView::ViewStateHandle {
      101U,
    });
  probe->inspect = {};
  latest = {};

  const auto slot = frame::Slot {
    sequence % 3U,
  };
  Backend().BeginFrame(
    frame::SequenceNumber {
      ++sequence,
    },
    slot);
  frame.SetFrameSlot(slot, engine::internal::EngineTagFactory::Get());
  frame.SetFrameSequenceNumber(
    frame::SequenceNumber {
      sequence,
    },
    engine::internal::EngineTagFactory::Get());
  renderer_->OnFrameStart(observer_ptr {
    &frame,
  });
  const auto completion = SignalQueue();
  std::array<DepthCopy, 2> copies;
  {
    auto recorder = AcquireDeferredRecorder("Retained depth alias consumers");
    for (unsigned index = 0U; index < copies.size(); ++index) {
      copies.at(index) = copy_depth(*recorder, *retained.at(index).texture);
    }
    // Both aliases share one tracked resource; finalize after its last copy.
    recorder->RequireResourceStateFinal(
      *retained.at(0).texture, ResourceStates::kShaderResource);
    recorder->RecordQueueSignal(completion.get());
  }
  // Deferred submission proves the consumers have not executed when the last
  // extract wrappers disappear. GPU-safe retirement must keep their source.
  retained = {};
  EXPECT_TRUE(extract_lifetime.expired());
  EXPECT_FALSE(resource_lifetime.expired());
  EXPECT_LT(GetQueue()->GetCompletedValue(), completion.get());
  SubmitDeferredRecorders();
  renderer_->OnFrameEnd(observer_ptr {
    &frame,
  });
  Backend().EndFrame(
    frame::SequenceNumber {
      sequence,
    },
    slot);
  WaitForQueue(completion);
  for (const auto& copy : copies) {
    EXPECT_EQ(depth_words(copy), reference);
  }
  WaitForQueueIdle();
  Backend().GetDeferredReclaimer().ProcessAllDeferredReleases();
  EXPECT_TRUE(resource_lifetime.expired());
  RecordProperty("queued_depth_aliases_checked", copies.size());
}

NOLINT_TEST_F(ExposureLightingGpuTest,
  QueuedHdrConsumersRetainMixedFormatsAcrossViewRetirement)
{
  verify_manual_p = false;
  probe->prepare = [](RenderContext&) -> void { };
  SceneTextureExtractRef latest;
  probe->inspect
    = [&](const RenderContext&, const SceneTextureExtractRef& color,
        unsigned) -> void { latest = color; };
  SetSurface(data::MaterialDomain::kOpaque, .25F);
  ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0));
  for (unsigned retry = 0;
    retry < 8 && latest.texture->GetDescriptor().format != Format::kRGBA16Float;
    ++retry) {
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 1));
  }
  ASSERT_EQ(latest.texture->GetDescriptor().format, Format::kRGBA16Float);
  struct Pending {
    SceneTextureExtractRef source;
    float scene_value;
    float displayed_gain;
    bool checked;
    std::shared_ptr<Texture> output;
    std::shared_ptr<Framebuffer> target;
  };
  std::vector<Pending> pending;
  const auto retain
    = [&](float value, float gain, bool checked = true) -> void {
    auto output = CreateRegisteredTexture({
      .width = 1,
      .height = 1,
      .format = Format::kRGBA32Float,
      .is_render_target = true,
      .initial_state = ResourceStates::kCommon,
    });
    auto target = Backend().CreateFramebuffer(
      FramebufferDesc {}.AddColorAttachment(output));
    pending.push_back({
      .source = latest,
      .scene_value = value,
      .displayed_gain = gain,
      .checked = checked,
      .output = std::move(output),
      .target = std::move(target),
    });
  };
  retain(.25F, 1.0F);
  SetSurface(data::MaterialDomain::kOpaque, 65504);
  ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 1));
  ASSERT_EQ(latest.texture->GetDescriptor().format, Format::kRGBA16Float);
  const auto rejected = Read<HdrSuitabilityData>(
    *latest.exposure->conversion_buffer, ResourceStates::kShaderResource);
  ASSERT_NE(rejected.failure_flags, 0U);
  ASSERT_NE(latest.fallback, nullptr);
  retain(65504, 1.0F);
  retain(0, 1.0F, false);
  surface_view_id = 101;
  SetSurface(data::MaterialDomain::kOpaque, .5F);
  ASSERT_NO_FATAL_FAILURE(RenderSurface(true, 0, 1));
  ASSERT_EQ(latest.texture->GetDescriptor().format, Format::kRGBA32Float);
  retain(.5F, 1.0F);
  surface_view_id = 100;
  SetSurface(data::MaterialDomain::kOpaque, .75F);
  ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 2, 1));
  ASSERT_EQ(latest.texture->GetDescriptor().format, Format::kRGBA32Float);
  retain(.75F, .25F);
  ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 2, 4));
  auto* owner
    = vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_);
  owner->RemoveViewState(
    ViewId {
      100U,
    },
    CompositionView::ViewStateHandle {
      100U,
    });
  owner->RemoveViewState(
    ViewId {
      101U,
    },
    CompositionView::ViewStateHandle {
      101U,
    });
  latest = {};
  const auto srv = [&](const Texture& texture) -> ShaderVisibleIndex {
    const auto view_desc = TextureViewDescription {
      .view_type = ResourceViewType::kTexture_SRV,
      .visibility = DescriptorVisibility::kShaderVisible,
      .format = texture.GetDescriptor().format,
      .dimension = texture.GetDescriptor().texture_type,
      .sub_resources = TextureSubResourceSet::EntireTexture(),
    };
    const auto index = Backend().GetResourceRegistry().FindShaderVisibleIndex(
      texture, view_desc);
    EXPECT_TRUE(index.has_value());
    return index.value_or(kInvalidShaderVisibleIndex);
  };
  const auto consumer_slot = frame::Slot {
    sequence % 3U,
  };
  Backend().BeginFrame(
    frame::SequenceNumber {
      ++sequence,
    },
    consumer_slot);
  frame.SetFrameSlot(consumer_slot, engine::internal::EngineTagFactory::Get());
  frame.SetFrameSequenceNumber(
    frame::SequenceNumber {
      sequence,
    },
    engine::internal::EngineTagFactory::Get());
  renderer_->OnFrameStart(observer_ptr {
    &frame,
  });
  // Poison only the rejected half destination. The checked consumer must read
  // the retained bright FP32 fallback; an unconditional control must read
  // black.
  const auto zeros = std::array<std::byte, 256> {};
  auto upload = CreateUploadBuffer(SizeBytes {
    zeros.size(),
  });
  upload->Update(zeros.data(), zeros.size(), 0U);
  {
    auto recorder = AcquireRecorder("Rejected half consumer sentinel");
    auto& rejected_half = *pending.at(1).source.texture;
    EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
    ASSERT_TRUE(recorder->AdoptKnownResourceState(rejected_half));
    recorder->RequireResourceState(rejected_half, ResourceStates::kCopyDest);
    recorder->FlushBarriers();
    recorder->CopyBufferToTexture(*upload,
      { .buffer_offset = 0U,
        .buffer_row_pitch = 256U,
        .buffer_slice_pitch = 256U,
        .dst_slice = { .width = 1U, .height = 1U, .depth = 1U, }, },
      rejected_half);
    recorder->RequireResourceStateFinal(
      rejected_half, ResourceStates::kShaderResource);
  }
  auto consumer = postprocess::TonemapPass(*renderer_);
  auto consume_context = RenderContext {};
  consume_context.frame_sequence = frame::SequenceNumber {
    sequence,
  };
  consume_context.frame_slot = consumer_slot;
  for (unsigned index = 0; index < pending.size(); ++index) {
    auto& job = pending.at(index);
    ASSERT_TRUE(job.source.valid);
    ASSERT_NE(job.source.retained_texture, nullptr);
    const auto& exposure = job.source.exposure;
    ASSERT_NE(exposure, nullptr);
    const auto* fallback = job.checked ? job.source.fallback : nullptr;
    consume_context.current_view.view_id = ViewId {
      900U + index,
    };
    auto result_inputs = postprocess::TonemapPass::Inputs {};
    result_inputs.scene_signal = job.source.texture;
    result_inputs.exposure_buffer = exposure->current_state->buffer.get();
    result_inputs.frame_exposure_buffer = exposure->buffer.get();
    result_inputs.scene_signal_srv = srv(*job.source.texture);
    result_inputs.exposure_buffer_srv = exposure->current_state->srv_index;
    result_inputs.frame_exposure_srv = exposure->srv_index;
    result_inputs.post_target = observer_ptr<const Framebuffer> {
      job.target.get(),
    };
    result_inputs.tone_mapper = engine::ToneMapper::kNone;
    result_inputs.gamma = 1;
    result_inputs.scene_fallback = fallback;
    result_inputs.scene_fallback_srv
      = (fallback != nullptr) ? srv(*fallback) : kInvalidShaderVisibleIndex;
    result_inputs.conversion_report
      = (fallback != nullptr) ? exposure->conversion_buffer.get() : nullptr;
    result_inputs.conversion_report_srv = (fallback != nullptr)
      ? exposure->conversion_srv
      : kInvalidShaderVisibleIndex;
    const auto result = consumer.Record(
      consume_context, owner->GetSceneTextures(), result_inputs);
    ASSERT_TRUE(result.executed);
  }
  // Release the last extraction owners after submission, before the frame's
  // fence completes. All consumers are queued before any readback waits.
  for (auto& job : pending) {
    job.source = {};
  }
  renderer_->OnFrameEnd(observer_ptr {
    &frame,
  });
  Backend().EndFrame(
    frame::SequenceNumber {
      sequence,
    },
    consumer_slot);
  WaitForQueueIdle();
  for (const auto& job : pending) {
    const auto pixel = ReadFloatTexture(*job.output);
    ASSERT_EQ(pixel.size(), 1U);
    const double expected = std::clamp(
      std::clamp(
        static_cast<double>(job.scene_value) * job.displayed_gain, 0.0, 1.0)
        - (.5 / 255),
      0.0, 1.0);
    for (unsigned c = 0; c < 3; ++c) {
      EXPECT_NEAR(pixel.at(0).at(c), expected, 2e-5);
    }
    EXPECT_EQ(pixel.at(0).at(3), 1.0F);
  }
  RecordProperty("queued_mixed_hdr_consumers", pending.size());
  probe->inspect = {};
  pending.clear();
}

} // namespace oxygen::vortex::testing::exposure
