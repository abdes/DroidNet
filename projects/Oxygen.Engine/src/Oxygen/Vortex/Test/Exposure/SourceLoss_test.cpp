//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <tuple>
#include <utility>

#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Scene/ExposureSettings.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/PostProcess/PostProcessService.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestGraphics.h>
#include <Oxygen/Vortex/Test/Fixtures/RendererPublicationProbe.h>
#include <Oxygen/Vortex/Types/ExposureStateData.h>
#include <Oxygen/Vortex/Types/ExposureTransition.h>

namespace oxygen::vortex::testing::exposure {

using graphics::ResourceStates;

NOLINT_TEST_F(
  ExposureGpuTest, RemovedSourcePreservesOneFrameThenAdaptsIndependently)
{
  auto& service = OwnedExposureService();
  auto frame = engine::FrameContext {};
  const auto consumer = StartSharedServiceView(service, frame);
  renderer_->RemovePublishedRuntimeView(frame,
    ViewId {
      50U,
    });
  ctx_.current_view.exposure_view_id = consumer;
  ctx_.current_view.exposure_view_state_handle
    = ctx_.current_view.view_state_handle;
  const auto signal = Uniform(8.0F, 4U, 4U);
  {
    auto pixel_options = ServicePixelOptions {};
    pixel_options.delta_time_seconds = 1.0F;
    EXPECT_NEAR(ServicePixel(service, signal, {}, pixel_options), .5F, 2e-5F);
  }
  const auto state
    = vortex::testing::RendererPublicationProbe::ExposureStateForView(
      service, ctx_.current_view.view_state_handle);
  ASSERT_NE(state, nullptr);
  EXPECT_EQ(
    Read<ExposureStateData>(*state->buffer, ResourceStates::kShaderResource)
      .fallback_reason,
    4U);
  const double target = std::log2(.0225);
  const double expected = target + ((-4.0 - target) * std::exp(-2.0));
  {
    auto pixel_options = ServicePixelOptions {};
    pixel_options.delta_time_seconds = 1.0F;
    EXPECT_NEAR(
      std::log2(ServicePixel(service, signal, {}, pixel_options) / 8.0),
      expected, 5e-4);
  }
}

NOLINT_TEST_F(
  ExposureGpuTest, RemovedZeroSourcePreservesBlackOnlyForContinuityFrame)
{
  auto& service = OwnedExposureService();
  auto frame = engine::FrameContext {};
  const auto consumer = StartSharedServiceView(service, frame);
  auto zero = scene::ExposureSettings {};
  zero.key = 12.5F;
  zero.target_luminance = 0.0F;
  zero.min_ev = zero.max_ev = 0.0F;
  const auto source = PublishExposureOwner(frame,
    ViewId {
      50U,
    },
    CompositionView::ViewStateHandle {
      50U,
    },
    zero);
  ctx_.current_view.view_id = source;
  ctx_.current_view.view_state_handle = CompositionView::ViewStateHandle {
    50U,
  };
  ctx_.current_view.exposure_view_state_handle
    = CompositionView::kInvalidViewStateHandle;
  EXPECT_NEAR(ServicePixel(service, Uniform(.25F, 4U, 4U), zero), 0.0F, 2e-5F);
  ctx_.current_view.view_id = consumer;
  ctx_.current_view.view_state_handle = CompositionView::ViewStateHandle {
    60U,
  };
  ctx_.current_view.exposure_view_id = source;
  ctx_.current_view.exposure_view_state_handle
    = CompositionView::ViewStateHandle {
        50U,
      };
  EXPECT_NEAR(ServicePixel(service, Uniform(8.0F, 4U, 4U)), 0.0F, 2e-5F);
  renderer_->RemovePublishedRuntimeView(frame,
    ViewId {
      50U,
    });
  ctx_.current_view.exposure_view_id = consumer;
  ctx_.current_view.exposure_view_state_handle
    = ctx_.current_view.view_state_handle;
  {
    auto pixel_options = ServicePixelOptions {};
    pixel_options.delta_time_seconds = 1.0F;
    EXPECT_NEAR(ServicePixel(service, Uniform(8.0F, 4U, 4U), {}, pixel_options),
      0.0F, 2e-5F);
  }
  std::ignore = ServicePixel(
    service, Uniform(std::numeric_limits<float>::quiet_NaN(), 4U, 4U));
  const auto state
    = vortex::testing::RendererPublicationProbe::ExposureStateForView(
      service, ctx_.current_view.view_state_handle);
  const auto recovered
    = Read<ExposureStateData>(*state->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(recovered.displayed_scale, 1.0F);
  EXPECT_EQ(recovered.latent_scale, 1.0F);
  EXPECT_EQ(recovered.flags & 12U, 0U);
}

NOLINT_TEST_F(
  ExposureGpuTest, RemovedNeverRenderedSourceUsesCapturedFallbackAcrossChain)
{
  auto frame = engine::FrameContext {};
  auto source_settings = scene::ExposureSettings {};
  source_settings.key = 12.5F;
  const auto source_handle = CompositionView::ViewStateHandle {
    50U,
  };
  PublishExposureOwner(frame,
    ViewId {
      50U,
    },
    source_handle, source_settings);
  auto local = source_settings;
  const auto middle = PublishExposureOwner(frame,
    ViewId {
      60U,
    },
    CompositionView::ViewStateHandle {
      60U,
    },
    local,
    ViewId {
      50U,
    });
  const auto leaf = PublishExposureOwner(frame,
    ViewId {
      70U,
    },
    CompositionView::ViewStateHandle {
      70U,
    },
    local,
    ViewId {
      60U,
    });
  const auto seed = renderer_->QueueExposureTransition(
    source_handle, ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  if (!seed.has_value()) {
    FAIL() << "Expected seed to contain a value";
  }
  // No SceneRenderer, source state or consumer state exists at removal.
  renderer_->RemovePublishedRuntimeView(frame,
    ViewId {
      50U,
    });
  auto& service = OwnedExposureService();
  for (const auto [view, handle] : {
         std::pair {
           leaf,
           CompositionView::ViewStateHandle {
             70U,
           },
         },
         std::pair {
           middle,
           CompositionView::ViewStateHandle {
             60U,
           },
         },
       }) {
    ctx_.current_view.view_id = view;
    ctx_.current_view.view_state_handle = handle;
    ctx_.current_view.exposure_view_id = view;
    ctx_.current_view.exposure_view_state_handle = handle;
    {
      auto pixel_options = ServicePixelOptions {};
      pixel_options.delta_time_seconds = 1.0F;
      EXPECT_NEAR(
        ServicePixel(service, Uniform(.25F, 4U, 4U), {}, pixel_options),
        .25F / 256.0F, 2e-5F);
    }
  }
  EXPECT_FALSE(renderer_->RetryExposureTransition(*seed).has_value());
}

NOLINT_TEST_F(
  ExposureGpuTest, DiagnosticSiblingCannotReplayAnotherConsumersSourceLoss)
{
  auto& service = OwnedExposureService();
  auto frame = engine::FrameContext {};
  auto settings = scene::ExposureSettings {};
  settings.key = 12.5F;
  const auto signal = Uniform(8.0F, 4U, 4U);
  for (const bool retire_first : {
         false,
         true,
       }) {
    const auto first = StartSharedServiceView(service, frame);
    const auto root = renderer_->ResolvePublishedRuntimeViewId(ViewId {
      50U,
    });
    const auto second = PublishExposureOwner(frame,
      ViewId {
        70U,
      },
      CompositionView::ViewStateHandle {
        70U,
      },
      settings,
      ViewId {
        50U,
      });
    const auto select = [&](ViewId view, std::uint64_t handle) -> void {
      ctx_.current_view.view_id = view;
      ctx_.current_view.view_state_handle = CompositionView::ViewStateHandle {
        handle,
      };
      ctx_.current_view.exposure_view_id = view;
      ctx_.current_view.exposure_view_state_handle
        = ctx_.current_view.view_state_handle;
    };
    select(second, 70U);
    ctx_.current_view.exposure_view_id = root;
    ctx_.current_view.exposure_view_state_handle
      = CompositionView::ViewStateHandle {
          50U,
        };
    EXPECT_NEAR(ServicePixel(service, signal), .5F, 2e-5F);
    PublishExposureOwner(frame,
      ViewId {
        70U,
      },
      CompositionView::ViewStateHandle {
        70U,
      },
      settings,
      ViewId {
        50U,
      },
      true);
    renderer_->RemovePublishedRuntimeView(frame,
      ViewId {
        50U,
      });
    select(first, 60U);
    {
      auto pixel_options = ServicePixelOptions {};
      pixel_options.delta_time_seconds = 1.0F;
      EXPECT_NEAR(ServicePixel(service, signal, {}, pixel_options), .5F, 2e-5F);
    }
    if (retire_first) {
      renderer_->RemovePublishedRuntimeView(frame,
        ViewId {
          60U,
        });
    }
    const double target = std::log2(.0225);
    for (unsigned i = 1U; i <= 3U; ++i) {
      select(second, 70U);
      {
        auto pixel_options = ServicePixelOptions {};
        pixel_options.diagnostic = true;
        pixel_options.delta_time_seconds = 1.0F;
        EXPECT_NEAR(
          ServicePixel(service, signal, {}, pixel_options), 1.0F, 2e-5F);
      }
      if (retire_first) {
        EXPECT_FALSE(
          vortex::testing::RendererPublicationProbe::HasExposureViewState(
            service,
            CompositionView::ViewStateHandle {
              60U,
            }));
      } else {
        select(first, 60U);
        {
          auto pixel_options = ServicePixelOptions {};
          pixel_options.delta_time_seconds = 1.0F;
          EXPECT_NEAR(
            std::log2(ServicePixel(service, signal, {}, pixel_options) / 8.0),
            target + ((-4.0 - target) * std::exp(-2.0 * i)), 5e-4);
        }
      }
    }
    PublishExposureOwner(frame,
      ViewId {
        70U,
      },
      CompositionView::ViewStateHandle {
        70U,
      },
      settings);
    select(second, 70U);
    {
      auto pixel_options = ServicePixelOptions {};
      pixel_options.delta_time_seconds = 1.0F;
      EXPECT_NEAR(ServicePixel(service, signal, {}, pixel_options), .5F, 2e-5F);
    }
    if (!retire_first) {
      select(first, 60U);
      {
        auto pixel_options = ServicePixelOptions {};
        pixel_options.delta_time_seconds = 1.0F;
        EXPECT_NEAR(
          std::log2(ServicePixel(service, signal, {}, pixel_options) / 8.0),
          target + ((-4.0 - target) * std::exp(-8.0)), 5e-4);
      }
    } else {
      EXPECT_FALSE(
        vortex::testing::RendererPublicationProbe::HasExposureViewState(service,
          CompositionView::ViewStateHandle {
            60U,
          }));
    }
  }
}

NOLINT_TEST_F(
  ExposureGpuTest, RemovedSourceHonorsLocalFixedZeroAndExplicitPolicies)
{
  auto& service = OwnedExposureService();
  auto frame = engine::FrameContext {};
  for (unsigned kind = 0U; kind < 5U; ++kind) {
    auto settings = scene::ExposureSettings {};
    settings.key = 12.5F;
    if (kind == 0U) {
      settings.mode = engine::ExposureMode::kManual;
      settings.manual_ev = 8.0F;
    }
    if (kind == 1U) {
      settings.enabled = false;
    }
    if (kind == 2U) {
      settings.target_luminance = 0.0F;
    }
    const auto consumer = StartSharedServiceView(service, frame, settings);
    std::optional<ExposureTransitionToken> explicit_request;
    if (kind >= 3U) {
      const auto issued = renderer_->QueueExposureTransition(
        ctx_.current_view.view_state_handle,
        kind == 3U ? ExposureTransitionPolicy::kSeedFromEv100
                   : ExposureTransitionPolicy::kRemeter,
        kind == 3U ? std::optional { 8.0F, } : std::nullopt);
      if (!issued.has_value()) {
        FAIL() << "Expected issued to contain a value";
      }
      explicit_request = *issued;
    }
    renderer_->RemovePublishedRuntimeView(frame,
      ViewId {
        50U,
      });
    ctx_.current_view.exposure_view_id = consumer;
    ctx_.current_view.exposure_view_state_handle
      = ctx_.current_view.view_state_handle;
    float expected = .18F;
    if (kind == 0U || kind == 3U) {
      expected = .25F / 256.0F;
    } else if (kind == 1U) {
      expected = .25F;
    } else if (kind == 2U) {
      expected = 0.0F;
    }
    EXPECT_NEAR(
      ServicePixel(service, Uniform(.25F, 4U, 4U), settings), expected, 2e-5F);
    if (explicit_request) {
      EXPECT_EQ(InspectRequiredTransition(explicit_request->target).request,
        *explicit_request);
    }
  }
}

NOLINT_TEST_F(ExposureGpuTest,
  RemovedSourceUsesLastSelectedFallbackAfterConsumerCopyFailure)
{
  auto& service = OwnedExposureService();
  auto frame = engine::FrameContext {};
  const auto consumer = StartSharedServiceView(service, frame);
  const auto consumer_handle = CompositionView::ViewStateHandle {
    60U,
  };
  const auto old
    = vortex::testing::RendererPublicationProbe::ExposureStateForView(
      service, consumer_handle);
  ASSERT_NE(old, nullptr);
  EXPECT_EQ(
    Read<ExposureStateData>(*old->buffer, ResourceStates::kShaderResource)
      .displayed_scale,
    0x1p-4F);
  auto settings = scene::ExposureSettings {};
  settings.key = 12.5F;
  const auto source_handle = CompositionView::ViewStateHandle {
    50U,
  };
  const auto root = PublishExposureOwner(frame,
    ViewId {
      50U,
    },
    source_handle, settings);
  for (unsigned i = 0; i < 4U; ++i) {
    const auto seed = renderer_->QueueExposureTransition(
      source_handle, ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
    if (!seed.has_value()) {
      FAIL() << "Expected seed to contain a value";
    }
  }
  ctx_.current_view.view_id = root;
  ctx_.current_view.view_state_handle = source_handle;
  ctx_.current_view.exposure_view_state_handle
    = CompositionView::kInvalidViewStateHandle;
  const auto signal = Uniform(.25F, 4U, 4U);
  EXPECT_NEAR(ServicePixel(service, signal), .25F / 256.0F, 2e-5F);
  ctx_.current_view.view_id = consumer;
  ctx_.current_view.view_state_handle = consumer_handle;
  ctx_.current_view.exposure_view_id = root;
  ctx_.current_view.exposure_view_state_handle = source_handle;
  const auto previous_selection
    = vortex::testing::RendererPublicationProbe::SelectedBorrowForView(
      service, consumer_handle);
  FailureBackend().fail_next_exposure_recorder = true;
  EXPECT_TRUE(std::isnan(ServicePixel(service, signal)));
  EXPECT_EQ(vortex::testing::RendererPublicationProbe::ExposureStateForView(
              service, consumer_handle),
    old);
  const auto selected
    = vortex::testing::RendererPublicationProbe::SelectedBorrowForView(
      service, consumer_handle);
  ASSERT_NE(selected, nullptr);
  EXPECT_EQ(selected, previous_selection);
  EXPECT_EQ(
    Read<ExposureStateData>(*selected->buffer, ResourceStates::kShaderResource)
      .displayed_scale,
    0x1p-4F);
  renderer_->RemovePublishedRuntimeView(frame,
    ViewId {
      50U,
    });
  ctx_.current_view.exposure_view_id = consumer;
  ctx_.current_view.exposure_view_state_handle = consumer_handle;
  {
    auto pixel_options = ServicePixelOptions {};
    pixel_options.delta_time_seconds = 1.0F;
    EXPECT_NEAR(
      ServicePixel(service, signal, {}, pixel_options), .25F / 16.0F, 2e-5F);
  }
  const auto state
    = vortex::testing::RendererPublicationProbe::ExposureStateForView(
      service, consumer_handle);
  const auto continuity
    = Read<ExposureStateData>(*state->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(continuity.displayed_scale, 0x1p-4F);
  EXPECT_EQ(continuity.latent_scale, 0x1p-4F);
  EXPECT_EQ(continuity.applied_generation.at(0), 1U);
  EXPECT_EQ(
    Read<ExposureStateData>(*selected->buffer, ResourceStates::kShaderResource)
      .displayed_scale,
    0x1p-4F);
  {
    auto pixel_options = ServicePixelOptions {};
    pixel_options.delta_time_seconds = 1.0F;
    EXPECT_NEAR(
      ServicePixel(service, signal, {}, pixel_options), .25F / 8.0F, 2e-5F);
  }
}

} // namespace oxygen::vortex::testing::exposure
