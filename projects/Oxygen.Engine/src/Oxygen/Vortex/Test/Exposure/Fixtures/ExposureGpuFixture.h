//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/FrameCaptureController.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/ReadbackTypes.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Direct3D12/Test/Fixtures/ReadbackTestFixture.h>
#include <Oxygen/Vortex/PostProcess/Passes/ExposurePass.h>
#include <Oxygen/Vortex/PostProcess/PostProcessService.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Types/ExposureStateData.h>

namespace oxygen::vortex::testing {
class FakeAssetLoader;
}

namespace oxygen::vortex::testing::exposure {

using Pixel = std::array<float, 4>;

auto ExposureProbeRootBindings() -> std::vector<graphics::RootBindingItem>;

struct ExposureSignal {
  std::shared_ptr<const graphics::Texture> texture;
  ShaderVisibleIndex srv;
};

struct ServicePixelOptions {
  bool diagnostic {
    false,
  };
  float delta_time_seconds {
    0.0F,
  };
  std::function<void()> before_execute;
  engine::ToneMapper tone_mapper {
    engine::ToneMapper::kNone,
  };
  bool start_new_frame {
    true,
  };
  const PostProcessService::PreparedExposure* prepared {
    nullptr,
  };
  const ExposureSignal* fallback {
    nullptr,
  };
  postprocess::ExposurePass::FrameLease checked_resolution;
  const ExposureSignal* bloom {
    nullptr,
  };
  float bloom_intensity {
    0.0F,
  };
};

class ExposureTestEngine;
class ExposureFailureGraphics;

class ExposureGpuTest : public graphics::d3d12::testing::ReadbackTestFixture {
public:
  ExposureGpuTest();
  ~ExposureGpuTest() override;

  OXYGEN_MAKE_NON_COPYABLE(ExposureGpuTest)
  OXYGEN_MAKE_NON_MOVABLE(ExposureGpuTest)

protected:
  //! Keeps direct pass checks on the renderer's existing GPU timeline.
  template <typename Operation>
  auto SubmitCommands(const std::string_view name, Operation&& operation)
    -> std::invoke_result_t<Operation, graphics::CommandRecorder&>
  {
    return graphics::testing::SubmitCommands(Backend(), name,
      [&](graphics::CommandRecorder& recorder)
        -> std::invoke_result_t<Operation, graphics::CommandRecorder&> {
        renderer_->GetDiagnosticsService().AttachGpuTimelineCollector(recorder);
        return std::invoke(std::forward<Operation>(operation), recorder);
      });
  }

  static auto MappedTextureBytes(const graphics::MappedTextureReadback& mapped,
    std::size_t texel_bytes) -> std::span<const std::byte>;
  auto FailureBackend() -> ExposureFailureGraphics&;
  auto InspectRequiredTransition(CompositionView::ViewStateHandle handle) const
    -> ExposureTransitionStatus;
  auto CheckOffscreenSharing(bool inside_frame) -> void;
  auto CheckSceneExposureRetry(bool inside_frame, bool late_failure = false)
    -> void;
  auto CheckFogViewRetirement(bool persistent, bool temporal) -> void;
  auto CreateBackend(const SerializedBackendConfig& config,
    const SerializedPathFinderConfig& paths)
    -> std::shared_ptr<graphics::d3d12::Graphics> override;
  using Signal = ExposureSignal;
  struct Snapshot {
    ExposureStateData state;
    std::array<std::uint32_t, 264> histogram {};
  };

  auto BackendConfigJson() const -> std::string override;
  static auto CapturePath() -> std::string;
  auto BeginOptionalCapture() -> observer_ptr<graphics::FrameCaptureController>;
  auto PathFinderConfigJson() const -> std::string override;
  auto SetUp() -> void override;
  auto TearDown() -> void override;
  auto MakeSignal(std::uint32_t width, std::uint32_t height,
    std::span<const Pixel> pixels, std::uint32_t depth = 1U,
    Format format = Format::kRGBA32Float, bool bindless_texture = false)
    -> Signal;
  auto Uniform(float value, std::uint32_t width = 1U, std::uint32_t height = 1U)
    -> Signal;
  template <typename T>
  auto PublishFixtureData(const T& value) -> ShaderVisibleIndex
  {
    static_assert(std::is_trivially_copyable_v<T>);
    auto buffer = CreateRegisteredBuffer({
      .size_bytes = sizeof(T),
      .usage = graphics::BufferUsage::kNone,
      .memory = graphics::BufferMemory::kUpload,
      .debug_name = "Fog edge fixture bindings",
    });
    CHECK_F(buffer != nullptr);
    buffer->Update(&value, sizeof(T), 0U);
    auto& allocator = renderer_->GetGraphics()->GetDescriptorAllocator();
    auto handle = allocator.AllocateBindless(
      oxygen::bindless::generated::kGlobalSrvDomain,
      graphics::ResourceViewType::kStructuredBuffer_SRV);
    CHECK_F(handle.IsValid());
    const auto index = allocator.GetShaderVisibleIndex(handle);
    Backend().GetResourceRegistry().RegisterView(*buffer, std::move(handle),
      graphics::BufferViewDescription {
        .view_type = graphics::ResourceViewType::kStructuredBuffer_SRV,
        .range = { 0U, sizeof(T), },
        .stride = sizeof(T), });
    return index;
  }
  auto RunToneProbe(std::span<const std::byte> inputs_data,
    std::uint32_t record_count, std::uint32_t mode = 0U,
    bool capture_enabled = true) -> std::vector<std::array<float, 8>>;
  auto ReadFloatTexture(const graphics::Texture& texture,
    bool allow_half = false) -> std::vector<Pixel>;
  template <typename Payload>
  auto Read(const graphics::Buffer& source,
    graphics::ResourceStates final_state) -> Payload
  {
    static_assert(std::is_trivially_copyable_v<Payload>);
    auto readback
      = GetReadbackManager()->CreateBufferReadback("Exposure fixture readback");
    CHECK_F(readback != nullptr);
    {
      auto recorder = AcquireRecorder("Exposure fixture copy");
      recorder->BeginTrackingResourceState(source, final_state, false);
      const auto ticket = readback->EnqueueCopy(*recorder, source,
        {
          0U,
          sizeof(Payload),
        });
      CHECK_F(ticket.has_value());
      recorder->RequireResourceStateFinal(source, final_state);
    }
    const auto mapped = readback->MapNow();
    CHECK_F(mapped.has_value());
    CHECK_F(mapped->Bytes().size() >= sizeof(Payload));
    Payload result {};
    std::memcpy(&result, mapped->Bytes().data(), sizeof(result));
    return result;
  }
  auto Run(const Signal& signal, scene::ExposureSettings settings = {},
    float dt = 0.0F, const Signal* mask = nullptr, float inverse_p = 1.0F,
    bool metering_available = true,
    std::optional<ExposureTransitionToken> transition = {},
    std::optional<float> camera_ev = {}, bool temporary_unit = false)
    -> Snapshot;
  auto ServicePixel(PostProcessService& service, const Signal& signal,
    scene::ExposureSettings settings = {}, ServicePixelOptions options = {})
    -> float;

  auto Qualify(const Signal& signal, bool meter,
    scene::ExposureSettings settings = {}, const Signal* mask = nullptr,
    bool coverage = false) -> HdrSuitabilityData;

  auto EligibilityStep(const Signal& signal,
    const ResolvedPostProcessConfig& config, std::uint64_t sequence,
    std::uint64_t layout = 7U, std::uint32_t expected = 1024U,
    bool invalidate_previous = false,
    const postprocess::ExposurePass::Source* source = nullptr,
    std::optional<ExposureTransitionToken> transition = {},
    bool metering_available = true, bool capture_eligibility = false)
    -> std::pair<ExposureStateData, ExposureCompletedStatus>;

  auto ResetHistory() -> void;
  auto PublishExposureOwner(engine::FrameContext& frame, ViewId intent_id,
    CompositionView::ViewStateHandle handle, scene::ExposureSettings settings,
    ViewId source = kInvalidViewId, bool diagnostic = false,
    std::optional<ShaderDebugMode> debug_override = {}) -> ViewId;
  auto SharedConfig(scene::ExposureSettings settings = {},
    std::optional<float> camera_ev = {}, std::uint64_t revision = 1U)
    -> ResolvedPostProcessConfig;
  auto RecordShared(const Signal& signal,
    const ResolvedPostProcessConfig& config,
    const postprocess::ExposurePass::Source* source = nullptr,
    std::optional<ExposureTransitionToken> token = {},
    std::uint64_t lifetime = 0U) -> postprocess::ExposurePass::Result;
  auto ReadState(const postprocess::ExposurePass::Result& result)
    -> ExposureStateData;
  auto OwnedExposureService() -> PostProcessService&;
  auto StartSharedServiceView(PostProcessService& service,
    engine::FrameContext& frame, scene::ExposureSettings consumer_settings = {})
    -> ViewId;
  std::unique_ptr<Renderer> renderer_;
  std::unique_ptr<vortex::testing::FakeAssetLoader> owned_asset_loader_;
  std::unique_ptr<::testing::NiceMock<ExposureTestEngine>> owned_test_engine_;
  std::vector<std::shared_ptr<graphics::Framebuffer>> registered_targets_;
  std::unique_ptr<postprocess::ExposurePass> pass_;
  RenderContext ctx_;
  std::uint64_t sequence_ {
    0U,
  };
  postprocess::ExposurePass::StateLease last_state_;
};

} // namespace oxygen::vortex::testing::exposure
