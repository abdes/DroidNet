//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>

#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/HalfFloat.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Engine/IAsyncEngine.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureLightingFixture.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestEngine.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestTags.h>
#include <Oxygen/Vortex/Test/Fakes/AssetLoader.h>
#include <Oxygen/Vortex/Test/Fixtures/RendererPublicationProbe.h>
#include <Oxygen/Vortex/Test/Fixtures/TextureBinderPayloads.h>

namespace oxygen::vortex::testing::exposure {

using graphics::Framebuffer;
using graphics::FramebufferDesc;
using graphics::ResourceStates;
using graphics::Texture;

auto ExposureLightingGpuTest::QualifyMixedPrecisionAuxiliaryHandoff(
  bool split_targets) -> void
{
  verify_manual_p = false;
  probe->prepare = [](RenderContext&) -> void { };
  SetSurface(data::MaterialDomain::kOpaque, .25F);
  scene->Update();
  scene->SyncObservers();
  std::array<std::shared_ptr<Texture>, 3> outputs;
  std::array<std::shared_ptr<Framebuffer>, 3> targets;
  std::array<std::shared_ptr<Framebuffer>, 3> scene_targets;
  for (unsigned i = 0; i < 3; ++i) {
    outputs.at(i) = CreateRegisteredTexture({
      .width = 1,
      .height = 1,
      .format = Format::kRGBA32Float,
      .is_render_target = true,
      .initial_state = ResourceStates::kCommon,
    });
    targets.at(i) = Backend().CreateFramebuffer(
      FramebufferDesc {}.AddColorAttachment(outputs.at(i)));
    scene_targets.at(i) = targets.at(i);
    if (split_targets) {
      auto scene_output = CreateRegisteredTexture({
        .width = 1,
        .height = 1,
        .format = Format::kRGBA32Float,
        .is_render_target = true,
        .initial_state = ResourceStates::kCommon,
      });
      scene_targets.at(i) = Backend().CreateFramebuffer(
        FramebufferDesc {}.AddColorAttachment(scene_output));
    }
  }
  std::unordered_map<CompositionView::ViewStateHandle, SceneTextureExtractRef>
    snapshots;
  probe->inspect = [&](const RenderContext& ctx,
                     const SceneTextureExtractRef& color, unsigned) -> void {
    snapshots.insert_or_assign(ctx.current_view.view_state_handle, color);
  };
  unsigned checked = 0;
  for (unsigned iteration = 0; iteration < 12; ++iteration) {
    SCOPED_TRACE(iteration);
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
    const auto order = iteration % 2 == 0 ? std::array { 1U, 2U, 0U, }
                                          : std::array { 2U, 1U, 0U, };
    for (const auto index : order) {
      auto input = CompositionView::ForScene(
        ViewId {
          500U + index,
        },
        view, camera);
      input.view_state_handle = index == 1
        ? CompositionView::kInvalidViewStateHandle
        : CompositionView::ViewStateHandle {
            500U + index,
          };
      auto exposure = settings;
      if (index == 0) {
        exposure.manual_ev = iteration >= 8 ? 3.0F : 0.0F;
      } else {
        exposure.manual_ev = index == 1 ? 1.0F : 2.0F;
      }
      input.render_settings.exposure = exposure;
      if (index == 0) {
        input.view_kind = CompositionView::ViewKind::kAuxiliary;
        input.produced_aux_outputs.push_back(
          { .id = CompositionView::AuxOutputId { 7001U, },
            .kind = CompositionView::AuxOutputKind::kColorTexture,
            .debug_name = "Exposure lifetime producer", });
      } else if (index == 1) {
        input.consumed_aux_outputs.push_back(
          { .id = CompositionView::AuxOutputId { 7001U, },
            .kind = CompositionView::AuxOutputKind::kColorTexture,
            .required = true, });
      }
      ASSERT_NE(
        renderer_->PublishRuntimeCompositionView(frame,
          { .composition_view = input,
            .render_target = observer_ptr { scene_targets.at(index).get(), },
            .composite_source = observer_ptr { targets.at(index).get(), }, }),
        kInvalidViewId);
    }
    auto loop = co::testing::TestEventLoop {};
    // co::Run completes synchronously before this closure and its captured
    // fixture state leave scope.
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
    co::Run(loop, [&]() -> co::Co<void> {
      co_await renderer_->OnPreRender(observer_ptr {
        &frame,
      });
      co_await renderer_->OnRender(observer_ptr {
        &frame,
      });
    });
    renderer_->OnFrameEnd(observer_ptr {
      &frame,
    });
    Backend().EndFrame(
      frame::SequenceNumber {
        sequence,
      },
      slot);
    WaitForQueueIdle();
    if (iteration < 6) {
      continue;
    }
    const double producer_gain = iteration >= 8 ? .125 : 1;
    const std::array expected {
      (.25 * producer_gain) - (.5 / 255),
      (.25 * producer_gain) - (.5 / 255),
      (.25 * .25) - (.5 / 255),
    };
    for (unsigned index = 0; index < 3; ++index) {
      const auto pixels = ReadFloatTexture(*outputs.at(index));
      ASSERT_EQ(pixels.size(), 1U);
      for (unsigned c = 0; c < 3; ++c) {
        EXPECT_NEAR(pixels.at(0).at(c), expected.at(index), 2e-5);
      }
      EXPECT_EQ(pixels.at(0).at(3), 1);
      ++checked;
    }
    EXPECT_EQ(snapshots.at(CompositionView::kInvalidViewStateHandle)
                .texture->GetDescriptor()
                .format,
      Format::kRGBA32Float);
    if (iteration >= 7) {
      EXPECT_EQ(snapshots
                  .at(CompositionView::ViewStateHandle {
                    502U,
                  })
                  .texture->GetDescriptor()
                  .format,
        Format::kRGBA16Float);
    }
    if (iteration == 7 || iteration == 11) {
      EXPECT_EQ(snapshots
                  .at(CompositionView::ViewStateHandle {
                    500U,
                  })
                  .texture->GetDescriptor()
                  .format,
        Format::kRGBA16Float);
    }
    if (iteration == 8) {
      EXPECT_EQ(snapshots
                  .at(CompositionView::ViewStateHandle {
                    500U,
                  })
                  .texture->GetDescriptor()
                  .format,
        Format::kRGBA32Float);
    }
  }
  probe->inspect = {};
  snapshots.clear();
  RecordProperty("mixed_family_auxiliary_outputs", checked);
}

} // namespace oxygen::vortex::testing::exposure
