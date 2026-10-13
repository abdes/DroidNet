//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Data/MaterialDomain.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsService.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsTypes.h>
#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererCapability.h>
#include <Oxygen/Vortex/SceneRenderer/SceneRenderer.h>
#include <Oxygen/Vortex/test/Exposure/Fixtures/ExposureGpuFixture.h>
#include <Oxygen/Vortex/test/Exposure/Fixtures/ExposureLightingFixture.h>
#include <Oxygen/Vortex/test/Fixtures/RendererPublicationProbe.h>

namespace oxygen::vortex::testing::exposure {

namespace {

  constexpr std::uint32_t kExtent = 32U;
  //! A box has 12 edges; the scene has one draw.
  constexpr std::uint32_t kBoundsLines = 12U;

  //! Renders the surface into a wider output, from far enough back that its
  //! whole box is in view.
  class GpuDebugPrimitivesGpuTest : public ExposureLightingGpuTest {
  protected:
    auto AdditionalCapabilities() const -> CapabilitySet override
    {
      return RendererCapabilityFamily::kDiagnosticsAndProfiling;
    }

    auto SetUp() -> void override
    {
      ExposureLightingGpuTest::SetUp();
      view.viewport = {
        .width = static_cast<float>(kExtent),
        .height = static_cast<float>(kExtent),
      };
      auto lens = camera.GetCameraAs<scene::PerspectiveCamera>();
      ASSERT_TRUE(lens);
      lens->get().SetViewport(view.viewport);
      ASSERT_TRUE(camera.GetTransform().SetLocalPosition({ 0, 0, 7 }));
      auto output = CreateRegisteredTexture({
        .width = kExtent,
        .height = kExtent,
        .format = Format::kRGBA32Float,
        .is_render_target = true,
        .initial_state = graphics::ResourceStates::kCommon,
      });
      framebuffer = Backend().CreateFramebuffer(
        graphics::FramebufferDesc {}.AddColorAttachment(output));
      settings.mode = engine::ExposureMode::kManual;
      settings.manual_ev = 0.0F;
    }

    auto EnableGpuDebug(const GpuDebugPrimitiveSource sources,
      const std::uint32_t capacity
      = DiagnosticsService::kDefaultGpuDebugLineCapacity) -> void
    {
      auto& diagnostics = renderer_->GetDiagnosticsService();
      diagnostics.SetEnabledFeatures(DiagnosticsFeature::kGpuDebugPrimitives
        | DiagnosticsFeature::kFrameLedger);
      diagnostics.SetGpuDebugPrimitiveSources(sources);
      diagnostics.SetGpuDebugLineCapacity(capacity);
    }

    auto Stats() -> std::optional<GpuDebugPrimitiveStats>
    {
      return RendererPublicationProbe::GetSceneRenderer(*renderer_)
        ->GetGpuDebugPrimitiveStats(ViewId { surface_view_id });
    }

    auto Output() -> std::vector<Pixel>
    {
      return ReadFloatTexture(
        *framebuffer->GetDescriptor().color_attachments.front().texture);
    }

    //! Pixels of the pure green the draw bounds of a drawn draw use.
    static auto CountBoundsPixels(const std::vector<Pixel>& pixels)
      -> std::size_t
    {
      return static_cast<std::size_t>(
        std::ranges::count_if(pixels, [](const Pixel& pixel) -> bool {
          return pixel.at(0) < 1.0e-3F && pixel.at(1) > 0.999F
            && pixel.at(2) < 1.0e-3F;
        }));
    }

    auto HasIssue(const std::string_view code) -> bool
    {
      const auto snapshot
        = renderer_->GetDiagnosticsService().GetLatestSnapshot();
      return std::ranges::any_of(
        snapshot.issues, [&](const DiagnosticsIssue& issue) -> bool {
          return issue.code == code;
        });
    }
  };

} // namespace

//! With draw bounds requested, the cull pass emits the box of the draw and
//! the debug pass draws it over the output; without a producer, nothing.
NOLINT_TEST_F(GpuDebugPrimitivesGpuTest, DrawBoundsAppearInTheImage)
{
  SetSurface(data::MaterialDomain::kOpaque);
  EnableGpuDebug(GpuDebugPrimitiveSource::kNone);
  RenderSurface(false, 0.0F, 3U);
  ASSERT_FALSE(HasFatalFailure());
  EXPECT_EQ(CountBoundsPixels(Output()), 0U);
  const auto quiet = Stats();
  ASSERT_HAS_VALUE(quiet);
  EXPECT_EQ(quiet->requested_lines, 0U);

  EnableGpuDebug(GpuDebugPrimitiveSource::kDrawBounds);
  RenderSurface(false, 0.0F, 3U);
  ASSERT_FALSE(HasFatalFailure());
  EXPECT_GT(CountBoundsPixels(Output()), 0U);
  const auto stats = Stats();
  ASSERT_HAS_VALUE(stats);
  EXPECT_EQ(stats->requested_lines, kBoundsLines);
  EXPECT_EQ(stats->drawn_lines, kBoundsLines);
  EXPECT_EQ(stats->invalid_bindless_accesses, 0U);
  EXPECT_EQ(stats->invalid_materials, 0U);
}

//! Lines past the capacity are counted but not stored or drawn, and the
//! overflow is reported.
NOLINT_TEST_F(GpuDebugPrimitivesGpuTest, OverflowIsBoundedAndReported)
{
  constexpr auto kCapacity = 4U;
  SetSurface(data::MaterialDomain::kOpaque);
  EnableGpuDebug(GpuDebugPrimitiveSource::kDrawBounds, kCapacity);
  RenderSurface(false, 0.0F, 3U);
  ASSERT_FALSE(HasFatalFailure());
  const auto stats = Stats();
  ASSERT_HAS_VALUE(stats);
  EXPECT_EQ(stats->line_capacity, kCapacity);
  EXPECT_EQ(stats->requested_lines, kBoundsLines);
  EXPECT_EQ(stats->drawn_lines, kCapacity);
  EXPECT_TRUE(HasIssue("gpu-debug.overflow"));
}

//! While the feature is disabled no view owns debug buffers or publishes a
//! debug record; disabling it releases the buffers of enabled views.
NOLINT_TEST_F(GpuDebugPrimitivesGpuTest, DisabledPathAllocatesNothing)
{
  SetSurface(data::MaterialDomain::kOpaque);
  const auto expect_no_debug_record = [&] -> void {
    const auto published
      = RendererPublicationProbe::GetSceneRenderer(*renderer_)
          ->GetPublishedViewFrameBindings(ViewId { surface_view_id });
    if (published.has_value()) {
      EXPECT_FALSE(published->debug_frame_slot.IsValid());
    }
    EXPECT_FALSE(Stats().has_value());
  };
  RenderSurface(false, 0.0F, 2U);
  ASSERT_FALSE(HasFatalFailure());
  expect_no_debug_record();

  EnableGpuDebug(GpuDebugPrimitiveSource::kDrawBounds);
  RenderSurface(false, 0.0F, 3U);
  ASSERT_FALSE(HasFatalFailure());
  ASSERT_TRUE(Stats().has_value());

  renderer_->GetDiagnosticsService().SetEnabledFeatures(
    DiagnosticsFeature::kNone);
  RenderSurface(false, 0.0F, 2U);
  ASSERT_FALSE(HasFatalFailure());
  expect_no_debug_record();
}

//! A draw whose material cannot be loaded is reported with the materials slot
//! and draws nothing, in both shading paths.
NOLINT_TEST_F(GpuDebugPrimitivesGpuTest, InvalidMaterialIsReportedNotDrawn)
{
  constexpr auto kEmission = 4.0F;
  SetSurface(data::MaterialDomain::kOpaque, kEmission);
  EnableGpuDebug(GpuDebugPrimitiveSource::kNone);
  const auto center = static_cast<std::size_t>(kExtent / 2U) * kExtent
    + static_cast<std::size_t>(kExtent / 2U);
  for (const bool forward : { false, true }) {
    SCOPED_TRACE(forward);
    RenderSurface(forward, 0.0F, 3U);
    ASSERT_FALSE(HasFatalFailure());
    const auto drawn = Output().at(center);
    EXPECT_GT(drawn.at(0), 0.25F);

    // The view renders with a copy of its prepared frame that has no
    // materials record.
    auto corrupted = std::optional<PreparedSceneFrame> {};
    const auto prepare = probe->prepare;
    probe->prepare = [&, prepare](RenderContext& ctx) -> void {
      prepare(ctx);
      ASSERT_NE(ctx.current_view.prepared_frame, nullptr);
      corrupted = *ctx.current_view.prepared_frame;
      corrupted->bindless_material_shading_slot = kInvalidShaderVisibleIndex;
      ctx.current_view.prepared_frame
        = observer_ptr<const PreparedSceneFrame> { &*corrupted };
    };
    RenderSurface(forward, 0.0F, 3U);
    probe->prepare = prepare;
    ASSERT_FALSE(HasFatalFailure());
    const auto missing = Output().at(center);
    EXPECT_LT(missing.at(0), drawn.at(0) * 0.5F);
    const auto stats = Stats();
    ASSERT_HAS_VALUE(stats);
    EXPECT_GT(stats->invalid_materials, 0U);
    EXPECT_EQ(stats->last_invalid_slot, kInvalidShaderVisibleIndex.get());
    EXPECT_TRUE(HasIssue("gpu-debug.invalid-access"));
  }
}

} // namespace oxygen::vortex::testing::exposure
