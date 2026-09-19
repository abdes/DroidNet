//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <numeric>
#include <span>
#include <stdexcept>
#include <unordered_set>
#include <utility>

#include <nlohmann/json.hpp>

#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Console/Console.h>
#include <Oxygen/Cooker/Import/Internal/TextureCooker.h>
#include <Oxygen/Cooker/Import/ScratchImage.h>
#include <Oxygen/Cooker/Import/TexturePackingPolicy.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Core/EngineTag.h>
#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/HalfFloat.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Engine/IAsyncEngine.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/FrameCaptureController.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/ShaderByteCode.h>
#include <Oxygen/Graphics/Common/TimestampQueryProvider.h>
#include <Oxygen/Graphics/Direct3D12/CommandList.h>
#include <Oxygen/Graphics/Direct3D12/Test/Fixtures/ReadbackTestFixture.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>
#include <Oxygen/Scene/Camera/Orthographic.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Environment/Background.h>
#include <Oxygen/Scene/Environment/Fog.h>
#include <Oxygen/Scene/Environment/LocalFogVolume.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Environment/SkyLight.h>
#include <Oxygen/Scene/Environment/SkySphere.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Light/PointLight.h>
#include <Oxygen/Scene/Light/SpotLight.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Vortex/Environment/Internal/AtmosphereLutCache.h>
#include <Oxygen/Vortex/Environment/Internal/AtmosphereState.h>
#include <Oxygen/Vortex/Environment/Internal/IblProcessor.h>
#include <Oxygen/Vortex/Environment/Internal/LocalFogVolumeState.h>
#include <Oxygen/Vortex/Environment/Passes/AtmosphereComposePass.h>
#include <Oxygen/Vortex/Environment/Passes/AtmosphereMultiScatteringLutPass.h>
#include <Oxygen/Vortex/Environment/Passes/AtmosphereSkyViewLutPass.h>
#include <Oxygen/Vortex/Environment/Passes/AtmosphereTransmittanceLutPass.h>
#include <Oxygen/Vortex/Environment/Passes/DistantSkyLightLutPass.h>
#include <Oxygen/Vortex/Environment/Passes/FogPass.h>
#include <Oxygen/Vortex/Environment/Passes/LocalFogVolumeComposePass.h>
#include <Oxygen/Vortex/Internal/PreviousViewHistoryCache.h>
#include <Oxygen/Vortex/Internal/RetainedTexturePool.h>
#include <Oxygen/Vortex/PostProcess/Passes/ExposurePass.h>
#include <Oxygen/Vortex/PostProcess/Passes/TonemapPass.h>
#include <Oxygen/Vortex/PostProcess/PostProcessService.h>
#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/RendererTag.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Hzb/ScreenHzbModule.h>
#include <Oxygen/Vortex/Test/Fakes/AssetLoader.h>
#include <Oxygen/Vortex/Test/Fixtures/ExposureBenchmarkScene.h>
#include <Oxygen/Vortex/Test/Fixtures/RendererPublicationProbe.h>
#include <Oxygen/Vortex/Test/Fixtures/TextureBinderPayloads.h>
#include <Oxygen/Vortex/Types/EnvironmentFrameBindings.h>
#include <Oxygen/Vortex/Types/EnvironmentStaticData.h>
#include <Oxygen/Vortex/Types/EnvironmentViewData.h>
#include <Oxygen/Vortex/Types/ExposureStateData.h>
#include <Oxygen/Vortex/Types/ViewConstants.h>
#include <Oxygen/Vortex/Types/ViewFrameBindings.h>
#include <Oxygen/Vortex/Types/ViewHistoryFrameBindings.h>
#include <Oxygen/Vortex/Upload/UploadCoordinator.h>
#include <Oxygen/Vortex/ViewExtension.h>

namespace oxygen::engine::internal {
struct EngineTagFactory {
  static auto Get() noexcept -> EngineTag { return EngineTag {}; }
};
}

namespace oxygen::vortex::internal {
auto RendererTagFactory::Get() noexcept -> RendererTag
{
  return RendererTag {};
}
} // namespace oxygen::vortex::internal

namespace {
using namespace oxygen;
using namespace oxygen::graphics;
using namespace oxygen::vortex;
using Pixel = std::array<float, 4>;

auto ExposureProbeRootBindings() -> std::vector<RootBindingItem>
{
  namespace root = oxygen::bindless::generated::d3d12;
  std::vector<RootBindingItem> bindings;
  for (const auto& parameter : root::kRootParamTable) {
    RootBindingDesc binding {};
    binding.binding_slot_desc.register_index = parameter.shader_register;
    binding.binding_slot_desc.register_space = parameter.register_space;
    binding.visibility = ShaderStageFlags::kAll;
    if (parameter.kind == root::RootParamKind::DescriptorTable) {
      const auto& range = parameter.ranges.front();
      CHECK_F(range.range_type == root::RangeType::SRV
        || range.range_type == root::RangeType::Sampler);
      binding.data = DescriptorTableBinding { .view_type
        = range.range_type == root::RangeType::Sampler
          ? ResourceViewType::kSampler
          : ResourceViewType::kRawBuffer_SRV,
        .base_index = range.base_register,
        .count = range.num_descriptors };
    } else if (parameter.kind == root::RootParamKind::CBV) {
      binding.data = DirectBufferBinding {};
    } else {
      binding.data = PushConstantsBinding { .size = parameter.constants_count };
    }
    bindings.emplace_back(binding);
  }
  return bindings;
}

class ExposureTestEngine : public IAsyncEngine {
public:
  MOCK_METHOD(observer_ptr<content::IAssetLoader>, GetAssetLoader, (),
    (const, noexcept, override));
  MOCK_METHOD(scripting::IScriptCompilationService&,
    GetScriptCompilationService, (), (noexcept, override));
  MOCK_METHOD(const scripting::IScriptCompilationService&,
    GetScriptCompilationService, (), (const, noexcept, override));
  MOCK_METHOD(
    const PathFinder&, GetPathFinder, (), (const, noexcept, override));
  MOCK_METHOD(
    std::weak_ptr<Graphics>, GetGraphics, (), (const, noexcept, override));
  MOCK_METHOD(std::shared_ptr<Platform>, GetPlatformShared, (),
    (const, noexcept, override));
  MOCK_METHOD(
    const EngineConfig&, GetEngineConfig, (), (const, noexcept, override));
  MOCK_METHOD(console::Console&, GetConsole, (), (noexcept, override));
  MOCK_METHOD(
    const console::Console&, GetConsole, (), (const, noexcept, override));
  MOCK_METHOD(bool, IsRunning, (), (const, override));
  MOCK_METHOD(void, Stop, (), (override));
  MOCK_METHOD(ModuleSubscription, SubscribeModuleAttached,
    (engine::ModuleAttachedCallback, bool), (override));
  MOCK_METHOD(std::optional<std::reference_wrapper<engine::EngineModule>>,
    GetModuleByType, (TypeId), (const, noexcept, override));
};

class ExposureFailureGraphics final : public graphics::d3d12::Graphics {
public:
  using graphics::d3d12::Graphics::Graphics;
  std::shared_ptr<graphics::IShaderByteCode> tone_probe;
  auto GetShader(const graphics::ShaderRequest& request) const
    -> std::shared_ptr<graphics::IShaderByteCode> override
  {
    if (request.source_path == "Tests/ToneBoundsProbe.hlsl") {
      return tone_probe;
    }
    return graphics::d3d12::Graphics::GetShader(request);
  }
  mutable std::weak_ptr<graphics::Texture> processed_sky;
  bool track_resources { false };
  bool account_texture_allocations { false };
  unsigned accounting_iteration { 0 };
  std::string accounting_phase;
  mutable std::uint64_t peak_texture_bytes { 0 };
  mutable std::uint64_t peak_hdr_bytes { 0 };
  mutable std::uint64_t peak_buffer_bytes { 0 };
  mutable std::uint64_t peak_placement_bytes { 0 };
  mutable std::uint64_t peak_engine_placement_bytes { 0 };
  mutable unsigned peak_hdr_iteration { 0 };
  mutable nlohmann::json allocation_peaks = nlohmann::json::object();
  mutable nlohmann::json allocation_peak_history = nlohmann::json::array();
  static auto IsExposureHdrTexture(std::string_view name) -> bool
  {
    return name == "SceneColor" || name == "ResolvedSceneColor"
      || name.find("SkyView") != std::string_view::npos
      || name.find("Aerial") != std::string_view::npos
      || name.find("IntegratedLightScattering") != std::string_view::npos
      || name.find("AtmosphereTransmittance") != std::string_view::npos
      || name.find("AtmosphereMultiScattering") != std::string_view::npos;
  }
  mutable std::vector<std::weak_ptr<graphics::Texture>> tracked_textures;
  mutable std::vector<std::weak_ptr<graphics::Buffer>> tracked_buffers;
  auto MeasureTrackedPlacement() const -> nlohmann::json
  {
    auto textures = nlohmann::json::array();
    auto buffers = nlohmann::json::array();
    std::unordered_set<ID3D12Resource*> unique;
    std::uint64_t texture_bytes = 0U;
    std::uint64_t buffer_bytes = 0U;
    std::uint64_t hdr_bytes = 0U;
    std::uint64_t fixture_output_bytes = 0U;
    std::uint64_t delayed_output_bytes = 0U;
    std::uint64_t depth_readback_bytes = 0U;
    for (std::size_t ordinal = 0U; ordinal < tracked_textures.size();
      ++ordinal) {
      const auto texture = tracked_textures[ordinal].lock();
      if (!texture) {
        continue;
      }
      auto* native = texture->GetNativeResource()->AsPointer<ID3D12Resource>();
      if (!native || !unique.insert(native).second) {
        continue;
      }
      const auto shape = native->GetDesc();
      const auto bytes = GetCurrentDevice()
                           ->GetResourceAllocationInfo(0U, 1U, &shape)
                           .SizeInBytes;
      const auto& desc = texture->GetDescriptor();
      const bool hdr = IsExposureHdrTexture(desc.debug_name);
      const bool fixture_output
        = desc.debug_name.starts_with("LifecycleAccounting.Output");
      const bool delayed_output
        = desc.debug_name.starts_with("LifecycleAccounting.DelayedConsumer");
      textures.push_back({ { "id", ordinal }, { "name", desc.debug_name },
        { "format", static_cast<unsigned>(desc.format) },
        { "width", desc.width }, { "height", desc.height },
        { "depth", desc.depth }, { "array_layers", desc.array_size },
        { "mips", desc.mip_levels }, { "samples", desc.sample_count },
        { "sample_quality", desc.sample_quality },
        { "texture_type", static_cast<unsigned>(desc.texture_type) },
        { "native_format", static_cast<unsigned>(shape.Format) },
        { "native_flags", static_cast<unsigned>(shape.Flags) },
        { "native_layout", static_cast<unsigned>(shape.Layout) },
        { "native_alignment", shape.Alignment },
        { "native_dimension", static_cast<unsigned>(shape.Dimension) },
        { "placement_bytes", bytes }, { "exposure_hdr", hdr },
        { "fixture_output", fixture_output || delayed_output },
        { "registered", GetResourceRegistry().Contains(*texture) } });
      texture_bytes += bytes;
      if (hdr) {
        hdr_bytes += bytes;
      }
      if (fixture_output) {
        fixture_output_bytes += bytes;
      }
      if (delayed_output) {
        delayed_output_bytes += bytes;
      }
    }
    for (std::size_t ordinal = 0U; ordinal < tracked_buffers.size();
      ++ordinal) {
      const auto buffer = tracked_buffers[ordinal].lock();
      if (!buffer) {
        continue;
      }
      auto* native = buffer->GetNativeResource()->AsPointer<ID3D12Resource>();
      if (!native || !unique.insert(native).second) {
        continue;
      }
      const auto shape = native->GetDesc();
      const auto bytes = GetCurrentDevice()
                           ->GetResourceAllocationInfo(0U, 1U, &shape)
                           .SizeInBytes;
      const auto desc = buffer->GetDescriptor();
      const bool depth_readback
        = desc.debug_name.starts_with("LifecycleAccounting.DepthReadback");
      buffers.push_back({ { "id", ordinal }, { "name", desc.debug_name },
        { "logical_bytes", desc.size_bytes }, { "placement_bytes", bytes },
        { "memory", static_cast<unsigned>(desc.memory) },
        { "usage", static_cast<unsigned>(desc.usage) },
        { "native_flags", static_cast<unsigned>(shape.Flags) },
        { "native_layout", static_cast<unsigned>(shape.Layout) },
        { "native_alignment", shape.Alignment },
        { "fixture_output", depth_readback },
        { "registered", GetResourceRegistry().Contains(*buffer) } });
      buffer_bytes += bytes;
      if (depth_readback) {
        depth_readback_bytes += bytes;
      }
    }
    return { { "textures", std::move(textures) },
      { "buffers", std::move(buffers) },
      { "texture_placement_bytes", texture_bytes },
      { "buffer_placement_bytes", buffer_bytes },
      { "hdr_placement_bytes", hdr_bytes },
      { "total_placement_bytes", texture_bytes + buffer_bytes },
      { "fixture_output_placement_bytes", fixture_output_bytes },
      { "delayed_output_placement_bytes", delayed_output_bytes },
      { "depth_readback_placement_bytes", depth_readback_bytes },
      { "engine_placement_bytes",
        texture_bytes + buffer_bytes - fixture_output_bytes
          - delayed_output_bytes - depth_readback_bytes },
      { "texture_creation_count", tracked_textures.size() },
      { "buffer_creation_count", tracked_buffers.size() } };
  }
  auto ObserveAllocationPeak() const -> void
  {
    auto snapshot = MeasureTrackedPlacement();
    auto changed = nlohmann::json::array();
    const auto update
      = [&](const char* name, const char* field, std::uint64_t& peak) {
          const auto bytes = snapshot.at(field).get<std::uint64_t>();
          if (bytes > peak) {
            peak = bytes;
            allocation_peaks[name] = { { "iteration", accounting_iteration },
              { "phase", accounting_phase }, { "bytes", bytes },
              { "history_index", allocation_peak_history.size() } };
            changed.push_back(name);
          }
        };
    update("textures", "texture_placement_bytes", peak_texture_bytes);
    update("buffers", "buffer_placement_bytes", peak_buffer_bytes);
    update("combined", "total_placement_bytes", peak_placement_bytes);
    update("engine", "engine_placement_bytes", peak_engine_placement_bytes);
    const auto previous_hdr = peak_hdr_bytes;
    update("hdr", "hdr_placement_bytes", peak_hdr_bytes);
    if (peak_hdr_bytes != previous_hdr) {
      peak_hdr_iteration = accounting_iteration;
    }
    if (!changed.empty()) {
      allocation_peak_history.push_back(
        { { "iteration", accounting_iteration }, { "phase", accounting_phase },
          { "changed_categories", std::move(changed) },
          { "resources", std::move(snapshot) } });
    }
  }
  auto CreateBuffer(const BufferDesc& desc) const
    -> std::shared_ptr<graphics::Buffer> override
  {
    auto buffer = graphics::d3d12::Graphics::CreateBuffer(desc);
    if (track_resources && buffer) {
      tracked_buffers.push_back(buffer);
    }
    if (account_texture_allocations && buffer) {
      ObserveAllocationPeak();
    }
    return buffer;
  }
  auto CreateTexture(const TextureDesc& desc) const
    -> std::shared_ptr<graphics::Texture> override
  {
    auto texture = graphics::d3d12::Graphics::CreateTexture(desc);
    if (desc.debug_name == "Vortex.StaticSkyLight.ProcessedCubemap") {
      processed_sky = texture;
    }
    if (track_resources && texture) {
      tracked_textures.push_back(texture);
    }
    if (account_texture_allocations && texture) {
      ObserveAllocationPeak();
    }
    return texture;
  }
  std::vector<std::string> recorder_names;
  bool fail_next_exposure_recorder { false };
  bool fail_next_frame_recorder { false };
  bool fail_next_fallback_recorder { false };
  bool fail_status_recorder { false };
  std::string fail_recorder_name;
  bool defer_tonemap_recorders { false };
  std::vector<std::shared_ptr<const graphics::CommandList>>
    deferred_tonemap_recordings;
  bool fail_next_suitability_recorder { false };
  auto AcquireCommandRecorder(const graphics::QueueKey& queue,
    std::string_view name, bool immediate = true)
    -> std::unique_ptr<graphics::CommandRecorder,
      std::function<void(graphics::CommandRecorder*)>> override
  {
    recorder_names.emplace_back(name);
    if (!fail_recorder_name.empty() && name == fail_recorder_name) {
      return { nullptr, [](graphics::CommandRecorder*) { } };
    }
    if (fail_status_recorder && name == "Exposure status readback") {
      return { nullptr, [](graphics::CommandRecorder*) { } };
    }
    if (fail_next_suitability_recorder
      && name == "Vortex Exposure Suitability") {
      fail_next_suitability_recorder = false;
      return { nullptr, [](graphics::CommandRecorder*) { } };
    }
    if (fail_next_fallback_recorder && name == "Vortex Exposure Fallback") {
      fail_next_fallback_recorder = false;
      return { nullptr, [](graphics::CommandRecorder*) { } };
    }
    if (fail_next_frame_recorder && name == "Vortex Exposure Frame") {
      fail_next_frame_recorder = false;
      return { nullptr, [](graphics::CommandRecorder*) { } };
    }
    if (fail_next_exposure_recorder && name == "Vortex Exposure") {
      fail_next_exposure_recorder = false;
      return { nullptr, [](graphics::CommandRecorder*) { } };
    }
    const bool defer = defer_tonemap_recorders && name == "Vortex PostProcess";
    auto recorder = graphics::d3d12::Graphics::AcquireCommandRecorder(
      queue, name, immediate && !defer);
    if (defer && recorder) {
      deferred_tonemap_recordings.push_back(
        recorder->GetCommandListForInspection());
    }
    return recorder;
  }
};

class ExposureGpuTest : public graphics::d3d12::testing::ReadbackTestFixture {
protected:
  auto CheckOffscreenSharing(bool inside_frame) -> void;
  auto CheckSceneExposureRetry(bool inside_frame, bool late_failure = false)
    -> void;
  auto CheckFogViewRetirement(bool persistent, bool temporal) -> void;
  auto CreateBackend(const SerializedBackendConfig& config,
    const SerializedPathFinderConfig& paths)
    -> std::shared_ptr<graphics::d3d12::Graphics> override
  {
    return std::make_shared<ExposureFailureGraphics>(config, paths);
  }
  struct Signal {
    std::shared_ptr<const Texture> texture;
    ShaderVisibleIndex srv;
  };
  struct Snapshot {
    ExposureStateData state;
    std::array<std::uint32_t, 264> histogram;
  };

  auto BackendConfigJson() const -> std::string override
  {
    if (!CapturePath().empty())
      return R"({"enable_debug_layer":true,"frame_capture":{"provider":"renderdoc","init_mode":"search"}})";
    return R"({"enable_debug_layer":true})";
  }
  static auto CapturePath() -> std::string
  {
    char* value = nullptr;
    std::size_t size = 0U;
    if (_dupenv_s(&value, &size, "OXYGEN_EXPOSURE_CAPTURE") != 0 || !value)
      return {};
    const auto owned = std::unique_ptr<char, decltype(&std::free)>(value, &std::free);
    return owned.get();
  }
  auto BeginOptionalCapture() -> observer_ptr<FrameCaptureController>
  {
    const auto path = CapturePath();
    if (path.empty())
      return {};
    WaitForQueueIdle();
    const auto capture = Backend().GetFrameCaptureController();
    CHECK_F(capture && capture->IsAvailable());
    CHECK_F(capture->SetCaptureFileTemplate(path));
    CHECK_F(capture->StartCapture());
    return capture;
  }
  auto PathFinderConfigJson() const -> std::string override
  {
    return R"({"workspace_root_path":")" OXYGEN_EXPOSURE_WORKSPACE R"("})";
  }
  auto SetUp() -> void override
  {
    ReadbackTestFixture::SetUp();
    auto config = RendererConfig {};
    config.upload_queue_key = QueueKeyFor().get();
    renderer_ = std::make_unique<Renderer>(GetGraphicsShared(), config);
    pass_ = std::make_unique<postprocess::ExposurePass>(*renderer_);
    ctx_.current_view.view_id = ViewId { 1U };
    ctx_.current_view.view_state_handle
      = CompositionView::ViewStateHandle { 1U };
    ctx_.frame_slot = frame::Slot { 0U };
  }
  auto TearDown() -> void override
  {
    FlushBackend();
    registered_targets_.clear();
    last_state_.reset();
    pass_.reset();
    if (renderer_) {
      renderer_->OnShutdown();
      renderer_.reset();
    }
    ReadbackTestFixture::TearDown();
  }
  auto MakeSignal(std::uint32_t width, std::uint32_t height,
    std::span<const Pixel> pixels, std::uint32_t depth = 1U,
    Format format = Format::kRGBA32Float, bool bindless_texture = false)
    -> Signal
  {
    CHECK_F(pixels.size() == 1U || pixels.size() == width * height * depth);
    CHECK_F(format == Format::kRGBA32Float || format == Format::kRGBA16Float);
    const auto type
      = depth == 1U ? TextureType::kTexture2D : TextureType::kTexture3D;
    const auto stride = format == Format::kRGBA32Float ? 16U : 8U;
    auto texture = CreateRegisteredTexture({
      .width = width,
      .height = height,
      .depth = depth,
      .format = format,
      .texture_type = type,
      .debug_name = "ExposureFloatFixture",
      .is_shader_resource = true,
      .initial_state = ResourceStates::kCommon,
    });
    const auto pitch = ((width * stride + 255U) / 256U) * 256U;
    auto upload
      = CreateUploadBuffer(SizeBytes { std::uint64_t(pitch) * height * depth });
    std::vector<std::byte> bytes(std::size_t(pitch) * height * depth);
    for (std::uint32_t z = 0; z < depth; ++z)
      for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
          const auto& pixel
            = pixels[pixels.size() == 1U ? 0U : (z * height + y) * width + x];
          auto* destination
            = bytes.data() + (std::size_t(z) * height + y) * pitch + x * stride;
          if (format == Format::kRGBA32Float) {
            std::memcpy(destination, pixel.data(), 16U);
          } else {
            const std::array packed { data::HalfFloat { pixel[0] }.get(),
              data::HalfFloat { pixel[1] }.get(),
              data::HalfFloat { pixel[2] }.get(),
              data::HalfFloat { pixel[3] }.get() };
            std::memcpy(destination, packed.data(), 8U);
          }
        }
      }
    upload->Update(bytes.data(), bytes.size(), 0U);
    {
      auto recorder = AcquireRecorder("Exposure fixture upload");
      EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
      EnsureTracked(*recorder, texture, ResourceStates::kCommon);
      recorder->RequireResourceState(*texture, ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyBufferToTexture(*upload,
        {
          .buffer_offset = 0U,
          .buffer_row_pitch = pitch,
          .buffer_slice_pitch = std::uint64_t(pitch) * height,
          .dst_slice = { .width = width, .height = height, .depth = depth },
        },
        *texture);
      recorder->RequireResourceStateFinal(
        *texture, ResourceStates::kShaderResource);
    }
    WaitForQueueIdle();
    auto& allocator = renderer_->GetGraphics()->GetDescriptorAllocator();
    auto handle = bindless_texture
      ? allocator.AllocateBindless(oxygen::bindless::generated::kTexturesDomain,
          ResourceViewType::kTexture_SRV)
      : allocator.AllocateRaw(
          ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
    auto srv = allocator.GetShaderVisibleIndex(handle);
    const auto view = Backend().GetResourceRegistry().RegisterView(*texture,
      std::move(handle),
      TextureViewDescription {
        .format = format,
        .dimension = type,
      });
    CHECK_F(view->IsValid());
    return { std::move(texture), srv };
  }
  auto Uniform(float value, std::uint32_t width = 1U, std::uint32_t height = 1U)
    -> Signal
  {
    const auto pixels
      = std::array<Pixel, 1> { Pixel { value, value, value, 1.0F } };
    return MakeSignal(width, height, pixels);
  }
  template <typename T>
  auto PublishFixtureData(const T& value) -> ShaderVisibleIndex
  {
    auto buffer = CreateRegisteredBuffer({ .size_bytes = sizeof(T),
      .usage = BufferUsage::kNone,
      .memory = BufferMemory::kUpload,
      .debug_name = "Fog edge fixture bindings" });
    buffer->Update(&value, sizeof(T), 0U);
    auto& allocator = renderer_->GetGraphics()->GetDescriptorAllocator();
    auto handle = allocator.AllocateBindless(
      oxygen::bindless::generated::kGlobalSrvDomain,
      ResourceViewType::kStructuredBuffer_SRV);
    const auto index = allocator.GetShaderVisibleIndex(handle);
    Backend().GetResourceRegistry().RegisterView(*buffer, std::move(handle),
      BufferViewDescription {
        .view_type = ResourceViewType::kStructuredBuffer_SRV,
        .range = { 0U, sizeof(T) },
        .stride = sizeof(T) });
    return index;
  }
  auto RunToneProbe(std::span<const std::byte> inputs_data,
    std::uint32_t record_count, std::uint32_t mode = 0U,
    bool capture_enabled = true) -> std::vector<std::array<float, 8>>
  {
    if (mode == 16384U) {
      CHECK_EQ_F(record_count, 64U);
    }
    std::ifstream shader(
      OXYGEN_EXPOSURE_TONE_PROBE, std::ios::binary | std::ios::ate);
    CHECK_F(shader.good());
    const auto bytes = static_cast<std::size_t>(shader.tellg());
    CHECK_GT_F(bytes, 0U);
    CHECK_EQ_F(bytes % sizeof(std::uint32_t), 0U);
    std::vector<std::uint32_t> code(bytes / sizeof(std::uint32_t));
    shader.seekg(0);
    shader.read(reinterpret_cast<char*>(code.data()),
      static_cast<std::streamsize>(bytes));
    CHECK_F(shader.good());
    static_cast<ExposureFailureGraphics&>(Backend()).tone_probe
      = std::make_shared<ShaderByteCode<std::vector<std::uint32_t>>>(
        std::move(code));

    const auto input_size = inputs_data.size_bytes();
    auto inputs = CreateRegisteredBuffer({ .size_bytes = input_size,
      .usage = BufferUsage::kNone,
      .memory = BufferMemory::kUpload,
      .debug_name = "Tone bound arithmetic inputs" });
    inputs->Update(inputs_data.data(), input_size, 0U);
    auto output = CreateRegisteredBuffer({ .size_bytes = record_count * 32U,
      .usage = BufferUsage::kStorage,
      .memory = BufferMemory::kDeviceLocal,
      .debug_name = "Tone bound arithmetic output" });
    auto& allocator = renderer_->GetGraphics()->GetDescriptorAllocator();
    auto input_handle = allocator.AllocateBindless(
      oxygen::bindless::generated::kGlobalSrvDomain,
      ResourceViewType::kStructuredBuffer_SRV);
    const auto input_slot = allocator.GetShaderVisibleIndex(input_handle);
    Backend().GetResourceRegistry().RegisterView(*inputs,
      std::move(input_handle),
      BufferViewDescription {
        .view_type = ResourceViewType::kStructuredBuffer_SRV,
        .range = { 0U, input_size },
        .stride = 16U });
    auto output_handle = allocator.AllocateRaw(
      ResourceViewType::kRawBuffer_UAV, DescriptorVisibility::kShaderVisible);
    const auto output_slot = allocator.GetShaderVisibleIndex(output_handle);
    Backend().GetResourceRegistry().RegisterView(*output,
      std::move(output_handle),
      BufferViewDescription { .view_type = ResourceViewType::kRawBuffer_UAV,
        .range = { 0U, record_count * 32U },
        .stride = 0U });
    const auto constants = PublishFixtureData(std::array<std::uint32_t, 4> {
      input_slot.get(), output_slot.get(), record_count, mode });
    const auto pipeline
      = ComputePipelineDesc::Builder {}
          .SetComputeShader(ShaderRequest { .stage = ShaderType::kCompute,
            .source_path = "Tests/ToneBoundsProbe.hlsl",
            .entry_point = "CS" })
          .SetRootBindings(ExposureProbeRootBindings())
          .SetDebugName("Tone bound arithmetic probe")
          .Build();
    auto readback = GetReadbackManager()->CreateBufferReadback(
      "Tone bound arithmetic results");
    const auto capture = capture_enabled
      ? BeginOptionalCapture()
      : observer_ptr<FrameCaptureController> {};
    const auto probe_view = ViewConstants::GpuData {};
    auto probe_view_buffer
      = CreateUploadBuffer(SizeBytes { 256U }, BufferUsage::kConstant);
    probe_view_buffer->Update(&probe_view, sizeof(probe_view), 0U);
    {
      auto recorder = AcquireRecorder("Tone bound arithmetic");
      EnsureTracked(*recorder, inputs, ResourceStates::kGenericRead);
      EnsureTracked(*recorder, output, ResourceStates::kCommon);
      recorder->RequireResourceState(*output, ResourceStates::kUnorderedAccess);
      recorder->FlushBarriers();
      recorder->SetPipelineState(pipeline);
      recorder->SetComputeRootConstantBufferView(
        static_cast<std::uint32_t>(
          oxygen::bindless::generated::d3d12::RootParam::kViewConstants),
        probe_view_buffer->GetGPUVirtualAddress());
      const auto root = static_cast<std::uint32_t>(
        oxygen::bindless::generated::d3d12::RootParam::kRootConstants);
      recorder->SetComputeRoot32BitConstant(root, 0U, 0U);
      recorder->SetComputeRoot32BitConstant(root, constants.get(), 1U);
      recorder->Dispatch(
        static_cast<std::uint32_t>((record_count + 63U) / 64U), 1U, 1U);
      CHECK_F(
        readback->EnqueueCopy(*recorder, *output, { 0U, record_count * 32U })
          .has_value());
    }
    if (capture)
      EXPECT_TRUE(capture->EndCapture());
    const auto mapped = readback->MapNow();
    CHECK_F(mapped.has_value());
    std::vector<std::array<float, 8>> result(record_count);
    std::memcpy(
      result.data(), mapped->Bytes().data(), result.size() * sizeof(result[0]));
    return result;
  }
  auto ReadFloatTexture(const Texture& texture, bool allow_half = false)
    -> std::vector<Pixel>
  {
    const bool half = texture.GetDescriptor().format == Format::kRGBA16Float;
    CHECK_F(texture.GetDescriptor().format == Format::kRGBA32Float
      || (allow_half && half));
    auto readback
      = GetReadbackManager()->CreateTextureReadback("Fog edge output");
    {
      auto recorder = AcquireRecorder("Fog edge readback");
      CHECK_F(recorder->AdoptKnownResourceState(texture));
      CHECK_F(readback->EnqueueCopy(*recorder, texture, {}).has_value());
    }
    const auto mapped = readback->MapNow();
    CHECK_F(mapped.has_value());
    const auto& desc = texture.GetDescriptor();
    std::vector<Pixel> pixels(desc.width * desc.height * desc.depth);
    for (unsigned z = 0; z < desc.depth; ++z)
      for (unsigned y = 0; y < desc.height; ++y)
        for (unsigned x = 0; x < desc.width; ++x) {
          auto& pixel = pixels[(z * desc.height + y) * desc.width + x];
          const auto* bytes = mapped->Data()
            + z * mapped->Layout().slice_pitch.get()
            + y * mapped->Layout().row_pitch.get()
            + x * (half ? 8U : sizeof(Pixel));
          if (half) {
            std::array<std::uint16_t, 4> packed;
            std::memcpy(packed.data(), bytes, sizeof(packed));
            for (unsigned c = 0; c < 4; ++c)
              pixel[c] = data::HalfFloat { packed[c] }.ToFloat();
          } else
            std::memcpy(pixel.data(), bytes, sizeof(pixel));
        }
    return pixels;
  }
  template <typename Payload>
  auto Read(const Buffer& source, ResourceStates final_state) -> Payload
  {
    auto readback
      = GetReadbackManager()->CreateBufferReadback("Exposure fixture readback");
    {
      auto recorder = AcquireRecorder("Exposure fixture copy");
      recorder->BeginTrackingResourceState(source, final_state, false);
      const auto ticket
        = readback->EnqueueCopy(*recorder, source, { 0U, sizeof(Payload) });
      CHECK_F(ticket.has_value());
      recorder->RequireResourceStateFinal(source, final_state);
    }
    const auto mapped = readback->MapNow();
    CHECK_F(mapped.has_value());
    Payload result {};
    std::memcpy(&result, mapped->Bytes().data(), sizeof(result));
    return result;
  }
  auto Run(const Signal& signal, scene::ExposureSettings settings = {},
    float dt = 0.0F, const Signal* mask = nullptr, float inverse_p = 1.0F,
    bool metering_available = true,
    std::optional<ExposureTransitionToken> transition = {},
    std::optional<float> camera_ev = {}, bool temporary_unit = false)
    -> Snapshot
  {
    settings.key = 12.5F;
    auto authored = PostProcessConfig { .exposure = settings };
    authored.temporary_unit_exposure = temporary_unit;
    const auto resolved
      = ResolvedPostProcessConfig::Resolve(authored, camera_ev, ++sequence_);
    CHECK_F(resolved.has_value());
    const auto& config = *resolved;
    ctx_.frame_sequence = frame::SequenceNumber { sequence_ };
    ctx_.delta_time = dt;
    const auto result = pass_->Execute(ctx_, config,
      {
        .scene_signal = signal.texture.get(),
        .scene_signal_srv = signal.srv,
        .metering_mask = mask ? mask->texture.get() : nullptr,
        .metering_mask_srv = mask ? mask->srv : kInvalidShaderVisibleIndex,
        .one_over_pre_exposure = inverse_p,
        .metering_available = metering_available,
        .transition = transition,
      });
    CHECK_F(result.executed);
    last_state_ = result.state;
    auto snapshot
      = Snapshot { .state = Read<ExposureStateData>(*result.exposure_buffer,
                     ResourceStates::kShaderResource) };
    if (result.histogram_buffer) {
      snapshot.histogram = Read<std::array<std::uint32_t, 264>>(
        *result.histogram_buffer, ResourceStates::kCommon);
    }
    return snapshot;
  }
  auto ServicePixel(PostProcessService& service, const Signal& signal,
    scene::ExposureSettings settings = {}, bool diagnostic = false,
    float dt = 0.0F, std::function<void()> before_execute = {},
    engine::ToneMapper tone_mapper = engine::ToneMapper::kNone,
    bool start_new_frame = true,
    const PostProcessService::PreparedExposure* prepared = nullptr,
    const Signal* fallback = nullptr,
    postprocess::ExposurePass::FrameLease checked_resolution = {},
    const Signal* bloom = nullptr, float bloom_intensity = 0.0F) -> float
  {
    settings.key = 12.5F;
    if (start_new_frame)
      ++sequence_;
    ctx_.frame_sequence = frame::SequenceNumber { sequence_ };
    ctx_.delta_time = dt;
    ctx_.render_mode = diagnostic ? RenderMode::kWireframe : RenderMode::kSolid;
    service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    [[maybe_unused]] const auto& accepted = service.CaptureViewExposureSettings(
      ctx_.current_view.view_id, ctx_.current_view.view_state_handle, settings,
      {}, diagnostic, ctx_.GetScene());
    auto config = PostProcessConfig {};
    config.enable_bloom = bloom != nullptr;
    config.bloom_intensity = bloom_intensity;
    config.tone_mapper = tone_mapper;
    config.gamma = 1.0F;
    service.SetResolvedConfig(service.BuildPassConfig(
      config, ctx_.current_view.view_id, ctx_.current_view.view_state_handle));
    if (before_execute)
      before_execute();
    auto output_desc = TextureDesc {};
    output_desc.debug_name = "ExposureServiceOutput";
    output_desc.width = output_desc.height = 4U;
    output_desc.format = Format::kRGBA32Float;
    output_desc.is_render_target = output_desc.is_shader_resource = true;
    output_desc.initial_state = ResourceStates::kCommon;
    auto output = CreateRegisteredTexture(output_desc);
    auto framebuffer = Backend().CreateFramebuffer(
      FramebufferDesc {}.AddColorAttachment(output));
    auto textures
      = SceneTextures(Backend(), SceneTexturesConfig { .extent = { 4U, 4U } });
    service.Execute(ctx_.current_view.view_id, ctx_, textures,
      {
        .scene_signal = signal.texture.get(),
        .post_target = observer_ptr<const Framebuffer> { framebuffer.get() },
        .scene_signal_srv = signal.srv,
        .bloom_texture_srv = bloom ? bloom->srv : kInvalidShaderVisibleIndex,
        .scene_fallback = fallback ? fallback->texture.get() : nullptr,
        .scene_fallback_srv
        = fallback ? fallback->srv : kInvalidShaderVisibleIndex,
        .checked_resolution = std::move(checked_resolution),
      },
      prepared);
    if (!service.GetLastExecutionState().tonemap_executed)
      return std::numeric_limits<float>::quiet_NaN();
    auto readback
      = GetReadbackManager()->CreateTextureReadback("Exposure service pixel");
    {
      auto recorder = AcquireRecorder("Exposure service pixel readback");
      CHECK_F(recorder->AdoptKnownResourceState(*output));
      const auto ticket = readback->EnqueueCopy(*recorder, *output,
        {
          .src_slice
          = { .x = 1U, .y = 0U, .width = 1U, .height = 1U, .depth = 1U },
        });
      CHECK_F(ticket.has_value());
    }
    const auto mapped = readback->MapNow();
    CHECK_F(mapped.has_value());
    Pixel pixel {};
    std::memcpy(pixel.data(), mapped->Data(), sizeof(pixel));
    return pixel[0];
  }

  auto Qualify(const Signal& signal, bool meter,
    scene::ExposureSettings settings = {}, const Signal* mask = nullptr,
    bool coverage = false) -> HdrSuitabilityData
  {
    settings.mode = engine::ExposureMode::kManual;
    const auto config = SharedConfig(settings);
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    const auto frame = pass_->ResolveFrame(ctx_, config, { .use_fp32 = true });
    CHECK_NOTNULL_F(frame.get());
    CHECK_F(RecordShared(signal, config).executed);
    const std::array products { postprocess::ExposurePass::HdrProduct {
      .texture = signal.texture.get(),
      .srv = signal.srv,
      .id = 1U,
      .metering = meter,
      .coverage = coverage } };
    CHECK_F(pass_->EvaluateFp16Products(ctx_, frame, config, products,
      { .metering_mask = mask ? mask->texture.get() : nullptr,
        .metering_mask_srv = mask ? mask->srv : kInvalidShaderVisibleIndex }));
    return Read<HdrSuitabilityData>(
      *frame->suitability_buffer, ResourceStates::kShaderResource);
  }

  auto EligibilityStep(const Signal& signal,
    const ResolvedPostProcessConfig& config, std::uint64_t sequence,
    std::uint64_t layout = 7U, std::uint32_t expected = 1024U,
    bool invalidate_previous = false,
    const postprocess::ExposurePass::Source* source = nullptr,
    std::optional<ExposureTransitionToken> transition = {},
    bool metering_available = true, bool capture_eligibility = false)
    -> std::pair<ExposureStateData, ExposureCompletedStatus>
  {
    ctx_.frame_sequence = frame::SequenceNumber { sequence };
    const auto lifetime = transition ? transition->lifetime : 0U;
    const auto frame = pass_->ResolveFrame(ctx_, config,
      { .use_fp32 = true,
        .source = source,
        .transition = transition,
        .lifetime = lifetime });
    CHECK_NOTNULL_F(frame.get());
    const auto solved = pass_->Execute(ctx_, config,
      { .scene_signal = signal.texture.get(),
        .scene_signal_srv = signal.srv,
        .metering_available = metering_available,
        .transition = transition,
        .source = source,
        .lifetime = lifetime });
    CHECK_F(solved.executed);
    const auto before = ReadState(solved);
    EXPECT_EQ(before.flags & 256U, 0U);
    EXPECT_EQ(before.fp16_eligible_streak, 0U);
    EXPECT_FALSE(pass_->FinalizeFp16Suitability(ctx_, frame,
      { .product_layout_revision = layout, .expected_products = expected }));
    const std::array products { postprocess::ExposurePass::HdrProduct {
      .texture = signal.texture.get(),
      .srv = signal.srv,
      .id = 11U,
      .metering = true } };
    const auto capture = capture_eligibility
      ? BeginOptionalCapture()
      : observer_ptr<FrameCaptureController> {};
    CHECK_F(pass_->EvaluateFp16Products(ctx_, frame, config, products, {}));
    CHECK_F(pass_->FinalizeFp16Suitability(ctx_, frame,
      { .product_layout_revision = layout,
        .expected_products = expected,
        .invalidate_previous = invalidate_previous }));
    if (capture)
      EXPECT_TRUE(capture->EndCapture());
    const auto state = ReadState(solved);
    const auto status = Read<ExposureCompletedStatus>(
      *solved.state->status_buffer, ResourceStates::kCopySource);
    EXPECT_EQ(std::memcmp(&before, &state, 24U), 0);
    EXPECT_EQ(before.settings_revision, state.settings_revision);
    EXPECT_EQ(before.fallback_reason, state.fallback_reason);
    EXPECT_EQ(before.requested_generation, state.requested_generation);
    EXPECT_EQ(before.applied_generation, state.applied_generation);
    EXPECT_EQ(before.frame_sequence, state.frame_sequence);
    EXPECT_EQ((before.flags ^ state.flags) & ~(32U | 256U), 0U);
    EXPECT_EQ(status.fp16_eligible_streak, state.fp16_eligible_streak);
    EXPECT_EQ(status.product_layout_revision, state.product_layout_revision);
    EXPECT_EQ(status.candidate_state_generation, state.frame_sequence);
    EXPECT_EQ(status.frame_sequence, state.frame_sequence);
    EXPECT_EQ(status.settings_revision, state.settings_revision);
    EXPECT_EQ(status.requested_generation, state.requested_generation);
    EXPECT_EQ(status.applied_generation, state.applied_generation);
    EXPECT_EQ(
      status.view_state_identity[0], static_cast<std::uint32_t>(lifetime));
    EXPECT_EQ(status.view_state_identity[1],
      static_cast<std::uint32_t>(lifetime >> 32U));
    EXPECT_EQ(status.reserved, 0U);
    EXPECT_EQ(status.flags & 4U, state.fp16_eligible_streak >= 2U ? 4U : 0U);
    CHECK_F(pass_->FinalizeFp16Suitability(ctx_, frame,
      { .product_layout_revision = layout,
        .expected_products = expected,
        .invalidate_previous = invalidate_previous }));
    const auto duplicate = ReadState(solved);
    EXPECT_EQ(std::memcmp(&state, &duplicate, sizeof(state)), 0);
    return { state, status };
  }

  auto ResetHistory() -> void
  {
    pass_->RemoveViewState(ctx_.current_view.view_state_handle);
  }
  auto PublishExposureOwner(engine::FrameContext& frame, const ViewId intent_id,
    CompositionView::ViewStateHandle handle, scene::ExposureSettings settings,
    ViewId source = kInvalidViewId, bool diagnostic = false,
    std::optional<ShaderDebugMode> debug_override = {}) -> ViewId
  {
    auto texture = CreateRegisteredTexture(TextureDesc { .width = 4U,
      .height = 4U,
      .format = Format::kRGBA32Float,
      .is_render_target = true,
      .initial_state = ResourceStates::kCommon });
    auto target = Backend().CreateFramebuffer(
      FramebufferDesc {}.AddColorAttachment(texture));
    registered_targets_.push_back(target);
    auto view = CompositionView {};
    view.id = intent_id;
    view.view_state_handle = handle;
    view.render_settings.exposure = std::move(settings);
    view.exposure_source_view_id = source;
    view.force_wireframe = diagnostic;
    view.render_settings.shader_debug_mode = debug_override;
    return renderer_->PublishRuntimeCompositionView(frame,
      { .composition_view = view,
        .render_target = observer_ptr { target.get() } });
  }
  auto SharedConfig(scene::ExposureSettings settings = {},
    std::optional<float> camera_ev = {}, std::uint64_t revision = 1U)
    -> ResolvedPostProcessConfig
  {
    settings.key = 12.5F;
    const auto config = ResolvedPostProcessConfig::Resolve(
      PostProcessConfig { .exposure = settings }, camera_ev, revision);
    CHECK_F(config.has_value());
    return *config;
  }
  auto RecordShared(const Signal& signal,
    const ResolvedPostProcessConfig& config,
    const postprocess::ExposurePass::Source* source = nullptr,
    std::optional<ExposureTransitionToken> token = {},
    std::uint64_t lifetime = 0U) -> postprocess::ExposurePass::Result
  {
    return pass_->Execute(ctx_, config,
      { .scene_signal = signal.texture.get(),
        .scene_signal_srv = signal.srv,
        .transition = token,
        .source = source,
        .lifetime = lifetime });
  }
  auto ReadState(const postprocess::ExposurePass::Result& result)
    -> ExposureStateData
  {
    CHECK_NOTNULL_F(result.exposure_buffer);
    return Read<ExposureStateData>(
      *result.exposure_buffer, ResourceStates::kShaderResource);
  }
  auto OwnedExposureService() -> PostProcessService&
  {
    if (!vortex::testing::RendererPublicationProbe::GetSceneRenderer(
          *renderer_)) {
      auto texture = CreateRegisteredTexture(TextureDesc { .width = 4U,
        .height = 4U,
        .format = Format::kRGBA32Float,
        .is_render_target = true,
        .initial_state = ResourceStates::kCommon });
      auto target = Backend().CreateFramebuffer(
        FramebufferDesc {}.AddColorAttachment(texture));
      registered_targets_.push_back(target);
      auto params = ResolvedView::Params {};
      params.view_config.viewport = { .width = 4.0F, .height = 4.0F };
      auto session
        = renderer_->ForSinglePassHarness()
            .SetFrameSession({ .frame_slot = frame::Slot { 0U },
              .frame_sequence = frame::SequenceNumber { 1U },
              .delta_time_seconds = 0.0F })
            .SetResolvedView(
              { .view_id = ViewId { 1000U }, .value = ResolvedView { params } })
            .SetOutputTarget({ .framebuffer = observer_ptr { target.get() } })
            .Finalize();
      CHECK_F(session.has_value());
    }
    return *vortex::testing::RendererPublicationProbe::GetPostProcessService(
      *vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_));
  }
  auto StartSharedServiceView(PostProcessService& service,
    engine::FrameContext& frame, scene::ExposureSettings consumer_settings = {})
    -> ViewId
  {
    auto source_settings = scene::ExposureSettings {};
    source_settings.key = 12.5F;
    source_settings.mode = engine::ExposureMode::kManual;
    source_settings.manual_ev = 4.0F;
    const auto root = PublishExposureOwner(frame, ViewId { 50U },
      CompositionView::ViewStateHandle { 50U }, source_settings);
    ctx_.current_view.view_id = root;
    ctx_.current_view.view_state_handle
      = CompositionView::ViewStateHandle { 50U };
    ctx_.current_view.exposure_view_state_handle
      = CompositionView::kInvalidViewStateHandle;
    ServicePixel(service, Uniform(.25F, 4U, 4U), source_settings);
    consumer_settings.key = 12.5F;
    const auto consumer = PublishExposureOwner(frame, ViewId { 60U },
      CompositionView::ViewStateHandle { 60U }, consumer_settings,
      ViewId { 50U });
    ctx_.current_view.view_id = consumer;
    ctx_.current_view.view_state_handle
      = CompositionView::ViewStateHandle { 60U };
    ctx_.current_view.exposure_view_id = root;
    ctx_.current_view.exposure_view_state_handle
      = CompositionView::ViewStateHandle { 50U };
    ServicePixel(service, Uniform(8.0F, 4U, 4U), consumer_settings);
    return consumer;
  }
  std::unique_ptr<Renderer> renderer_;
  std::unique_ptr<vortex::testing::FakeAssetLoader> owned_asset_loader_;
  std::unique_ptr<::testing::NiceMock<ExposureTestEngine>> owned_test_engine_;
  std::vector<std::shared_ptr<Framebuffer>> registered_targets_;
  std::unique_ptr<postprocess::ExposurePass> pass_;
  RenderContext ctx_;
  std::uint64_t sequence_ { 0U };
  postprocess::ExposurePass::StateLease last_state_;
};

NOLINT_TEST_F(ExposureGpuTest, CompositionConstantsSurviveLaterSubmission)
{
  static_cast<void>(OwnedExposureService());
  const std::array sources { Uniform(.25F), Uniform(.5F), Uniform(.75F) };
  auto output = CreateRegisteredTexture({ .width = 1,
    .height = 1,
    .format = Format::kRGBA32Float,
    .is_render_target = true,
    .initial_state = ResourceStates::kCommon });
  auto target = Backend().CreateFramebuffer(
    FramebufferDesc {}.AddColorAttachment(output));
  engine::FrameContext frame;
  frame.SetFrameSlot(
    frame::Slot { 0 }, engine::internal::EngineTagFactory::Get());
  frame.SetFrameSequenceNumber(
    frame::SequenceNumber { 1 }, engine::internal::EngineTagFactory::Get());
  Backend().BeginFrame(frame::SequenceNumber { 1 }, frame::Slot { 0 });
  renderer_->OnFrameStart(observer_ptr { &frame });
  const ViewPort viewport { .width = 1, .height = 1 };
  auto first = CompositionSubmission {};
  first.composite_target = target;
  for (unsigned i = 0; i < 2; ++i)
    first.tasks.push_back(CompositingTask::MakeTextureBlend(
      std::const_pointer_cast<Texture>(sources[i].texture), viewport, 1));
  renderer_->RegisterComposition(std::move(first), {});
  auto last = CompositionSubmission {};
  last.composite_target = target;
  last.tasks.push_back(CompositingTask::MakeTextureBlend(
    std::const_pointer_cast<Texture>(sources[2].texture), viewport, 1));
  renderer_->RegisterComposition(std::move(last), {});
  const auto capture = BeginOptionalCapture();
  auto loop = co::testing::TestEventLoop {};
  co::Run(loop, [&]() -> co::Co<void> {
    co_await renderer_->OnCompositing(observer_ptr { &frame });
  });
  renderer_->OnFrameEnd(observer_ptr { &frame });
  Backend().EndFrame(frame::SequenceNumber { 1 }, frame::Slot { 0 });
  WaitForQueueIdle();
  if (capture)
    EXPECT_TRUE(capture->EndCapture());
  const auto pixel = ReadFloatTexture(*output);
  ASSERT_EQ(pixel.size(), 1U);
  EXPECT_EQ(pixel[0], (Pixel { .75F, .75F, .75F, 1 }));
}

NOLINT_TEST_F(ExposureGpuTest, NativeExposureTimelineRecordsMeteringScopes)
{
  pass_.reset();
  renderer_->OnShutdown();
  auto config = RendererConfig {};
  config.upload_queue_key = QueueKeyFor().get();
  renderer_ = std::make_unique<Renderer>(
    GetGraphicsShared(), config,
    kPhase1DefaultRuntimeCapabilityFamilies
      | RendererCapabilityFamily::kDiagnosticsAndProfiling);
  pass_ = std::make_unique<postprocess::ExposurePass>(*renderer_);
  auto& diagnostics = renderer_->GetDiagnosticsService();
  diagnostics.SetEnabledFeatures(DiagnosticsFeature::kGpuTimeline);
  diagnostics.SetGpuTimelineEnabled(true);
  const auto path = std::filesystem::path { OXYGEN_EXPOSURE_WORKSPACE }
    / "out/build-ninja/analysis/vortex/exposure-lightbench/slice51"
#ifdef NDEBUG
    / "native-timeline-smoke-Release.json";
#else
    / "native-timeline-smoke-Debug.json";
#endif
  const auto signal = Uniform(.25F, 4U, 4U);
  auto frame_context = engine::FrameContext {};
  for (unsigned sequence = 1U; sequence <= 3U; ++sequence) {
    const auto slot = frame::Slot { sequence - 1U };
    const auto frame_sequence = frame::SequenceNumber { sequence };
    frame_context.SetFrameSequenceNumber(
      frame_sequence, engine::internal::EngineTagFactory::Get());
    frame_context.SetFrameSlot(slot, engine::internal::EngineTagFactory::Get());
    Backend().BeginFrame(frame_sequence, slot);
    renderer_->OnFrameStart(observer_ptr { &frame_context });
    if (sequence == 1U) {
      ASSERT_TRUE(diagnostics.RequestGpuTimelineRecording(path, 2U));
    }
    if (sequence <= 2U) {
      ctx_.frame_slot = slot;
      pass_->OnFrameStart(frame_sequence, slot);
      const auto snapshot = Run(signal);
      EXPECT_NEAR(snapshot.state.displayed_scale, .72F, 2e-4F);
    }
    auto loop = co::testing::TestEventLoop {};
    co::Run(loop, [&]() -> co::Co<void> {
      co_await renderer_->OnCompositing(observer_ptr { &frame_context });
    });
    renderer_->OnFrameEnd(observer_ptr { &frame_context });
    Backend().EndFrame(frame_sequence, slot);
    WaitForQueueIdle();
  }
  auto stream = std::ifstream(path);
  const auto report = nlohmann::json::parse(stream);
  EXPECT_EQ(report.at("complete"), true);
  EXPECT_EQ(report.at("timing_valid"), true);
  ASSERT_EQ(report.at("frames").size(), 2U);
  for (const auto& frame : report.at("frames")) {
    auto names = std::unordered_set<std::string> {};
    for (const auto& scope : frame.at("scopes")) {
      EXPECT_EQ(scope.at("valid"), true);
      EXPECT_GT(scope.at("duration_ms").get<double>(), 0.0);
      names.insert(scope.at("name").get<std::string>());
    }
    EXPECT_TRUE(names.contains("Vortex.Frame"));
    EXPECT_TRUE(names.contains("Vortex.PostProcess.Exposure.MeterAndAdapt"));
    EXPECT_TRUE(names.contains("Vortex.PostProcess.Exposure.Histogram"));
    EXPECT_TRUE(names.contains("Vortex.PostProcess.Exposure.Solve"));
  }
  RecordProperty("native_timeline_report", path.string());
}

NOLINT_TEST_F(ExposureGpuTest, ConservedTwoBinMassAndIndependentMeter)
{
  auto settings = scene::ExposureSettings {};
  settings.low_percentile = 0.0F;
  settings.high_percentile = 1.0F;
  const auto result = Run(Uniform(0.375F, 17U, 19U), settings);
  // Independent double-precision scatter at L=3/8, window [-12,13].
  const double x = (std::log2(0.375) + 12.0) * 255.0 / 25.0;
  const auto bin = static_cast<unsigned>(std::floor(x));
  const auto upper
    = static_cast<unsigned>(std::floor(4095.0 * (x - bin) + 0.5));
  EXPECT_EQ(result.histogram[bin], 323U * (4095U - upper));
  EXPECT_EQ(result.histogram[bin + 1U], 323U * upper);
  EXPECT_EQ(std::accumulate(
              result.histogram.begin(), result.histogram.begin() + 256, 0U),
    323U * 4095U);
  const double expected_log = -12.0 + (bin + upper / 4095.0) * 25.0 / 255.0;
  EXPECT_NEAR(
    result.state.raw_metered_ev, expected_log - std::log2(0.18), 2e-4);
  EXPECT_NEAR(std::log2(result.state.displayed_scale),
    std::log2(0.18) - expected_log, 2e-4);
  EXPECT_EQ(result.histogram[256], 323U);
  EXPECT_EQ(result.histogram[257], 323U);
  EXPECT_EQ(result.histogram[260], 0U);
}

NOLINT_TEST_F(ExposureGpuTest, MaximumGridMassRemainsBounded)
{
  const auto result = Run(Uniform(0.25F, 1024U, 513U));
  EXPECT_EQ(std::accumulate(
              result.histogram.begin(), result.histogram.begin() + 256, 0U),
    1073479680U);
  EXPECT_EQ(result.histogram[256], 262144U);
  EXPECT_EQ(result.histogram[257], 262144U);
}

NOLINT_TEST_F(ExposureGpuTest, DarkValidAndZeroMaskInvalidRemainDistinct)
{
  const auto dark = Uniform(0.0F);
  const auto zero_mask = Uniform(0.0F);
  const auto result = Run(dark);
  EXPECT_EQ(result.histogram[258], 1U);
  EXPECT_EQ(result.histogram[261], 1U);
  EXPECT_EQ(result.histogram[0], 0U);
  EXPECT_EQ(result.state.flags & 31U, 27U);
  EXPECT_FLOAT_EQ(result.state.displayed_scale, 64.0F);
  const auto invalid = Run(dark, {}, 1.0F, &zero_mask);
  EXPECT_EQ(invalid.histogram[257], 0U);
  EXPECT_EQ(invalid.state.flags & 12U, 0U);
  EXPECT_EQ(invalid.state.fallback_reason, 2U);
  EXPECT_EQ(invalid.state.displayed_scale, result.state.displayed_scale);
}

NOLINT_TEST_F(ExposureGpuTest, NonfiniteSamplesAreRejectedAndNeverBlack)
{
  const auto nan = std::numeric_limits<float>::quiet_NaN();
  const auto inf = std::numeric_limits<float>::infinity();
  const auto pixels = std::array { Pixel { nan, 0, 0, 1 },
    Pixel { inf, 0, 0, 1 }, Pixel { .25F, .25F, .25F, 1 } };
  const auto result = Run(MakeSignal(3U, 1U, pixels));
  EXPECT_EQ(result.histogram[256], 1U);
  EXPECT_EQ(result.histogram[257], 1U);
  EXPECT_EQ(result.histogram[258], 0U);
  EXPECT_EQ(result.histogram[260], 2U);
  EXPECT_NEAR(result.state.raw_metered_ev, std::log2(.25 / .18), 2e-4);
}

NOLINT_TEST_F(ExposureGpuTest, TinyPercentileIntervalRetainsItsContainingBin)
{
  auto settings = scene::ExposureSettings {};
  settings.low_percentile = std::numeric_limits<float>::denorm_min();
  settings.high_percentile = settings.low_percentile * 2.0F;
  const auto result = Run(Uniform(.25F, 512U, 512U), settings);
  EXPECT_EQ(result.state.flags & 12U, 12U);
  EXPECT_NEAR(result.state.raw_metered_ev, std::log2(.25 / .18), 2e-4);
}

NOLINT_TEST_F(ExposureGpuTest, ZeroTargetRestoresImmediatelyFromLastValidMeter)
{
  auto settings = scene::ExposureSettings {};
  const auto signal = Uniform(.25F);
  const auto initial = Run(signal, settings);
  const auto zero_mask = Uniform(0.0F);
  settings.target_luminance = 0.0F;
  const auto black = Run(signal, settings, 0.0F, &zero_mask);
  EXPECT_EQ(black.state.displayed_scale, 0.0F);
  EXPECT_EQ(black.state.latent_scale, initial.state.latent_scale);
  settings.target_luminance = .36F;
  const auto restored = Run(signal, settings, 0.0F, &zero_mask);
  EXPECT_NEAR(restored.state.displayed_scale, 1.44F, 2e-5F);
  EXPECT_EQ(restored.state.flags & 12U, 0U);
}

NOLINT_TEST_F(ExposureGpuTest, ZeroSpeedAndPauseFreezeOrdinaryAdaptation)
{
  auto settings = scene::ExposureSettings {};
  const auto signal = Uniform(.25F);
  const auto initial = Run(signal, settings);
  settings.compensation_ev = -10.0F;
  EXPECT_EQ(
    Run(signal, settings, 0.0F).state.latent_scale, initial.state.latent_scale);
  settings.speed_up = 0.0F;
  EXPECT_EQ(Run(signal, settings, 10.0F).state.latent_scale,
    initial.state.latent_scale);
  settings.compensation_ev = 10.0F;
  settings.speed_down = 0.0F;
  EXPECT_EQ(Run(signal, settings, 10.0F).state.latent_scale,
    initial.state.latent_scale);
}

NOLINT_TEST_F(
  ExposureGpuTest, HybridCrossingMatchesElapsedTimeAcrossFrameSchedules)
{
  const auto signal = Uniform(.25F);
  // Starting target is .72, changed target is .72*2^-8. SpeedUp=3 EV/s,
  // D=1.5: at t=3, remaining error is 1.5*exp(-5/3) stops.
  const double expected = std::log2(.72) - 8.0 + 1.5 * std::exp(-5.0 / 3.0);
  for (const unsigned frequency : { 30U, 60U, 120U }) {
    ResetHistory();
    auto settings = scene::ExposureSettings {};
    Run(signal, settings);
    settings.compensation_ev = -8.0F;
    Snapshot result {};
    for (unsigned frame = 0; frame < 3U * frequency; ++frame) {
      result = Run(signal, settings, 1.0F / frequency);
    }
    EXPECT_NEAR(std::log2(result.state.latent_scale), expected, 5e-4)
      << frequency;
  }
}
NOLINT_TEST_F(
  ExposureGpuTest, HugeFiniteSpeedAndDistanceDoNotOverflowTheExponent)
{
  const auto signal = Uniform(.25F);
  auto settings = scene::ExposureSettings {};
  settings.min_ev = -16.0F;
  settings.max_ev = 16.0F;
  settings.compensation_ev = 8.0F;
  settings.target_luminance = .25F;
  const auto initial = Run(signal, settings);
  settings.compensation_ev = -8.0F;
  settings.speed_up = std::exp2(127.0F);
  settings.transition_distance = std::exp2(127.0F);
  const auto result = Run(signal, settings, 2.0F);
  const double expected = std::log2(double(initial.state.latent_scale)) - 16.0
    + 16.0 * std::exp(-2.0);
  EXPECT_NEAR(std::log2(result.state.latent_scale), expected, 5e-4);
}

NOLINT_TEST_F(ExposureGpuTest, TinySpeedTimesHugeDeltaRetainsFiniteLinearTravel)
{
  const auto signal = Uniform(.25F);
  auto settings = scene::ExposureSettings {};
  const auto initial = Run(signal, settings);
  settings.compensation_ev = -8.0F;
  settings.speed_up = std::exp2(-140.0F);
  const auto result = Run(signal, settings, std::exp2(127.0F));
  EXPECT_NEAR(std::log2(result.state.latent_scale),
    std::log2(initial.state.latent_scale) - std::exp2(-13.0F), 1e-5);
}
NOLINT_TEST_F(
  ExposureGpuTest, FractionalPercentileBoundariesMatchIndependentCdf)
{
  const auto pixels
    = std::array { Pixel { .25F, .25F, .25F, 1 }, Pixel { .25F, .25F, .25F, 1 },
        Pixel { .25F, .25F, .25F, 1 }, Pixel { 8, 8, 8, 1 } };
  auto settings = scene::ExposureSettings {};
  settings.low_percentile = .5F;
  settings.high_percentile = .875F;
  const auto result = Run(MakeSignal(4U, 1U, pixels), settings);
  // Retain one .25 sample and half an 8 sample: (-2 + .5*3)/1.5.
  EXPECT_NEAR(result.state.raw_metered_ev, -1.0 / 3.0 - std::log2(.18), 2e-4);
}

NOLINT_TEST_F(ExposureGpuTest, DarkInfluenceDoesNotAttenuatePositiveBinZeroMass)
{
  auto settings = scene::ExposureSettings {};
  settings.min_log_luminance = -2.0F;
  settings.log_luminance_range = 25.0F;
  settings.low_percentile = 0.0F;
  settings.high_percentile = 1.0F;
  const auto pixels = std::array { Pixel { 0, 0, 0, 1 },
    Pixel { .2578125F, .2578125F, .2578125F, 1 } };
  const auto signal = MakeSignal(2U, 1U, pixels);
  const auto zero = Run(signal, settings);
  EXPECT_GT(zero.histogram[0], 0U);
  EXPECT_GT(zero.histogram[1], 0U);
  EXPECT_EQ(zero.histogram[0] + zero.histogram[1], 4095U);
  settings.black_influence = .5F;
  const auto half = Run(signal, settings);
  EXPECT_EQ(half.histogram[0], zero.histogram[0] + 2048U);
  EXPECT_EQ(half.histogram[1], zero.histogram[1]);
}

NOLINT_TEST_F(ExposureGpuTest, BilinearMaskUsesOnlyClampedLinearRed)
{
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const auto pixels
    = std::array { Pixel { 0, nan, nan, nan }, Pixel { 1, nan, nan, nan } };
  const auto mask = MakeSignal(2U, 1U, pixels);
  const auto result = Run(Uniform(.25F), {}, 0.0F, &mask);
  EXPECT_EQ(result.histogram[102], 2048U);
  EXPECT_EQ(result.histogram[260], 0U);
  const auto high_mask = Uniform(4.0F);
  EXPECT_EQ(Run(Uniform(.25F), {}, 0.0F, &high_mask).histogram[102], 4095U);
  const auto low_mask = Uniform(-1.0F);
  EXPECT_EQ(Run(Uniform(.25F), {}, 0.0F, &low_mask).histogram[257], 0U);
}

NOLINT_TEST_F(
  ExposureGpuTest, CoverageUnpremultipliesAndScalesMassOnlyWithBackground)
{
  auto scene = scene::Scene("CoverageFixture", 1U);
  scene.SetEnvironment(std::make_unique<scene::SceneEnvironment>());
  auto& background
    = scene.GetEnvironment()->AddSystem<scene::environment::Background>();
  background.SetEnabled(true);
  ctx_.scene = observer_ptr { &scene };
  const auto pixels
    = std::array<Pixel, 1> { Pixel { .125F, .125F, .125F, .5F } };
  const auto signal = MakeSignal(1U, 1U, pixels);
  const auto result = Run(signal);
  EXPECT_EQ(result.histogram[102], 2048U);
  EXPECT_NEAR(result.state.raw_metered_ev, std::log2(.25 / .18), 2e-4);
  background.SetEnabled(false);
  auto untrimmed = scene::ExposureSettings {};
  untrimmed.low_percentile = 0.0F;
  untrimmed.high_percentile = 1.0F;
  const auto opaque = Run(signal, untrimmed);
  EXPECT_EQ(std::accumulate(
              opaque.histogram.begin(), opaque.histogram.begin() + 256, 0U),
    4095U);
  EXPECT_NEAR(opaque.state.raw_metered_ev, std::log2(.125 / .18), 2e-4);
  ctx_.scene.reset();
}

NOLINT_TEST_F(
  ExposureGpuTest, ProfilesAndZeroRadiusUseNormalizedContentCoordinates)
{
  const auto signal = Uniform(.25F, 3U, 1U);
  auto settings = scene::ExposureSettings {};
  settings.metering_mode = engine::MeteringMode::kCenterWeighted;
  EXPECT_EQ(Run(signal, settings).histogram[102], 6825U);
  settings.metering_mode = engine::MeteringMode::kSpot;
  settings.spot_meter_radius = 1.0e-20F;
  EXPECT_EQ(Run(signal, settings).histogram[102], 4095U);
  settings.spot_meter_radius = 0.0F;
  EXPECT_EQ(Run(signal, settings).histogram[102], 4095U);
  EXPECT_EQ(Run(Uniform(.25F, 2U, 1U), settings).histogram[257], 0U);
}

NOLINT_TEST_F(ExposureGpuTest, ContentRectangleExcludesOutputBars)
{
  const auto pixels
    = std::array { Pixel { 8, 8, 8, 1 }, Pixel { .25F, .25F, .25F, 1 },
        Pixel { .25F, .25F, .25F, 1 }, Pixel { 8, 8, 8, 1 } };
  const auto signal = MakeSignal(4U, 1U, pixels);
  auto params = ResolvedView::Params {};
  params.view_config.viewport
    = { .top_left_x = 1.0F, .top_left_y = 0.0F, .width = 2.0F, .height = 1.0F };
  params.view_config.scissor = { .left = 1, .top = 0, .right = 3, .bottom = 1 };
  const auto view = ResolvedView { params };
  ctx_.current_view.resolved_view = observer_ptr { &view };
  const auto result = Run(signal);
  EXPECT_EQ(result.histogram[102], 8190U);
  EXPECT_EQ(result.histogram[256], 2U);
  EXPECT_NEAR(result.state.raw_metered_ev, std::log2(.25 / .18), 2e-4);
  ctx_.current_view.resolved_view.reset();
}

NOLINT_TEST_F(ExposureGpuTest, SceneReferredMeterIsInvariantToInputPreExposure)
{
  const auto ordinary = Run(Uniform(.25F));
  const auto scaled = Run(Uniform(1024.0F), {}, 0.0F, nullptr, 1.0F / 4096.0F);
  EXPECT_EQ(ordinary.histogram, scaled.histogram);
  EXPECT_EQ(ordinary.state.raw_metered_ev, scaled.state.raw_metered_ev);
}

NOLINT_TEST_F(ExposureGpuTest, LongAndIrregularHybridStepsRemainEquivalent)
{
  const auto signal = Uniform(.25F);
  auto settings = scene::ExposureSettings {};
  Run(signal, settings);
  settings.compensation_ev = -8.0F;
  Snapshot result {};
  for (float dt : { .125F, .875F, .25F, .25F, .5F, 1.0F }) {
    result = Run(signal, settings, dt);
  }
  EXPECT_NEAR(std::log2(result.state.latent_scale),
    std::log2(.72) - 8.0 + 1.5 * std::exp(-5.0 / 3.0), 5e-4);
  const auto settled = Run(signal, settings, 1e30F);
  EXPECT_NEAR(
    std::log2(settled.state.latent_scale), std::log2(.72) - 8.0, 5e-4);
}
NOLINT_TEST_F(ExposureGpuTest, NarrowPercentilesStraddleLargeIntegerCdfBoundary)
{
  std::vector<Pixel> pixels(512U * 512U, Pixel { 8, 8, 8, 1 });
  std::fill_n(pixels.begin(), pixels.size() / 2, Pixel { .25F, .25F, .25F, 1 });
  auto settings = scene::ExposureSettings {};
  settings.low_percentile = std::nextafter(.5F, 0.0F);
  settings.high_percentile = std::nextafter(.5F, 1.0F);
  const auto result = Run(MakeSignal(512U, 512U, pixels), settings);
  // Adjacent binary32 fractions straddle the half-mass boundary with a 1:2
  // retained-mass ratio, independent of the nearly 2^30 total count.
  EXPECT_NEAR(result.state.raw_metered_ev, 4.0 / 3.0 - std::log2(.18), 2e-4);
}

NOLINT_TEST_F(ExposureGpuTest, EightKOutputStillUsesAtMost512SquaredSamples)
{
  const auto result = Run(Uniform(.25F, 7680U, 4320U));
  EXPECT_EQ(result.histogram[256], 262144U);
  EXPECT_EQ(result.histogram[102], 1073479680U);
}
NOLINT_TEST_F(ExposureGpuTest, OrdinaryAutoCurveInterpolatesAtRawMeterEv)
{
  auto settings = scene::ExposureSettings {};
  settings.compensation_curve = { { 0.0F, -2.0F }, { 2.0F, 2.0F } };
  const auto result = Run(Uniform(.25F), settings);
  const double raw_ev = std::log2(.25 / .18);
  // Curve contributes 2*EV-2; the denominator contributes -EV.
  EXPECT_NEAR(std::log2(result.state.target_scale), raw_ev - 2.0, 2e-4);
  EXPECT_NEAR(std::log2(result.state.displayed_scale), raw_ev - 2.0, 2e-4);
}

NOLINT_TEST_F(ExposureGpuTest, OrdinaryAutoCurveClampsBothAuthoredEndpoints)
{
  auto settings = scene::ExposureSettings {};
  settings.compensation_curve = { { 1.0F, 2.0F }, { 2.0F, 4.0F } };
  const auto below = Run(Uniform(.25F), settings);
  EXPECT_NEAR(
    std::log2(below.state.target_scale), 2.0 - std::log2(.25 / .18), 2e-4);
  const auto above = Run(Uniform(8.0F), settings);
  EXPECT_NEAR(
    std::log2(above.state.target_scale), 4.0 - std::log2(8.0 / .18), 2e-4);
}

NOLINT_TEST_F(ExposureGpuTest, CurveInputIgnoresEvClampAndAdaptedHistory)
{
  auto settings = scene::ExposureSettings {};
  const auto signal = Uniform(.25F);
  const auto initial = Run(signal, settings);
  settings.min_ev = 0.0F;
  settings.max_ev = .1F;
  settings.compensation_curve = { { 0.0F, -2.0F }, { 2.0F, 2.0F } };
  settings.speed_up = settings.speed_down = 0.0F;
  const auto result = Run(signal, settings, 1.0F);
  const double expected = 2.0 * std::log2(.25 / .18) - 2.0 - .1;
  EXPECT_NEAR(std::log2(result.state.target_scale), expected, 2e-4);
  EXPECT_EQ(result.state.latent_scale, initial.state.latent_scale);
}

NOLINT_TEST_F(ExposureGpuTest, BrighteningUsesSpeedDownAcrossFrameSchedules)
{
  const auto signal = Uniform(.25F);
  const double expected = std::log2(.72) + 8.0 - 1.5 * std::exp(-1.0);
  for (const unsigned frequency : { 30U, 60U, 120U }) {
    ResetHistory();
    auto settings = scene::ExposureSettings {};
    Run(signal, settings);
    settings.compensation_ev = 8.0F;
    settings.speed_up = 7.0F;
    settings.speed_down = 1.0F;
    Snapshot result {};
    for (unsigned frame = 0; frame < 8U * frequency; ++frame) {
      result = Run(signal, settings, 1.0F / frequency);
    }
    EXPECT_NEAR(std::log2(result.state.latent_scale), expected, 5e-4)
      << frequency;
  }
}

NOLINT_TEST_F(ExposureGpuTest, MovingEdgeHasBoundedGridSamplingError)
{
  auto settings = scene::ExposureSettings {};
  settings.low_percentile = 0.0F;
  settings.high_percentile = 1.0F;
  // A 1024x1 content rectangle has 512 cell-centre samples at odd pixels.
  // Moving a sharp five-stop edge by one pixel changes at most one grid cell:
  // geometric-mean error against all pixels is bounded by 5/1024 EV.
  for (const unsigned edge : { 1U, 2U, 3U, 255U, 256U, 257U, 511U, 512U }) {
    std::vector<Pixel> pixels(1024U, Pixel { .25F, .25F, .25F, 1 });
    std::fill_n(pixels.begin(), edge, Pixel { 8, 8, 8, 1 });
    const auto result = Run(MakeSignal(1024U, 1U, pixels), settings);
    EXPECT_EQ(result.histogram[153], (edge / 2U) * 4095U);
    const double exact_sample_ev
      = -2.0 + 5.0 * (edge / 2U) / 512.0 - std::log2(.18);
    const double full_image_ev = -2.0 + 5.0 * edge / 1024.0 - std::log2(.18);
    EXPECT_NEAR(result.state.raw_metered_ev, exact_sample_ev, 2e-4);
    EXPECT_LE(std::abs(result.state.raw_metered_ev - full_image_ev),
      5.0 / 1024.0 + 2e-4);
  }
}

NOLINT_TEST_F(
  ExposureGpuTest, SingleBrightPixelRecordsSamplingAliasingWithoutAreaAveraging)
{
  auto settings = scene::ExposureSettings {};
  settings.low_percentile = 0.0F;
  settings.high_percentile = 1.0F;
  for (const unsigned x : { 0U, 1U, 2U, 3U, 510U, 511U, 1022U, 1023U }) {
    std::vector<Pixel> pixels(1024U, Pixel { .25F, .25F, .25F, 1 });
    pixels[x] = Pixel { 8, 8, 8, 1 };
    const auto result = Run(MakeSignal(1024U, 1U, pixels), settings);
    EXPECT_EQ(result.histogram[153], (x % 2U) * 4095U);
    EXPECT_LE(std::abs(result.state.raw_metered_ev
                - (-2.0 + 5.0 / 1024.0 - std::log2(.18))),
      5.0 / 1024.0 + 2e-4);
  }
}
NOLINT_TEST_F(
  ExposureGpuTest, UnavailableMaskPreventsMeterInitializationButNotLockedSolve)
{
  const auto signal = Uniform(.25F);
  const auto pending = Run(signal, {}, 1.0F, nullptr, 1.0F, false);
  EXPECT_EQ(pending.histogram[257], 0U);
  EXPECT_EQ(pending.state.flags & 15U, 0U);
  EXPECT_EQ(pending.state.displayed_scale, 1.0F);
  const auto ready = Run(signal);
  const auto invalid = Run(signal, {}, 1.0F, nullptr, 1.0F, false);
  EXPECT_EQ(invalid.state.displayed_scale, ready.state.displayed_scale);
  EXPECT_EQ(invalid.state.flags & 12U, 0U);
  auto locked = scene::ExposureSettings {};
  locked.min_ev = locked.max_ev = 2.0F;
  EXPECT_EQ(
    Run(signal, locked, 0.0F, nullptr, 1.0F, false).state.displayed_scale,
    .25F);
}

NOLINT_TEST_F(
  ExposureGpuTest, CookedMaskUploadAndResidentLeaseReachProductionHistogram)
{
  auto loader = vortex::testing::FakeAssetLoader {};
  auto service = PostProcessService(*renderer_, observer_ptr { &loader });
  auto payload = vortex::testing::MakeCookedTexture1x1Rgba8Payload();
  data::pak::render::TexturePayloadHeader header {};
  std::memcpy(&header,
    payload.data() + sizeof(data::pak::core::TextureResourceDesc),
    sizeof(header));
  payload[sizeof(data::pak::core::TextureResourceDesc)
    + header.data_offset_bytes] = 128U;
  auto settings = scene::ExposureSettings {};
  settings.metering_mask = loader.PreloadCookedTexture(std::span(payload));
  const auto tag = internal::RendererTagFactory::Get();
  renderer_->GetUploadCoordinator().OnFrameStart(tag, frame::Slot { 0U });
  service.OnFrameStart(frame::SequenceNumber { 1U }, frame::Slot { 0U });
  EXPECT_EQ(service
              .ResolveViewExposureSettings(
                ctx_.current_view.view_state_handle, settings)
              .mask_status,
    PostProcessService::ExposureMaskStatus::kPending);
  // Each step flushes submitted upload work and observes its completed ticket.
  // This is resource-readiness synchronization, not exposure-settling warmup.
  for (unsigned i = 0U; i < 8U; ++i) {
    WaitForQueueIdle();
    const auto slot = frame::Slot { (i + 1U) % 3U };
    renderer_->GetUploadCoordinator().OnFrameStart(tag, slot);
    service.OnFrameStart(frame::SequenceNumber { i + 2U }, slot);
    if (service
          .ResolveViewExposureSettings(
            ctx_.current_view.view_state_handle, settings)
          .mask_status
      == PostProcessService::ExposureMaskStatus::kReady) {
      break;
    }
  }
  const auto& accepted = service.ResolveViewExposureSettings(
    ctx_.current_view.view_state_handle, settings);
  ASSERT_EQ(
    accepted.mask_status, PostProcessService::ExposureMaskStatus::kReady);
  ASSERT_NE(accepted.mask, nullptr);
  const auto mask = Signal { accepted.mask->texture, accepted.mask->srv };
  const auto result = Run(Uniform(.25F), settings, 0.0F, &mask);
  EXPECT_EQ(result.histogram[102], 2056U); // round-half-up(4095*128/255).
  EXPECT_EQ(result.histogram[257], 1U);
  EXPECT_NEAR(result.state.raw_metered_ev, std::log2(.25 / .18), 2e-4);
  WaitForQueueIdle();
}

NOLINT_TEST_F(
  ExposureGpuTest, SetConfigLoadsMasksAndRetainsAcceptedReplacementAtomically)
{
  auto loader = vortex::testing::FakeAssetLoader {};
  auto service = PostProcessService(*renderer_, observer_ptr { &loader });
  auto payload = vortex::testing::MakeCookedTexture1x1Rgba8Payload();
  data::pak::render::TexturePayloadHeader header {};
  std::memcpy(&header,
    payload.data() + sizeof(data::pak::core::TextureResourceDesc),
    sizeof(header));
  payload[sizeof(data::pak::core::TextureResourceDesc)
    + header.data_offset_bytes] = 128U;
  auto config = PostProcessConfig {};
  config.exposure.key = 12.5F;
  config.exposure.metering_mask
    = loader.PreloadCookedTexture(std::span(payload));
  service.SetConfig(config);
  const auto signal = Uniform(.25F);
  const auto render = [&] {
    ++sequence_;
    ctx_.frame_sequence = frame::SequenceNumber { sequence_ };
    ctx_.frame_slot = frame::Slot { static_cast<unsigned>(sequence_ % 3U) };
    renderer_->GetUploadCoordinator().OnFrameStart(
      internal::RendererTagFactory::Get(), ctx_.frame_slot);
    service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    return service.PrepareSceneExposure(ctx_.current_view.view_id, ctx_,
      { .scene_signal = signal.texture.get(), .scene_signal_srv = signal.srv });
  };
  auto prepared = render();
  ASSERT_TRUE(prepared.has_value());
  EXPECT_EQ(prepared->config.Exposure().authored.metering_mask.get(), 0U);
  EXPECT_EQ(ReadState(prepared->exposure).flags & 4U, 0U);
  for (unsigned i = 0U; i < 8U
    && prepared->config.Exposure().authored.metering_mask
      != config.exposure.metering_mask;
    ++i) {
    WaitForQueueIdle();
    prepared = render();
    ASSERT_TRUE(prepared.has_value());
  }
  ASSERT_EQ(prepared->config.Exposure().authored, config.exposure);
  const auto histogram = Read<std::array<std::uint32_t, 264>>(
    *prepared->exposure.histogram_buffer, ResourceStates::kCommon);
  EXPECT_EQ(histogram[102], 2056U);
  EXPECT_EQ(histogram[257], 1U);
  const auto accepted = prepared->config;
  const auto accepted_state = ReadState(prepared->exposure);
  EXPECT_NEAR(accepted_state.displayed_scale, .72F, 2e-4F);
  config.exposure.metering_mask = loader.MintSyntheticTextureKey();
  config.exposure.compensation_ev = 1.0F;
  service.SetConfig(config);
  for (unsigned i = 0U; i < 3U; ++i) {
    WaitForQueueIdle();
    prepared = render();
    ASSERT_TRUE(prepared.has_value());
    EXPECT_EQ(
      prepared->config.Exposure().authored, accepted.Exposure().authored);
    EXPECT_EQ(prepared->config.Revision(), accepted.Revision());
    EXPECT_EQ(
      (Read<std::array<std::uint32_t, 264>>(
        *prepared->exposure.histogram_buffer, ResourceStates::kCommon)[102]),
      2056U);
    EXPECT_EQ(ReadState(prepared->exposure).displayed_scale,
      accepted_state.displayed_scale);
  }
  // The scene entry point supplies the same authored request through capture.
  ctx_.current_view.view_state_handle
    = CompositionView::ViewStateHandle { 902U };
  ctx_.current_view.view_id = ViewId { 902U };
  service.OnFrameStart(frame::SequenceNumber { ++sequence_ }, ctx_.frame_slot);
  ctx_.frame_sequence = frame::SequenceNumber { sequence_ };
  static_cast<void>(
    service.CaptureViewExposureSettings(ctx_.current_view.view_id,
      ctx_.current_view.view_state_handle, accepted.Exposure().authored));
  const auto scene_result
    = service.PrepareSceneExposure(ctx_.current_view.view_id, ctx_,
      { .scene_signal = signal.texture.get(), .scene_signal_srv = signal.srv });
  ASSERT_TRUE(scene_result.has_value());
  EXPECT_EQ(ReadState(scene_result->exposure).displayed_scale,
    accepted_state.displayed_scale);
  EXPECT_EQ(
    (Read<std::array<std::uint32_t, 264>>(
      *scene_result->exposure.histogram_buffer, ResourceStates::kCommon)),
    histogram);
  WaitForQueueIdle();
}

NOLINT_TEST_F(
  ExposureGpuTest, LockedServiceGainReachesTonemapDespitePendingOrFailedMask)
{
  auto loader = vortex::testing::FakeAssetLoader {};
  auto service = PostProcessService(*renderer_, observer_ptr { &loader });
  const auto signal = Uniform(1.0F, 4U, 4U);
  auto output_desc = TextureDesc {};
  output_desc.width = output_desc.height = 4U;
  output_desc.format = Format::kRGBA32Float;
  output_desc.is_render_target = true;
  output_desc.initial_state = ResourceStates::kCommon;
  const auto output = CreateRegisteredTexture(output_desc);
  auto framebuffer = Backend().CreateFramebuffer(
    FramebufferDesc {}.AddColorAttachment(output));
  auto textures
    = SceneTextures(Backend(), SceneTexturesConfig { .extent = { 4U, 4U } });
  unsigned id = 1U;
  for (bool previous : { false, true }) {
    for (bool failure : { false, true }) {
      const auto handle = CompositionView::ViewStateHandle { id++ };
      ctx_.current_view.view_state_handle = handle;
      ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
      service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
      auto requested = scene::ExposureSettings {};
      requested.key = 12.5F;
      if (previous) {
        static_cast<void>(
          service.ResolveViewExposureSettings(handle, requested));
      }
      requested.metering_mask = loader.MintSyntheticTextureKey();
      EXPECT_EQ(
        service.ResolveViewExposureSettings(handle, requested).mask_status,
        PostProcessService::ExposureMaskStatus::kPending);
      if (failure) {
        ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
        service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
        EXPECT_EQ(
          service.ResolveViewExposureSettings(handle, requested).mask_status,
          PostProcessService::ExposureMaskStatus::kFailed);
      }
      requested.min_ev = requested.max_ev = 2.0F;
      const auto& accepted = service.CaptureViewExposureSettings(
        ctx_.current_view.view_id, handle, requested);
      ASSERT_EQ(accepted.resolved.authored.min_ev, 2.0F);
      auto config = PostProcessConfig {};
      config.enable_bloom = false;
      config.bloom_intensity = 0.0F;
      config.tone_mapper = engine::ToneMapper::kNone;
      config.gamma = 1.0F;
      service.SetResolvedConfig(service.BuildPassConfig(config,
        ctx_.current_view.view_id, ctx_.current_view.view_state_handle));
      ctx_.delta_time = 0.0F;
      service.Execute(ctx_.current_view.view_id, ctx_, textures,
        {
          .scene_signal = signal.texture.get(),
          .post_target = observer_ptr<const Framebuffer> { framebuffer.get() },
          .scene_signal_srv = signal.srv,
        });
      EXPECT_TRUE(service.GetLastExecutionState().tonemap_executed);
      auto readback
        = GetReadbackManager()->CreateTextureReadback("Locked service result");
      {
        auto recorder = AcquireRecorder("Read locked service tonemap");
        ASSERT_TRUE(recorder->AdoptKnownResourceState(*output));
        const auto ticket = readback->EnqueueCopy(*recorder, *output,
          {
            .src_slice
            = { .x = 1U, .y = 0U, .width = 1U, .height = 1U, .depth = 1U },
          });
        ASSERT_TRUE(ticket.has_value());
      }
      const auto mapped = readback->MapNow();
      ASSERT_TRUE(mapped.has_value());
      Pixel pixel {};
      std::memcpy(pixel.data(), mapped->Data(), sizeof(pixel));
      for (unsigned channel = 0U; channel < 3U; ++channel) {
        EXPECT_NEAR(pixel[channel], .25F, 2e-5F) << previous << failure;
      }
    }
  }
  WaitForQueueIdle();
}

NOLINT_TEST_F(
  ExposureGpuTest, NarrowCdfBoundariesRetainIntegerAndFractionalProductBits)
{
  // Power-of-two CDF boundaries exercise both sides of the 32-bit word shift
  // and narrow fractional tails. Below each exact boundary binary32 spacing
  // is half the spacing above it, giving retained mass ratio 1:2.
  for (unsigned low_pixels : { 1U, 512U, 32768U, 131072U }) {
    std::vector<Pixel> pixels(262144U, Pixel { .25F, .25F, .25F, 1 });
    std::fill_n(pixels.begin(), low_pixels,
      Pixel { 1.0F / 128.0F, 1.0F / 128.0F, 1.0F / 128.0F, 1 });
    auto settings = scene::ExposureSettings {};
    const float boundary = static_cast<float>(low_pixels) / 262144.0F;
    settings.low_percentile = std::nextafter(boundary, 0.0F);
    settings.high_percentile = std::nextafter(boundary, 1.0F);
    const auto result = Run(MakeSignal(512U, 512U, pixels), settings);
    EXPECT_EQ(result.histogram[51], low_pixels * 4095U);
    EXPECT_EQ(result.histogram[102], (262144U - low_pixels) * 4095U);
    EXPECT_NEAR(result.state.raw_metered_ev, -11.0 / 3.0 - std::log2(.18), 2e-4)
      << low_pixels;
  }
}

NOLINT_TEST_F(
  ExposureGpuTest, DarkCountersSeparateBlackPositiveBelowWindowAndNegative)
{
  const auto pixels = std::array { Pixel { 0, 0, 0, 1 },
    Pixel { 0x1p-20F, 0x1p-20F, 0x1p-20F, 1 },
    Pixel { -.25F, -.25F, -.25F, 1 } };
  const auto result = Run(MakeSignal(3U, 1U, pixels));
  EXPECT_EQ(result.histogram[256], 3U);
  EXPECT_EQ(result.histogram[257], 3U);
  EXPECT_EQ(result.histogram[258], 1U);
  EXPECT_EQ(result.histogram[259], 1U);
  EXPECT_EQ(result.histogram[260], 0U);
  EXPECT_EQ(result.histogram[261], 3U);
  EXPECT_EQ(result.state.flags & 31U, 27U);
  EXPECT_EQ(result.state.raw_metered_ev, -6.0F);
  EXPECT_EQ(result.state.displayed_scale, 64.0F);
}

NOLINT_TEST_F(ExposureGpuTest,
  AllNonfiniteInputKeepsAutoEvZeroFallbackIndependentOfManualEv)
{
  auto settings = scene::ExposureSettings {};
  settings.manual_ev = 31.0F;
  const auto result
    = Run(Uniform(std::numeric_limits<float>::quiet_NaN()), settings);
  EXPECT_EQ(result.histogram[256], 0U);
  EXPECT_EQ(result.histogram[260], 1U);
  EXPECT_EQ(result.state.flags & 15U, 0U);
  EXPECT_EQ(result.state.displayed_scale, 1.0F);
  EXPECT_EQ(result.state.latent_scale, 1.0F);
}

NOLINT_TEST_F(
  ExposureGpuTest, ZeroTargetContinuesLatentAdaptationAndUpdatesLastValidMeter)
{
  auto settings = scene::ExposureSettings {};
  Run(Uniform(.25F), settings);
  settings.target_luminance = 0.0F;
  const auto dark_output = Run(Uniform(8.0F), settings, 2.0F);
  EXPECT_EQ(dark_output.state.displayed_scale, 0.0F);
  EXPECT_EQ(dark_output.state.target_scale, 0.0F);
  EXPECT_NEAR(dark_output.state.latent_target_scale, .0225F, 2e-6);
  EXPECT_NEAR(std::log2(dark_output.state.latent_scale),
    std::log2(.0225) + 1.5 * std::exp(-5.0 / 3.0), 5e-4);
  EXPECT_NEAR(dark_output.state.raw_metered_ev, std::log2(8.0 / .18), 2e-4);
  settings.target_luminance = .36F;
  const auto restored
    = Run(Uniform(0.0F), settings, 0.0F, nullptr, 1.0F, false);
  EXPECT_NEAR(restored.state.displayed_scale, .045F, 2e-6);
}

NOLINT_TEST_F(ExposureGpuTest, LockedCurveUsesBoundInsteadOfRawMeteredEv)
{
  auto settings = scene::ExposureSettings {};
  settings.min_ev = settings.max_ev = 2.0F;
  settings.compensation_curve = { { 0.0F, -2.0F }, { 4.0F, 6.0F } };
  EXPECT_NEAR(Run(Uniform(.25F), settings).state.displayed_scale, 1.0F, 2e-6);
  EXPECT_NEAR(Run(Uniform(8.0F), settings).state.displayed_scale, 1.0F, 2e-6);
  EXPECT_NEAR(Run(Uniform(0.0F), settings, 0.0F, nullptr, 1.0F, false)
                .state.displayed_scale,
    1.0F, 2e-6);
}

NOLINT_TEST_F(
  ExposureGpuTest, SubnormalCurveCoordinatesInterpolateAtExactZeroMeterEv)
{
  for (const float coordinate :
    { 1.0e-40F, std::numeric_limits<float>::denorm_min() }) {
    ResetHistory();
    auto settings = scene::ExposureSettings {};
    settings.min_log_luminance = std::log2(.18F);
    settings.log_luminance_range = 25.0F;
    settings.low_percentile = 0.0F;
    settings.high_percentile = 0x1p-16F;
    settings.compensation_curve
      = { { -coordinate, -1.0F }, { coordinate, 1.0F } };
    // Brighter than the dark threshold, but entirely quantized to bin zero.
    // The exact bin position gives EV zero and midpoint compensation zero.
    const auto result = Run(Uniform(.1800001F), settings);
    ASSERT_EQ(result.histogram[261], 0U);
    ASSERT_EQ(result.histogram[0], 4095U);
    ASSERT_EQ(result.state.raw_metered_ev, 0.0F);
    EXPECT_NEAR(result.state.target_scale, 1.0F, 2e-5F);
    EXPECT_NEAR(result.state.displayed_scale, 1.0F, 2e-5F);
  }
}

NOLINT_TEST_F(ExposureGpuTest, PublicPausedFrameSessionFreezesGpuAdaptation)
{
  const auto before = Run(Uniform(.25F));
  auto output_desc = TextureDesc {};
  output_desc.width = output_desc.height = 1U;
  output_desc.format = Format::kRGBA8UNorm;
  output_desc.is_render_target = output_desc.is_shader_resource = true;
  output_desc.initial_state = ResourceStates::kCommon;
  const auto output = CreateRegisteredTexture(output_desc);
  auto framebuffer = Backend().CreateFramebuffer(
    FramebufferDesc {}.AddColorAttachment(output));
  auto params = ResolvedView::Params {};
  params.view_config.viewport = { .width = 1.0F, .height = 1.0F };
  auto facade = renderer_->ForSinglePassHarness();
  facade.SetFrameSession(Renderer::FrameSessionInput {
    .frame_slot = frame::Slot { 0U },
    .frame_sequence = frame::SequenceNumber { 2U },
    .delta_time_seconds = 0.0F,
  });
  facade.SetResolvedView(Renderer::ResolvedViewInput {
    .view_id = ViewId { 1U }, .value = ResolvedView { params } });
  facade.SetOutputTarget(Renderer::OutputTargetInput {
    .framebuffer = observer_ptr<Framebuffer> { framebuffer.get() } });
  const auto paused = facade.Finalize();
  ASSERT_TRUE(paused.has_value());
  ASSERT_EQ(paused->GetRenderContext().delta_time, 0.0F);
  const auto after
    = Run(Uniform(8.0F), {}, paused->GetRenderContext().delta_time);
  EXPECT_EQ(after.state.latent_scale, before.state.latent_scale);
  EXPECT_NE(after.state.latent_target_scale, before.state.latent_target_scale);
}

NOLINT_TEST_F(ExposureGpuTest, ManualCameraAndDisabledWriteUnifiedGpuState)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 14.0F;
  const auto manual = Run(Signal {}, settings);
  EXPECT_EQ(manual.state.displayed_scale, 0x1p-14F);
  EXPECT_EQ(manual.state.latent_scale, 0x1p-14F);
  EXPECT_EQ(manual.state.flags & 12U, 0U);
  settings.mode = engine::ExposureMode::kManualCamera;
  const auto camera = Run(Signal {}, settings, 0.0F, nullptr, 1.0F, true, {},
    static_cast<float>(std::log2(15125.0)));
  EXPECT_NEAR(camera.state.displayed_scale, 1.0 / 15125.0, 2e-5 / 15125.0);
  EXPECT_EQ((camera.state.flags >> 10U) & 3U, 1U);
  settings.enabled = false;
  const auto disabled = Run(Signal {}, settings);
  EXPECT_EQ(disabled.state.displayed_scale, 1.0F);
  EXPECT_EQ(disabled.state.latent_scale, 1.0F);
  EXPECT_EQ((disabled.state.flags >> 10U) & 3U, 3U);
}

NOLINT_TEST_F(
  ExposureGpuTest, ManualToAutoPreservesGainForTransitionFrameThenAdapts)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 4.0F;
  const auto manual = Run(Uniform(.25F), settings);
  settings.mode = engine::ExposureMode::kAuto;
  const auto transition = Run(Uniform(.25F), settings, 1.0F);
  EXPECT_EQ(transition.state.displayed_scale, manual.state.displayed_scale);
  EXPECT_NEAR(transition.state.target_scale, .72F, 2e-5);
  const auto next = Run(Uniform(.25F), settings, 1.0F);
  EXPECT_NEAR(next.state.displayed_scale, .125F, 2e-5);
}

NOLINT_TEST_F(
  ExposureGpuTest, SeedUsesRequestedEvAndDuplicateGenerationDoesNotReapply)
{
  const auto token
    = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
      ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(token.has_value());
  const auto event = Run(Uniform(.25F), {}, 1.0F, nullptr, 1.0F, true, *token);
  EXPECT_EQ(event.state.displayed_scale, 0x1p-8F);
  EXPECT_EQ(event.state.applied_generation[0], token->generation);
  const auto next = Run(Uniform(.25F), {}, 1.0F, nullptr, 1.0F, true, *token);
  EXPECT_NEAR(next.state.displayed_scale, 0x1p-7F, 2e-5);
  EXPECT_EQ(next.state.applied_generation, event.state.applied_generation);
}

NOLINT_TEST_F(
  ExposureGpuTest, RemeterRemainsPendingWithoutInputAndAppliesAtZeroDelta)
{
  const auto token = renderer_->QueueExposureTransition(
    ctx_.current_view.view_state_handle, ExposureTransitionPolicy::kRemeter);
  ASSERT_TRUE(token.has_value());
  const auto invalid = Run(Signal {}, {}, 0.0F, nullptr, 1.0F, false, *token);
  EXPECT_EQ(invalid.state.applied_generation[0], 0U);
  EXPECT_EQ(invalid.state.requested_generation[0], token->generation);
  const auto applied
    = Run(Uniform(.25F), {}, 0.0F, nullptr, 1.0F, true, *token);
  EXPECT_NEAR(applied.state.displayed_scale, .72F, 2e-5);
  EXPECT_EQ(applied.state.applied_generation[0], token->generation);
  const auto retry = Run(Uniform(8.0F), {}, 0.0F, nullptr, 1.0F, true, *token);
  EXPECT_EQ(retry.state.displayed_scale, applied.state.displayed_scale);
}

NOLINT_TEST_F(
  ExposureGpuTest, RejectedManualSeedDoesNotReactivateOnLaterAutoEntry)
{
  const auto token
    = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
      ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(token.has_value());
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 4.0F;
  const auto rejected
    = Run(Signal {}, settings, 0.0F, nullptr, 1.0F, false, *token);
  EXPECT_EQ(rejected.state.applied_generation[0], 0U);
  EXPECT_NE(rejected.state.flags & (1U << 12U), 0U);
  settings.mode = engine::ExposureMode::kAuto;
  const auto next
    = Run(Uniform(.25F), settings, 1.0F, nullptr, 1.0F, true, *token);
  EXPECT_EQ(next.state.displayed_scale, 0x1p-4F);
  EXPECT_EQ(next.state.applied_generation[0], 0U);
}

NOLINT_TEST_F(
  ExposureGpuTest, StatelessAutoSolvesEachInvocationWithoutAdaptation)
{
  ctx_.current_view.view_state_handle
    = CompositionView::kInvalidViewStateHandle;
  const auto first = Run(Uniform(.25F), {}, 0.0F);
  const auto next = Run(Uniform(8.0F), {}, 0.0F);
  EXPECT_NEAR(first.state.displayed_scale, .72F, 2e-5);
  EXPECT_NEAR(next.state.displayed_scale, .0225F, 2e-5);
}

NOLINT_TEST_F(ExposureGpuTest, PriorStateLeaseRemainsImmutableAcrossLaterSolves)
{
  const auto first = Run(Uniform(.25F));
  const auto retained = last_state_;
  const auto second = Run(Uniform(8.0F), {}, 10.0F);
  EXPECT_NE(second.state.displayed_scale, first.state.displayed_scale);
  EXPECT_NE(retained->buffer, last_state_->buffer);
  const auto reread = Read<ExposureStateData>(
    *retained->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(reread.displayed_scale, first.state.displayed_scale);
  EXPECT_EQ(reread.frame_sequence, first.state.frame_sequence);
}

NOLINT_TEST_F(ExposureGpuTest, SceneAndDirectSettingsUseIdenticalGainsAndRates)
{
  auto service = PostProcessService(*renderer_);
  auto settings = scene::ExposureSettings {};
  settings.speed_up = .875F;
  settings.speed_down = .625F;
  const auto initial = Uniform(.25F, 4U, 4U);
  const auto bright = Uniform(.5F, 4U, 4U);
  const auto dark = Uniform(.015625F, 4U, 4U);
  const auto compare = [&](const Signal& signal, float luminance, float dt) {
    const auto direct = Run(signal, settings, dt);
    const auto pixel = ServicePixel(service, signal, settings, false, dt);
    EXPECT_NEAR(pixel, luminance * direct.state.displayed_scale, 2e-5F);
  };
  compare(initial, .25F, 0.0F);
  compare(bright, .5F, .25F);
  compare(dark, .015625F, .25F);
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 14.0F;
  compare(bright, .5F, 0.0F);
  EXPECT_NEAR(ServicePixel(service, bright, settings), 0x1p-15F, 1e-7F);
}

NOLINT_TEST_F(ExposureGpuTest,
  TwoViewsInitializeIndependentlyWithoutWaitingBetweenSubmissions)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.key = 12.5F;
  settings.manual_ev = 4.0F;
  auto config = SharedConfig(settings);
  ctx_.frame_sequence = frame::SequenceNumber { 1U };
  const auto first = pass_->Execute(ctx_, config, {});
  ASSERT_TRUE(first.executed);
  ctx_.current_view.view_state_handle = CompositionView::ViewStateHandle { 2U };
  ctx_.current_view.view_id = ViewId { 2U };
  settings.manual_ev = 8.0F;
  config = SharedConfig(settings);
  const auto second = pass_->Execute(ctx_, config, {});
  ASSERT_TRUE(second.executed);
  EXPECT_NE(first.exposure_buffer, second.exposure_buffer);
  EXPECT_EQ(Read<ExposureStateData>(
              *first.exposure_buffer, ResourceStates::kShaderResource)
              .displayed_scale,
    0x1p-4F);
  EXPECT_EQ(Read<ExposureStateData>(
              *second.exposure_buffer, ResourceStates::kShaderResource)
              .displayed_scale,
    0x1p-8F);
  const auto duplicate = pass_->Execute(ctx_, config, {});
  EXPECT_FALSE(duplicate.executed);
  EXPECT_EQ(duplicate.state, second.state);
}

NOLINT_TEST_F(
  ExposureGpuTest, DiagnosticUnitStatePreservesAutoHistoryAndPendingSeed)
{
  const auto before = Run(Uniform(.25F));
  const auto persistent = last_state_;
  const auto token
    = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
      ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(token.has_value());
  for (unsigned i = 0U; i < 3U; ++i) {
    const auto diagnostic
      = Run(Uniform(8.0F), {}, 5.0F, nullptr, 1.0F, true, *token, {}, true);
    EXPECT_EQ(diagnostic.state.displayed_scale, 1.0F);
    EXPECT_EQ(diagnostic.state.applied_generation[0], 0U);
    const auto retained = Read<ExposureStateData>(
      *persistent->buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(retained.displayed_scale, before.state.displayed_scale);
    EXPECT_EQ(retained.applied_generation, before.state.applied_generation);
  }
  const auto resumed
    = Run(Uniform(8.0F), {}, 1.0F, nullptr, 1.0F, true, *token);
  EXPECT_EQ(resumed.state.displayed_scale, 0x1p-8F);
  const auto next = Run(Uniform(8.0F), {}, 1.0F, nullptr, 1.0F, true, *token);
  EXPECT_NEAR(next.state.displayed_scale, 0x1p-7F, 2e-5);
}

NOLINT_TEST_F(ExposureGpuTest, DiagnosticUnitStateDoesNotOverwriteManualHistory)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 4.0F;
  const auto before = Run(Signal {}, settings);
  const auto persistent = last_state_;
  const auto diagnostic
    = Run(Signal {}, settings, 5.0F, nullptr, 1.0F, true, {}, {}, true);
  EXPECT_EQ(diagnostic.state.displayed_scale, 1.0F);
  EXPECT_EQ(Read<ExposureStateData>(
              *persistent->buffer, ResourceStates::kShaderResource)
              .displayed_scale,
    before.state.displayed_scale);
  EXPECT_EQ(Run(Signal {}, settings).state.displayed_scale, 0x1p-4F);
}

NOLINT_TEST_F(
  ExposureGpuTest, ServiceDiagnosticFramesDoNotAcknowledgePendingTransition)
{
  auto service = PostProcessService(*renderer_);
  const auto signal = Uniform(.25F, 4U, 4U);
  EXPECT_NEAR(ServicePixel(service, signal), .18F, 2e-5);
  const auto token
    = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
      ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(token.has_value());
  for (unsigned i = 0U; i < 3U; ++i) {
    EXPECT_NEAR(ServicePixel(service, signal, {}, true, 3.0F), .25F, 2e-5);
    EXPECT_FALSE(service.GetLastExecutionState().auto_exposure_requested);
    EXPECT_FALSE(service.GetLastExecutionState().auto_exposure_executed);
    ASSERT_NE(service.InspectBindings(ctx_.current_view.view_id), nullptr);
    EXPECT_EQ(
      service.InspectBindings(ctx_.current_view.view_id)->enable_auto_exposure,
      0U);
    EXPECT_EQ(renderer_->InspectExposureTransition(token->target)->phase,
      ExposureTransitionPhase::kQueued);
  }
  EXPECT_NEAR(
    ServicePixel(service, signal, {}, false, 1.0F), .25F / 256.0F, 2e-5);
  EXPECT_EQ(renderer_->InspectExposureTransition(token->target)->phase,
    ExposureTransitionPhase::kQueued);
  service.OnFrameStart(frame::SequenceNumber { ++sequence_ }, ctx_.frame_slot);
  const auto status = renderer_->InspectExposureTransition(token->target);
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(status->phase, ExposureTransitionPhase::kApplied);
  EXPECT_EQ(status->applied_generation, token->generation);
}

NOLINT_TEST_F(
  ExposureGpuTest, CompletedOldGenerationCannotConsumeNewerQueuedTransition)
{
  auto service = PostProcessService(*renderer_);
  const auto signal = Uniform(.25F, 4U, 4U);
  const auto first
    = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
      ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(first.has_value());
  EXPECT_NEAR(ServicePixel(service, signal), .25F / 256.0F, 2e-5);
  const auto second = renderer_->QueueExposureTransition(
    first->target, ExposureTransitionPolicy::kRemeter);
  ASSERT_TRUE(second.has_value());
  service.OnFrameStart(frame::SequenceNumber { ++sequence_ }, ctx_.frame_slot);
  const auto intermediate = renderer_->InspectExposureTransition(first->target);
  ASSERT_TRUE(intermediate.has_value());
  EXPECT_EQ(intermediate->request.generation, second->generation);
  EXPECT_EQ(intermediate->phase, ExposureTransitionPhase::kQueued);
  EXPECT_EQ(intermediate->applied_generation, first->generation);
  EXPECT_NEAR(ServicePixel(service, signal), .18F, 2e-5);
  service.OnFrameStart(frame::SequenceNumber { ++sequence_ }, ctx_.frame_slot);
  EXPECT_EQ(
    renderer_->InspectExposureTransition(first->target)->applied_generation,
    second->generation);
}

NOLINT_TEST_F(
  ExposureGpuTest, TransitionQueuedAfterFrameCaptureWaitsForNextFrame)
{
  auto service = PostProcessService(*renderer_);
  const auto signal = Uniform(.25F, 4U, 4U);
  std::optional<ExposureTransitionToken> token;
  const auto current = ServicePixel(service, signal, {}, false, 0.0F, [&] {
    const auto issued
      = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
        ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
    CHECK_F(issued.has_value());
    token = *issued;
  });
  EXPECT_NEAR(current, .18F, 2e-5);
  ASSERT_TRUE(token.has_value());
  EXPECT_EQ(renderer_->InspectExposureTransition(token->target)->phase,
    ExposureTransitionPhase::kQueued);
  EXPECT_NEAR(ServicePixel(service, signal), .25F / 256.0F, 2e-5);
}

NOLINT_TEST_F(
  ExposureGpuTest, RetiredViewAcknowledgementCannotApplyToReusedHandle)
{
  auto service = PostProcessService(*renderer_);
  const auto signal = Uniform(.25F, 4U, 4U);
  const auto first
    = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
      ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(first.has_value());
  ServicePixel(service, signal);
  service.RemoveViewState(
    ctx_.current_view.view_id, ctx_.current_view.view_state_handle);
  const auto next = renderer_->QueueExposureTransition(
    first->target, ExposureTransitionPolicy::kRemeter);
  ASSERT_TRUE(next.has_value());
  EXPECT_NE(next->lifetime, first->lifetime);
  service.OnFrameStart(frame::SequenceNumber { ++sequence_ }, ctx_.frame_slot);
  const auto status = renderer_->InspectExposureTransition(next->target);
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(status->phase, ExposureTransitionPhase::kQueued);
  EXPECT_EQ(status->applied_generation, 0U);
  EXPECT_FALSE(renderer_->RetryExposureTransition(*first).has_value());
}

NOLINT_TEST_F(ExposureGpuTest, PreserveHoldsOnlyItsEventFrame)
{
  const auto before = Run(Uniform(.25F));
  const auto token = renderer_->QueueExposureTransition(
    ctx_.current_view.view_state_handle, ExposureTransitionPolicy::kPreserve);
  ASSERT_TRUE(token.has_value());
  const auto event = Run(Uniform(8.0F), {}, 1.0F, nullptr, 1.0F, true, *token);
  EXPECT_EQ(event.state.displayed_scale, before.state.displayed_scale);
  EXPECT_EQ(event.state.applied_generation[0], token->generation);
  const auto next = Run(Uniform(8.0F), {}, 1.0F, nullptr, 1.0F, true, *token);
  EXPECT_NEAR(
    next.state.displayed_scale, before.state.displayed_scale / 8.0F, 2e-5F);
}

NOLINT_TEST_F(ExposureGpuTest, SeedOutsideLockedRangeOwnsOnlyItsEventFrame)
{
  auto settings = scene::ExposureSettings {};
  settings.min_ev = settings.max_ev = 4.0F;
  const auto token
    = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
      ExposureTransitionPolicy::kSeedFromEv100, 12.0F);
  ASSERT_TRUE(token.has_value());
  const auto event
    = Run(Signal {}, settings, 0.0F, nullptr, 1.0F, false, *token);
  EXPECT_EQ(event.state.displayed_scale, 0x1p-12F);
  EXPECT_EQ(event.state.applied_generation[0], token->generation);
  const auto next
    = Run(Signal {}, settings, 0.0F, nullptr, 1.0F, false, *token);
  EXPECT_EQ(next.state.displayed_scale, 0x1p-4F);
}

NOLINT_TEST_F(ExposureGpuTest, ServiceManualConsumesExactGpuGain)
{
  auto service = PostProcessService(*renderer_);
  const auto signal = Uniform(4096.0F, 4U, 4U);
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  for (const auto ev : { 14.0F, 16.0F, 32.0F }) {
    settings.manual_ev = ev;
    const auto expected = std::exp2(12.0F - ev);
    EXPECT_NEAR(
      ServicePixel(service, signal, settings), expected, expected * 2e-5F);
  }
}

NOLINT_TEST_F(
  ExposureGpuTest, CompletedUnsupportedSeedRejectsWithoutChangingGain)
{
  auto service = PostProcessService(*renderer_);
  const auto signal = Uniform(.25F, 4U, 4U);
  const auto before = ServicePixel(service, signal);
  const auto token
    = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
      ExposureTransitionPolicy::kSeedFromEv100, 1000.0F);
  ASSERT_TRUE(token.has_value());
  EXPECT_EQ(ServicePixel(service, signal), before);
  service.OnFrameStart(frame::SequenceNumber { ++sequence_ }, ctx_.frame_slot);
  const auto status = renderer_->InspectExposureTransition(token->target);
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(status->phase, ExposureTransitionPhase::kRejected);
  EXPECT_EQ(status->error, ExposureTransitionError::kUnsupportedSeed);
  EXPECT_EQ(status->applied_generation, 0U);
}

NOLINT_TEST_F(
  ExposureGpuTest, LateModeChangeCannotAlterCapturedTransitionSemantics)
{
  auto service = PostProcessService(*renderer_);
  const auto signal = Uniform(.25F, 4U, 4U);
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 4.0F;
  const auto token
    = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
      ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(token.has_value());
  const auto pixel = ServicePixel(service, signal, settings, false, 0.0F, [&] {
    settings.key = 12.5F;
    settings.mode = engine::ExposureMode::kAuto;
    const auto& late = service.ResolveViewExposureSettings(
      ctx_.current_view.view_state_handle, settings);
    auto config = service.GetConfig();
    config.exposure = late.resolved.authored;
    service.SetConfig(config);
  });
  EXPECT_NEAR(pixel, .25F / 16.0F, 2e-5F);
  service.OnFrameStart(frame::SequenceNumber { ++sequence_ }, ctx_.frame_slot);
  const auto status = renderer_->InspectExposureTransition(token->target);
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(status->phase, ExposureTransitionPhase::kRejected);
  EXPECT_EQ(status->error, ExposureTransitionError::kNotAuto);
  EXPECT_NEAR(
    ServicePixel(service, signal, settings, false, 1.0F), .25F / 16.0F, 2e-5F);
}

NOLINT_TEST_F(
  ExposureGpuTest, LateMaskRemovalCannotEnableMeteringForCapturedFrame)
{
  auto service = PostProcessService(*renderer_);
  const auto signal = Uniform(.25F, 4U, 4U);
  auto settings = scene::ExposureSettings {};
  settings.metering_mask = content::ResourceKey { 123U };
  const auto pixel = ServicePixel(service, signal, settings, false, 0.0F, [&] {
    settings.key = 12.5F;
    settings.metering_mask = {};
    const auto& late = service.ResolveViewExposureSettings(
      ctx_.current_view.view_state_handle, settings);
    auto config = service.GetConfig();
    config.exposure = late.resolved.authored;
    service.SetConfig(config);
  });
  // The initial invalid-mask fallback uses the canonical key 10 at EV0.
  EXPECT_NEAR(pixel, .25F * .8F, 2e-5F);
  EXPECT_NEAR(ServicePixel(service, signal, settings), .18F, 2e-5F);
}

NOLINT_TEST_F(
  ExposureGpuTest, SharedConsumersUsePriorOwnerStateInBothRenderOrders)
{
  const auto owner_signal = Uniform(.25F);
  const auto consumer_signal = Uniform(8.0F);
  const auto config = SharedConfig();
  const auto source = postprocess::ExposurePass::Source {
    .handle = CompositionView::ViewStateHandle { 1U }, .config = config
  };
  for (bool consumer_first : { false, true }) {
    pass_->RemoveViewState(CompositionView::ViewStateHandle { 1U });
    pass_->RemoveViewState(CompositionView::ViewStateHandle { 2U });
    for (unsigned frame_index = 0; frame_index < 2U; ++frame_index) {
      ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
      ctx_.delta_time = 0.0F;
      postprocess::ExposurePass::Result owner;
      postprocess::ExposurePass::Result consumer;
      const auto render_owner = [&] {
        ctx_.current_view.view_id = ViewId { 1U };
        ctx_.current_view.view_state_handle = source.handle;
        owner = RecordShared(owner_signal, config);
      };
      const auto render_consumer = [&] {
        ctx_.current_view.view_id = ViewId { 2U };
        ctx_.current_view.view_state_handle
          = CompositionView::ViewStateHandle { 2U };
        consumer = RecordShared(consumer_signal, config, &source);
      };
      if (consumer_first) {
        render_consumer();
        render_owner();
      } else {
        render_owner();
        render_consumer();
      }
      ASSERT_TRUE(owner.executed);
      ASSERT_TRUE(consumer.executed);
      EXPECT_EQ(consumer.histogram_buffer, nullptr);
      EXPECT_NE(owner.state, consumer.state);
      EXPECT_NEAR(ReadState(owner).displayed_scale, .72F, 2e-5F);
      const auto borrowed = ReadState(consumer);
      EXPECT_NEAR(
        borrowed.displayed_scale, frame_index == 0U ? 1.0F : .72F, 2e-5F);
      EXPECT_EQ(borrowed.flags & (4U | 8U | 512U), 0U);
      EXPECT_NE(borrowed.flags & 128U, 0U);
    }
  }
}

NOLINT_TEST_F(ExposureGpuTest, SharedSourceResetBecomesVisibleOnTheNextFrame)
{
  const auto signal = Uniform(.25F);
  const auto config = SharedConfig();
  auto source = postprocess::ExposurePass::Source {
    .handle = CompositionView::ViewStateHandle { 1U }, .config = config
  };
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  const auto first = RecordShared(signal, config);
  ASSERT_TRUE(first.executed);
  const auto seed = renderer_->QueueExposureTransition(
    source.handle, ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(seed.has_value());
  source.transition = *seed;
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  const auto reset = RecordShared(signal, config, nullptr, *seed);
  ctx_.current_view.view_id = ViewId { 2U };
  ctx_.current_view.view_state_handle = CompositionView::ViewStateHandle { 2U };
  const auto same_frame = RecordShared(signal, config, &source);
  EXPECT_EQ(ReadState(reset).displayed_scale, 0x1p-8F);
  EXPECT_NEAR(ReadState(same_frame).displayed_scale, .72F, 2e-5F);
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  // The source is inactive in this frame; retain its last completed
  // publication.
  const auto next = RecordShared(signal, config, &source);
  EXPECT_EQ(ReadState(next).displayed_scale, 0x1p-8F);
}

NOLINT_TEST_F(ExposureGpuTest, SharedBootstrapUsesSourceModeBoundsAndSeed)
{
  const auto signal = Uniform(8.0F);
  auto consumer_settings = scene::ExposureSettings {};
  consumer_settings.enabled = false;
  const auto consumer_config = SharedConfig(consumer_settings);
  auto settings = scene::ExposureSettings {};
  settings.min_ev = 4.0F;
  auto source = postprocess::ExposurePass::Source { .handle
    = CompositionView::ViewStateHandle { 1U },
    .config = SharedConfig(settings) };
  ctx_.current_view.view_id = ViewId { 2U };
  ctx_.current_view.view_state_handle = CompositionView::ViewStateHandle { 2U };
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  const auto initial = RecordShared(signal, consumer_config, &source);
  EXPECT_EQ(ReadState(initial).displayed_scale, 0x1p-4F);
  EXPECT_EQ(ReadState(initial).fallback_reason, 3U);
  const auto seed = renderer_->QueueExposureTransition(
    source.handle, ExposureTransitionPolicy::kSeedFromEv100, -2.0F);
  ASSERT_TRUE(seed.has_value());
  source.transition = *seed;
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  EXPECT_EQ(
    ReadState(RecordShared(signal, consumer_config, &source)).displayed_scale,
    4.0F);
  source.transition.reset();
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 14.0F;
  source.config = SharedConfig(settings);
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  EXPECT_EQ(
    ReadState(RecordShared(signal, consumer_config, &source)).displayed_scale,
    0x1p-14F);
  settings.mode = engine::ExposureMode::kManualCamera;
  source.config
    = SharedConfig(settings, static_cast<float>(std::log2(15125.0)));
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  EXPECT_NEAR(
    ReadState(RecordShared(signal, consumer_config, &source)).displayed_scale,
    1.0 / 15125.0, 2e-5 / 15125.0);
  settings.enabled = false;
  source.config = SharedConfig(settings);
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  EXPECT_EQ(
    ReadState(RecordShared(signal, consumer_config, &source)).displayed_scale,
    1.0F);
  settings.enabled = true;
  settings.mode = engine::ExposureMode::kAuto;
  settings.target_luminance = 0.0F;
  source.config = SharedConfig(settings);
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  const auto zero = ReadState(RecordShared(signal, consumer_config, &source));
  EXPECT_EQ(zero.displayed_scale, 0.0F);
  EXPECT_EQ(zero.latent_scale, 0x1p-4F);
}

NOLINT_TEST_F(
  ExposureGpuTest, SharedConsumerTransitionIsRejectedAfterGpuCompletion)
{
  auto service = PostProcessService(*renderer_);
  ctx_.current_view.view_id = ViewId { 2U };
  ctx_.current_view.view_state_handle = CompositionView::ViewStateHandle { 2U };
  ctx_.current_view.exposure_view_id = ViewId { 1U };
  ctx_.current_view.exposure_view_state_handle
    = CompositionView::ViewStateHandle { 1U };
  const auto token
    = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
      ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(token.has_value());
  const auto signal = Uniform(.25F, 4U, 4U);
  const auto capture = BeginOptionalCapture();
  const auto pixel
    = ServicePixel(service, signal, {}, false, 0.0F, [&] {
        auto source = scene::ExposureSettings {};
        source.key = 12.5F;
        source.mode = engine::ExposureMode::kManual;
        source.manual_ev = 4.0F;
        static_cast<void>(service.CaptureViewExposureSettings(
          ViewId { 1U }, CompositionView::ViewStateHandle { 1U }, source));
      });
  EXPECT_NEAR(pixel, .25F / 16.0F, 2e-5F);
  EXPECT_FALSE(service.GetLastExecutionState().auto_exposure_executed);
  service.OnFrameStart(frame::SequenceNumber { ++sequence_ }, ctx_.frame_slot);
  const auto status = renderer_->InspectExposureTransition(token->target);
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(status->phase, ExposureTransitionPhase::kRejected);
  EXPECT_EQ(status->error, ExposureTransitionError::kSharedConsumer);
  if (capture)
    EXPECT_TRUE(capture->EndCapture());
}

NOLINT_TEST_F(ExposureGpuTest,
  RegisteredInactiveSourceBootstrapsConsumersWithoutConsumingItsSeed)
{
  auto service = PostProcessService(*renderer_);
  auto frame = engine::FrameContext {};
  auto texture = CreateRegisteredTexture(TextureDesc { .width = 4U,
    .height = 4U,
    .format = Format::kRGBA32Float,
    .is_render_target = true,
    .initial_state = ResourceStates::kCommon });
  auto target = Backend().CreateFramebuffer(
    FramebufferDesc {}.AddColorAttachment(texture));
  auto view = CompositionView {};
  view.id = ViewId { 50U };
  view.view_state_handle = CompositionView::ViewStateHandle { 50U };
  view.render_settings.exposure = scene::ExposureSettings {};
  view.render_settings.exposure->key = 12.5F;
  const auto published = renderer_->PublishRuntimeCompositionView(frame,
    { .composition_view = view,
      .render_target = observer_ptr { target.get() } });
  ASSERT_NE(published, kInvalidViewId);
  const auto token = renderer_->QueueExposureTransition(
    view.view_state_handle, ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(token.has_value());
  ctx_.current_view.view_id = ViewId { 900U };
  ctx_.current_view.view_state_handle
    = CompositionView::ViewStateHandle { 900U };
  ctx_.current_view.exposure_view_id = published;
  ctx_.current_view.exposure_view_state_handle = view.view_state_handle;
  EXPECT_NEAR(
    ServicePixel(service, Uniform(.25F, 4U, 4U)), .25F / 256.0F, 2e-5F);
  service.OnFrameStart(frame::SequenceNumber { ++sequence_ }, ctx_.frame_slot);
  EXPECT_EQ(renderer_->InspectExposureTransition(token->target)->phase,
    ExposureTransitionPhase::kQueued);
}

NOLINT_TEST_F(
  ExposureGpuTest, SharingDiscardsDormantMeterHistoryAndExplicitDetachRemeters)
{
  ctx_.current_view.view_id = ViewId { 2U };
  ctx_.current_view.view_state_handle = CompositionView::ViewStateHandle { 2U };
  const auto before = Run(Uniform(.25F));
  EXPECT_NEAR(before.state.displayed_scale, .72F, 2e-5F);
  const auto config = SharedConfig();
  const auto source = postprocess::ExposurePass::Source {
    .handle = CompositionView::ViewStateHandle { 1U }, .config = config
  };
  const auto dark = Uniform(8.0F);
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  const auto shared = ReadState(RecordShared(dark, config, &source));
  EXPECT_EQ(shared.displayed_scale, 1.0F);
  EXPECT_EQ(shared.flags & (4U | 8U | 512U), 0U);
  const auto remeter = renderer_->QueueExposureTransition(
    ctx_.current_view.view_state_handle, ExposureTransitionPolicy::kRemeter);
  ASSERT_TRUE(remeter.has_value());
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  ctx_.delta_time = 0.0F;
  const auto detached
    = ReadState(RecordShared(dark, config, nullptr, *remeter));
  EXPECT_NEAR(detached.displayed_scale, .0225F, 2e-5F);
  EXPECT_EQ(detached.applied_generation[0], remeter->generation);
  EXPECT_EQ(detached.flags & 128U, 0U);
}

NOLINT_TEST_F(ExposureGpuTest,
  BackloggedStatusAcknowledgesLatestSubmissionWithoutRenderingOwnerAgain)
{
  auto service = PostProcessService(*renderer_);
  const auto signal = Uniform(.25F);
  std::optional<ExposureTransitionToken> latest;
  // Withhold CPU delivery while real GPU status copies fill the bounded queue.
  // Subsequent completed states must coalesce into one retained catch-up
  // record.
  for (unsigned i = 1U; i <= 6U; ++i) {
    const auto token
      = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
        ExposureTransitionPolicy::kSeedFromEv100, static_cast<float>(i));
    ASSERT_TRUE(token.has_value());
    latest = *token;
    const auto result = Run(signal, {}, 0.0F, nullptr, 1.0F, true, *token);
    EXPECT_EQ(result.state.applied_generation[0], token->generation);
    vortex::testing::RendererPublicationProbe::EnqueueExposureStatus(
      service, *token, last_state_, ctx_, sequence_);
  }
  const auto full
    = vortex::testing::RendererPublicationProbe::ExposureStatusCounts(
      service, latest->target);
  EXPECT_EQ(full.first, frame::kFramesInFlight.get());
  EXPECT_EQ(full.second, 1U);
  service.OnFrameStart(
    frame::SequenceNumber { ++sequence_ }, frame::Slot { 1U });
  EXPECT_EQ(renderer_->InspectExposureTransition(latest->target)->phase,
    ExposureTransitionPhase::kQueued);
  EXPECT_EQ(
    renderer_->InspectExposureTransition(latest->target)->applied_generation,
    3U);
  WaitForQueueIdle();
  service.OnFrameStart(
    frame::SequenceNumber { ++sequence_ }, frame::Slot { 2U });
  const auto completed = renderer_->InspectExposureTransition(latest->target);
  ASSERT_TRUE(completed.has_value());
  EXPECT_EQ(completed->phase, ExposureTransitionPhase::kApplied);
  EXPECT_EQ(completed->applied_generation, latest->generation);
  EXPECT_EQ(vortex::testing::RendererPublicationProbe::ExposureStatusCounts(
              service, latest->target),
    (std::pair<std::size_t, std::size_t> { 0U, 0U }));
}

NOLINT_TEST_F(
  ExposureGpuTest, DeferredOldAcknowledgementCannotConsumeNewUnsubmittedIntent)
{
  auto service = PostProcessService(*renderer_);
  const auto signal = Uniform(.25F);
  std::optional<ExposureTransitionToken> submitted;
  for (unsigned i = 0U; i < 4U; ++i) {
    const auto token
      = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
        ExposureTransitionPolicy::kSeedFromEv100, static_cast<float>(i));
    ASSERT_TRUE(token.has_value());
    submitted = *token;
    Run(signal, {}, 0.0F, nullptr, 1.0F, true, *token);
    vortex::testing::RendererPublicationProbe::EnqueueExposureStatus(
      service, *token, last_state_, ctx_, sequence_);
  }
  const auto pending = renderer_->QueueExposureTransition(
    submitted->target, ExposureTransitionPolicy::kRemeter);
  ASSERT_TRUE(pending.has_value());
  service.OnFrameStart(
    frame::SequenceNumber { ++sequence_ }, frame::Slot { 1U });
  WaitForQueueIdle();
  service.OnFrameStart(
    frame::SequenceNumber { ++sequence_ }, frame::Slot { 2U });
  const auto status = renderer_->InspectExposureTransition(pending->target);
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(status->request, *pending);
  EXPECT_EQ(status->phase, ExposureTransitionPhase::kQueued);
  EXPECT_EQ(status->applied_generation, submitted->generation);
}

NOLINT_TEST_F(
  ExposureGpuTest, InactiveInvalidRequestsCannotReactivateAfterSettingsChange)
{
  auto service = PostProcessService(*renderer_);
  auto frame = engine::FrameContext {};
  const auto signal = Uniform(.25F, 4U, 4U);
  for (unsigned kind = 0U; kind < 3U; ++kind) {
    const auto handle = CompositionView::ViewStateHandle { 50U + kind };
    const auto intent = ViewId { 50U + kind };
    auto settings = scene::ExposureSettings {};
    settings.key = 12.5F;
    settings.mode = kind == 0U ? engine::ExposureMode::kManual
                               : engine::ExposureMode::kAuto;
    settings.enabled = kind != 1U;
    const auto view = PublishExposureOwner(frame, intent, handle, settings);
    ASSERT_NE(view, kInvalidViewId);
    const auto token = renderer_->QueueExposureTransition(handle,
      ExposureTransitionPolicy::kSeedFromEv100, kind == 2U ? 1000.0F : 8.0F);
    ASSERT_TRUE(token.has_value());
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    service.CaptureRegisteredExposureControls(ctx_);
    const auto rejected = renderer_->InspectExposureTransition(handle);
    ASSERT_TRUE(rejected.has_value());
    EXPECT_EQ(rejected->phase, ExposureTransitionPhase::kRejected);
    EXPECT_EQ(rejected->error,
      kind == 2U ? ExposureTransitionError::kUnsupportedSeed
                 : ExposureTransitionError::kNotAuto);
    settings.enabled = true;
    settings.mode = engine::ExposureMode::kAuto;
    if (kind == 2U) {
      // The formerly unsupported seed would now produce gain one; the locked
      // target instead produces 1/16. A rejected generation must remain
      // rejected.
      settings.compensation_ev = 1000.0F;
      settings.min_ev = settings.max_ev = 1004.0F;
    }
    ASSERT_EQ(PublishExposureOwner(frame, intent, handle, settings), view);
    ctx_.current_view.view_id = view;
    ctx_.current_view.view_state_handle = handle;
    EXPECT_NEAR(ServicePixel(service, signal, settings),
      kind == 2U ? .25F / 16.0F : .18F, 2e-5F);
    const auto state
      = vortex::testing::RendererPublicationProbe::ExposureStateForView(
        service, handle);
    ASSERT_NE(state, nullptr);
    const auto gpu = Read<ExposureStateData>(
      *state->buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(gpu.requested_generation[0], token->generation);
    EXPECT_EQ(gpu.applied_generation[0], 0U);
    EXPECT_NE(gpu.flags & (1U << 12U), 0U);
    EXPECT_EQ(renderer_->RetryExposureTransition(*token),
      ExposureTransitionPhase::kRejected);
  }
}

NOLINT_TEST_F(
  ExposureGpuTest, InactiveModeValidationCannotRejectAnObservedSubmission)
{
  auto service = PostProcessService(*renderer_);
  auto frame = engine::FrameContext {};
  auto settings = scene::ExposureSettings {};
  settings.key = 12.5F;
  const auto handle = CompositionView::ViewStateHandle { 50U };
  const auto view
    = PublishExposureOwner(frame, ViewId { 50U }, handle, settings);
  ctx_.current_view.view_id = view;
  ctx_.current_view.view_state_handle = handle;
  const auto token = renderer_->QueueExposureTransition(
    handle, ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(token.has_value());
  Run(Uniform(.25F), settings, 0.0F, nullptr, 1.0F, true, *token);
  service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
  vortex::testing::RendererPublicationProbe::EnqueueExposureStatus(
    service, *token, last_state_, ctx_, sequence_);
  settings.mode = engine::ExposureMode::kManual;
  PublishExposureOwner(frame, ViewId { 50U }, handle, settings);
  service.CaptureRegisteredExposureControls(ctx_);
  EXPECT_EQ(renderer_->InspectExposureTransition(handle)->phase,
    ExposureTransitionPhase::kQueued);
  WaitForQueueIdle();
  service.OnFrameStart(frame::SequenceNumber { ++sequence_ }, ctx_.frame_slot);
  EXPECT_EQ(renderer_->InspectExposureTransition(handle)->phase,
    ExposureTransitionPhase::kApplied);
}

NOLINT_TEST_F(ExposureGpuTest, InactiveDiagnosticOwnerPreservesPendingRequest)
{
  auto service = PostProcessService(*renderer_);
  auto frame = engine::FrameContext {};
  auto settings = scene::ExposureSettings {};
  settings.key = 12.5F;
  settings.mode = engine::ExposureMode::kManual;
  const auto handle = CompositionView::ViewStateHandle { 50U };
  const auto view = PublishExposureOwner(
    frame, ViewId { 50U }, handle, settings, kInvalidViewId, true);
  const auto token = renderer_->QueueExposureTransition(
    handle, ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(token.has_value());
  service.OnFrameStart(frame::SequenceNumber { ++sequence_ }, ctx_.frame_slot);
  service.CaptureRegisteredExposureControls(ctx_);
  EXPECT_EQ(renderer_->InspectExposureTransition(handle)->phase,
    ExposureTransitionPhase::kQueued);
  settings.mode = engine::ExposureMode::kAuto;
  PublishExposureOwner(frame, ViewId { 50U }, handle, settings);
  ctx_.current_view.view_id = view;
  ctx_.current_view.view_state_handle = handle;
  EXPECT_NEAR(ServicePixel(service, Uniform(.25F, 4U, 4U), settings),
    .25F / 256.0F, 2e-5F);
}

NOLINT_TEST_F(
  ExposureGpuTest, InactiveSharingConsumerRequestStaysRejectedAfterDetach)
{
  auto service = PostProcessService(*renderer_);
  auto frame = engine::FrameContext {};
  auto settings = scene::ExposureSettings {};
  settings.key = 12.5F;
  PublishExposureOwner(
    frame, ViewId { 50U }, CompositionView::ViewStateHandle { 50U }, settings);
  const auto handle = CompositionView::ViewStateHandle { 60U };
  const auto view = PublishExposureOwner(
    frame, ViewId { 60U }, handle, settings, ViewId { 50U });
  const auto token = renderer_->QueueExposureTransition(
    handle, ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(token.has_value());
  service.OnFrameStart(frame::SequenceNumber { ++sequence_ }, ctx_.frame_slot);
  service.CaptureRegisteredExposureControls(ctx_);
  EXPECT_EQ(renderer_->InspectExposureTransition(handle)->error,
    ExposureTransitionError::kSharedConsumer);
  PublishExposureOwner(frame, ViewId { 60U }, handle, settings);
  ctx_.current_view.view_id = view;
  ctx_.current_view.view_state_handle = handle;
  EXPECT_NEAR(
    ServicePixel(service, Uniform(.25F, 4U, 4U), settings), .18F, 2e-5F);
  EXPECT_EQ(renderer_->RetryExposureTransition(*token),
    ExposureTransitionPhase::kSuperseded);
}

NOLINT_TEST_F(
  ExposureGpuTest, AutoDetachRemetersAtZeroDeltaWithOneImplicitGeneration)
{
  auto service = PostProcessService(*renderer_);
  auto frame = engine::FrameContext {};
  const auto consumer = StartSharedServiceView(service, frame);
  auto settings = scene::ExposureSettings {};
  settings.key = 12.5F;
  PublishExposureOwner(
    frame, ViewId { 60U }, ctx_.current_view.view_state_handle, settings);
  ctx_.current_view.exposure_view_id = consumer;
  ctx_.current_view.exposure_view_state_handle
    = ctx_.current_view.view_state_handle;
  EXPECT_NEAR(ServicePixel(service, Uniform(8.0F, 4U, 4U)), .18F, 2e-5F);
  const auto event
    = renderer_->InspectExposureTransition(ctx_.current_view.view_state_handle);
  ASSERT_TRUE(event.has_value());
  EXPECT_EQ(event->request.policy, ExposureTransitionPolicy::kRemeter);
  EXPECT_GT(event->request.generation, 0U);
  EXPECT_NEAR(
    ServicePixel(service, Uniform(.25F, 4U, 4U)), .25F * .0225F, 2e-5F);
  const auto after
    = renderer_->InspectExposureTransition(event->request.target);
  EXPECT_EQ(after->request, event->request);
  EXPECT_EQ(after->phase, ExposureTransitionPhase::kApplied);
}

NOLINT_TEST_F(ExposureGpuTest, ExplicitDetachPolicyOverridesDefaultRemeter)
{
  auto service = PostProcessService(*renderer_);
  auto frame = engine::FrameContext {};
  for (const auto policy : { ExposureTransitionPolicy::kPreserve,
         ExposureTransitionPolicy::kSeedFromEv100 }) {
    const auto consumer = StartSharedServiceView(service, frame);
    const auto token = renderer_->QueueExposureTransition(
      ctx_.current_view.view_state_handle, policy,
      policy == ExposureTransitionPolicy::kSeedFromEv100
        ? std::optional { 8.0F }
        : std::nullopt);
    ASSERT_TRUE(token.has_value());
    auto settings = scene::ExposureSettings {};
    settings.key = 12.5F;
    PublishExposureOwner(frame, ViewId { 60U }, token->target, settings);
    ctx_.current_view.exposure_view_id = consumer;
    ctx_.current_view.exposure_view_state_handle = token->target;
    EXPECT_NEAR(ServicePixel(service, Uniform(8.0F, 4U, 4U)),
      policy == ExposureTransitionPolicy::kPreserve ? .5F : 8.0F / 256.0F,
      2e-5F);
    EXPECT_EQ(
      renderer_->InspectExposureTransition(token->target)->request, *token);
  }
}

NOLINT_TEST_F(
  ExposureGpuTest, FixedModeDetachAppliesAuthoredGainWithoutAnAutoRejection)
{
  auto service = PostProcessService(*renderer_);
  auto frame = engine::FrameContext {};
  for (const bool enabled : { true, false }) {
    auto settings = scene::ExposureSettings {};
    settings.key = 12.5F;
    settings.mode = engine::ExposureMode::kManual;
    settings.manual_ev = 8.0F;
    settings.enabled = enabled;
    const auto consumer = StartSharedServiceView(service, frame, settings);
    PublishExposureOwner(
      frame, ViewId { 60U }, ctx_.current_view.view_state_handle, settings);
    ctx_.current_view.exposure_view_id = consumer;
    ctx_.current_view.exposure_view_state_handle
      = ctx_.current_view.view_state_handle;
    EXPECT_NEAR(ServicePixel(service, Uniform(.25F, 4U, 4U), settings),
      enabled ? .25F / 256.0F : .25F, 2e-5F);
    service.OnFrameStart(
      frame::SequenceNumber { ++sequence_ }, ctx_.frame_slot);
    const auto status = renderer_->InspectExposureTransition(
      ctx_.current_view.view_state_handle);
    ASSERT_TRUE(status.has_value());
    EXPECT_EQ(status->phase, ExposureTransitionPhase::kApplied);
    EXPECT_EQ(status->request.policy, ExposureTransitionPolicy::kPreserve);
  }
}

NOLINT_TEST_F(
  ExposureGpuTest, DiagnosticDetachDefersTheImplicitEventUntilNormalRendering)
{
  auto service = PostProcessService(*renderer_);
  auto frame = engine::FrameContext {};
  const auto consumer = StartSharedServiceView(service, frame);
  auto settings = scene::ExposureSettings {};
  settings.key = 12.5F;
  PublishExposureOwner(
    frame, ViewId { 60U }, ctx_.current_view.view_state_handle, settings);
  ctx_.current_view.exposure_view_id = consumer;
  ctx_.current_view.exposure_view_state_handle
    = ctx_.current_view.view_state_handle;
  EXPECT_NEAR(
    ServicePixel(service, Uniform(.25F, 4U, 4U), settings, true), .25F, 2e-5F);
  EXPECT_FALSE(
    renderer_->InspectExposureTransition(ctx_.current_view.view_state_handle)
      .has_value());
  EXPECT_NEAR(
    ServicePixel(service, Uniform(8.0F, 4U, 4U), settings), .18F, 2e-5F);
  EXPECT_EQ(
    renderer_->InspectExposureTransition(ctx_.current_view.view_state_handle)
      ->request.policy,
    ExposureTransitionPolicy::kRemeter);
}

NOLINT_TEST_F(
  ExposureGpuTest, ReusedLifetimeCannotConsumeOldOwnOrSharedPriorState)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 4.0F;
  const auto signal = Uniform(.25F);
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  const auto old
    = RecordShared(signal, SharedConfig(settings), nullptr, {}, 100U);
  EXPECT_EQ(ReadState(old).displayed_scale, 0x1p-4F);
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  pass_->OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
  const auto config = SharedConfig();
  const auto new_owner = RecordShared(signal, config, nullptr, {}, 101U);
  EXPECT_NEAR(ReadState(new_owner).displayed_scale, .72F, 2e-5F);
  const auto source = postprocess::ExposurePass::Source { .handle
    = CompositionView::ViewStateHandle { 1U },
    .config = config,
    .lifetime = 101U };
  ctx_.current_view.view_id = ViewId { 2U };
  ctx_.current_view.view_state_handle = CompositionView::ViewStateHandle { 2U };
  const auto consumer = RecordShared(signal, config, &source, {}, 200U);
  EXPECT_EQ(ReadState(consumer).displayed_scale, 1.0F);
  EXPECT_EQ(consumer.state->owner_lifetime, 200U);
}

NOLINT_TEST_F(ExposureGpuTest, CanceledDetachDoesNotIssueAnImplicitGeneration)
{
  auto service = PostProcessService(*renderer_);
  auto frame = engine::FrameContext {};
  StartSharedServiceView(service, frame);
  auto settings = scene::ExposureSettings {};
  settings.key = 12.5F;
  const auto handle = ctx_.current_view.view_state_handle;
  PublishExposureOwner(frame, ViewId { 60U }, handle, settings);
  PublishExposureOwner(frame, ViewId { 60U }, handle, settings, ViewId { 50U });
  EXPECT_NEAR(ServicePixel(service, Uniform(8.0F, 4U, 4U)), .5F, 2e-5F);
  EXPECT_FALSE(renderer_->InspectExposureTransition(handle).has_value());
}

NOLINT_TEST_F(ExposureGpuTest,
  PublicHandleReplacementRetiresHistoryRequestsAndMaskOwnership)
{
  auto frame = engine::FrameContext {};
  owned_asset_loader_ = std::make_unique<vortex::testing::FakeAssetLoader>();
  auto output = CreateRegisteredTexture(TextureDesc { .width = 4U,
    .height = 4U,
    .format = Format::kRGBA32Float,
    .is_render_target = true,
    .initial_state = ResourceStates::kCommon });
  auto framebuffer = Backend().CreateFramebuffer(
    FramebufferDesc {}.AddColorAttachment(output));
  auto params = ResolvedView::Params {};
  params.view_config.viewport = { .width = 4.0F, .height = 4.0F };
  auto session
    = renderer_->ForSinglePassHarness()
        .SetFrameSession({ .frame_slot = frame::Slot { 0U },
          .frame_sequence = frame::SequenceNumber { 1U },
          .delta_time_seconds = 0.0F })
        .SetResolvedView(
          { .view_id = ViewId { 1000U }, .value = ResolvedView { params } })
        .SetOutputTarget({ .framebuffer = observer_ptr { framebuffer.get() } })
        .Finalize();
  ASSERT_TRUE(session.has_value());
  auto* scene_renderer
    = vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_);
  ASSERT_NE(scene_renderer, nullptr);
  auto* service
    = vortex::testing::RendererPublicationProbe::GetPostProcessService(
      *scene_renderer);
  ASSERT_NE(service, nullptr);
  vortex::testing::RendererPublicationProbe::SetExposureAssetLoader(
    *service, observer_ptr { owned_asset_loader_.get() });
  auto settings = scene::ExposureSettings {};
  settings.key = 12.5F;
  const auto payload = vortex::testing::MakeCookedTexture1x1Rgba8Payload();
  settings.metering_mask
    = owned_asset_loader_->PreloadCookedTexture(std::span(payload));
  const auto h1 = CompositionView::ViewStateHandle { 50U };
  const auto h2 = CompositionView::ViewStateHandle { 51U };
  const auto first_view
    = PublishExposureOwner(frame, ViewId { 50U }, h1, settings);
  std::weak_ptr<const resources::TextureBinder::ReadyTexture> old_mask;
  for (unsigned i = 0U; i < 8U; ++i) {
    WaitForQueueIdle();
    ctx_.frame_slot = frame::Slot { i % 3U };
    renderer_->GetUploadCoordinator().OnFrameStart(
      internal::RendererTagFactory::Get(), ctx_.frame_slot);
    service->OnFrameStart(
      frame::SequenceNumber { ++sequence_ }, ctx_.frame_slot);
    const auto& ready = service->ResolveViewExposureSettings(h1, settings);
    if (ready.mask) {
      old_mask = ready.mask;
      break;
    }
  }
  ASSERT_FALSE(old_mask.expired());
  const auto mask_slot = ctx_.frame_slot;
  ctx_.current_view.view_id = first_view;
  ctx_.current_view.view_state_handle = h1;
  const auto applied = renderer_->QueueExposureTransition(
    h1, ExposureTransitionPolicy::kSeedFromEv100, 4.0F);
  ASSERT_TRUE(applied.has_value());
  EXPECT_NEAR(ServicePixel(*service, Uniform(.25F, 4U, 4U), settings),
    .25F / 16.0F, 2e-5F);
  const auto old_state
    = vortex::testing::RendererPublicationProbe::ExposureStateForView(
      *service, h1);
  ASSERT_NE(old_state, nullptr);
  const auto pending = renderer_->QueueExposureTransition(
    h1, ExposureTransitionPolicy::kRemeter);
  ASSERT_TRUE(pending.has_value());
  const auto old_mask_key = settings.metering_mask;
  settings.metering_mask = {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 8.0F;
  ASSERT_EQ(
    PublishExposureOwner(frame, ViewId { 50U }, h2, settings), first_view);
  EXPECT_FALSE(renderer_->RetryExposureTransition(*pending).has_value());
  EXPECT_FALSE(renderer_->RetryExposureTransition(*applied).has_value());
  EXPECT_EQ(vortex::testing::RendererPublicationProbe::ExposureStateForView(
              *service, h1),
    nullptr);
  EXPECT_EQ(vortex::testing::RendererPublicationProbe::ExposureStatusCounts(
              *service, h1),
    (std::pair<std::size_t, std::size_t> { 0U, 0U }));
  owned_asset_loader_->EmitTextureEviction(
    old_mask_key, content::EvictionReason::kRefCountZero);
  EXPECT_FALSE(old_mask.expired());
  const auto next_view
    = PublishExposureOwner(frame, ViewId { 70U }, h1, settings);
  const auto next = renderer_->QueueExposureTransition(
    h1, ExposureTransitionPolicy::kPreserve);
  ASSERT_TRUE(next.has_value());
  EXPECT_NE(next->lifetime, applied->lifetime);
  ctx_.current_view.view_id = next_view;
  ctx_.current_view.view_state_handle = h1;
  ctx_.frame_slot = frame::Slot { (mask_slot.get() + 1U) % 3U };
  EXPECT_NEAR(ServicePixel(*service, Uniform(.25F, 4U, 4U), settings),
    .25F / 256.0F, 2e-5F);
  EXPECT_EQ(
    Read<ExposureStateData>(*old_state->buffer, ResourceStates::kShaderResource)
      .displayed_scale,
    0x1p-4F);
  EXPECT_FALSE(old_mask.expired());
  WaitForQueueIdle();
  service->OnFrameStart(frame::SequenceNumber { ++sequence_ }, mask_slot);
  EXPECT_TRUE(old_mask.expired());
  const auto next_state
    = vortex::testing::RendererPublicationProbe::ExposureStateForView(
      *service, h1);
  ASSERT_NE(next_state, nullptr);
  const auto last = renderer_->QueueExposureTransition(
    h1, ExposureTransitionPolicy::kPreserve);
  ASSERT_TRUE(last.has_value());
  PublishExposureOwner(
    frame, ViewId { 70U }, CompositionView::kInvalidViewStateHandle, settings);
  EXPECT_FALSE(renderer_->RetryExposureTransition(*last).has_value());
  EXPECT_EQ(vortex::testing::RendererPublicationProbe::ExposureStateForView(
              *service, h1),
    nullptr);
  EXPECT_EQ(Read<ExposureStateData>(
              *next_state->buffer, ResourceStates::kShaderResource)
              .displayed_scale,
    0x1p-8F);
}

NOLINT_TEST_F(
  ExposureGpuTest, RemovedSourcePreservesOneFrameThenAdaptsIndependently)
{
  auto& service = OwnedExposureService();
  auto frame = engine::FrameContext {};
  const auto consumer = StartSharedServiceView(service, frame);
  renderer_->RemovePublishedRuntimeView(frame, ViewId { 50U });
  ctx_.current_view.exposure_view_id = consumer;
  ctx_.current_view.exposure_view_state_handle
    = ctx_.current_view.view_state_handle;
  const auto signal = Uniform(8.0F, 4U, 4U);
  EXPECT_NEAR(ServicePixel(service, signal, {}, false, 1.0F), .5F, 2e-5F);
  const auto state
    = vortex::testing::RendererPublicationProbe::ExposureStateForView(
      service, ctx_.current_view.view_state_handle);
  ASSERT_NE(state, nullptr);
  EXPECT_EQ(
    Read<ExposureStateData>(*state->buffer, ResourceStates::kShaderResource)
      .fallback_reason,
    4U);
  const double target = std::log2(.0225);
  const double expected = target + (-4.0 - target) * std::exp(-2.0);
  EXPECT_NEAR(std::log2(ServicePixel(service, signal, {}, false, 1.0F) / 8.0),
    expected, 5e-4);
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
  const auto source = PublishExposureOwner(
    frame, ViewId { 50U }, CompositionView::ViewStateHandle { 50U }, zero);
  ctx_.current_view.view_id = source;
  ctx_.current_view.view_state_handle
    = CompositionView::ViewStateHandle { 50U };
  ctx_.current_view.exposure_view_state_handle
    = CompositionView::kInvalidViewStateHandle;
  EXPECT_NEAR(ServicePixel(service, Uniform(.25F, 4U, 4U), zero), 0.0F, 2e-5F);
  ctx_.current_view.view_id = consumer;
  ctx_.current_view.view_state_handle
    = CompositionView::ViewStateHandle { 60U };
  ctx_.current_view.exposure_view_id = source;
  ctx_.current_view.exposure_view_state_handle
    = CompositionView::ViewStateHandle { 50U };
  EXPECT_NEAR(ServicePixel(service, Uniform(8.0F, 4U, 4U)), 0.0F, 2e-5F);
  renderer_->RemovePublishedRuntimeView(frame, ViewId { 50U });
  ctx_.current_view.exposure_view_id = consumer;
  ctx_.current_view.exposure_view_state_handle
    = ctx_.current_view.view_state_handle;
  EXPECT_NEAR(
    ServicePixel(service, Uniform(8.0F, 4U, 4U), {}, false, 1.0F), 0.0F, 2e-5F);
  static_cast<void>(ServicePixel(
    service, Uniform(std::numeric_limits<float>::quiet_NaN(), 4U, 4U)));
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
  const auto source_handle = CompositionView::ViewStateHandle { 50U };
  PublishExposureOwner(frame, ViewId { 50U }, source_handle, source_settings);
  auto local = source_settings;
  const auto middle = PublishExposureOwner(frame, ViewId { 60U },
    CompositionView::ViewStateHandle { 60U }, local, ViewId { 50U });
  const auto leaf = PublishExposureOwner(frame, ViewId { 70U },
    CompositionView::ViewStateHandle { 70U }, local, ViewId { 60U });
  const auto seed = renderer_->QueueExposureTransition(
    source_handle, ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(seed.has_value());
  // No SceneRenderer, source state or consumer state exists at removal.
  renderer_->RemovePublishedRuntimeView(frame, ViewId { 50U });
  auto& service = OwnedExposureService();
  for (const auto [view, handle] :
    { std::pair { leaf, CompositionView::ViewStateHandle { 70U } },
      std::pair { middle, CompositionView::ViewStateHandle { 60U } } }) {
    ctx_.current_view.view_id = view;
    ctx_.current_view.view_state_handle = handle;
    ctx_.current_view.exposure_view_id = view;
    ctx_.current_view.exposure_view_state_handle = handle;
    EXPECT_NEAR(ServicePixel(service, Uniform(.25F, 4U, 4U), {}, false, 1.0F),
      .25F / 256.0F, 2e-5F);
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
  for (const bool retire_first : { false, true }) {
    const auto first = StartSharedServiceView(service, frame);
    const auto root = renderer_->ResolvePublishedRuntimeViewId(ViewId { 50U });
    const auto second = PublishExposureOwner(frame, ViewId { 70U },
      CompositionView::ViewStateHandle { 70U }, settings, ViewId { 50U });
    const auto select = [&](ViewId view, std::uint64_t handle) {
      ctx_.current_view.view_id = view;
      ctx_.current_view.view_state_handle
        = CompositionView::ViewStateHandle { handle };
      ctx_.current_view.exposure_view_id = view;
      ctx_.current_view.exposure_view_state_handle
        = ctx_.current_view.view_state_handle;
    };
    select(second, 70U);
    ctx_.current_view.exposure_view_id = root;
    ctx_.current_view.exposure_view_state_handle
      = CompositionView::ViewStateHandle { 50U };
    EXPECT_NEAR(ServicePixel(service, signal), .5F, 2e-5F);
    PublishExposureOwner(frame, ViewId { 70U },
      CompositionView::ViewStateHandle { 70U }, settings, ViewId { 50U }, true);
    renderer_->RemovePublishedRuntimeView(frame, ViewId { 50U });
    select(first, 60U);
    EXPECT_NEAR(ServicePixel(service, signal, {}, false, 1.0F), .5F, 2e-5F);
    if (retire_first)
      renderer_->RemovePublishedRuntimeView(frame, ViewId { 60U });
    const double target = std::log2(.0225);
    for (unsigned i = 1U; i <= 3U; ++i) {
      select(second, 70U);
      EXPECT_NEAR(ServicePixel(service, signal, {}, true, 1.0F), 1.0F, 2e-5F);
      if (retire_first) {
        EXPECT_FALSE(
          vortex::testing::RendererPublicationProbe::HasExposureViewState(
            service, CompositionView::ViewStateHandle { 60U }));
      } else {
        select(first, 60U);
        EXPECT_NEAR(
          std::log2(ServicePixel(service, signal, {}, false, 1.0F) / 8.0),
          target + (-4.0 - target) * std::exp(-2.0 * i), 5e-4);
      }
    }
    PublishExposureOwner(frame, ViewId { 70U },
      CompositionView::ViewStateHandle { 70U }, settings);
    select(second, 70U);
    EXPECT_NEAR(ServicePixel(service, signal, {}, false, 1.0F), .5F, 2e-5F);
    if (!retire_first) {
      select(first, 60U);
      EXPECT_NEAR(
        std::log2(ServicePixel(service, signal, {}, false, 1.0F) / 8.0),
        target + (-4.0 - target) * std::exp(-8.0), 5e-4);
    } else {
      EXPECT_FALSE(
        vortex::testing::RendererPublicationProbe::HasExposureViewState(
          service, CompositionView::ViewStateHandle { 60U }));
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
    if (kind == 1U)
      settings.enabled = false;
    if (kind == 2U)
      settings.target_luminance = 0.0F;
    const auto consumer = StartSharedServiceView(service, frame, settings);
    std::optional<ExposureTransitionToken> explicit_request;
    if (kind >= 3U) {
      const auto issued = renderer_->QueueExposureTransition(
        ctx_.current_view.view_state_handle,
        kind == 3U ? ExposureTransitionPolicy::kSeedFromEv100
                   : ExposureTransitionPolicy::kRemeter,
        kind == 3U ? std::optional { 8.0F } : std::nullopt);
      ASSERT_TRUE(issued.has_value());
      explicit_request = *issued;
    }
    renderer_->RemovePublishedRuntimeView(frame, ViewId { 50U });
    ctx_.current_view.exposure_view_id = consumer;
    ctx_.current_view.exposure_view_state_handle
      = ctx_.current_view.view_state_handle;
    const float expected = kind == 0U || kind == 3U ? .25F / 256.0F
      : kind == 1U                                  ? .25F
      : kind == 2U                                  ? 0.0F
                                                    : .18F;
    EXPECT_NEAR(
      ServicePixel(service, Uniform(.25F, 4U, 4U), settings), expected, 2e-5F);
    if (explicit_request)
      EXPECT_EQ(
        renderer_->InspectExposureTransition(explicit_request->target)->request,
        *explicit_request);
  }
}

NOLINT_TEST_F(ExposureGpuTest,
  RemovedSourceUsesLastSelectedFallbackAfterConsumerCopyFailure)
{
  auto& service = OwnedExposureService();
  auto frame = engine::FrameContext {};
  const auto consumer = StartSharedServiceView(service, frame);
  const auto consumer_handle = CompositionView::ViewStateHandle { 60U };
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
  const auto source_handle = CompositionView::ViewStateHandle { 50U };
  const auto root
    = PublishExposureOwner(frame, ViewId { 50U }, source_handle, settings);
  for (unsigned i = 0; i < 4U; ++i) {
    const auto seed = renderer_->QueueExposureTransition(
      source_handle, ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
    ASSERT_TRUE(seed.has_value());
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
  static_cast<ExposureFailureGraphics&>(Backend()).fail_next_exposure_recorder
    = true;
  EXPECT_NEAR(ServicePixel(service, signal), .25F / 256.0F, 2e-5F);
  EXPECT_EQ(vortex::testing::RendererPublicationProbe::ExposureStateForView(
              service, consumer_handle),
    old);
  const auto selected
    = vortex::testing::RendererPublicationProbe::SelectedBorrowForView(
      service, consumer_handle);
  ASSERT_NE(selected, nullptr);
  EXPECT_NE(selected, old);
  EXPECT_EQ(
    Read<ExposureStateData>(*selected->buffer, ResourceStates::kShaderResource)
      .applied_generation[0],
    4U);
  renderer_->RemovePublishedRuntimeView(frame, ViewId { 50U });
  ctx_.current_view.exposure_view_id = consumer;
  ctx_.current_view.exposure_view_state_handle = consumer_handle;
  EXPECT_NEAR(
    ServicePixel(service, signal, {}, false, 1.0F), .25F / 256.0F, 2e-5F);
  const auto state
    = vortex::testing::RendererPublicationProbe::ExposureStateForView(
      service, consumer_handle);
  const auto continuity
    = Read<ExposureStateData>(*state->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(continuity.displayed_scale, 0x1p-8F);
  EXPECT_EQ(continuity.latent_scale, 0x1p-8F);
  EXPECT_EQ(continuity.applied_generation[0], 1U);
  EXPECT_EQ(
    Read<ExposureStateData>(*selected->buffer, ResourceStates::kShaderResource)
      .displayed_scale,
    0x1p-8F);
  EXPECT_NEAR(
    ServicePixel(service, signal, {}, false, 1.0F), .25F / 128.0F, 2e-5F);
}

NOLINT_TEST_F(
  ExposureGpuTest, CameraCutRemetersOnceAndInvalidatesOnlyItsCameraHistory)
{
  auto service = PostProcessService(*renderer_);
  const auto handle = ctx_.current_view.view_state_handle;
  EXPECT_NEAR(ServicePixel(service, Uniform(.25F, 4U, 4U)), .18F, 2e-5F);
  auto& history
    = vortex::testing::RendererPublicationProbe::PreviousViewHistory(
      *renderer_);
  auto state = internal::PreviousViewHistoryCache::CurrentState {};
  state.viewport = { .width = 4.0F, .height = 4.0F };
  history.BeginFrame(1U, {});
  history.TouchCurrent(handle, state);
  history.TouchCurrent(CompositionView::ViewStateHandle { 99U }, state);
  history.EndFrame();
  history.BeginFrame(2U, {});
  ASSERT_TRUE(
    renderer_->NotifyViewDiscontinuity(handle, ViewDiscontinuity::kCameraCut)
      .has_value());
  EXPECT_NEAR(ServicePixel(service, Uniform(8.0F, 4U, 4U)), .18F, 2e-5F);
  EXPECT_FALSE(history.TouchCurrent(handle, state).previous_valid);
  EXPECT_TRUE(
    history.TouchCurrent(CompositionView::ViewStateHandle { 99U }, state)
      .previous_valid);
  const auto event = renderer_->InspectExposureTransition(handle);
  ASSERT_TRUE(event.has_value());
  EXPECT_EQ(event->request.policy, ExposureTransitionPolicy::kRemeter);
  EXPECT_NEAR(
    ServicePixel(service, Uniform(.25F, 4U, 4U)), .25F * .0225F, 2e-5F);
  EXPECT_EQ(
    renderer_->InspectExposureTransition(handle)->request, event->request);
}

NOLINT_TEST_F(ExposureGpuTest, ExplicitSeedOverridesCameraCutDefaultPolicy)
{
  auto service = PostProcessService(*renderer_);
  ServicePixel(service, Uniform(.25F, 4U, 4U));
  const auto handle = ctx_.current_view.view_state_handle;
  ASSERT_TRUE(
    renderer_->NotifyViewDiscontinuity(handle, ViewDiscontinuity::kCameraCut)
      .has_value());
  const auto seed = renderer_->QueueExposureTransition(
    handle, ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(seed.has_value());
  EXPECT_NEAR(
    ServicePixel(service, Uniform(.25F, 4U, 4U)), .25F / 256.0F, 2e-5F);
  EXPECT_EQ(renderer_->InspectExposureTransition(handle)->request, *seed);
}

NOLINT_TEST_F(ExposureGpuTest,
  BorrowingCameraCutPreservesRootExposureWithoutRequestingReset)
{
  auto& service = OwnedExposureService();
  auto frame = engine::FrameContext {};
  StartSharedServiceView(service, frame);
  const auto consumer = ctx_.current_view.view_state_handle;
  const auto root_before
    = vortex::testing::RendererPublicationProbe::ExposureStateForView(
      service, CompositionView::ViewStateHandle { 50U });
  ASSERT_TRUE(
    renderer_->NotifyViewDiscontinuity(consumer, ViewDiscontinuity::kCameraCut)
      .has_value());
  EXPECT_NEAR(ServicePixel(service, Uniform(8.0F, 4U, 4U)), .5F, 2e-5F);
  EXPECT_FALSE(renderer_->InspectExposureTransition(consumer).has_value());
  EXPECT_EQ(vortex::testing::RendererPublicationProbe::ExposureStateForView(
              service, CompositionView::ViewStateHandle { 50U }),
    root_before);
}

NOLINT_TEST_F(
  ExposureGpuTest, WorldReplacementRemetersButOrdinaryImageChangesAdapt)
{
  auto service = PostProcessService(*renderer_);
  auto first = std::make_shared<scene::Scene>("FirstExposureWorld", 4U);
  auto second = std::make_shared<scene::Scene>("SecondExposureWorld", 4U);
  ctx_.scene = observer_ptr { first.get() };
  EXPECT_NEAR(ServicePixel(service, Uniform(.25F, 4U, 4U)), .18F, 2e-5F);
  EXPECT_NEAR(ServicePixel(service, Uniform(8.0F, 4U, 4U)), 1.0F, 2e-5F);
  ctx_.scene = observer_ptr { second.get() };
  EXPECT_NEAR(ServicePixel(service, Uniform(8.0F, 4U, 4U)), .18F, 2e-5F);
  EXPECT_EQ(
    renderer_->InspectExposureTransition(ctx_.current_view.view_state_handle)
      ->request.policy,
    ExposureTransitionPolicy::kRemeter);
}

NOLINT_TEST_F(
  ExposureGpuTest, DiagnosticFramesDeferCameraCutUntilNormalExposureResumes)
{
  auto service = PostProcessService(*renderer_);
  ServicePixel(service, Uniform(.25F, 4U, 4U));
  const auto handle = ctx_.current_view.view_state_handle;
  ASSERT_TRUE(
    renderer_->NotifyViewDiscontinuity(handle, ViewDiscontinuity::kCameraCut)
      .has_value());
  EXPECT_NEAR(
    ServicePixel(service, Uniform(.25F, 4U, 4U), {}, true), .25F, 2e-5F);
  EXPECT_FALSE(renderer_->InspectExposureTransition(handle).has_value());
  EXPECT_NEAR(ServicePixel(service, Uniform(8.0F, 4U, 4U)), .18F, 2e-5F);
}

NOLINT_TEST_F(ExposureGpuTest,
  RegisteredDebugOverrideControlsCameraCutsIndependentlyOfGlobalMode)
{
  auto service = PostProcessService(*renderer_);
  auto frame = engine::FrameContext {};
  const auto normal_handle = CompositionView::ViewStateHandle { 81U };
  const auto debug_handle = CompositionView::ViewStateHandle { 82U };
  auto settings = scene::ExposureSettings {};
  settings.key = 12.5F;
  const auto normal = PublishExposureOwner(frame, ViewId { 81U }, normal_handle,
    settings, kInvalidViewId, false, ShaderDebugMode::kDisabled);
  auto debug = PublishExposureOwner(frame, ViewId { 82U }, debug_handle,
    settings, kInvalidViewId, false, ShaderDebugMode::kDisabled);
  const auto dim = Uniform(.25F, 4U, 4U);
  const auto bright = Uniform(8.0F, 4U, 4U);
  const auto run = [&](ViewId id, CompositionView::ViewStateHandle handle,
                     const Signal& signal, bool diagnostic) {
    ctx_.current_view.view_id = id;
    ctx_.current_view.view_state_handle = handle;
    ctx_.shader_debug_mode = diagnostic ? ShaderDebugMode::kWorldNormals
                                        : ShaderDebugMode::kDisabled;
    return ServicePixel(service, signal, settings, diagnostic, 0.0F, {},
      engine::ToneMapper::kNone, false);
  };
  const auto capture_frame = [&] {
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    ctx_.shader_debug_mode = ShaderDebugMode::kWorldNormals;
    ctx_.render_mode = RenderMode::kSolid;
    service.CaptureRegisteredExposureControls(ctx_);
  };
  renderer_->SetShaderDebugMode(ShaderDebugMode::kWorldNormals);
  capture_frame();
  EXPECT_NEAR(run(normal, normal_handle, dim, false), .18F, 2e-5F);
  EXPECT_NEAR(run(debug, debug_handle, dim, false), .18F, 2e-5F);

  ASSERT_EQ(PublishExposureOwner(frame, ViewId { 82U }, debug_handle, settings,
              kInvalidViewId, false, ShaderDebugMode::kWorldNormals),
    debug);
  ASSERT_TRUE(renderer_
      ->NotifyViewDiscontinuity(normal_handle, ViewDiscontinuity::kCameraCut)
      .has_value());
  ASSERT_TRUE(renderer_
      ->NotifyViewDiscontinuity(debug_handle, ViewDiscontinuity::kCameraCut)
      .has_value());
  capture_frame();
  EXPECT_NEAR(run(normal, normal_handle, bright, false), .18F, 2e-5F);
  EXPECT_NEAR(run(debug, debug_handle, dim, true), .25F, 2e-5F);
  EXPECT_TRUE(renderer_->InspectExposureTransition(normal_handle).has_value());
  EXPECT_FALSE(renderer_->InspectExposureTransition(debug_handle).has_value());

  ASSERT_EQ(PublishExposureOwner(frame, ViewId { 82U }, debug_handle, settings,
              kInvalidViewId, false, ShaderDebugMode::kDisabled),
    debug);
  capture_frame();
  EXPECT_NEAR(run(debug, debug_handle, bright, false), .18F, 2e-5F);
  EXPECT_TRUE(renderer_->InspectExposureTransition(debug_handle).has_value());
}

NOLINT_TEST_F(
  ExposureGpuTest, LateCameraCutWaitsForNextCaptureAndRecoveryRemeters)
{
  auto service = PostProcessService(*renderer_);
  ServicePixel(service, Uniform(.25F, 4U, 4U));
  const auto handle = ctx_.current_view.view_state_handle;
  EXPECT_NEAR(
    ServicePixel(service, Uniform(8.0F, 4U, 4U), {}, false, 0.0F,
      [&] {
        EXPECT_TRUE(renderer_
            ->NotifyViewDiscontinuity(handle, ViewDiscontinuity::kCameraCut)
            .has_value());
      }),
    1.0F, 2e-5F);
  EXPECT_NEAR(ServicePixel(service, Uniform(8.0F, 4U, 4U)), .18F, 2e-5F);
  const auto cut
    = renderer_->InspectExposureTransition(handle)->request.generation;
  ASSERT_TRUE(renderer_
      ->NotifyViewDiscontinuity(handle, ViewDiscontinuity::kDeviceRecovery)
      .has_value());
  EXPECT_NEAR(ServicePixel(service, Uniform(.25F, 4U, 4U)), .18F, 2e-5F);
  EXPECT_GT(
    renderer_->InspectExposureTransition(handle)->request.generation, cut);
}

NOLINT_TEST_F(
  ExposureGpuTest, PublishingADifferentCameraTriggersTheDefaultCutPolicy)
{
  auto service = PostProcessService(*renderer_);
  auto frame = engine::FrameContext {};
  auto scene = std::make_shared<scene::Scene>("CameraSelection", 8U);
  auto first = scene->CreateNode("First");
  auto second = scene->CreateNode("Second");
  ASSERT_TRUE(first.AttachCamera(std::make_unique<scene::PerspectiveCamera>()));
  ASSERT_TRUE(
    second.AttachCamera(std::make_unique<scene::PerspectiveCamera>()));
  auto texture = CreateRegisteredTexture(TextureDesc { .width = 4U,
    .height = 4U,
    .format = Format::kRGBA32Float,
    .is_render_target = true,
    .initial_state = ResourceStates::kCommon });
  auto target = Backend().CreateFramebuffer(
    FramebufferDesc {}.AddColorAttachment(texture));
  auto view = CompositionView {};
  view.id = ViewId { 50U };
  view.view_state_handle = CompositionView::ViewStateHandle { 50U };
  view.view.viewport = { .width = 4.0F, .height = 4.0F };
  view.camera = first;
  const auto publish = [&] {
    return renderer_->PublishRuntimeCompositionView(frame,
      { .composition_view = view,
        .render_target = observer_ptr { target.get() } });
  };
  const auto id = publish();
  ctx_.scene = observer_ptr { scene.get() };
  ctx_.current_view.view_id = id;
  ctx_.current_view.view_state_handle = view.view_state_handle;
  EXPECT_NEAR(ServicePixel(service, Uniform(.25F, 4U, 4U)), .18F, 2e-5F);
  view.camera = second;
  ASSERT_EQ(publish(), id);
  EXPECT_NEAR(ServicePixel(service, Uniform(8.0F, 4U, 4U)), .18F, 2e-5F);
  EXPECT_EQ(renderer_->InspectExposureTransition(view.view_state_handle)
              ->request.policy,
    ExposureTransitionPolicy::kRemeter);
}

NOLINT_TEST_F(ExposureGpuTest, ThreeFramesInFlightKeepDistinctExposureRecordsAndUploads)
{
  std::array<postprocess::ExposurePass::Result, 3> frames;
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  for (unsigned i = 0U; i < frames.size(); ++i) {
    settings.manual_ev = 4.0F + 4.0F * i;
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    ctx_.frame_slot = frame::Slot { i };
    frames[i] = RecordShared(Signal {}, SharedConfig(settings));
    ASSERT_TRUE(frames[i].executed);
  }
  // No CPU fence wait occurred between the three submissions.
  for (unsigned i = 0U; i < frames.size(); ++i) {
    const auto state = ReadState(frames[i]);
    EXPECT_EQ(state.displayed_scale, std::exp2(-4.0F - 4.0F * i));
    EXPECT_EQ(state.frame_sequence[0], i + 1U);
    for (unsigned j = i + 1U; j < frames.size(); ++j)
      EXPECT_NE(frames[i].state, frames[j].state);
  }
}

auto ExposureGpuTest::CheckOffscreenSharing(const bool inside_frame) -> void
{
  pass_.reset();
  renderer_->OnShutdown();
  auto config = RendererConfig {};
  config.upload_queue_key = QueueKeyFor().get();
  renderer_ = std::make_unique<Renderer>(GetGraphicsShared(), config,
    RendererCapabilityFamily::kScenePreparation
      | RendererCapabilityFamily::kDeferredShading
      | RendererCapabilityFamily::kLightingData
      | RendererCapabilityFamily::kFinalOutputComposition);
  auto scene = std::make_shared<scene::Scene>("OffscreenExposure", 4U);
  auto camera = scene->CreateNode("Camera");
  auto lens = std::make_unique<scene::PerspectiveCamera>();
  auto view = View {};
  view.viewport = { .width = 4.0F, .height = 4.0F };
  lens->SetViewport(view.viewport);
  ASSERT_TRUE(camera.AttachCamera(std::move(lens)));
  scene->Update();
  auto frame = engine::FrameContext {};
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.key = 12.5F;
  settings.manual_ev = 4.0F;
  const auto root = PublishExposureOwner(
    frame, ViewId { 800U }, CompositionView::ViewStateHandle { 90U }, settings);
  ASSERT_NE(root, kInvalidViewId);
  ASSERT_NE(root, ViewId { 800U });
  PublishExposureOwner(frame, ViewId { 801U },
    CompositionView::ViewStateHandle { 91U }, settings, ViewId { 800U });
  auto output = CreateRegisteredTexture(TextureDesc { .width = 4U,
    .height = 4U,
    .format = Format::kRGBA32Float,
    .is_render_target = true,
    .initial_state = ResourceStates::kCommon });
  auto framebuffer = Backend().CreateFramebuffer(
    FramebufferDesc {}.AddColorAttachment(output));
  auto input = Renderer::OffscreenSceneViewInput::FromCamera(
    "Offscreen", root, view, camera);
  input.SetExposureSourceViewId(ViewId { 801U });
  input.SetViewStateHandle(CompositionView::ViewStateHandle { 92U });
  auto facade = renderer_->ForOffscreenScene();
  facade.SetFrameSession({ .frame_slot = frame::Slot { 0U },
    .frame_sequence = frame::SequenceNumber { 1U },
    .delta_time_seconds = 0.0F });
  facade.SetSceneSource({ .scene = observer_ptr { scene.get() } });
  facade.SetOutputTarget({ .framebuffer = observer_ptr { framebuffer.get() } });
  facade.SetViewIntent(input);
  for (const auto& issue : facade.Validate().issues)
    ADD_FAILURE() << issue.code << ": " << issue.message;
  auto session = facade.Finalize();
  ASSERT_TRUE(session.has_value());
  if (inside_frame)
    session->ExecuteInsideFrame(frame);
  else
    session->ExecuteNow();
  WaitForQueueIdle();
  auto* scene_renderer
    = vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_);
  ASSERT_NE(scene_renderer, nullptr);
  auto* service
    = vortex::testing::RendererPublicationProbe::GetPostProcessService(
      *scene_renderer);
  ASSERT_NE(service, nullptr);
  EXPECT_TRUE(service->GetLastExecutionState().tonemap_executed);
  EXPECT_FALSE(service->GetLastExecutionState().auto_exposure_requested);
  const auto states
    = vortex::testing::RendererPublicationProbe::FrameExposureStates(
      *service, frame::Slot { 0U });
  ASSERT_GE(states.size(), 2U);
  const auto state = Read<ExposureStateData>(
    *states.back()->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(state.displayed_scale, 0x1p-4F);
  EXPECT_NE(state.flags & 128U, 0U);
  EXPECT_EQ(states.back()->histogram_buffer, nullptr);
  EXPECT_FALSE(vortex::testing::RendererPublicationProbe::HasExposureViewState(
    *service, CompositionView::kInvalidViewStateHandle));
  // Rejected borrowers must neither replace the source image history nor
  // consume its queued request, including ownership changes after Finalize.
  ctx_.current_view.view_id = root;
  ctx_.current_view.view_state_handle
    = CompositionView::ViewStateHandle { 90U };
  EXPECT_NEAR(
    ServicePixel(*service, Uniform(.25F, 4U, 4U), settings), .25F / 16.0F, 2e-5F);
  const auto source_state
    = vortex::testing::RendererPublicationProbe::ExposureStateForView(
      *service, CompositionView::ViewStateHandle { 90U });
  ASSERT_NE(source_state, nullptr);
  const auto request = renderer_->QueueExposureTransition(
    CompositionView::ViewStateHandle { 90U },
    ExposureTransitionPolicy::kPreserve);
  ASSERT_TRUE(request.has_value());
  for (const auto handle : { CompositionView::kInvalidViewStateHandle,
         CompositionView::ViewStateHandle { 90U },
         CompositionView::ViewStateHandle { 91U } }) {
    input.SetViewStateHandle(handle);
    facade.SetViewIntent(input);
    EXPECT_FALSE(facade.Finalize().has_value());
  }
  ASSERT_NE(PublishExposureOwner(frame, ViewId { 802U },
              CompositionView::ViewStateHandle { 92U }, settings),
    kInvalidViewId);
  session->ExecuteNow();
  session->ExecuteInsideFrame(frame);
  EXPECT_EQ(vortex::testing::RendererPublicationProbe::ExposureStateForView(
              *service, CompositionView::ViewStateHandle { 90U }),
    source_state);
  EXPECT_EQ(Read<ExposureStateData>(
              *source_state->buffer, ResourceStates::kShaderResource)
              .displayed_scale,
    0x1p-4F);
  const auto status = renderer_->InspectExposureTransition(
    CompositionView::ViewStateHandle { 90U });
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(status->request, *request);
  EXPECT_EQ(status->phase, ExposureTransitionPhase::kQueued);
  FlushBackend();
}

NOLINT_TEST_F(
  ExposureGpuTest, OffscreenFacadeResolvesSharedRootDespitePublishedIdCollision)
{
  CheckOffscreenSharing(false);
}

NOLINT_TEST_F(ExposureGpuTest, OffscreenFacadeSharesRootInsideFrame)
{
  CheckOffscreenSharing(true);
}

NOLINT_TEST_F(
  ExposureGpuTest, FrameResolvePinsManualGainAndDistinctInFlightRecords)
{
  const auto capture = BeginOptionalCapture();
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 14.0F;
  ctx_.frame_sequence = frame::SequenceNumber { 1U };
  const auto first = pass_->ResolveFrame(ctx_, SharedConfig(settings), {});
  ASSERT_NE(first, nullptr);
  settings.manual_ev = 4.0F;
  EXPECT_EQ(pass_->ResolveFrame(ctx_, SharedConfig(settings), {}), first);
  ctx_.frame_sequence = frame::SequenceNumber { 2U };
  ctx_.frame_slot = frame::Slot { 1U };
  const auto second = pass_->ResolveFrame(ctx_, SharedConfig(settings), {});
  ASSERT_NE(second, nullptr);
  EXPECT_NE(first->buffer, second->buffer);
  const auto a
    = Read<FrameExposureData>(*first->buffer, ResourceStates::kShaderResource);
  const auto b
    = Read<FrameExposureData>(*second->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(a.pre_exposure, 0x1p-14F);
  EXPECT_EQ(a.one_over_pre_exposure, 0x1p14F);
  EXPECT_EQ(a.global_exposure_state_slot, first->current_state->srv_index);
  EXPECT_EQ(b.pre_exposure, 0x1p-4F);
  EXPECT_EQ(b.flags, 0U);
  EXPECT_EQ(Read<ExposureStateData>(
              *first->current_state->buffer, ResourceStates::kShaderResource)
              .displayed_scale,
    0x1p-14F);
  if (capture)
    EXPECT_TRUE(capture->EndCapture());
}

NOLINT_TEST_F(
  ExposureGpuTest, FrameResolveUsesGpuPriorGainAndPositiveLatentAfterZero)
{
  auto settings = scene::ExposureSettings {};
  const auto signal = Uniform(.25F);
  const auto initial = Run(signal, settings);
  settings.target_luminance = 0.0F;
  Run(signal, settings);
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  const auto resolved = pass_->ResolveFrame(ctx_, SharedConfig(settings), {});
  ASSERT_NE(resolved, nullptr);
  const auto frame = Read<FrameExposureData>(
    *resolved->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(frame.pre_exposure, initial.state.latent_scale);
  EXPECT_NEAR(frame.pre_exposure * frame.one_over_pre_exposure, 1.0F, 2e-6F);
  EXPECT_EQ(frame.flags, 0U);
  ctx_.current_view.view_state_handle
    = CompositionView::ViewStateHandle { 20U };
  const auto fresh = pass_->ResolveFrame(ctx_, SharedConfig(settings), {});
  ASSERT_NE(fresh, nullptr);
  const auto bootstrap
    = Read<FrameExposureData>(*fresh->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(bootstrap.pre_exposure, 1.0F);
  EXPECT_EQ(bootstrap.flags, 1U);
  const auto zero = Read<ExposureStateData>(
    *fresh->current_state->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(zero.displayed_scale, 0.0F);
  EXPECT_GT(zero.latent_scale, 0.0F);
}

NOLINT_TEST_F(
  ExposureGpuTest, BorrowedUninitializedPublicationCannotAuthorizeHalfDomain)
{
  ctx_.current_view.view_state_handle
    = CompositionView::ViewStateHandle { 10U };
  ctx_.frame_sequence = frame::SequenceNumber { 1U };
  const auto config = SharedConfig();
  const auto invalid
    = RecordShared(Uniform(std::numeric_limits<float>::quiet_NaN()), config);
  ASSERT_TRUE(invalid.executed);
  const auto source_state = ReadState(invalid);
  ASSERT_EQ(source_state.flags & 2U, 0U);
  auto source = postprocess::ExposurePass::Source {
    .handle = ctx_.current_view.view_state_handle, .config = config
  };
  ctx_.current_view.view_state_handle
    = CompositionView::ViewStateHandle { 20U };
  ctx_.frame_sequence = frame::SequenceNumber { 2U };
  ctx_.frame_slot = frame::Slot { 1U };
  const auto borrowed = pass_->ResolveFrame(
    ctx_, config, { .use_fp32 = false, .source = &source });
  ASSERT_NE(borrowed, nullptr);
  const auto domain = Read<FrameExposureData>(
    *borrowed->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(domain.flags & 11U, 11U);
  EXPECT_EQ(domain.pre_exposure, 1.0F);
}

NOLINT_TEST_F(ExposureGpuTest, FrameResolveBorrowsPriorRootAndTagsRootFallback)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 4.0F;
  const auto signal = Uniform(.25F);
  ctx_.current_view.view_state_handle
    = CompositionView::ViewStateHandle { 10U };
  ctx_.frame_sequence = frame::SequenceNumber { 1U };
  const auto prior = RecordShared(signal, SharedConfig(settings));
  ASSERT_TRUE(prior.executed);
  auto source = postprocess::ExposurePass::Source { .handle
    = ctx_.current_view.view_state_handle,
    .config = SharedConfig(settings) };
  settings.manual_ev = 8.0F;
  ctx_.frame_sequence = frame::SequenceNumber { 2U };
  // The first solve is still queued; its transient descriptors belong to slot
  // 0.
  ctx_.frame_slot = frame::Slot { 1U };
  ASSERT_TRUE(RecordShared(signal, SharedConfig(settings)).executed);
  ctx_.current_view.view_state_handle
    = CompositionView::ViewStateHandle { 20U };
  const auto borrowed
    = pass_->ResolveFrame(ctx_, SharedConfig(), { .source = &source });
  ASSERT_NE(borrowed, nullptr);
  const auto frame = Read<FrameExposureData>(
    *borrowed->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(frame.pre_exposure, 0x1p-4F);
  EXPECT_EQ(frame.global_exposure_state_slot, prior.state->srv_index);
  EXPECT_EQ(frame.flags, 2U);
  ctx_.current_view.view_state_handle
    = CompositionView::ViewStateHandle { 21U };
  source.handle = CompositionView::ViewStateHandle { 30U };
  source.config = SharedConfig(settings);
  const auto fallback
    = pass_->ResolveFrame(ctx_, SharedConfig(), { .source = &source });
  ASSERT_NE(fallback, nullptr);
  const auto fallback_frame = Read<FrameExposureData>(
    *fallback->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(fallback_frame.pre_exposure, 1.0F);
  EXPECT_EQ(fallback_frame.flags, 11U);
  const auto fallback_state = Read<ExposureStateData>(
    *fallback->current_state->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(fallback_state.displayed_scale, 0x1p-8F);
  EXPECT_EQ(fallback_state.applied_generation[0], 0U);
}

NOLINT_TEST_F(ExposureGpuTest,
  FrameResolveSeedsWithoutAcknowledgingAndRetriesRecordingFailure)
{
  ctx_.frame_sequence = frame::SequenceNumber { 1U };
  const auto seed
    = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
      ExposureTransitionPolicy::kSeedFromEv100, 12.0F);
  ASSERT_TRUE(seed.has_value());
  static_cast<ExposureFailureGraphics&>(Backend()).fail_next_frame_recorder
    = true;
  EXPECT_EQ(pass_->ResolveFrame(ctx_, SharedConfig(),
              { .transition = *seed, .lifetime = seed->lifetime }),
    nullptr);
  const auto resolved = pass_->ResolveFrame(
    ctx_, SharedConfig(), { .transition = *seed, .lifetime = seed->lifetime });
  ASSERT_NE(resolved, nullptr);
  EXPECT_EQ(
    Read<FrameExposureData>(*resolved->buffer, ResourceStates::kShaderResource)
      .pre_exposure,
    0x1p-12F);
  EXPECT_EQ(Read<ExposureStateData>(
              *resolved->current_state->buffer, ResourceStates::kShaderResource)
              .applied_generation[0],
    0U);
  EXPECT_EQ(renderer_->InspectExposureTransition(seed->target)->phase,
    ExposureTransitionPhase::kQueued);
  ctx_.frame_sequence = frame::SequenceNumber { 2U };
  const auto fp32 = pass_->ResolveFrame(ctx_, SharedConfig(),
    { .use_fp32 = true, .transition = *seed, .lifetime = seed->lifetime });
  ASSERT_NE(fp32, nullptr);
  EXPECT_EQ(
    Read<FrameExposureData>(*fp32->buffer, ResourceStates::kShaderResource)
      .pre_exposure,
    1.0F);
  ctx_.frame_sequence = frame::SequenceNumber { 3U };
  auto diagnostic = SharedConfig();
  diagnostic = diagnostic.WithDiagnosticOverride(true);
  const auto unit = pass_->ResolveFrame(ctx_, diagnostic, {});
  ASSERT_NE(unit, nullptr);
  const auto unit_frame
    = Read<FrameExposureData>(*unit->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(unit_frame.pre_exposure, 1.0F);
  EXPECT_EQ(unit_frame.flags, 4U);
}

NOLINT_TEST_F(ExposureGpuTest,
  FrameResolvePinsQualifiedCandidateAndRejectsIneligibleCandidate)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  ctx_.frame_sequence = frame::SequenceNumber { 1U };
  const auto seed = pass_->ResolveFrame(ctx_, SharedConfig(settings), {});
  ASSERT_NE(seed, nullptr);
  auto candidate = ExposureStateData {};
  candidate.flags = 1U | 256U;
  candidate.fp16_candidate_pre_exposure = 0.125F;
  candidate.fp16_eligible_streak = 2U;
  auto upload = CreateUploadBuffer(SizeBytes { sizeof(candidate) });
  upload->Update(&candidate, sizeof(candidate), 0U);
  {
    auto recorder = AcquireRecorder("Qualified candidate fixture");
    EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
    ASSERT_TRUE(
      recorder->AdoptKnownResourceState(*seed->current_state->buffer));
    recorder->RequireResourceState(
      *seed->current_state->buffer, ResourceStates::kCopyDest);
    recorder->FlushBarriers();
    recorder->CopyBuffer(
      *seed->current_state->buffer, 0U, *upload, 0U, sizeof(candidate));
    recorder->RequireResourceStateFinal(
      *seed->current_state->buffer, ResourceStates::kShaderResource);
  }
  ctx_.frame_sequence = frame::SequenceNumber { 2U };
  const auto resolved = pass_->ResolveFrame(ctx_, SharedConfig(settings),
    { .qualified_candidate = seed->current_state });
  ASSERT_NE(resolved, nullptr);
  const auto frame = Read<FrameExposureData>(
    *resolved->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(frame.pre_exposure, 0.125F);
  EXPECT_EQ(frame.one_over_pre_exposure, 8.0F);
  EXPECT_EQ(frame.flags, 0U);
  ctx_.frame_sequence = frame::SequenceNumber { 3U };
  const auto invalid = pass_->ResolveFrame(ctx_, SharedConfig(settings),
    { .qualified_candidate = resolved->current_state });
  ASSERT_NE(invalid, nullptr);
  const auto invalid_frame = Read<FrameExposureData>(
    *invalid->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(invalid_frame.pre_exposure, 1.0F);
  EXPECT_EQ(invalid_frame.flags, 1U);
}

NOLINT_TEST_F(
  ExposureGpuTest, Fp32ReferencePreservesQualifiedCandidateAndExposureHistory)
{
  auto settings = scene::ExposureSettings {};
  const auto config = SharedConfig(settings, {}, 71U);
  const auto signal = Uniform(.25F);
  ctx_.frame_sequence = frame::SequenceNumber { 1U };
  const auto previous = RecordShared(signal, config);
  ASSERT_TRUE(previous.executed);
  auto candidate = ReadState(previous);
  ASSERT_NEAR(candidate.displayed_scale, .72F, 2e-5F);
  ASSERT_NE(candidate.raw_metered_luminance, 0.0F);
  candidate.flags |= 256U;
  candidate.fp16_candidate_pre_exposure = .125F;
  candidate.fp16_eligible_streak = 2U;
  auto upload = CreateUploadBuffer(SizeBytes { sizeof(candidate) });
  upload->Update(&candidate, sizeof(candidate), 0U);
  {
    auto recorder = AcquireRecorder("FP32 reference qualified candidate");
    EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
    ASSERT_TRUE(recorder->AdoptKnownResourceState(*previous.state->buffer));
    recorder->RequireResourceState(
      *previous.state->buffer, ResourceStates::kCopyDest);
    recorder->FlushBarriers();
    recorder->CopyBuffer(
      *previous.state->buffer, 0U, *upload, 0U, sizeof(candidate));
    recorder->RequireResourceStateFinal(
      *previous.state->buffer, ResourceStates::kShaderResource);
  }
  ctx_.frame_sequence = frame::SequenceNumber { 2U };
  const auto resolved = pass_->ResolveFrame(ctx_, config,
    { .use_fp32 = true,
      .preserve_fp32_candidate_p = true,
      .qualified_candidate = previous.state });
  ASSERT_NE(resolved, nullptr);
  EXPECT_EQ(resolved->selected_history, previous.state);
  const auto domain = Read<FrameExposureData>(
    *resolved->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(domain.pre_exposure, .125F);
  EXPECT_EQ(domain.one_over_pre_exposure, 8.0F);
  EXPECT_EQ(domain.flags, 1U);
  EXPECT_EQ(
    domain.global_exposure_state_slot, resolved->current_state->srv_index);
  const auto prepared = Read<ExposureStateData>(
    *resolved->current_state->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(std::memcmp(&candidate, &prepared, 24U), 0);
  EXPECT_EQ(prepared.settings_revision, candidate.settings_revision);
  EXPECT_EQ(prepared.requested_generation, candidate.requested_generation);
  EXPECT_EQ(prepared.applied_generation, candidate.applied_generation);
  const auto retained = ReadState(previous);
  EXPECT_EQ(std::memcmp(&candidate, &retained, sizeof(candidate)), 0);
  // Execute meters the pinned frame domain, so its input already contains P.
  const auto result = RecordShared(Uniform(.25F * .125F), config);
  ASSERT_TRUE(result.executed);
  const auto solved = ReadState(result);
  EXPECT_EQ(std::memcmp(&candidate, &solved, 24U), 0);
  EXPECT_EQ(solved.settings_revision, candidate.settings_revision);
  EXPECT_EQ(solved.requested_generation, candidate.requested_generation);
  EXPECT_EQ(solved.applied_generation, candidate.applied_generation);
  EXPECT_EQ(solved.frame_sequence, (std::array<std::uint32_t, 2> { 2U, 0U }));
}

NOLINT_TEST_F(
  ExposureGpuTest, Fp32ReferenceRequiresValidCandidateAndHonorsUnitFallbacks)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 4.0F;
  const auto config = SharedConfig(settings);
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  const auto previous = RecordShared(Uniform(.25F), config);
  ASSERT_TRUE(previous.executed);
  const auto original = ReadState(previous);
  struct Case {
    const char* name;
    float candidate_p;
    unsigned streak;
    bool eligible;
    bool absent;
    bool diagnostic;
    bool missing_source;
  };
  const std::array cases {
    Case { "absent", .125F, 2U, true, true, false, false },
    Case { "not eligible", .125F, 2U, false, false, false, false },
    Case { "one eligible frame", .125F, 1U, true, false, false, false },
    Case { "zero", 0, 2U, true, false, false, false },
    Case { "below supported P", 0x1p-33F, 2U, true, false, false, false },
    Case { "above supported P", 0x1p33F, 2U, true, false, false, false },
    Case { "NaN", std::numeric_limits<float>::quiet_NaN(), 2U, true, false,
      false, false },
    Case { "infinite", std::numeric_limits<float>::infinity(), 2U, true, false,
      false, false },
    Case { "diagnostic wins", .125F, 2U, true, false, true, false },
    Case { "missing source wins", .125F, 2U, true, false, false, true },
  };
  for (const auto& test_case : cases) {
    SCOPED_TRACE(test_case.name);
    auto candidate = original;
    candidate.flags
      = test_case.eligible ? original.flags | 256U : original.flags & ~256U;
    candidate.fp16_candidate_pre_exposure = test_case.candidate_p;
    candidate.fp16_eligible_streak = test_case.streak;
    auto upload = CreateUploadBuffer(SizeBytes { sizeof(candidate) });
    upload->Update(&candidate, sizeof(candidate), 0U);
    {
      auto recorder = AcquireRecorder("FP32 reference fallback candidate");
      EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
      ASSERT_TRUE(recorder->AdoptKnownResourceState(*previous.state->buffer));
      recorder->RequireResourceState(
        *previous.state->buffer, ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyBuffer(
        *previous.state->buffer, 0U, *upload, 0U, sizeof(candidate));
      recorder->RequireResourceStateFinal(
        *previous.state->buffer, ResourceStates::kShaderResource);
    }
    const auto source = postprocess::ExposurePass::Source {
      .handle = CompositionView::ViewStateHandle { 900U }, .config = config
    };
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    const auto resolved = pass_->ResolveFrame(ctx_,
      test_case.diagnostic ? config.WithDiagnosticOverride(true) : config,
      { .use_fp32 = true,
        .preserve_fp32_candidate_p = true,
        .qualified_candidate = test_case.absent ? nullptr : previous.state,
        .source = test_case.missing_source ? &source : nullptr });
    ASSERT_NE(resolved, nullptr);
    const auto domain = Read<FrameExposureData>(
      *resolved->buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(domain.pre_exposure, 1.0F);
    EXPECT_EQ(domain.one_over_pre_exposure, 1.0F);
    EXPECT_EQ(domain.flags,
      test_case.diagnostic         ? 5U
        : test_case.missing_source ? 11U
                                   : 1U);
    const auto state = Read<ExposureStateData>(
      *resolved->current_state->buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(state.displayed_scale, test_case.diagnostic ? 1.0F : 0x1p-4F);
    const auto retained = ReadState(previous);
    EXPECT_EQ(std::memcmp(&candidate, &retained, sizeof(candidate)), 0);
  }
}
NOLINT_TEST_F(
  ExposureGpuTest, FrameResolvePreservesOperationalEndpointsAndCameraGain)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  for (const float ev : { -32.0F, 32.0F }) {
    settings.manual_ev = ev;
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    const auto resolved = pass_->ResolveFrame(ctx_, SharedConfig(settings), {});
    ASSERT_NE(resolved, nullptr);
    const auto frame = Read<FrameExposureData>(
      *resolved->buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(frame.pre_exposure, std::exp2(-ev));
    EXPECT_EQ(frame.one_over_pre_exposure, std::exp2(ev));
  }
  settings.mode = engine::ExposureMode::kManualCamera;
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  const auto camera
    = pass_->ResolveFrame(ctx_, SharedConfig(settings, 16.0F), {});
  ASSERT_NE(camera, nullptr);
  EXPECT_EQ(
    Read<FrameExposureData>(*camera->buffer, ResourceStates::kShaderResource)
      .pre_exposure,
    0x1p-16F);
  settings.enabled = false;
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  const auto disabled
    = pass_->ResolveFrame(ctx_, SharedConfig(settings, 16.0F), {});
  ASSERT_NE(disabled, nullptr);
  EXPECT_EQ(
    Read<FrameExposureData>(*disabled->buffer, ResourceStates::kShaderResource)
      .pre_exposure,
    1.0F);
}

NOLINT_TEST_F(ExposureGpuTest, ExternalBloomUsesFrameDomainAndHonorsDisable)
{
  auto service = PostProcessService(*renderer_);
  auto textures = SceneTextures(Backend(), { .extent = { 4U, 4U } });
  unsigned cases = 0;
  for (const float ev : { -16.0F, 0.0F, 16.0F })
    for (const bool fp32 : { false, true })
      for (const bool enabled : { false, true })
        for (const float intensity : { 0.0F, .5F }) {
          SCOPED_TRACE(::testing::Message()
            << "ev=" << ev << " fp32=" << fp32 << " enabled=" << enabled
            << " intensity=" << intensity);
          ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
          service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
          auto settings = scene::ExposureSettings {};
          settings.mode = engine::ExposureMode::kManual;
          settings.manual_ev = ev;
          settings.key = 12.5F;
          [[maybe_unused]] const auto& captured
            = service.CaptureViewExposureSettings(ctx_.current_view.view_id,
              ctx_.current_view.view_state_handle, settings);
          auto config = PostProcessConfig {};
          config.enable_bloom = enabled;
          config.bloom_intensity = intensity;
          config.tone_mapper = engine::ToneMapper::kNone;
          config.gamma = 1;
          service.SetResolvedConfig(service.BuildPassConfig(config,
            ctx_.current_view.view_id, ctx_.current_view.view_state_handle));
          const auto frame = service.PrepareFrameExposure(ctx_, fp32);
          ASSERT_NE(frame, nullptr);
          const double s = std::exp2(-double(ev));
          const double p = fp32 ? 1 : s;
          const auto source = Uniform(float(.125 * p / s), 4U, 4U);
          const auto bloom = Uniform(float(.25 * p / s), 4U, 4U);
          auto output = CreateRegisteredTexture({ .width = 4,
            .height = 4,
            .format = Format::kRGBA32Float,
            .is_render_target = true,
            .initial_state = ResourceStates::kCommon });
          auto target = Backend().CreateFramebuffer(
            FramebufferDesc {}.AddColorAttachment(output));
          service.Execute(ctx_.current_view.view_id, ctx_, textures,
            { .scene_signal = source.texture.get(),
              .post_target = observer_ptr<const Framebuffer> { target.get() },
              .scene_signal_srv = source.srv,
              .bloom_texture_srv = bloom.srv });
          ASSERT_TRUE(service.GetLastExecutionState().tonemap_executed);
          EXPECT_EQ(service.GetLastExecutionState().bloom_requested, enabled);
          // Pixel (1,0) has zero Bayer offset, independent of production
          // helpers.
          const auto pixels = ReadFloatTexture(*output);
          const double expected = .125 + (enabled ? .25 * intensity : 0);
          for (unsigned c = 0; c < 3; ++c)
            EXPECT_NEAR(pixels[1][c], expected, 2e-6);
          ++cases;
        }
  RecordProperty("external_bloom_cases", cases);
}

NOLINT_TEST_F(
  ExposureGpuTest, FrameDomainSolvesReservedStateAndAppliesManualRatio)
{
  const auto capture = BeginOptionalCapture();
  auto service = PostProcessService(*renderer_);
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  postprocess::ExposurePass::FrameLease frame;
  for (const float ev : { 4.0F, 8.0F }) {
    settings.manual_ev = ev;
    const auto pixel = ServicePixel(service,
      Uniform(.25F * std::exp2(-ev), 4U, 4U), settings, false, 0.0F, [&] {
        frame = service.PrepareFrameExposure(ctx_, false);
        ASSERT_NE(frame, nullptr);
      });
    EXPECT_NEAR(pixel, .25F * std::exp2(-ev), 2e-7F);
    ASSERT_NE(frame, nullptr);
    const auto state
      = vortex::testing::RendererPublicationProbe::ExposureStateForView(
        service, ctx_.current_view.view_state_handle);
    EXPECT_EQ(state, frame->current_state);
    EXPECT_EQ(
      Read<ExposureStateData>(*state->buffer, ResourceStates::kShaderResource)
        .displayed_scale,
      std::exp2(-ev));
    EXPECT_EQ(
      Read<FrameExposureData>(*frame->buffer, ResourceStates::kShaderResource)
        .pre_exposure,
      std::exp2(-ev));
  }
  if (capture)
    EXPECT_TRUE(capture->EndCapture());
}

NOLINT_TEST_F(ExposureGpuTest,
  FrameDomainMetersWithGpuReciprocalAndPreservesZeroAndDisabled)
{
  auto service = PostProcessService(*renderer_);
  auto settings = scene::ExposureSettings {};
  postprocess::ExposurePass::FrameLease frame;
  const auto prepare = [&] {
    frame = service.PrepareFrameExposure(ctx_, false);
    ASSERT_NE(frame, nullptr);
  };
  EXPECT_NEAR(ServicePixel(
                service, Uniform(.25F, 4U, 4U), settings, false, 0.0F, prepare),
    .18F, 2e-5F);
  EXPECT_NEAR(ServicePixel(
                service, Uniform(.18F, 4U, 4U), settings, false, 0.0F, prepare),
    .18F, 2e-5F);
  ASSERT_NE(frame, nullptr);
  const auto state = Read<ExposureStateData>(
    *frame->current_state->buffer, ResourceStates::kShaderResource);
  EXPECT_NEAR(state.raw_metered_luminance, .25F, 2e-5F);
  const auto numerical
    = Read<FrameExposureData>(*frame->buffer, ResourceStates::kShaderResource);
  EXPECT_NEAR(numerical.pre_exposure, .72F, 2e-5F);
  settings.target_luminance = 0.0F;
  EXPECT_EQ(ServicePixel(
              service, Uniform(.18F, 4U, 4U), settings, false, 0.0F, prepare),
    0.0F);
  settings.enabled = false;
  EXPECT_NEAR(ServicePixel(
                service, Uniform(.25F, 4U, 4U), settings, false, 0.0F, prepare),
    .25F, 2e-6F);
}

NOLINT_TEST_F(ExposureGpuTest,
  FrameDomainFailedSeedKeepsPriorDisplayedGainAndPendingRequest)
{
  auto service = PostProcessService(*renderer_);
  EXPECT_NEAR(ServicePixel(service, Uniform(.25F, 4U, 4U)), .18F, 2e-5F);
  const auto request
    = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
      ExposureTransitionPolicy::kSeedFromEv100, 12.0F);
  ASSERT_TRUE(request.has_value());
  postprocess::ExposurePass::FrameLease frame;
  const auto pixel = ServicePixel(
    service, Uniform(.25F / 4096.0F, 4U, 4U), {}, false, 0.0F, [&] {
      frame = service.PrepareFrameExposure(ctx_, false);
      ASSERT_NE(frame, nullptr);
      static_cast<ExposureFailureGraphics&>(Backend())
        .fail_next_exposure_recorder = true;
    });
  ASSERT_NE(frame, nullptr);
  EXPECT_NEAR(pixel, .18F, 2e-5F);
  EXPECT_EQ(
    Read<FrameExposureData>(*frame->buffer, ResourceStates::kShaderResource)
      .pre_exposure,
    0x1p-12F);
  EXPECT_NEAR(Read<ExposureStateData>(
                *frame->current_state->buffer, ResourceStates::kShaderResource)
                .displayed_scale,
    .72F, 2e-5F);
  EXPECT_EQ(renderer_->InspectExposureTransition(request->target)->phase,
    ExposureTransitionPhase::kQueued);
}

NOLINT_TEST_F(ExposureGpuTest,
  FrameDomainSharedConsumerUsesOwnerGainAndItsOwnNumericalDomain)
{
  auto service = PostProcessService(*renderer_);
  auto frame_context = engine::FrameContext {};
  auto settings = scene::ExposureSettings {};
  settings.key = 12.5F;
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 4.0F;
  const auto root = PublishExposureOwner(frame_context, ViewId { 50U },
    CompositionView::ViewStateHandle { 10U }, settings);
  const auto consumer = PublishExposureOwner(frame_context, ViewId { 60U },
    CompositionView::ViewStateHandle { 20U }, {}, ViewId { 50U });
  ctx_.current_view.view_id = root;
  ctx_.current_view.view_state_handle
    = CompositionView::ViewStateHandle { 10U };
  EXPECT_NEAR(ServicePixel(service, Uniform(.25F, 4U, 4U), settings),
    .25F / 16.0F, 2e-7F);
  ctx_.current_view.view_id = consumer;
  ctx_.current_view.view_state_handle
    = CompositionView::ViewStateHandle { 20U };
  ctx_.current_view.exposure_view_id = root;
  ctx_.current_view.exposure_view_state_handle
    = CompositionView::ViewStateHandle { 10U };
  postprocess::ExposurePass::FrameLease frame;
  EXPECT_NEAR(
    ServicePixel(service, Uniform(.25F / 16.0F, 4U, 4U), {}, false, 0.0F,
      [&] {
        frame = service.PrepareFrameExposure(ctx_, false);
        ASSERT_NE(frame, nullptr);
      }),
    .25F / 16.0F, 2e-7F);
  ASSERT_NE(frame, nullptr);
  EXPECT_EQ(frame->current_state->histogram_buffer, nullptr);
  EXPECT_EQ(
    Read<FrameExposureData>(*frame->buffer, ResourceStates::kShaderResource)
      .pre_exposure,
    0x1p-4F);
}

NOLINT_TEST_F(
  ExposureGpuTest, FrameDomainToneCurvesRemainFiniteAtMaximumSceneTimesGain)
{
  auto service = PostProcessService(*renderer_);
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = -32.0F;
  for (const auto mapper : { engine::ToneMapper::kAcesFitted,
         engine::ToneMapper::kFilmic, engine::ToneMapper::kReinhard }) {
    const auto pixel = ServicePixel(
      service, Uniform(0x1p32F, 4U, 4U), settings, false, 0.0F,
      [&] { ASSERT_NE(service.PrepareFrameExposure(ctx_, true), nullptr); },
      mapper);
    EXPECT_TRUE(std::isfinite(pixel));
    EXPECT_NEAR(pixel, 1.0F, 2e-6F);
  }
}

NOLINT_TEST_F(
  ExposureGpuTest, FrameDomainToneCurvesPreserveOrdinaryNeutralResponse)
{
  auto service = PostProcessService(*renderer_);
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 0.0F;
  const auto filmic = [](const double x) {
    return (x * (.15 * x + .05) + .004) / (x * (.15 * x + .5) + .06) - .02 / .3;
  };
  for (const auto mapper : { engine::ToneMapper::kAcesFitted,
         engine::ToneMapper::kFilmic, engine::ToneMapper::kReinhard }) {
    for (const double x : { .01, .18, 1.0, 16.0 }) {
      const double reference = mapper == engine::ToneMapper::kAcesFitted
        ? (x * (x + .0245786) - .000090537)
          / (x * (.983729 * x + .4329510) + .238081)
        : mapper == engine::ToneMapper::kFilmic ? filmic(2.0 * x) / filmic(11.2)
                                                : x / (x + 1.0);
      const auto pixel = ServicePixel(
        service, Uniform(static_cast<float>(x), 4U, 4U), settings, false, 0.0F,
        [&] { ASSERT_NE(service.PrepareFrameExposure(ctx_, false), nullptr); },
        mapper);
      EXPECT_NEAR(pixel, std::clamp(reference, 0.0, 1.0), 2e-5);
    }
  }
}

NOLINT_TEST_F(ExposureGpuTest, FrameDomainFailedSolveStillHonorsZeroTarget)
{
  auto service = PostProcessService(*renderer_);
  ServicePixel(service, Uniform(.25F, 4U, 4U));
  auto settings = scene::ExposureSettings {};
  settings.target_luminance = 0.0F;
  postprocess::ExposurePass::FrameLease frame;
  EXPECT_EQ(ServicePixel(service, Uniform(.18F, 4U, 4U), settings, false, 0.0F,
              [&] {
                frame = service.PrepareFrameExposure(ctx_, false);
                ASSERT_NE(frame, nullptr);
                static_cast<ExposureFailureGraphics&>(Backend())
                  .fail_next_exposure_recorder = true;
              }),
    0.0F);
  ASSERT_NE(frame, nullptr);
  const auto current = Read<ExposureStateData>(
    *frame->current_state->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(current.displayed_scale, 0.0F);
  EXPECT_GT(current.latent_scale, 0.0F);
}

NOLINT_TEST_F(ExposureGpuTest,
  FrameDomainFailedSourceLossRetainsLatestBorrowInReservedState)
{
  auto& service = OwnedExposureService();
  auto publication = engine::FrameContext {};
  const auto consumer = StartSharedServiceView(service, publication);
  const auto consumer_handle = ctx_.current_view.view_state_handle;
  const auto old
    = vortex::testing::RendererPublicationProbe::ExposureStateForView(
      service, consumer_handle);
  auto settings = scene::ExposureSettings {};
  settings.key = 12.5F;
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 8.0F;
  const auto root = PublishExposureOwner(publication, ViewId { 50U },
    CompositionView::ViewStateHandle { 50U }, settings);
  ctx_.current_view.view_id = root;
  ctx_.current_view.view_state_handle
    = CompositionView::ViewStateHandle { 50U };
  ctx_.current_view.exposure_view_state_handle
    = CompositionView::kInvalidViewStateHandle;
  EXPECT_NEAR(ServicePixel(service, Uniform(.25F, 4U, 4U), settings),
    .25F / 256.0F, 2e-7F);
  ctx_.current_view.view_id = consumer;
  ctx_.current_view.view_state_handle = consumer_handle;
  ctx_.current_view.exposure_view_id = root;
  ctx_.current_view.exposure_view_state_handle
    = CompositionView::ViewStateHandle { 50U };
  const auto fail_copy = [&] {
    ASSERT_NE(service.PrepareFrameExposure(ctx_, false), nullptr);
    static_cast<ExposureFailureGraphics&>(Backend()).fail_next_exposure_recorder
      = true;
  };
  EXPECT_NEAR(ServicePixel(service, Uniform(.25F / 256.0F, 4U, 4U), {}, false,
                0.0F, fail_copy),
    .25F / 256.0F, 2e-7F);
  renderer_->RemovePublishedRuntimeView(publication, ViewId { 50U });
  ctx_.current_view.exposure_view_id = consumer;
  ctx_.current_view.exposure_view_state_handle = consumer_handle;
  postprocess::ExposurePass::FrameLease frame;
  EXPECT_NEAR(ServicePixel(service, Uniform(.25F, 4U, 4U), {}, false, 0.0F,
                [&] {
                  frame = service.PrepareFrameExposure(ctx_, true);
                  ASSERT_NE(frame, nullptr);
                  static_cast<ExposureFailureGraphics&>(Backend())
                    .fail_next_exposure_recorder = true;
                }),
    .25F / 256.0F, 2e-7F);
  ASSERT_NE(frame, nullptr);
  EXPECT_EQ(Read<ExposureStateData>(
              *frame->current_state->buffer, ResourceStates::kShaderResource)
              .displayed_scale,
    0x1p-8F);
  EXPECT_EQ(vortex::testing::RendererPublicationProbe::ExposureStateForView(
              service, consumer_handle),
    old);
  EXPECT_EQ(
    Read<ExposureStateData>(*old->buffer, ResourceStates::kShaderResource)
      .displayed_scale,
    0x1p-4F);
}

NOLINT_TEST_F(ExposureGpuTest, FrameDomainSkipsTonemapWhenFallbackCannotSubmit)
{
  auto service = PostProcessService(*renderer_);
  ServicePixel(service, Uniform(.25F, 4U, 4U));
  const auto previous
    = vortex::testing::RendererPublicationProbe::ExposureStateForView(
      service, ctx_.current_view.view_state_handle);
  static_cast<void>(
    ServicePixel(service, Uniform(.18F, 4U, 4U), {}, false, 0.0F, [&] {
      ASSERT_NE(service.PrepareFrameExposure(ctx_, false), nullptr);
      auto& backend = static_cast<ExposureFailureGraphics&>(Backend());
      backend.fail_next_exposure_recorder = true;
      backend.fail_next_fallback_recorder = true;
    }));
  EXPECT_TRUE(service.GetLastExecutionState().tonemap_requested);
  EXPECT_FALSE(service.GetLastExecutionState().tonemap_executed);
  EXPECT_FALSE(service.GetLastExecutionState().wrote_visible_output);
  EXPECT_EQ(vortex::testing::RendererPublicationProbe::ExposureStateForView(
              service, ctx_.current_view.view_state_handle),
    previous);
}

NOLINT_TEST_F(
  ExposureGpuTest, Fp32SceneColorPreservesWideRangeRadianceAndCoverage)
{
  auto textures = SceneTextures(Backend(),
    { .extent = { 1U, 1U }, .scene_color_format = Format::kRGBA32Float });
  auto color = textures.GetSceneColorResource();
  ASSERT_EQ(color->GetDescriptor().format, Format::kRGBA32Float);
  const Pixel expected { 0x1p30F, 0x1p-24F, 1.0F, .25F };
  auto upload = CreateUploadBuffer(SizeBytes { 256U });
  upload->Update(expected.data(), sizeof(expected), 0U);
  {
    auto recorder = AcquireRecorder("FP32 SceneColor fixture upload");
    EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
    if (!recorder->AdoptKnownResourceState(*color))
      recorder->BeginTrackingResourceState(
        *color, color->GetDescriptor().initial_state);
    recorder->RequireResourceState(*color, ResourceStates::kCopyDest);
    recorder->FlushBarriers();
    recorder->CopyBufferToTexture(*upload,
      { .buffer_offset = 0U,
        .buffer_row_pitch = 256U,
        .buffer_slice_pitch = 256U,
        .dst_slice = { .width = 1U, .height = 1U, .depth = 1U } },
      *color);
    recorder->RequireResourceStateFinal(
      *color, ResourceStates::kShaderResource);
  }
  auto readback
    = GetReadbackManager()->CreateTextureReadback("FP32 SceneColor readback");
  {
    auto recorder = AcquireRecorder("FP32 SceneColor fixture readback");
    ASSERT_TRUE(recorder->AdoptKnownResourceState(*color));
    ASSERT_TRUE(readback->EnqueueCopy(*recorder, *color, {}).has_value());
  }
  const auto mapped = readback->MapNow();
  ASSERT_TRUE(mapped.has_value());
  Pixel actual {};
  std::memcpy(actual.data(), mapped->Data(), sizeof(actual));
  EXPECT_EQ(actual, expected);
  FlushBackend();
}

auto ExposureGpuTest::CheckSceneExposureRetry(
  const bool inside_frame, const bool late_failure) -> void
{
  pass_.reset();
  renderer_->OnShutdown();
  auto config = RendererConfig {};
  config.upload_queue_key = QueueKeyFor().get();
  renderer_ = std::make_unique<Renderer>(GetGraphicsShared(), config,
    RendererCapabilityFamily::kScenePreparation
      | RendererCapabilityFamily::kDeferredShading
      | RendererCapabilityFamily::kLightingData
      | RendererCapabilityFamily::kFinalOutputComposition);
  auto scene = std::make_shared<scene::Scene>("ExposureRetryScene", 4U);
  scene->SetEnvironment(std::make_unique<scene::SceneEnvironment>());
  auto& post = scene->GetEnvironment()
                 ->AddSystem<scene::environment::PostProcessVolume>();
  auto settings = scene::ExposureSettings {};
  settings.key = 12.5F;
  post.SetExposureSettings(settings);
  post.SetToneMapper(engine::ToneMapper::kNone);
  post.SetDisplayGamma(1.0F);
  post.SetBloomIntensity(0.0F);
  auto camera = scene->CreateNode("Camera");
  auto lens = std::make_unique<scene::PerspectiveCamera>();
  auto view = View {};
  view.viewport = { .width = 4.0F, .height = 4.0F };
  lens->SetViewport(view.viewport);
  ASSERT_TRUE(camera.AttachCamera(std::move(lens)));
  scene->Update();
  auto output = CreateRegisteredTexture({ .width = 4U,
    .height = 4U,
    .format = Format::kRGBA32Float,
    .is_shader_resource = true,
    .is_render_target = true,
    .initial_state = ResourceStates::kCommon });
  auto framebuffer = Backend().CreateFramebuffer(
    FramebufferDesc {}.AddColorAttachment(output));
  const Pixel sentinel { .125F, .25F, .5F, 1.0F };
  std::array<std::byte, 1024U> bytes {};
  for (unsigned y = 0U; y < 4U; ++y)
    for (unsigned x = 0U; x < 4U; ++x)
      std::memcpy(bytes.data() + y * 256U + x * sizeof(Pixel), sentinel.data(),
        sizeof(Pixel));
  auto upload = CreateUploadBuffer(SizeBytes { bytes.size() });
  upload->Update(bytes.data(), bytes.size(), 0U);
  {
    auto recorder = AcquireRecorder("Prior offscreen output");
    EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
    EnsureTracked(*recorder, output, ResourceStates::kCommon);
    recorder->RequireResourceState(*output, ResourceStates::kCopyDest);
    recorder->FlushBarriers();
    recorder->CopyBufferToTexture(*upload,
      { .buffer_offset = 0U,
        .buffer_row_pitch = 256U,
        .buffer_slice_pitch = 1024U,
        .dst_slice = { .width = 4U, .height = 4U, .depth = 1U } },
      *output);
    recorder->RequireResourceStateFinal(
      *output, ResourceStates::kShaderResource);
  }
  const auto read_pixel = [&]() {
    auto readback
      = GetReadbackManager()->CreateTextureReadback("Offscreen retry pixel");
    {
      auto recorder = AcquireRecorder("Offscreen retry readback");
      CHECK_F(recorder->AdoptKnownResourceState(*output));
      CHECK_F(readback
          ->EnqueueCopy(*recorder, *output,
            { .src_slice
              = { .x = 1U, .y = 0U, .width = 1U, .height = 1U, .depth = 1U } })
          .has_value());
    }
    const auto mapped = readback->MapNow();
    CHECK_F(mapped.has_value());
    Pixel pixel {};
    std::memcpy(pixel.data(), mapped->Data(), sizeof(pixel));
    return pixel;
  };
  const auto handle = CompositionView::ViewStateHandle { 7000U };
  auto seed = renderer_->QueueExposureTransition(
    handle, ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
  ASSERT_TRUE(seed.has_value());
  auto input = Renderer::OffscreenSceneViewInput::FromCamera(
    "Retry", ViewId { 7000U }, view, camera);
  input.SetViewStateHandle(handle);
  auto successful_input = Renderer::OffscreenSceneViewInput::FromCamera(
    "Successful sibling", ViewId { 6999U }, view, camera);
  successful_input.SetViewStateHandle(
    CompositionView::ViewStateHandle { 6999U });
  auto successful_output = CreateRegisteredTexture(output->GetDescriptor());
  auto successful_target = Backend().CreateFramebuffer(
    FramebufferDesc {}.AddColorAttachment(successful_output));
  auto frame = engine::FrameContext {};
  frame.SetScene(observer_ptr { scene.get() });
  auto invoke = [&](const unsigned sequence, const bool sibling = false) {
    auto facade = renderer_->ForOffscreenScene();
    facade.SetFrameSession({ .frame_slot = frame::Slot { sequence - 1U },
      .frame_sequence = frame::SequenceNumber { sequence },
      .delta_time_seconds = 0.0F });
    facade.SetSceneSource({ .scene = observer_ptr { scene.get() } });
    facade.SetViewIntent(sibling ? successful_input : input);
    facade.SetOutputTarget(
      { .framebuffer = observer_ptr {
          sibling ? successful_target.get() : framebuffer.get() } });
    auto session = facade.Finalize();
    CHECK_F(session.has_value());
    if (!inside_frame)
      return session->ExecuteNow();
    frame.SetFrameSequenceNumber(frame::SequenceNumber { sequence },
      engine::internal::EngineTagFactory::Get());
    frame.SetFrameSlot(
      frame::Slot { sequence - 1U }, engine::internal::EngineTagFactory::Get());
    renderer_->OnFrameStart(observer_ptr { &frame });
    const auto result = session->ExecuteInsideFrame(frame);
    renderer_->OnFrameEnd(observer_ptr { &frame });
    return result;
  };
  auto& backend = static_cast<ExposureFailureGraphics&>(Backend());
  auto prior_output = sentinel;
  if (late_failure) {
    ASSERT_TRUE(invoke(1U));
    prior_output = read_pixel();
    seed = renderer_->QueueExposureTransition(
      handle, ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
    ASSERT_TRUE(seed.has_value());
    ASSERT_TRUE(invoke(2U, true));
  }
  backend.recorder_names.clear();
  backend.fail_next_frame_recorder = !late_failure;
  backend.fail_next_exposure_recorder = late_failure;
  backend.fail_next_fallback_recorder = late_failure;
  EXPECT_FALSE(invoke(late_failure ? 2U : 1U));
  for (const auto& name : backend.recorder_names) {
    if (!late_failure) {
      EXPECT_EQ(name.find("BasePass"), std::string::npos);
      EXPECT_EQ(name.find("DeferredLight"), std::string::npos);
    }
    EXPECT_EQ(name.find("Tonemap"), std::string::npos);
    EXPECT_EQ(name.find("ResolveSceneColor"), std::string::npos);
  }
  EXPECT_EQ(read_pixel(), prior_output);
  EXPECT_EQ(renderer_->InspectExposureTransition(handle)->phase,
    ExposureTransitionPhase::kQueued);
  auto* scene_renderer
    = vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_);
  ASSERT_NE(scene_renderer, nullptr);
  EXPECT_FALSE(
    scene_renderer->GetSceneTextureExtracts().resolved_scene_color.valid);
  auto* service
    = vortex::testing::RendererPublicationProbe::GetPostProcessService(
      *scene_renderer);
  ASSERT_NE(service, nullptr);
  EXPECT_FALSE(service->GetLastExecutionState().wrote_visible_output);
  EXPECT_TRUE(invoke(late_failure ? 3U : 2U));
  EXPECT_EQ(read_pixel()[0], 0.0F);
  const auto state
    = vortex::testing::RendererPublicationProbe::ExposureStateForView(
      *service, handle);
  ASSERT_NE(state, nullptr);
  const auto solved
    = Read<ExposureStateData>(*state->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(solved.applied_generation[0], seed->generation);
  EXPECT_EQ(solved.displayed_scale, 0x1p-8F);
  FlushBackend();
}

NOLINT_TEST_F(ExposureGpuTest,
  SceneExposurePreparationFailurePreservesOutputAndRetriesStandalone)
{
  CheckSceneExposureRetry(false);
}

NOLINT_TEST_F(ExposureGpuTest,
  SceneExposurePreparationFailurePreservesOutputAndRetriesInsideFrame)
{
  CheckSceneExposureRetry(true);
}

NOLINT_TEST_F(ExposureGpuTest,
  LateExposureFailureAfterSuccessfulSiblingPreservesStandaloneOutput)
{
  CheckSceneExposureRetry(false, true);
}

NOLINT_TEST_F(ExposureGpuTest,
  LateExposureFailureAfterSuccessfulSiblingPreservesInsideFrameOutput)
{
  CheckSceneExposureRetry(true, true);
}

NOLINT_TEST_F(ExposureGpuTest,
  SceneSkyRadianceIsInvariantToNumericalDomainAndPreservesHighRange)
{
  pass_.reset();
  renderer_->OnShutdown();
  auto config = RendererConfig {};
  config.upload_queue_key = QueueKeyFor().get();
  renderer_ = std::make_unique<Renderer>(GetGraphicsShared(), config,
    RendererCapabilityFamily::kScenePreparation
      | RendererCapabilityFamily::kDeferredShading
      | RendererCapabilityFamily::kLightingData
      | RendererCapabilityFamily::kEnvironmentLighting
      | RendererCapabilityFamily::kFinalOutputComposition);
  auto scene = std::make_shared<scene::Scene>("SceneDomainFixture", 4U);
  scene->SetEnvironment(std::make_unique<scene::SceneEnvironment>());
  auto& sky
    = scene->GetEnvironment()->AddSystem<scene::environment::SkySphere>();
  sky.SetEnabled(true);
  sky.SetSource(scene::environment::SkySphereSource::kSolidColor);
  auto& post = scene->GetEnvironment()
                 ->AddSystem<scene::environment::PostProcessVolume>();
  auto settings = scene::ExposureSettings {};
  settings.key = 12.5F;
  settings.mode = engine::ExposureMode::kManual;
  post.SetToneMapper(engine::ToneMapper::kNone);
  post.SetDisplayGamma(1.0F);
  post.SetBloomIntensity(0.0F);
  auto camera = scene->CreateNode("Camera");
  auto lens = std::make_unique<scene::PerspectiveCamera>();
  auto view = View {};
  view.viewport = { .width = 4.0F, .height = 4.0F };
  lens->SetViewport(view.viewport);
  ASSERT_TRUE(camera.AttachCamera(std::move(lens)));
  auto output = CreateRegisteredTexture({ .width = 4U,
    .height = 4U,
    .format = Format::kRGBA32Float,
    .is_shader_resource = true,
    .is_render_target = true,
    .initial_state = ResourceStates::kCommon });
  auto framebuffer = Backend().CreateFramebuffer(
    FramebufferDesc {}.AddColorAttachment(output));
  struct ExposureOverride final : IViewExtension {
    std::function<void(RenderContext&)> before;
    auto OnViewSetup(const ViewSetupContext& context) -> void override
    {
      before(context.render_context);
    }
  };
  bool force_nonunit = false;
  auto extension = std::make_shared<ExposureOverride>();
  extension->before = [&](RenderContext& ctx) {
    if (!force_nonunit)
      return;
    auto* owner
      = vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_);
    auto* service
      = vortex::testing::RendererPublicationProbe::GetPostProcessService(
        *owner);
    service->SetResolvedConfig(SharedConfig(settings));
    ASSERT_NE(service->PrepareFrameExposure(ctx, false), nullptr);
  };
  renderer_->RegisterViewExtension(extension);
  unsigned sequence = 0U;
  for (const bool high_range : { false, true }) {
    settings.manual_ev = high_range ? 30.0F : 4.0F;
    post.SetExposureSettings(settings);
    sky.SetSolidColorRgb({ .25F, .5F, .75F });
    sky.SetIntensity(high_range ? 0x1p30F : 1.0F);
    scene->Update();
    const float expected_scale = high_range ? 1.0F : 1.0F / 16.0F;
    for (const bool nonunit : { false, true }) {
      force_nonunit = nonunit;
      ++sequence;
      auto input = Renderer::OffscreenSceneViewInput::FromCamera(
        "SkyDomain", ViewId { 8100U }, view, camera);
      input.SetViewStateHandle(CompositionView::ViewStateHandle { 8100U });
      input.SetWithAtmosphere(true);
      auto facade = renderer_->ForOffscreenScene();
      facade.SetFrameSession(
        { .frame_slot = frame::Slot { (sequence - 1U) % 3U },
          .frame_sequence = frame::SequenceNumber { sequence },
          .delta_time_seconds = 0.0F });
      facade.SetSceneSource({ .scene = observer_ptr { scene.get() } });
      facade.SetOutputTarget(
        { .framebuffer = observer_ptr { framebuffer.get() } });
      facade.SetViewIntent(input);
      auto session = facade.Finalize();
      ASSERT_TRUE(session.has_value());
      ASSERT_TRUE(session->ExecuteNow());
      auto readback
        = GetReadbackManager()->CreateTextureReadback("Scene sky domain pixel");
      {
        auto recorder = AcquireRecorder("Scene sky domain readback");
        ASSERT_TRUE(recorder->AdoptKnownResourceState(*output));
        ASSERT_TRUE(readback
            ->EnqueueCopy(*recorder, *output,
              { .src_slice = { .x = 1U,
                  .y = 0U,
                  .width = 1U,
                  .height = 1U,
                  .depth = 1U } })
            .has_value());
      }
      const auto mapped = readback->MapNow();
      ASSERT_TRUE(mapped.has_value());
      Pixel pixel {};
      std::memcpy(pixel.data(), mapped->Data(), sizeof(pixel));
      EXPECT_NEAR(pixel[0], .25F * expected_scale, 2e-5F);
      EXPECT_NEAR(pixel[1], .5F * expected_scale, 2e-5F);
      EXPECT_NEAR(pixel[2], .75F * expected_scale, 2e-5F);
      auto* owner = vortex::testing::RendererPublicationProbe::GetSceneRenderer(
        *renderer_);
      const auto& product
        = owner->GetSceneTextureExtracts().resolved_scene_color;
      ASSERT_TRUE(product.valid);
      ASSERT_NE(product.exposure, nullptr);
      EXPECT_EQ(product.texture->GetDescriptor().format, Format::kRGBA32Float);
      const auto domain = Read<FrameExposureData>(
        *product.exposure->buffer, ResourceStates::kShaderResource);
      EXPECT_EQ(
        domain.pre_exposure, nonunit ? std::exp2(-settings.manual_ev) : 1.0F);
    }
  }
  extension->before = [](RenderContext&) { };
  FlushBackend();
}

auto ExposureGpuTest::CheckFogViewRetirement(
  const bool persistent, const bool temporal) -> void
{
  auto& tracked = static_cast<ExposureFailureGraphics&>(Backend());
  tracked.track_resources = true;
  pass_.reset();
  renderer_->OnShutdown();
  auto config = RendererConfig {};
  config.upload_queue_key = QueueKeyFor().get();
  renderer_ = std::make_unique<Renderer>(GetGraphicsShared(), config,
    RendererCapabilityFamily::kScenePreparation
      | RendererCapabilityFamily::kDeferredShading
      | RendererCapabilityFamily::kLightingData
      | RendererCapabilityFamily::kEnvironmentLighting
      | RendererCapabilityFamily::kFinalOutputComposition);
  console::Console console;
  renderer_->RegisterConsoleBindings(observer_ptr { &console });
  ASSERT_EQ(
    console
      .Execute(temporal ? "vtx.volumetric_fog.temporal_reprojection true"
                        : "vtx.volumetric_fog.temporal_reprojection false")
      .status,
    console::ExecutionStatus::kOk);
  auto scene = std::make_shared<scene::Scene>("Fog retirement", 4U);
  scene->SetEnvironment(std::make_unique<scene::SceneEnvironment>());
  auto& fog = scene->GetEnvironment()->AddSystem<scene::environment::Fog>();
  fog.SetEnabled(true);
  fog.SetEnableHeightFog(true);
  fog.SetEnableVolumetricFog(true);
  fog.SetExtinctionSigmaTPerMeter(.01F);
  fog.SetHeightFalloffPerMeter(0.0F);
  fog.SetVolumetricFogDistance(1000.0F);
  fog.SetVolumetricFogEmissive({ .1F, .2F, .3F });
  auto camera = scene->CreateNode("Camera");
  auto lens = std::make_unique<scene::PerspectiveCamera>();
  auto view = View {};
  view.viewport = { .width = 4.0F, .height = 4.0F };
  lens->SetViewport(view.viewport);
  ASSERT_TRUE(camera.AttachCamera(std::move(lens)));
  scene->Update();
  auto color = CreateRegisteredTexture({ .width = 4U,
    .height = 4U,
    .format = Format::kRGBA32Float,
    .is_shader_resource = true,
    .is_render_target = true,
    .initial_state = ResourceStates::kCommon });
  auto target
    = Backend().CreateFramebuffer(FramebufferDesc {}.AddColorAttachment(color));
  struct Capture final : IViewExtension {
    Renderer& renderer;
    std::vector<std::shared_ptr<Texture>> textures;
    explicit Capture(Renderer& value)
      : renderer(value)
    {
    }
    auto OnViewSetup(const ViewSetupContext& hook) -> void override
    {
      hook.render_context.current_view.with_height_fog = true;
    }
    auto OnPostRenderViewGpu(const ViewRenderGpuContext& hook) -> void override
    {
      auto* owner
        = vortex::testing::RendererPublicationProbe::GetSceneRenderer(renderer);
      textures = vortex::testing::RendererPublicationProbe::EnvironmentTextures(
        *owner, hook.render_context.current_view.view_id);
    }
  };
  auto capture = std::make_shared<Capture>(*renderer_);
  renderer_->RegisterViewExtension(capture);
  auto frame = engine::FrameContext {};
  frame.SetScene(observer_ptr { scene.get() });
  std::optional<std::size_t> baseline_resources;
  std::uint64_t sequence = 0U;
  auto& registry = Backend().GetResourceRegistry();
  auto& reclaimer = Backend().GetDeferredReclaimer();
  for (unsigned iteration = 0U; iteration < 8U; ++iteration) {
    SCOPED_TRACE(iteration);
    const auto slot = frame::Slot { 0U };
    reclaimer.OnBeginFrame(slot);
    frame.SetFrameSequenceNumber(frame::SequenceNumber { ++sequence },
      engine::internal::EngineTagFactory::Get());
    frame.SetFrameSlot(slot, engine::internal::EngineTagFactory::Get());
    renderer_->OnFrameStart(observer_ptr { &frame });
    const auto intent = ViewId { 12000U + iteration };
    ViewId published = intent;
    if (persistent) {
      auto input = CompositionView::ForScene(intent, view, camera);
      input.view_state_handle
        = CompositionView::ViewStateHandle { intent.get() };
      input.with_height_fog = true;
      published = renderer_->PublishRuntimeCompositionView(frame,
        { .composition_view = input,
          .render_target = observer_ptr { target.get() } });
      ASSERT_NE(published, kInvalidViewId);
      auto loop = co::testing::TestEventLoop {};
      co::Run(loop, [&]() -> co::Co<void> {
        co_await renderer_->OnPreRender(observer_ptr { &frame });
        co_await renderer_->OnRender(observer_ptr { &frame });
      });
    } else {
      auto input = Renderer::OffscreenSceneViewInput::FromCamera(
        "Stateless fog", intent, view, camera);
      auto facade = renderer_->ForOffscreenScene();
      facade.SetFrameSession({ .frame_slot = slot,
        .frame_sequence = frame::SequenceNumber { sequence },
        .delta_time_seconds = 0.0F });
      facade.SetSceneSource({ .scene = observer_ptr { scene.get() } });
      facade.SetViewIntent(input);
      facade.SetOutputTarget({ .framebuffer = observer_ptr { target.get() } });
      auto session = facade.Finalize();
      ASSERT_TRUE(session.has_value());
      ASSERT_TRUE(session->ExecuteInsideFrame(frame));
    }
    auto* owner
      = vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_);
    ASSERT_NE(owner, nullptr);
    EXPECT_EQ(
      vortex::testing::RendererPublicationProbe::FogHistoryCount(*owner),
      persistent && temporal ? 1U : 0U);
    ASSERT_EQ(capture->textures.size(), 1U);
    auto texture = capture->textures.front();
    // This observer owns the underlying allocation, not the retained wrapper.
    // Keeping it alive lets the test inspect registration after wrapper
    // release.
    const auto underlying = texture->shared_from_this();
    ASSERT_EQ(underlying.get(), texture.get());
    ASSERT_TRUE(
      texture.owner_before(underlying) || underlying.owner_before(texture));
    ASSERT_TRUE(registry.Contains(*texture));
    if (persistent) {
      owner->OnFrameStart(frame);
      EXPECT_EQ(
        vortex::testing::RendererPublicationProbe::FogHistoryCount(*owner),
        temporal ? 1U : 0U);
      renderer_->RemovePublishedRuntimeView(frame, intent);
      EXPECT_EQ(
        vortex::testing::RendererPublicationProbe::FogHistoryCount(*owner), 0U);
    }
    auto readback
      = GetReadbackManager()->CreateTextureReadback("Retiring fog voxel");
    {
      auto recorder = AcquireRecorder("Retiring fog output readback");
      ASSERT_TRUE(recorder->AdoptKnownResourceState(*texture));
      ASSERT_TRUE(readback
          ->EnqueueCopy(*recorder, *texture,
            { .src_slice
              = { .z = 16U, .width = 1U, .height = 1U, .depth = 1U } })
          .has_value());
    }
    const auto mapped = readback->MapNow();
    ASSERT_TRUE(mapped.has_value());
    Pixel voxel {};
    std::memcpy(voxel.data(), mapped->Data(), sizeof(voxel));
    EXPECT_GT(voxel[0], 0.0F);
    EXPECT_TRUE(std::isfinite(voxel[0]));
    renderer_->OnFrameEnd(observer_ptr { &frame });
    capture->textures.clear();
    WaitForQueueIdle();
    for (unsigned retire = 0U; retire < frame::kFramesInFlight.get();
      ++retire) {
      const auto retired_slot = frame::Slot { retire };
      owner->OnStandaloneFrameStart(
        frame::SequenceNumber { ++sequence }, retired_slot, std::nullopt);
      reclaimer.OnBeginFrame(retired_slot);
    }
    EXPECT_TRUE(registry.Contains(*texture));
    texture.reset();
    for (unsigned retire = 0U; retire < frame::kFramesInFlight.get();
      ++retire) {
      const auto retired_slot = frame::Slot { retire };
      owner->OnStandaloneFrameStart(
        frame::SequenceNumber { ++sequence }, retired_slot, std::nullopt);
      reclaimer.OnBeginFrame(retired_slot);
    }
    EXPECT_FALSE(registry.Contains(*underlying));
    auto* service
      = vortex::testing::RendererPublicationProbe::GetPostProcessService(
        *owner);
    EXPECT_EQ(
      vortex::testing::RendererPublicationProbe::RetainedExposureFrameCount(
        *service),
      0U);
    const auto resources = registry.GetRegisteredResourceCount();
    if (iteration == 2U)
      baseline_resources = resources;
    if (baseline_resources)
      EXPECT_EQ(resources, *baseline_resources);
    if (baseline_resources && resources != *baseline_resources) {
      std::map<std::string, unsigned> names;
      for (auto weak : tracked.tracked_buffers)
        if (auto resource = weak.lock();
          resource && registry.Contains(*resource))
          ++names[std::string(resource->GetName())];
      for (auto weak : tracked.tracked_textures)
        if (auto resource = weak.lock();
          resource && registry.Contains(*resource))
          ++names[std::string(resource->GetName())];
      for (const auto& [name, count] : names)
        LOG_F(ERROR, "retained {} {}", count, name);
    }
  }
}

NOLINT_TEST_F(ExposureGpuTest, QueuedHzbBuildsKeepTheirOwnDepthPyramids)
{
  auto module = ScreenHzbModule(*renderer_, SceneTexturesConfig {});
  const std::array extents { glm::uvec2 { 8U, 8U }, glm::uvec2 { 16U, 8U },
    glm::uvec2 { 8U, 16U } };
  std::vector<std::unique_ptr<SceneTextures>> textures;
  std::vector<std::vector<float>> sources;
  std::vector<ScreenHzbModule::Output> outputs;
  for (unsigned view = 0U; view < extents.size(); ++view) {
    const auto extent = extents[view];
    textures.push_back(std::make_unique<SceneTextures>(
      Backend(), SceneTexturesConfig { .extent = extent }));
    auto depth = textures.back()->GetSceneDepthResource();
    sources.emplace_back(extent.x * extent.y);
    auto& registry = Backend().GetResourceRegistry();
    if (!registry.Contains(*depth))
      registry.Register(depth);
    const auto dsv_desc
      = TextureViewDescription { .view_type = ResourceViewType::kTexture_DSV,
          .visibility = DescriptorVisibility::kCpuOnly,
          .format = depth->GetDescriptor().format,
          .dimension = TextureType::kTexture2D };
    auto dsv = registry.Find(*depth, dsv_desc);
    if (!dsv->IsValid()) {
      auto allocation
        = renderer_->GetGraphics()->GetDescriptorAllocator().AllocateRaw(
          ResourceViewType::kTexture_DSV, DescriptorVisibility::kCpuOnly);
      dsv = registry.RegisterView(*depth, std::move(allocation), dsv_desc);
    }
    CHECK_F(dsv->IsValid());
    auto recorder = AcquireRecorder("HZB depth fixture pattern");
    EnsureTracked(*recorder, depth, depth->GetDescriptor().initial_state);
    recorder->RequireResourceState(*depth, ResourceStates::kDepthWrite);
    recorder->FlushBarriers();
    for (unsigned y = 0U; y < extent.y; ++y)
      for (unsigned x = 0U; x < extent.x; ++x) {
        const float value
          = static_cast<float>((x * 3U + y * 5U + view * 17U) % 63U + 1U)
          / 64.0F;
        sources.back()[y * extent.x + x] = value;
        const std::array rects { Scissors { .left = static_cast<int>(x),
          .top = static_cast<int>(y),
          .right = static_cast<int>(x + 1U),
          .bottom = static_cast<int>(y + 1U) } };
        recorder->ClearDepthStencilView(
          *depth, dsv, ClearFlags::kDepth, value, 0U, rects);
      }
    recorder->RequireResourceStateFinal(
      *depth, ResourceStates::kShaderResource);
  }
  WaitForQueueIdle();
  ctx_.frame_sequence = frame::SequenceNumber { 1U };
  ctx_.frame_slot = frame::Slot { 0U };
  ctx_.current_view.screen_hzb_request
    = { .current_furthest = true, .current_closest = true };
  const auto capture = BeginOptionalCapture();
  for (unsigned view = 0U; view < extents.size(); ++view) {
    module.OnFrameStart();
    ctx_.current_view.view_id = ViewId { 14000U + view };
    ctx_.current_view.view_state_handle
      = CompositionView::ViewStateHandle { 14000U + view };
    module.Execute(ctx_, *textures[view]);
    outputs.push_back(module.GetCurrentOutput());
    ASSERT_TRUE(outputs.back().available);
  }
  if (capture)
    EXPECT_TRUE(capture->EndCapture());
  for (unsigned view = 0U; view < extents.size(); ++view) {
    for (const bool closest : { true, false }) {
      SCOPED_TRACE(view);
      SCOPED_TRACE(closest);
      auto reference = sources[view];
      auto extent = extents[view];
      const auto& texture = closest ? outputs[view].closest_texture
                                    : outputs[view].furthest_texture;
      ASSERT_NE(texture, nullptr);
      for (unsigned mip = 0U; mip < texture->GetDescriptor().mip_levels;
        ++mip) {
        const glm::uvec2 reduced_extent { std::max(1U, extent.x / 2U),
          std::max(1U, extent.y / 2U) };
        std::vector<float> reduced(reduced_extent.x * reduced_extent.y);
        for (unsigned y = 0U; y < reduced_extent.y; ++y)
          for (unsigned x = 0U; x < reduced_extent.x; ++x) {
            float value = closest ? 0.0F : 1.0F;
            for (unsigned dy = 0U; dy < 2U; ++dy)
              for (unsigned dx = 0U; dx < 2U; ++dx) {
                const float sample
                  = reference[std::min(y * 2U + dy, extent.y - 1U) * extent.x
                    + std::min(x * 2U + dx, extent.x - 1U)];
                value
                  = closest ? std::max(value, sample) : std::min(value, sample);
              }
            reduced[y * reduced_extent.x + x] = value;
          }
        auto readback = GetReadbackManager()->CreateTextureReadback(
          "HZB independent oracle");
        {
          auto recorder = AcquireRecorder("HZB pyramid readback");
          ASSERT_TRUE(recorder->AdoptKnownResourceState(*texture));
          ASSERT_TRUE(readback
              ->EnqueueCopy(*recorder, *texture,
                { .src_slice = { .width = reduced_extent.x,
                    .height = reduced_extent.y,
                    .depth = 1U,
                    .mip_level = mip } })
              .has_value());
        }
        const auto mapped = readback->MapNow();
        ASSERT_TRUE(mapped.has_value());
        for (unsigned y = 0U; y < reduced_extent.y; ++y)
          for (unsigned x = 0U; x < reduced_extent.x; ++x) {
            float actual;
            std::memcpy(&actual,
              mapped->Data() + y * mapped->Layout().row_pitch.get()
                + x * sizeof(float),
              sizeof(float));
            EXPECT_EQ(actual, reduced[y * reduced_extent.x + x]);
          }
        reference = std::move(reduced);
        extent = reduced_extent;
      }
    }
  }
}

NOLINT_TEST_F(ExposureGpuTest, RemovedViewsRetireFogHistoryAndExposureLeases)
{
  CheckFogViewRetirement(true, true);
}

NOLINT_TEST_F(ExposureGpuTest, StatelessFogViewsRetainNoPersistentHistory)
{
  CheckFogViewRetirement(false, true);
}

NOLINT_TEST_F(
  ExposureGpuTest, NonTemporalFogOutputsRetireWithoutPersistentHistory)
{
  CheckFogViewRetirement(true, false);
}

NOLINT_TEST_F(
  ExposureGpuTest, SameFrameOffscreenEnvironmentDescriptorsSurviveQueuedViews)
{
  pass_.reset();
  renderer_->OnShutdown();
  auto config = RendererConfig {};
  config.upload_queue_key = QueueKeyFor().get();
  renderer_ = std::make_unique<Renderer>(GetGraphicsShared(), config,
    RendererCapabilityFamily::kScenePreparation
      | RendererCapabilityFamily::kDeferredShading
      | RendererCapabilityFamily::kLightingData
      | RendererCapabilityFamily::kEnvironmentLighting
      | RendererCapabilityFamily::kFinalOutputComposition);
  console::Console console;
  renderer_->RegisterConsoleBindings(observer_ptr { &console });
  ASSERT_EQ(console.Execute("vtx.volumetric_fog.jitter false").status,
    console::ExecutionStatus::kOk);
  auto scene = std::make_shared<scene::Scene>("OffscreenRetirement", 8U);
  scene->SetEnvironment(std::make_unique<scene::SceneEnvironment>());
  scene->GetEnvironment()
    ->AddSystem<scene::environment::SkyAtmosphere>()
    .SetEnabled(true);
  auto& fog = scene->GetEnvironment()->AddSystem<scene::environment::Fog>();
  fog.SetEnabled(true);
  fog.SetEnableHeightFog(true);
  fog.SetEnableVolumetricFog(true);
  fog.SetExtinctionSigmaTPerMeter(.01F);
  fog.SetHeightFalloffPerMeter(0.0F);
  fog.SetVolumetricFogDistance(1000.0F);
  fog.SetVolumetricFogEmissive({ .125F, .25F, .5F });
  auto& post = scene->GetEnvironment()
                 ->AddSystem<scene::environment::PostProcessVolume>();
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 4.0F;
  settings.key = 12.5F;
  post.SetExposureSettings(settings);
  post.SetToneMapper(engine::ToneMapper::kNone);
  post.SetDisplayGamma(1.0F);
  post.SetBloomIntensity(0.0F);
  auto sun = scene->CreateNode("Sun");
  auto light = std::make_unique<scene::DirectionalLight>();
  light->SetEnvironmentContribution(true);
  light->SetAtmosphereLightSlot(scene::AtmosphereLightSlot::kPrimary);
  light->SetIntensityLux(1000.0F);
  ASSERT_TRUE(sun.AttachLight(std::move(light)));
  auto view = View {};
  view.viewport = { .width = 16.0F, .height = 16.0F };
  std::array<scene::SceneNode, 2> cameras;
  std::array<std::shared_ptr<Texture>, 2> colors;
  std::array<std::shared_ptr<Framebuffer>, 2> targets;
  for (unsigned i = 0U; i < 2U; ++i) {
    cameras[i] = scene->CreateNode(i ? "High camera" : "Low camera");
    auto lens = std::make_unique<scene::PerspectiveCamera>();
    lens->SetViewport(view.viewport);
    ASSERT_TRUE(cameras[i].AttachCamera(std::move(lens)));
    cameras[i].GetTransform().SetLocalPosition({ 0, -10, i ? 2000.0F : 2.0F });
    colors[i] = CreateRegisteredTexture({ .width = 16U,
      .height = 16U,
      .format = Format::kRGBA32Float,
      .is_shader_resource = true,
      .is_render_target = true,
      .initial_state = ResourceStates::kCommon });
    targets[i] = Backend().CreateFramebuffer(
      FramebufferDesc {}.AddColorAttachment(colors[i]));
  }
  scene->Update();
  struct Capture final : IViewExtension {
    Renderer& renderer;
    std::unordered_map<ViewId, std::vector<std::shared_ptr<Texture>>> textures;
    std::unordered_map<ViewId, std::vector<ShaderVisibleIndex>> slots;
    std::unordered_map<ViewId, postprocess::ExposurePass::FrameLease> exposure;
    explicit Capture(Renderer& value)
      : renderer(value)
    {
    }
    auto OnViewSetup(const ViewSetupContext& hook) -> void override
    {
      // The public extension supplies the fog participation flag absent from
      // the offscreen builder's current authoring surface.
      hook.render_context.current_view.with_height_fog = true;
    }
    auto OnPostRenderViewGpu(const ViewRenderGpuContext& hook) -> void override
    {
      auto* owner
        = vortex::testing::RendererPublicationProbe::GetSceneRenderer(renderer);
      const auto id = hook.render_context.current_view.view_id;
      exposure[id] = hook.render_context.current_view.frame_exposure;
      textures[id]
        = vortex::testing::RendererPublicationProbe::EnvironmentTextures(
          *owner, id);
      slots[id].clear();
      for (const auto& texture : textures[id]) {
        const auto slot
          = renderer.GetGraphics()
              ->GetResourceRegistry()
              .FindShaderVisibleIndex(*texture,
                TextureViewDescription {
                  .format = texture->GetDescriptor().format,
                  .dimension = texture->GetDescriptor().texture_type });
        CHECK_F(slot.has_value());
        slots[id].push_back(*slot);
      }
    }
  };
  auto capture = std::make_shared<Capture>(*renderer_);
  renderer_->RegisterViewExtension(capture);
  auto frame = engine::FrameContext {};
  frame.SetScene(observer_ptr { scene.get() });
  const auto begin = [&](std::uint64_t sequence, frame::Slot slot) {
    frame.SetFrameSequenceNumber(frame::SequenceNumber { sequence },
      engine::internal::EngineTagFactory::Get());
    frame.SetFrameSlot(slot, engine::internal::EngineTagFactory::Get());
    renderer_->OnFrameStart(observer_ptr { &frame });
  };
  const auto render = [&](unsigned index) {
    auto input = Renderer::OffscreenSceneViewInput::FromCamera(
      "Environment retirement", ViewId { 111U + index }, view, cameras[index]);
    input.SetWithAtmosphere(true);
    input.SetViewStateHandle(CompositionView::ViewStateHandle { 111U + index });
    auto facade = renderer_->ForOffscreenScene();
    facade.SetFrameSession({ .frame_slot = frame.GetFrameSlot(),
      .frame_sequence = frame.GetFrameSequenceNumber(),
      .delta_time_seconds = 0.0F });
    facade.SetSceneSource({ .scene = observer_ptr { scene.get() } });
    facade.SetViewIntent(input);
    facade.SetOutputTarget(
      { .framebuffer = observer_ptr { targets[index].get() } });
    auto session = facade.Finalize();
    CHECK_F(session.has_value());
    return session->ExecuteInsideFrame(frame);
  };
  const auto read = [&] {
    auto readback = GetReadbackManager()->CreateTextureReadback(
      "Offscreen environment image");
    {
      auto recorder = AcquireRecorder("Offscreen environment readback");
      CHECK_F(recorder->AdoptKnownResourceState(*colors[0]));
      CHECK_F(readback->EnqueueCopy(*recorder, *colors[0], {}).has_value());
    }
    const auto mapped = readback->MapNow();
    CHECK_F(mapped.has_value());
    std::array<Pixel, 256U> pixels;
    for (unsigned y = 0; y < 16U; ++y) {
      std::memcpy(pixels.data() + y * 16U,
        mapped->Data() + y * mapped->Layout().row_pitch.get(),
        16U * sizeof(Pixel));
    }
    return pixels;
  };
  auto& reclaimer = Backend().GetDeferredReclaimer();
  reclaimer.OnBeginFrame(frame::Slot { 0U });
  begin(1U, frame::Slot { 0U });
  ASSERT_TRUE(render(0U));
  const auto reference = read();
  renderer_->OnFrameEnd(observer_ptr { &frame });
  reclaimer.OnBeginFrame(frame::Slot { 1U });
  begin(2U, frame::Slot { 1U });
  const auto gpu_capture = BeginOptionalCapture();
  ASSERT_TRUE(render(0U));
  auto retained = capture->textures.at(ViewId { 111U });
  const auto retained_slots = capture->slots.at(ViewId { 111U });
  ASSERT_GE(
    retained.size(), 4U); // Sky/AP plus replaced and current fog history.
  auto& registry = Backend().GetResourceRegistry();
  for (const auto& texture : retained) {
    ASSERT_NE(texture, nullptr);
    ASSERT_TRUE(registry.Contains(*texture)) << texture->GetName();
  }
  // No queue-idle wait or readback map occurs between these offscreen views.
  ASSERT_TRUE(render(1U));
  if (gpu_capture) {
    EXPECT_TRUE(gpu_capture->EndCapture());
  }
  for (std::size_t i = 0U; i < retained.size(); ++i) {
    const auto& texture = retained[i];
    EXPECT_TRUE(registry.Contains(*texture)) << texture->GetName();
    EXPECT_EQ(
      registry.FindShaderVisibleIndex(*texture,
        TextureViewDescription { .format = texture->GetDescriptor().format,
          .dimension = texture->GetDescriptor().texture_type }),
      retained_slots[i]);
  }
  constexpr std::uint32_t required
    = (1U << 4U) | (1U << 5U) | (1U << 9U) | (1U << 10U);
  for (const auto id : { ViewId { 111U }, ViewId { 112U } }) {
    const auto& exposure = capture->exposure.at(id);
    ASSERT_NE(exposure, nullptr);
    const auto report = Read<HdrSuitabilityData>(
      *exposure->suitability_buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(report.expected_products, required);
    EXPECT_EQ(report.checked_products, required);
    EXPECT_GT(report.checked_samples, 256U);
    const auto state = Read<ExposureStateData>(
      *exposure->current_state->buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(state.displayed_scale, .0625F);
    EXPECT_NE(state.product_layout_revision[0], 0U);
  }
  const auto actual = read();
  unsigned nontrivial = 0U;
  for (unsigned i = 0; i < actual.size(); ++i) {
    for (unsigned c = 0; c < 3U; ++c) {
      EXPECT_TRUE(std::isfinite(actual[i][c]));
      EXPECT_NEAR(actual[i][c], reference[i][c],
        2e-5F + .005F * std::abs(reference[i][c]));
      nontrivial += reference[i][c] > .001F && reference[i][c] < .99F ? 1U : 0U;
    }
  }
  EXPECT_GT(nontrivial, 0U);
  renderer_->OnFrameEnd(observer_ptr { &frame });
  WaitForQueueIdle();
  reclaimer.OnBeginFrame(frame::Slot { 2U });
  EXPECT_TRUE(registry.Contains(*retained.front()));
  reclaimer.OnBeginFrame(frame::Slot { 1U });
  EXPECT_TRUE(registry.Contains(*retained.front()));
  const auto original_state = Read<ExposureStateData>(
    *capture->exposure.at(ViewId { 111U })->current_state->buffer,
    ResourceStates::kShaderResource);
  scene->GetEnvironment()
    ->TryGetSystem<scene::environment::SkyAtmosphere>()
    ->SetAerialScatteringStrength(2.0F);
  scene->Update();
  reclaimer.OnBeginFrame(frame::Slot { 2U });
  begin(3U, frame::Slot { 2U });
  ASSERT_TRUE(render(0U));
  const auto amplified_state = Read<ExposureStateData>(
    *capture->exposure.at(ViewId { 111U })->current_state->buffer,
    ResourceStates::kShaderResource);
  EXPECT_NE(amplified_state.product_layout_revision,
    original_state.product_layout_revision);
  EXPECT_EQ(amplified_state.displayed_scale, original_state.displayed_scale);
  EXPECT_EQ(
    amplified_state.requested_generation, original_state.requested_generation);
  EXPECT_EQ(
    amplified_state.applied_generation, original_state.applied_generation);
  renderer_->OnFrameEnd(observer_ptr { &frame });
  WaitForQueueIdle();
  // The producer and captured view now refer to the replacement snapshot.
  // Keep only an underlying observer when releasing the old retained readers.
  const auto retired_resource = retained.front()->shared_from_this();
  EXPECT_TRUE(registry.Contains(*retired_resource));
  retained.clear();
  capture->textures.clear();
  reclaimer.OnBeginFrame(frame::Slot { 1U });
  EXPECT_TRUE(registry.Contains(*retired_resource));
  reclaimer.OnBeginFrame(frame::Slot { 2U });
  EXPECT_FALSE(registry.Contains(*retired_resource));
  begin(4U, frame::Slot { 0U });
  ASSERT_TRUE(render(0U));
  const auto stable_state = Read<ExposureStateData>(
    *capture->exposure.at(ViewId { 111U })->current_state->buffer,
    ResourceStates::kShaderResource);
  EXPECT_EQ(stable_state.product_layout_revision,
    amplified_state.product_layout_revision);
  renderer_->OnFrameEnd(observer_ptr { &frame });
  WaitForQueueIdle();
  begin(5U, frame::Slot { 1U });
  static_cast<ExposureFailureGraphics&>(Backend()).fail_recorder_name
    = "EnvironmentLightingService AtmosphereSkyViewLut";
  ASSERT_TRUE(render(0U));
  static_cast<ExposureFailureGraphics&>(Backend()).fail_recorder_name.clear();
  const auto& missing = capture->exposure.at(ViewId { 111U });
  const auto report = Read<HdrSuitabilityData>(
    *missing->suitability_buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(report.expected_products, required);
  EXPECT_EQ(report.checked_products, required & ~(1U << 4U));
  const auto status = Read<ExposureCompletedStatus>(
    *missing->current_state->status_buffer, ResourceStates::kCopySource);
  EXPECT_NE(status.first_failure_kind & 16U, 0U);
  EXPECT_EQ(status.fp16_eligible_streak, 0U);
  renderer_->OnFrameEnd(observer_ptr { &frame });
  WaitForQueueIdle();
}

NOLINT_TEST_F(
  ExposureGpuTest, SceneFogHistoryRescalesRgbWithoutScalingTransmittance)
{
  pass_.reset();
  renderer_->OnShutdown();
  auto config = RendererConfig {};
  config.upload_queue_key = QueueKeyFor().get();
  renderer_ = std::make_unique<Renderer>(GetGraphicsShared(), config,
    RendererCapabilityFamily::kScenePreparation
      | RendererCapabilityFamily::kDeferredShading
      | RendererCapabilityFamily::kLightingData
      | RendererCapabilityFamily::kEnvironmentLighting
      | RendererCapabilityFamily::kFinalOutputComposition);
  console::Console console;
  renderer_->RegisterConsoleBindings(observer_ptr { &console });
  ASSERT_EQ(console.Execute("vtx.volumetric_fog.jitter false").status,
    console::ExecutionStatus::kOk);
  auto scene = std::make_shared<scene::Scene>("FogDomainFixture", 4U);
  scene->SetEnvironment(std::make_unique<scene::SceneEnvironment>());
  auto& fog = scene->GetEnvironment()->AddSystem<scene::environment::Fog>();
  fog.SetEnabled(true);
  fog.SetEnableHeightFog(true);
  fog.SetEnableVolumetricFog(true);
  fog.SetExtinctionSigmaTPerMeter(.01F);
  fog.SetHeightFalloffPerMeter(0.0F);
  fog.SetVolumetricFogDistance(1000.0F);
  auto camera = scene->CreateNode("Camera");
  auto lens = std::make_unique<scene::PerspectiveCamera>();
  auto view = View {};
  view.viewport = { .width = 4.0F, .height = 4.0F };
  lens->SetViewport(view.viewport);
  ASSERT_TRUE(camera.AttachCamera(std::move(lens)));
  struct DomainOverride final : IViewExtension {
    Renderer& renderer;
    explicit DomainOverride(Renderer& value)
      : renderer(value)
    {
    }
    auto OnViewSetup(const ViewSetupContext& hook) -> void override
    {
      auto* owner
        = vortex::testing::RendererPublicationProbe::GetSceneRenderer(renderer);
      auto* service
        = vortex::testing::RendererPublicationProbe::GetPostProcessService(
          *owner);
      auto cfg = PostProcessConfig {};
      cfg.exposure = *hook.render_context.current_view.exposure_override;
      service->SetConfig(cfg);
      ASSERT_NE(
        service->PrepareFrameExposure(hook.render_context, false), nullptr);
    }
  };
  renderer_->RegisterViewExtension(
    std::make_shared<DomainOverride>(*renderer_));
  auto frame = engine::FrameContext {};
  frame.SetScene(observer_ptr { scene.get() });
  std::array<std::shared_ptr<Framebuffer>, 2> outputs;
  for (auto& output : outputs) {
    auto texture = CreateRegisteredTexture({ .width = 4U,
      .height = 4U,
      .format = Format::kRGBA32Float,
      .is_shader_resource = true,
      .is_render_target = true,
      .initial_state = ResourceStates::kCommon });
    output = Backend().CreateFramebuffer(
      FramebufferDesc {}.AddColorAttachment(texture));
  }
  Pixel initial {};
  for (unsigned sequence = 1U; sequence <= 2U; ++sequence) {
    fog.SetVolumetricFogEmissive(
      sequence == 1U ? Vec3 { .1F, .2F, .3F } : Vec3 { 1.0F, 2.0F, 3.0F });
    scene->Update();
    frame.SetFrameSequenceNumber(frame::SequenceNumber { sequence },
      engine::internal::EngineTagFactory::Get());
    frame.SetFrameSlot(
      frame::Slot { sequence - 1U }, engine::internal::EngineTagFactory::Get());
    renderer_->OnFrameStart(observer_ptr { &frame });
    std::array<ViewId, 2> published;
    for (unsigned index = 0U; index < 2U; ++index) {
      auto input
        = CompositionView::ForScene(ViewId { 9100U + index }, view, camera);
      input.view_state_handle
        = CompositionView::ViewStateHandle { 9100U + index };
      input.with_height_fog = true;
      auto settings = scene::ExposureSettings {};
      settings.key = 12.5F;
      settings.mode = engine::ExposureMode::kManual;
      settings.manual_ev = sequence == 2U && index == 1U ? 8.0F : 4.0F;
      input.render_settings.exposure = settings;
      published[index] = renderer_->PublishRuntimeCompositionView(frame,
        { .composition_view = input,
          .render_target = observer_ptr { outputs[index].get() } });
      ASSERT_NE(published[index], kInvalidViewId);
    }
    auto loop = co::testing::TestEventLoop {};
    co::Run(loop, [&]() -> co::Co<void> {
      co_await renderer_->OnPreRender(observer_ptr { &frame });
      co_await renderer_->OnRender(observer_ptr { &frame });
    });
    renderer_->OnFrameEnd(observer_ptr { &frame });
    auto* owner
      = vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_);
    ASSERT_NE(owner, nullptr);
    std::array<Pixel, 2> samples;
    for (unsigned index = 0U; index < 2U; ++index) {
      const auto [texture, exposure]
        = vortex::testing::RendererPublicationProbe::FogHistory(
          *owner, published[index]);
      ASSERT_NE(texture, nullptr);
      ASSERT_NE(exposure, nullptr);
      const auto certificate = Read<ExposureStatusStorage>(
        *exposure->current_state->status_buffer, ResourceStates::kCopySource);
      const auto& fog_error = certificate.producer_errors[2];
      EXPECT_EQ(fog_error.rgb_relative, 0.0F);
      EXPECT_EQ(fog_error.rgb_absolute, 0.0F);
      EXPECT_EQ(fog_error.transmittance_relative, 0.0F);
      EXPECT_EQ(fog_error.transmittance_absolute, 0.0F);
      const auto domain = Read<FrameExposureData>(
        *exposure->buffer, ResourceStates::kShaderResource);
      EXPECT_EQ(domain.pre_exposure,
        sequence == 2U && index == 1U ? 1.0F / 256.0F : 1.0F / 16.0F);
      auto readback
        = GetReadbackManager()->CreateTextureReadback("Fog domain voxel");
      {
        auto recorder = AcquireRecorder("Fog domain voxel readback");
        ASSERT_TRUE(recorder->AdoptKnownResourceState(*texture));
        ASSERT_TRUE(readback
            ->EnqueueCopy(*recorder, *texture,
              { .src_slice = { .x = 0U,
                  .y = 0U,
                  .z = 16U,
                  .width = 1U,
                  .height = 1U,
                  .depth = 1U } })
            .has_value());
      }
      const auto mapped = readback->MapNow();
      ASSERT_TRUE(mapped.has_value());
      std::memcpy(samples[index].data(), mapped->Data(), sizeof(Pixel));
    }
    ASSERT_GT(samples[0][0], 1e-8F);
    const float ratio = sequence == 2U ? 1.0F / 16.0F : 1.0F;
    for (unsigned channel = 0U; channel < 3U; ++channel)
      EXPECT_NEAR(samples[1][channel], samples[0][channel] * ratio,
        std::max(1e-7F, samples[0][channel] * ratio * 2e-4F));
    EXPECT_NEAR(samples[1][3], samples[0][3], 2e-6F);
    if (sequence == 1U)
      initial = samples[0];
    else {
      EXPECT_GT(samples[0][0], initial[0] * 1.1F);
      EXPECT_LT(samples[0][0], initial[0] * 5.0F);
    }
  }
  renderer_->RegisterConsoleBindings({});
  FlushBackend();
}

NOLINT_TEST_F(
  ExposureGpuTest, HeightFogInputPeakUsesSceneUnitsAndRejectsNonfiniteInput)
{
  pass_.reset();
  renderer_->OnShutdown();
  auto renderer_config = RendererConfig {};
  renderer_config.upload_queue_key = QueueKeyFor().get();
  renderer_ = std::make_unique<Renderer>(GetGraphicsShared(), renderer_config,
    kPhase1DefaultRuntimeCapabilityFamilies
      | RendererCapabilityFamily::kEnvironmentLighting);
  pass_ = std::make_unique<postprocess::ExposurePass>(*renderer_);
  auto scene = scene::Scene("Height fog source range", 1U);
  scene.SetEnvironment(std::make_unique<scene::SceneEnvironment>());
  auto& fog = scene.GetEnvironment()->AddSystem<scene::environment::Fog>();
  fog.SetEnabled(true);
  fog.SetEnableHeightFog(true);
  ctx_.scene = observer_ptr { &scene };
  ctx_.current_view.with_height_fog = true;
  auto textures = SceneTextures(Backend(),
    { .extent = { 8U, 8U },
      .enable_velocity = false,
      .scene_color_format = Format::kRGBA32Float });
  auto framebuffer = Backend().CreateFramebuffer(FramebufferDesc {}
      .AddColorAttachment(textures.GetSceneColorResource())
      .SetDepthAttachment({ .texture = textures.GetSceneDepthResource() }));
  auto& allocator = renderer_->GetGraphics()->GetDescriptorAllocator();
  auto depth_handle = allocator.AllocateRaw(
    ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
  const auto depth_slot = allocator.GetShaderVisibleIndex(depth_handle);
  Backend().GetResourceRegistry().RegisterView(textures.GetSceneDepth(),
    std::move(depth_handle),
    TextureViewDescription {
      .format = textures.GetSceneDepth().GetDescriptor().format,
      .dimension = TextureType::kTexture2D });
  auto scene_bindings = SceneTextureBindings {};
  scene_bindings.scene_depth_srv = depth_slot.get();
  const auto scene_slot = PublishFixtureData(scene_bindings);
  const auto environment_view_slot = PublishFixtureData(EnvironmentViewData {});
  auto compose = environment::FogPass(*renderer_);
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev
    = -2.0F; // P=4; the collected source peak must remain scene referred.
  const auto config = SharedConfig(settings);
  const auto capture = BeginOptionalCapture();
  for (const float scale :
    { .8F, .2F, std::numeric_limits<float>::quiet_NaN() }) {
    SCOPED_TRACE(scale);
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    const auto frame = pass_->ResolveFrame(ctx_, config, { .use_fp32 = false });
    ASSERT_NE(frame, nullptr);
    ctx_.current_view.frame_exposure = frame;
    auto environment_static = EnvironmentStaticData {};
    environment_static.fog.flags = kGpuFogFlagEnabled
      | kGpuFogFlagRenderInMainPass | kGpuFogFlagHeightFogEnabled;
    environment_static.fog.primary_density = .1F;
    environment_static.fog.primary_height_falloff = 0.0F;
    environment_static.fog.fog_inscattering_luminance_rgb
      = { scale * .25F, scale * .5F, scale };
    auto environment_bindings = EnvironmentFrameBindings {};
    environment_bindings.environment_static_slot
      = PublishFixtureData(environment_static);
    environment_bindings.environment_view_slot = environment_view_slot;
    auto view_bindings = ViewFrameBindings {};
    view_bindings.environment_frame_slot
      = PublishFixtureData(environment_bindings);
    view_bindings.scene_texture_frame_slot = scene_slot;
    view_bindings.frame_exposure_slot = frame->srv_index;
    view_bindings.exposure_status_uav = frame->current_state->status_uav_index;
    auto view = ViewConstants::GpuData {};
    view.view_frame_bindings_bslot
      = BindlessViewFrameBindingsSlot { PublishFixtureData(view_bindings) };
    view.reverse_z = 0U;
    view.inverse_view_projection_matrix = glm::mat4 { 0.0F };
    view.inverse_view_projection_matrix[3] = { 0, 0, -1, 1 };
    auto constants
      = CreateUploadBuffer(SizeBytes { 256U }, BufferUsage::kConstant);
    constants->Update(&view, sizeof(view), 0U);
    ctx_.view_constants = constants;
    {
      auto recorder = AcquireRecorder("Height fog source initialization");
      for (const auto& texture :
        { textures.GetSceneColorResource(), textures.GetSceneDepthResource() })
        if (!recorder->AdoptKnownResourceState(*texture))
          recorder->BeginTrackingResourceState(
            *texture, texture->GetDescriptor().initial_state);
      recorder->RequireResourceState(
        textures.GetSceneColor(), ResourceStates::kRenderTarget);
      recorder->RequireResourceState(
        textures.GetSceneDepth(), ResourceStates::kDepthWrite);
      recorder->FlushBarriers();
      recorder->ClearFramebuffer(
        *framebuffer, std::vector<std::optional<Color>> { Color {} }, 0.0F);
    }
    ASSERT_TRUE(compose.Record(ctx_, textures).executed);
    const auto pixels = ReadFloatTexture(textures.GetSceneColor());
    const auto input = Read<ExposureStatusStorage>(
      *frame->current_state->status_buffer, ResourceStates::kUnorderedAccess)
                         .consumer_inputs;
    EXPECT_EQ(input.translucent_rgb_max, 0.0F);
    EXPECT_EQ(input.sky_rgb_gain_max, 0.0F);
    if (std::isfinite(scale)) {
      EXPECT_EQ(input.flags, 2U);
      const double expected
        = double(scale) * (1.0 - std::exp2(-std::log(2.0) * double(.1F)));
      EXPECT_NEAR(input.height_fog_rgb_max, expected, 2e-6);
      for (const auto& pixel : pixels)
        EXPECT_EQ(input.height_fog_rgb_max, pixel[2] / 4.0F);
    } else {
      EXPECT_EQ(input.flags, 10U);
      EXPECT_EQ(input.height_fog_rgb_max, 0.0F);
    }
  }
  if (capture)
    EXPECT_TRUE(capture->EndCapture());
  ctx_.view_constants.reset();
  ctx_.current_view.frame_exposure.reset();
  ctx_.scene.reset();
  WaitForQueueIdle();
}

NOLINT_TEST_F(ExposureGpuTest, FogCompositionClampsViewportAndDepthEdges)
{
  pass_.reset();
  renderer_->OnShutdown();
  auto config = RendererConfig {};
  config.upload_queue_key = QueueKeyFor().get();
  renderer_ = std::make_unique<Renderer>(GetGraphicsShared(), config,
    kPhase1DefaultRuntimeCapabilityFamilies
      | RendererCapabilityFamily::kEnvironmentLighting);
  auto scene = scene::Scene("Fog composition edges", 1U);
  scene.SetEnvironment(std::make_unique<scene::SceneEnvironment>());
  auto& fog = scene.GetEnvironment()->AddSystem<scene::environment::Fog>();
  fog.SetEnabled(true);
  fog.SetEnableVolumetricFog(true);
  ctx_.scene = observer_ptr { &scene };
  ctx_.current_view.with_height_fog = true;
  auto textures = SceneTextures(Backend(),
    { .extent = { 8U, 8U },
      .enable_velocity = false,
      .scene_color_format = Format::kRGBA32Float });
  auto framebuffer = Backend().CreateFramebuffer(FramebufferDesc {}
      .AddColorAttachment(textures.GetSceneColorResource())
      .SetDepthAttachment({ .texture = textures.GetSceneDepthResource() }));
  auto& allocator = renderer_->GetGraphics()->GetDescriptorAllocator();
  auto depth_handle = allocator.AllocateRaw(
    ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
  const auto depth_slot = allocator.GetShaderVisibleIndex(depth_handle);
  Backend().GetResourceRegistry().RegisterView(textures.GetSceneDepth(),
    std::move(depth_handle),
    TextureViewDescription {
      .format = textures.GetSceneDepth().GetDescriptor().format,
      .dimension = TextureType::kTexture2D });
  auto scene_bindings = SceneTextureBindings {};
  scene_bindings.scene_depth_srv = depth_slot.get();
  const auto scene_slot = PublishFixtureData(scene_bindings);
  const auto environment_view_slot = PublishFixtureData(EnvironmentViewData {});
  auto compose = environment::FogPass(*renderer_);
  std::array<Pixel, 8> pixels;
  for (unsigned z = 0; z < 2; ++z)
    for (unsigned y = 0; y < 2; ++y)
      for (unsigned x = 0; x < 2; ++x)
        pixels[(z * 2 + y) * 2 + x]
          = Pixel { float(x), float(y), float(z), 1.0F - float(z) };
  const auto capture = BeginOptionalCapture();
  for (const auto format : { Format::kRGBA32Float, Format::kRGBA16Float }) {
    const auto volume = MakeSignal(2, 2, pixels, 2, format, true);
    auto environment_static = EnvironmentStaticData {};
    environment_static.fog.flags
      = kGpuFogFlagEnabled | kGpuFogFlagRenderInMainPass;
    environment_static.volumetric_fog.flags = kGpuVolumetricFogFlagEnabled
      | kGpuVolumetricFogFlagIntegratedScatteringValid;
    environment_static.volumetric_fog.integrated_light_scattering_srv
      = volume.srv.get();
    environment_static.volumetric_fog.distance_m = 1.0F;
    environment_static.volumetric_fog.grid_depth = 2U;
    auto environment_bindings = EnvironmentFrameBindings {};
    environment_bindings.environment_static_slot
      = PublishFixtureData(environment_static);
    environment_bindings.environment_view_slot = environment_view_slot;
    auto view_bindings = ViewFrameBindings {};
    view_bindings.environment_frame_slot
      = PublishFixtureData(environment_bindings);
    view_bindings.scene_texture_frame_slot = scene_slot;
    const auto view_slot = PublishFixtureData(view_bindings);
    for (const auto distance : { 0.0F, .25F, 1.0F, 4.0F }) {
      SCOPED_TRACE(distance);
      auto view = ViewConstants::GpuData {};
      view.view_frame_bindings_bslot
        = BindlessViewFrameBindingsSlot { view_slot };
      view.reverse_z = 0U;
      view.inverse_view_projection_matrix = glm::mat4 { 0.0F };
      view.inverse_view_projection_matrix[3] = { 0, 0, -distance, 1 };
      auto constants
        = CreateUploadBuffer(SizeBytes { 256U }, BufferUsage::kConstant);
      constants->Update(&view, sizeof(view), 0U);
      ctx_.view_constants = constants;
      {
        auto recorder = AcquireRecorder("Fog edge initialization");
        for (const auto& texture : { textures.GetSceneColorResource(),
               textures.GetSceneDepthResource() })
          if (!recorder->AdoptKnownResourceState(*texture))
            recorder->BeginTrackingResourceState(
              *texture, texture->GetDescriptor().initial_state);
        recorder->RequireResourceState(
          textures.GetSceneColor(), ResourceStates::kRenderTarget);
        recorder->RequireResourceState(
          textures.GetSceneDepth(), ResourceStates::kDepthWrite);
        recorder->FlushBarriers();
        recorder->ClearFramebuffer(
          *framebuffer, std::vector<std::optional<Color>> { Color {} }, 0.0F);
      }
      ASSERT_TRUE(compose.Record(ctx_, textures).executed);
      const auto result = ReadFloatTexture(textures.GetSceneColor());
      const double z = std::clamp(
        2 * std::sqrt(std::clamp(double(distance), 0.0, 1.0)) - .5, 0.0, 1.0);
      for (unsigned y = 0; y < 8; ++y)
        for (unsigned x = 0; x < 8; ++x) {
          const auto& pixel = result[y * 8 + x];
          EXPECT_NEAR(
            pixel[0], std::clamp((double(x) + .5) / 4 - .5, 0.0, 1.0), 3e-7);
          EXPECT_NEAR(
            pixel[1], std::clamp((double(y) + .5) / 4 - .5, 0.0, 1.0), 3e-7);
          EXPECT_NEAR(pixel[2], z, 3e-7);
          EXPECT_NEAR(pixel[3], z, 3e-7);
        }
    }
  }
  if (capture)
    EXPECT_TRUE(capture->EndCapture());
  ctx_.view_constants.reset();
  ctx_.scene.reset();
  WaitForQueueIdle();
}

NOLINT_TEST_F(
  ExposureGpuTest, FogHistoryClampsEdgesAndRejectsOutsideCoordinates)
{
  namespace root = oxygen::bindless::generated::d3d12;
  const auto bindings = ExposureProbeRootBindings();
  const auto pipeline
    = ComputePipelineDesc::Builder {}
        .SetComputeShader(ShaderRequest { .stage = ShaderType::kCompute,
          .source_path = "Vortex/Services/Environment/VolumetricFog.hlsl",
          .entry_point = "VortexVolumetricFogCS" })
        .SetRootBindings(bindings)
        .SetDebugName("Fog history edge fixture")
        .Build();
  auto output = CreateRegisteredTexture({ .width = 1U,
    .height = 1U,
    .depth = 2U,
    .format = Format::kRGBA32Float,
    .texture_type = TextureType::kTexture3D,
    .is_shader_resource = true,
    .is_uav = true,
    .initial_state = ResourceStates::kCommon });
  auto& allocator = renderer_->GetGraphics()->GetDescriptorAllocator();
  auto handle = allocator.AllocateRaw(
    ResourceViewType::kTexture_UAV, DescriptorVisibility::kShaderVisible);
  const auto output_slot = allocator.GetShaderVisibleIndex(handle);
  Backend().GetResourceRegistry().RegisterView(*output, std::move(handle),
    TextureViewDescription { .view_type = ResourceViewType::kTexture_UAV,
      .format = Format::kRGBA32Float,
      .dimension = TextureType::kTexture3D });
  std::array<Pixel, 8> samples;
  for (unsigned z = 0; z < 2; ++z)
    for (unsigned y = 0; y < 2; ++y)
      for (unsigned x = 0; x < 2; ++x)
        samples[(z * 2 + y) * 2 + x]
          = Pixel { float(x), float(y), float(z), 1.0F - float(z) };
  const std::array coordinates { glm::vec2 { 0, .5F }, glm::vec2 { 1, .5F },
    glm::vec2 { .5F, 0 }, glm::vec2 { .5F, 1 }, glm::vec2 { .0625F, .5F },
    glm::vec2 { .9375F, .5F }, glm::vec2 { .5F, .0625F },
    glm::vec2 { .5F, .9375F }, glm::vec2 { .25F, .25F }, glm::vec2 { .5F, .5F },
    glm::vec2 { .75F, .75F }, glm::vec2 { -.01F, .5F },
    glm::vec2 { 1.01F, .5F }, glm::vec2 { .5F, -.01F },
    glm::vec2 { .5F, 1.01F } };
  const auto capture = BeginOptionalCapture();
  for (const auto format : { Format::kRGBA32Float, Format::kRGBA16Float }) {
    const auto volume = MakeSignal(2, 2, samples, 2, format, true);
    auto params
      = vortex::testing::RendererPublicationProbe::FogPassConstants {};
    params.output_header = { output_slot.get(), 1U, 1U, 2U };
    params.grid.end_distance_m = 10.0F;
    params.grid_z.grid_z_params[0] = 1.0F;
    params.grid_z.grid_z_params[1] = 0.0F;
    params.grid_z.grid_z_params[2] = 1.0F;
    params.temporal_history0 = { volume.srv.get(), 1U, 1.0F, 1U };
    const auto pass_slot = PublishFixtureData(params);
    for (const auto uv : coordinates)
      for (const float depth : { 1.0F, std::sqrt(2.0F), 3.0F, .5F, 8.0F }) {
        SCOPED_TRACE(uv.x);
        SCOPED_TRACE(uv.y);
        SCOPED_TRACE(depth);
        auto history = ViewHistoryFrameBindings {};
        history.validity_flags = static_cast<std::uint32_t>(
          ViewHistoryValidityFlagBits::kPreviousViewValid);
        history.previous_view_matrix[3].z = -depth;
        history.previous_projection_matrix = glm::mat4 { 0.0F };
        history.previous_projection_matrix[3]
          = { 2 * uv.x - 1, 1 - 2 * uv.y, 0, 1 };
        auto frame_bindings = ViewFrameBindings {};
        frame_bindings.history_frame_slot = PublishFixtureData(history);
        auto view = ViewConstants::GpuData {};
        view.view_frame_bindings_bslot = BindlessViewFrameBindingsSlot {
          PublishFixtureData(frame_bindings)
        };
        view.inverse_view_projection_matrix = glm::mat4 { 0.0F };
        view.inverse_view_projection_matrix[3].w = 1.0F;
        auto view_buffer
          = CreateUploadBuffer(SizeBytes { 256U }, BufferUsage::kConstant);
        view_buffer->Update(&view, sizeof(view), 0U);
        {
          auto recorder = AcquireRecorder("Fog history edge dispatch");
          if (!recorder->AdoptKnownResourceState(*output))
            recorder->BeginTrackingResourceState(
              *output, ResourceStates::kCommon, false);
          recorder->RequireResourceState(
            *output, ResourceStates::kUnorderedAccess);
          recorder->FlushBarriers();
          recorder->SetPipelineState(pipeline);
          recorder->SetComputeRootConstantBufferView(
            static_cast<std::uint32_t>(root::RootParam::kViewConstants),
            view_buffer->GetGPUVirtualAddress());
          recorder->SetComputeRoot32BitConstant(
            static_cast<std::uint32_t>(root::RootParam::kRootConstants), 0U,
            0U);
          recorder->SetComputeRoot32BitConstant(
            static_cast<std::uint32_t>(root::RootParam::kRootConstants),
            pass_slot.get(), 1U);
          recorder->Dispatch(1U, 1U, 1U);
        }
        const bool rejected = uv.x < 0 || uv.x > 1 || uv.y < 0 || uv.y > 1
          || depth < 1 || depth >= 4;
        const double w
          = std::clamp(std::log2(double(depth)) / 2, 0.0, 1.0);
        const double z = std::clamp(2 * w - .5, 0.0, 1.0);
        const std::array expected = rejected
          ? std::array<double, 4> { 0, 0, 0, 1 }
          : std::array<double, 4> { std::clamp(2 * double(uv.x) - .5, 0.0, 1.0),
              std::clamp(2 * double(uv.y) - .5, 0.0, 1.0), z, 1 - z };
        for (const auto& actual : ReadFloatTexture(*output))
          for (unsigned c = 0; c < 4; ++c)
            EXPECT_NEAR(actual[c], expected[c], 3e-6);
      }
  }
  if (capture)
    EXPECT_TRUE(capture->EndCapture());
  WaitForQueueIdle();
}

NOLINT_TEST_F(ExposureGpuTest, LocalFogInjectionMatchesMixedMediumIntegral)
{
  namespace root = oxygen::bindless::generated::d3d12;
  auto fog_scene = std::make_shared<scene::Scene>("Injected medium range", 8U);
  auto node = fog_scene->CreateNode("Local medium");
  auto impl = node.GetImpl();
  ASSERT_TRUE(impl.has_value());
  auto& local = impl->get().AddComponent<scene::environment::LocalFogVolume>();
  local.SetEnabled(true);
  local.SetHeightFogFalloff(1);
  local.SetHeightFogOffset(0);
  const float sample_depth = std::sqrt(2.0F);
  node.GetTransform().SetLocalPosition({ 0, 0, -sample_depth });
  node.GetTransform().SetLocalScale({ .2F, .2F, .2F });
  fog_scene->Update();
  auto resolved = ResolvedView { ResolvedView::Params {} };
  ctx_.scene = observer_ptr { fog_scene.get() };
  ctx_.current_view.resolved_view = observer_ptr { &resolved };
  ctx_.current_view.with_local_fog = true;
  auto local_state = environment::internal::LocalFogVolumeState(*renderer_);
  auto output = CreateRegisteredTexture({ .width = 1,
    .height = 1,
    .depth = 1,
    .format = Format::kRGBA32Float,
    .texture_type = TextureType::kTexture3D,
    .is_shader_resource = true,
    .is_uav = true,
    .initial_state = ResourceStates::kCommon });
  auto tiles = CreateRegisteredTexture({ .width = 1,
    .height = 1,
    .array_size = 2,
    .format = Format::kR32UInt,
    .texture_type = TextureType::kTexture2DArray,
    .is_shader_resource = true,
    .initial_state = ResourceStates::kCommon });
  auto tile_upload = CreateUploadBuffer(SizeBytes { 1024 });
  std::array<std::uint32_t, 256> tile_data {};
  tile_data[0] = 1;
  tile_upload->Update(tile_data.data(), sizeof(tile_data), 0);
  {
    auto recorder = AcquireRecorder("Injected medium tile upload");
    EnsureTracked(*recorder, tile_upload, ResourceStates::kGenericRead);
    EnsureTracked(*recorder, tiles, ResourceStates::kCommon);
    recorder->RequireResourceState(*tiles, ResourceStates::kCopyDest);
    recorder->FlushBarriers();
    recorder->CopyBufferToTexture(*tile_upload,
      { .buffer_row_pitch = 256,
        .buffer_slice_pitch = 256,
        .dst_slice = { .width = 1, .height = 1, .depth = 1 } },
      *tiles);
    recorder->RequireResourceStateFinal(
      *tiles, ResourceStates::kShaderResource);
  }
  auto& allocator = renderer_->GetGraphics()->GetDescriptorAllocator();
  auto tile_handle = allocator.AllocateRaw(
    ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
  const auto tile_slot = allocator.GetShaderVisibleIndex(tile_handle);
  Backend().GetResourceRegistry().RegisterView(*tiles, std::move(tile_handle),
    TextureViewDescription {
      .format = Format::kR32UInt, .dimension = TextureType::kTexture2DArray });
  auto output_handle = allocator.AllocateRaw(
    ResourceViewType::kTexture_UAV, DescriptorVisibility::kShaderVisible);
  const auto output_slot = allocator.GetShaderVisibleIndex(output_handle);
  Backend().GetResourceRegistry().RegisterView(*output,
    std::move(output_handle),
    TextureViewDescription { .view_type = ResourceViewType::kTexture_UAV,
      .format = Format::kRGBA32Float,
      .dimension = TextureType::kTexture3D });
  const auto pipeline
    = ComputePipelineDesc::Builder {}
        .SetComputeShader(ShaderRequest { .stage = ShaderType::kCompute,
          .source_path = "Vortex/Services/Environment/VolumetricFog.hlsl",
          .entry_point = "VortexVolumetricFogCS" })
        .SetRootBindings(ExposureProbeRootBindings())
        .SetDebugName("Injected medium analytic fixture")
        .Build();
  struct Case {
    const char* name;
    float global_density, radial, height, global_emission, local_emission,
      illuminance;
    bool scattering;
    bool unsupported;
  };
  const std::array cases { Case {
                             "vacuum", 0, 0, 1, 1, 0x1p32F, 0, false, false },
    Case { "height only", .06F, 0, 1, 2, 0, 0, false, false },
    Case { "thin local emission", 0, 1, 0x1p-40F, 0, 0x1p32F, 0, false, false },
    Case { "small local emission", 0, 1, 1, 0, 0x1p-22F, 0, false, false },
    Case { "bright local emission", 0, 1, 1, 0, 0x1p32F, 0, false, false },
    Case { "mixed emission", .1F, 1, 1, 1, 3, 0, false, false },
    Case {
      "thin local scattering", 0, 1, 0x1p-24F, 0, 0, 0x1p32F, true, false },
    Case { "mixed scattering", .06F, 1, 1, 0, 0, 0x1p32F, true, false },
    Case { "unsupported", 0, 1, 1, 0, 0x1p38F, 0, false, true } };
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.key = 12.5F;
  auto context = engine::FrameContext {};
  context.SetScene(observer_ptr { fog_scene.get() });
  unsigned sequence = 0;
  for (const auto& test : cases)
    for (const float ev : { -32.0F, 0.0F, 32.0F }) {
      SCOPED_TRACE(test.name);
      SCOPED_TRACE(ev);
      const auto slot = frame::Slot { sequence % 3 };
      const auto seq = frame::SequenceNumber { ++sequence };
      Backend().BeginFrame(seq, slot);
      context.SetFrameSlot(slot, engine::internal::EngineTagFactory::Get());
      context.SetFrameSequenceNumber(
        seq, engine::internal::EngineTagFactory::Get());
      renderer_->OnFrameStart(observer_ptr { &context });
      ctx_.frame_slot = slot;
      ctx_.frame_sequence = seq;
      local.SetRadialFogExtinction(test.radial);
      local.SetHeightFogExtinction(test.height);
      local.SetFogEmissive(
        { test.local_emission, test.local_emission, test.local_emission });
      local.SetFogAlbedo(test.scattering ? Vec3 { 1, 1, 1 } : Vec3 { 0, 0, 0 });
      local_state.OnFrameStart(seq, slot);
      const auto products = local_state.Prepare(ctx_);
      ASSERT_TRUE(products.buffer_ready);
      ASSERT_EQ(products.instance_count, 1U);
      settings.manual_ev = ev;
      const auto exposure = pass_->ResolveFrame(
        ctx_, SharedConfig(settings), { .use_fp32 = false });
      ASSERT_NE(exposure, nullptr);
      auto bindings = ViewFrameBindings {};
      bindings.frame_exposure_slot = exposure->srv_index;
      bindings.exposure_status_uav = exposure->current_state->status_uav_index;
      auto view = ViewConstants::GpuData {};
      view.view_frame_bindings_bslot
        = BindlessViewFrameBindingsSlot { PublishFixtureData(bindings) };
      view.projection_matrix[2][2] = -1.0F / 32;
      view.inverse_view_projection_matrix[2][2] = -32;
      auto view_buffer
        = CreateUploadBuffer(SizeBytes { 256 }, BufferUsage::kConstant);
      view_buffer->Update(&view, sizeof(view), 0);
      auto params
        = vortex::testing::RendererPublicationProbe::FogPassConstants {};
      params.output_header = { output_slot.get(), 1, 1, 1 };
      params.grid = { 1, 32, 0, 1 };
      params.grid_z = { { 1, 0, 1 }, 0 };
      params.height_fog0.primary_density = test.global_density;
      params.height_fog1.match_height_fog_factor = 1;
      params.height_fog1.enabled = 1;
      params.media0
        = { { test.scattering ? 1.0F : 0.0F, test.scattering ? 1.0F : 0.0F,
              test.scattering ? 1.0F : 0.0F },
            0 };
      params.media1 = {
        { test.global_emission, test.global_emission, test.global_emission }, 1
      };
      params.local_fog0
        = { products.instance_buffer_slot.get(), tile_slot.get(), 1, 1 };
      params.local_fog1 = { 1, 1, 1, 0 };
      params.local_fog2.max_density_into_volumetric_fog = 100;
      params.light0_direction_enabled[2] = 1;
      params.light0_direction_enabled[3] = 1;
      for (unsigned c = 0; c < 3; ++c)
        params.light0_illuminance_rgb[c] = test.illuminance;
      for (unsigned c = 0; c < 3; ++c)
        params.temporal_history1.frame_jitter_offsets[0][c] = .5F;
      params.exposure_status_uav
        = exposure->current_state->status_uav_index.get();
      const auto constants = PublishFixtureData(params);
      // The single froxel samples exactly at the authored local-volume center.
      // Independent double Beer-Lambert solution for mixed source coefficients.
      const double local_opacity
        = -std::expm1(-double(test.radial)) * -std::expm1(-double(test.height));
      const double local_density = -std::log1p(-local_opacity);
      const double total_density = test.global_density + local_density;
      const double length = std::sqrt(2.0) - 1;
      const double opacity = -std::expm1(-total_density * length);
      const double light = test.scattering
        ? double(test.illuminance) / (4 * std::acos(-1.0)) * double(2e-5F)
        : 0;
      const double source
        = double(test.global_density) * (test.global_emission + light)
        + local_density * (test.local_emission + light);
      const double radiance
        = total_density > 0 ? source / total_density * opacity : 0;
      const auto capture = ev == 0 && test.height == 0x1p-40F
        ? BeginOptionalCapture()
        : observer_ptr<FrameCaptureController> {};
      {
        auto recorder = AcquireRecorder("Injected medium production dispatch");
        if (!recorder->AdoptKnownResourceState(*output))
          recorder->BeginTrackingResourceState(
            *output, ResourceStates::kCommon, false);
        ASSERT_TRUE(recorder->AdoptKnownResourceState(
          *exposure->current_state->status_buffer));
        recorder->RequireResourceState(
          *output, ResourceStates::kUnorderedAccess);
        recorder->RequireResourceState(*exposure->current_state->status_buffer,
          ResourceStates::kUnorderedAccess);
        recorder->FlushBarriers();
        recorder->SetPipelineState(pipeline);
        recorder->SetComputeRootConstantBufferView(
          static_cast<unsigned>(root::RootParam::kViewConstants),
          view_buffer->GetGPUVirtualAddress());
        recorder->SetComputeRoot32BitConstant(
          static_cast<unsigned>(root::RootParam::kRootConstants), 0, 0);
        recorder->SetComputeRoot32BitConstant(
          static_cast<unsigned>(root::RootParam::kRootConstants),
          constants.get(), 1);
        recorder->Dispatch(1, 1, 1);
      }
      if (capture)
        EXPECT_TRUE(capture->EndCapture());
      const auto pixels = ReadFloatTexture(*output);
      ASSERT_EQ(pixels.size(), 1U);
      const double expected = radiance * std::exp2(-double(ev));
      for (unsigned c = 0; c < 3; ++c)
        EXPECT_NEAR(
          pixels[0][c], expected, std::abs(expected) * 2e-5 + 0x1p-120);
      EXPECT_NEAR(pixels[0][3], std::exp(-total_density * length), 2e-5);
      const auto status
        = Read<ExposureCompletedStatus>(*exposure->current_state->status_buffer,
          ResourceStates::kUnorderedAccess);
      if (test.unsupported) {
        ASSERT_GT(radiance, 0x1p32);
        EXPECT_EQ(status.first_failure_product, 10U);
        EXPECT_NE(status.first_failure_kind & 32U, 0U);
        EXPECT_EQ(status.flags & 18U, 18U);
      } else
        EXPECT_EQ(status.flags & 16U, 0U);
      renderer_->OnFrameEnd(observer_ptr { &context });
      Backend().EndFrame(seq, slot);
      WaitForQueueIdle();
    }
  ctx_.scene.reset();
  ctx_.current_view.resolved_view.reset();
  RecordProperty("local_injection_cases", sequence);
}

NOLINT_TEST_F(ExposureGpuTest, FogStableCellsPreserveHistoryAndHomogeneousMedia)
{
  namespace root = oxygen::bindless::generated::d3d12;
  const auto pipeline
    = ComputePipelineDesc::Builder {}
        .SetComputeShader(ShaderRequest { .stage = ShaderType::kCompute,
          .source_path = "Vortex/Services/Environment/VolumetricFog.hlsl",
          .entry_point = "VortexVolumetricFogCS" })
        .SetRootBindings(ExposureProbeRootBindings())
        .SetDebugName("Fog stable cell fixture")
        .Build();
  auto output = CreateRegisteredTexture({ .width = 4U,
    .height = 4U,
    .depth = 4U,
    .format = Format::kRGBA32Float,
    .texture_type = TextureType::kTexture3D,
    .is_shader_resource = true,
    .is_uav = true,
    .initial_state = ResourceStates::kCommon });
  auto& allocator = renderer_->GetGraphics()->GetDescriptorAllocator();
  auto handle = allocator.AllocateRaw(
    ResourceViewType::kTexture_UAV, DescriptorVisibility::kShaderVisible);
  const auto output_slot = allocator.GetShaderVisibleIndex(handle);
  Backend().GetResourceRegistry().RegisterView(*output, std::move(handle),
    TextureViewDescription { .view_type = ResourceViewType::kTexture_UAV,
      .format = Format::kRGBA32Float,
      .dimension = TextureType::kTexture3D });
  std::array<Pixel, 64> samples;
  for (unsigned z = 0; z < 4; ++z)
    for (unsigned y = 0; y < 4; ++y)
      for (unsigned x = 0; x < 4; ++x)
        samples[(z * 4 + y) * 4 + x]
          = { float(x) / 4, float(y) / 4, float(z) / 4, 1 - float(z) / 4 };
  auto history = ViewHistoryFrameBindings {};
  history.validity_flags = static_cast<std::uint32_t>(
    ViewHistoryValidityFlagBits::kPreviousViewValid);
  history.previous_projection_matrix[2][2] = -1.0F / 32;
  auto frame_bindings = ViewFrameBindings {};
  frame_bindings.history_frame_slot = PublishFixtureData(history);
  auto view = ViewConstants::GpuData {};
  view.view_frame_bindings_bslot
    = BindlessViewFrameBindingsSlot { PublishFixtureData(frame_bindings) };
  view.projection_matrix = history.previous_projection_matrix;
  view.inverse_view_projection_matrix = glm::mat4 { 1.0F };
  view.inverse_view_projection_matrix[2][2] = -32.0F;
  auto view_buffer
    = CreateUploadBuffer(SizeBytes { 256U }, BufferUsage::kConstant);
  view_buffer->Update(&view, sizeof(view), 0U);
  const std::array offsets { glm::vec4 { .5F, .5F, .5F, 0 },
    glm::vec4 { .125F, .375F, .75F, 0 }, glm::vec4 { .875F, .625F, .25F, 0 } };
  const auto capture = BeginOptionalCapture();
  for (const auto format : { Format::kRGBA32Float, Format::kRGBA16Float }) {
    const auto volume = MakeSignal(4, 4, samples, 4, format, true);
    for (const bool reuse_history : { false, true })
      for (const float density : { .06F, 0x1p-40F })
        for (const float fade_distance : { 0.0F, 2.0F })
          for (const auto offset : offsets) {
            SCOPED_TRACE(reuse_history);
            SCOPED_TRACE(fade_distance);
            SCOPED_TRACE(offset.z);
            SCOPED_TRACE(density);
            const float emission_scale = density < .001F ? 1e9F : 1.0F;
            auto params
              = vortex::testing::RendererPublicationProbe::FogPassConstants {};
            params.output_header = { output_slot.get(), 4U, 4U, 4U };
            params.grid = { 1.0F, 32.0F, fade_distance, 1.0F };
            params.grid_z = { { 1.0F, 0.0F, 1.0F }, 0.0F };
            params.height_fog0.primary_density = density;
            params.height_fog1.match_height_fog_factor = 1.0F;
            params.height_fog1.enabled = 1U;
            params.media1 = { { 2 * emission_scale, 3 * emission_scale,
                                4 * emission_scale },
              1.0F };
            params.temporal_history0 = { volume.srv.get(),
              reuse_history ? (format == Format::kRGBA16Float ? 3U : 1U) : 0U,
              1.0F, 1U };
            for (unsigned c = 0; c < 4; ++c)
              params.temporal_history1.frame_jitter_offsets[0][c] = offset[c];
            const auto pass_slot = PublishFixtureData(params);
            {
              auto recorder = AcquireRecorder("Fog stable cell dispatch");
              if (!recorder->AdoptKnownResourceState(*output))
                recorder->BeginTrackingResourceState(
                  *output, ResourceStates::kCommon, false);
              recorder->RequireResourceState(
                *output, ResourceStates::kUnorderedAccess);
              recorder->FlushBarriers();
              recorder->SetPipelineState(pipeline);
              recorder->SetComputeRootConstantBufferView(
                static_cast<std::uint32_t>(root::RootParam::kViewConstants),
                view_buffer->GetGPUVirtualAddress());
              recorder->SetComputeRoot32BitConstant(
                static_cast<std::uint32_t>(root::RootParam::kRootConstants), 0U,
                0U);
              recorder->SetComputeRoot32BitConstant(
                static_cast<std::uint32_t>(root::RootParam::kRootConstants),
                pass_slot.get(), 1U);
              recorder->Dispatch(1U, 1U, 1U);
            }
            const auto actual = ReadFloatTexture(*output);
            ASSERT_EQ(actual.size(), samples.size());
            for (unsigned i = 0; i < samples.size(); ++i) {
              // Independent fixed-depth Beer-Lambert oracle, including near
              // fade.
              const double length = std::exp2(double(i / 16) + .5) - 1;
              const double fade = fade_distance > 0
                ? std::min(length / double(fade_distance), 1.0)
                : 1.0;
              const double t = std::exp(-double(density) * fade * length);
              const double opacity
                = -std::expm1(-double(density) * fade * length);
              for (unsigned c = 0; c < 4; ++c) {
                const double expected = reuse_history ? double(samples[i][c])
                  : c == 3                            ? t
                           : double(params.media1.emissive_rgb[c]) * opacity;
                const double tolerance = reuse_history
                  ? 3e-6
                  : std::min(3e-6, std::abs(expected) * 2e-5 + 0x1p-120);
                EXPECT_NEAR(actual[i][c], expected, tolerance)
                  << "voxel=" << i << " channel=" << c;
              }
            }
          }
  }
  if (capture)
    EXPECT_TRUE(capture->EndCapture());
  WaitForQueueIdle();
}

NOLINT_TEST_F(ExposureGpuTest, DeferredApPreservesInscatterAtLowAndZeroOpacity)
{
  pass_.reset();
  renderer_->OnShutdown();
  auto config = RendererConfig {};
  config.upload_queue_key = QueueKeyFor().get();
  renderer_ = std::make_unique<Renderer>(GetGraphicsShared(), config,
    kPhase1DefaultRuntimeCapabilityFamilies
      | RendererCapabilityFamily::kEnvironmentLighting);
  auto scene = scene::Scene("ApBlend", 1U);
  scene.SetEnvironment(std::make_unique<scene::SceneEnvironment>());
  scene.GetEnvironment()
    ->AddSystem<scene::environment::SkyAtmosphere>()
    .SetEnabled(true);
  ctx_.scene = observer_ptr { &scene };
  ctx_.current_view.with_atmosphere = true;
  auto textures = SceneTextures(Backend(),
    { .extent = { 4U, 4U },
      .enable_velocity = false,
      .scene_color_format = Format::kRGBA32Float });
  auto framebuffer
    = Backend().CreateFramebuffer(FramebufferDesc {}.SetDepthAttachment(
      { .texture = textures.GetSceneDepthResource() }));
  auto& allocator = renderer_->GetGraphics()->GetDescriptorAllocator();
  auto& registry = Backend().GetResourceRegistry();
  const auto texture_srv
    = [&](const Texture& texture, Format format, TextureType dimension) {
        auto handle = allocator.AllocateRaw(
          ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
        const auto index = allocator.GetShaderVisibleIndex(handle);
        registry.RegisterView(texture, std::move(handle),
          TextureViewDescription { .format = format, .dimension = dimension });
        return index;
      };
  const auto publish = [&]<typename T>(const T& value) {
    auto buffer = CreateRegisteredBuffer({ .size_bytes = sizeof(T),
      .usage = BufferUsage::kNone,
      .memory = BufferMemory::kUpload,
      .debug_name = "AP blend fixture bindings" });
    buffer->Update(&value, sizeof(T), 0U);
    auto handle = allocator.AllocateBindless(
      oxygen::bindless::generated::kGlobalSrvDomain,
      ResourceViewType::kStructuredBuffer_SRV);
    const auto index = allocator.GetShaderVisibleIndex(handle);
    registry.RegisterView(*buffer, std::move(handle),
      BufferViewDescription {
        .view_type = ResourceViewType::kStructuredBuffer_SRV,
        .range = { 0U, sizeof(T) },
        .stride = sizeof(T) });
    return index;
  };
  auto scene_bindings = SceneTextureBindings {};
  scene_bindings.scene_depth_srv = texture_srv(textures.GetSceneDepth(),
    textures.GetSceneDepth().GetDescriptor().format, TextureType::kTexture2D)
                                     .get();
  const auto scene_slot = publish(scene_bindings);
  auto environment_view = EnvironmentViewData {};
  environment_view.sky_aerial_luminance_aerial_start_depth_km.w = 0.0F;
  environment_view.camera_aerial_volume_depth_params = { 1, 1, 1, 10000 };
  const auto environment_view_slot = publish(environment_view);
  auto compose = environment::AtmosphereComposePass(*renderer_);
  const auto capture = BeginOptionalCapture();
  // 1e-5 is not a representable value of 1-T near T=1 in binary32.
  // Exercise the adjacent representable opacities on each side, plus zero,
  // the reported FP32-loss case, an ordinary opacity and full attenuation.
  const std::array transmittances { 1.0F, 1.0F - 0x1p-17F,
    1.0F - 167.0F * 0x1p-24F, 1.0F - 168.0F * 0x1p-24F, .5F, 0.0F };
  for (const auto format : { Format::kRGBA32Float, Format::kRGBA16Float }) {
    SCOPED_TRACE(static_cast<unsigned>(format));
    for (const auto transmittance : transmittances) {
      for (const float background : { 0.0F, .5F }) {
        for (const float coverage : { 0.0F, .25F, 1.0F }) {
          SCOPED_TRACE(transmittance);
          SCOPED_TRACE(background);
          SCOPED_TRACE(coverage);
          auto volume = CreateRegisteredTexture({ .width = 1U,
            .height = 1U,
            .depth = 1U,
            .format = format,
            .texture_type = TextureType::kTexture3D,
            .is_shader_resource = true,
            .initial_state = ResourceStates::kCommon });
          Pixel sample { .01F, .02F, .04F, transmittance };
          std::array<std::byte, 1536U> bytes {};
          if (format == Format::kRGBA16Float) {
            // Independently specified binary16 payload and exact decoded
            // values. All four near-one T cases round to 1; .5 and 0 are exact.
            const std::array<std::uint16_t, 4> bits { 0x211fU, 0x251fU, 0x291fU,
              static_cast<std::uint16_t>(transmittance > .5F ? 0x3c00U
                  : transmittance == .5F                     ? 0x3800U
                                                             : 0U) };
            std::memcpy(bytes.data(), bits.data(), sizeof(bits));
            sample = { 1311.0F / 131072.0F, 1311.0F / 65536.0F,
              1311.0F / 32768.0F, transmittance > .5F ? 1.0F : transmittance };
          } else {
            std::memcpy(bytes.data(), sample.data(), sizeof(sample));
          }
          const Pixel destination { background, background, background,
            coverage };
          for (unsigned y = 0; y < 4U; ++y)
            for (unsigned x = 0; x < 4U; ++x)
              std::memcpy(bytes.data() + 512U + y * 256U + x * sizeof(Pixel),
                destination.data(), sizeof(destination));
          auto upload = CreateUploadBuffer(SizeBytes { bytes.size() });
          upload->Update(bytes.data(), bytes.size(), 0U);
          {
            auto recorder = AcquireRecorder("AP blend fixture initialization");
            EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
            EnsureTracked(*recorder, volume, ResourceStates::kCommon);
            recorder->RequireResourceState(*volume, ResourceStates::kCopyDest);
            for (const auto& texture : { textures.GetSceneColorResource(),
                   textures.GetSceneDepthResource() }) {
              if (!recorder->AdoptKnownResourceState(*texture))
                recorder->BeginTrackingResourceState(
                  *texture, texture->GetDescriptor().initial_state);
            }
            recorder->RequireResourceState(
              textures.GetSceneColor(), ResourceStates::kCopyDest);
            recorder->RequireResourceState(
              textures.GetSceneDepth(), ResourceStates::kDepthWrite);
            recorder->FlushBarriers();
            recorder->CopyBufferToTexture(*upload,
              { .buffer_row_pitch = 256U,
                .buffer_slice_pitch = 256U,
                .dst_slice = { .width = 1U, .height = 1U, .depth = 1U } },
              *volume);
            recorder->CopyBufferToTexture(*upload,
              { .buffer_offset = 512U,
                .buffer_row_pitch = 256U,
                .buffer_slice_pitch = 1024U,
                .dst_slice = { .width = 4U, .height = 4U, .depth = 1U } },
              textures.GetSceneColor());
            recorder->ClearFramebuffer(*framebuffer, std::nullopt, 0.0F);
            recorder->RequireResourceStateFinal(
              *volume, ResourceStates::kShaderResource);
          }
          auto environment_static = EnvironmentStaticData {};
          environment_static.atmosphere.camera_volume_lut_slot
            = texture_srv(*volume, format, TextureType::kTexture3D).get();
          auto environment_bindings = EnvironmentFrameBindings {};
          environment_bindings.environment_static_slot
            = publish(environment_static);
          environment_bindings.environment_view_slot = environment_view_slot;
          auto view_bindings = ViewFrameBindings {};
          view_bindings.environment_frame_slot = publish(environment_bindings);
          view_bindings.scene_texture_frame_slot = scene_slot;
          auto view = ViewConstants::GpuData {};
          view.view_frame_bindings_bslot
            = BindlessViewFrameBindingsSlot { publish(view_bindings) };
          view.reverse_z = 0U;
          auto constants
            = CreateUploadBuffer(SizeBytes { 256U }, BufferUsage::kConstant);
          constants->Update(&view, sizeof(view), 0U);
          ctx_.view_constants = constants;
          ASSERT_TRUE(compose.Record(ctx_, textures).executed);
          auto readback
            = GetReadbackManager()->CreateTextureReadback("AP composed pixel");
          {
            auto recorder = AcquireRecorder("AP composed pixel readback");
            ASSERT_TRUE(
              recorder->AdoptKnownResourceState(textures.GetSceneColor()));
            ASSERT_TRUE(readback
                ->EnqueueCopy(*recorder, textures.GetSceneColor(),
                  { .src_slice = { .x = 1U,
                      .y = 1U,
                      .width = 1U,
                      .height = 1U,
                      .depth = 1U } })
                .has_value());
          }
          const auto mapped = readback->MapNow();
          ASSERT_TRUE(mapped.has_value());
          Pixel result {};
          std::memcpy(result.data(), mapped->Data(), sizeof(result));
          // Independent double-precision transfer also describes forward AP.
          // 3e-7 bounds FP32 sample/arithmetic/blend rounding for these unit
          // inputs.
          for (unsigned c = 0U; c < 3U; ++c)
            EXPECT_NEAR(result[c],
              double(sample[c]) + double(background) * sample[3], 3e-7);
          EXPECT_NEAR(result[3],
            1.0 - double(sample[3]) + double(coverage) * sample[3], 3e-7);
        }
      }
    }
  }
  if (capture)
    EXPECT_TRUE(capture->EndCapture());
  ctx_.view_constants.reset();
  ctx_.scene.reset();
  WaitForQueueIdle();
}

NOLINT_TEST_F(ExposureGpuTest, SuitabilitySelectsGpuCandidateWithTwoStopMargin)
{
  const auto capture = BeginOptionalCapture();
  const auto result = Qualify(Uniform(1.0F, 4U, 4U), true);
  EXPECT_EQ(result.candidate_pre_exposure, 8192.0F);
  EXPECT_GE(result.maximum_scene_rgb, 1.0F);
  // Unit P and zero retained error need only the four-ULP outward product
  // guard.
  EXPECT_LE(std::bit_cast<std::uint32_t>(result.maximum_scene_rgb),
    std::bit_cast<std::uint32_t>(1.0F) + 4U);
  EXPECT_EQ(result.failure_flags, 0U);
  EXPECT_EQ(result.checked_samples, 16U);
  EXPECT_EQ(result.checked_products, 1U);
  EXPECT_EQ(result.expected_products, 1U);
  if (capture)
    EXPECT_TRUE(capture->EndCapture());
}

NOLINT_TEST_F(ExposureGpuTest, SuitabilityRejectsRequiredFortySixStopSignal)
{
  const std::array pixels { Pixel { 0x1p30F, 0x1p30F, 0x1p30F, 1.0F },
    Pixel { 0x1p-16F, 0x1p-16F, 0x1p-16F, 1.0F } };
  auto settings = scene::ExposureSettings {};
  settings.min_log_luminance = -24.0F;
  settings.log_luminance_range = 56.0F;
  const auto result = Qualify(MakeSignal(2U, 1U, pixels), true, settings);
  EXPECT_EQ(result.candidate_pre_exposure, 0x1p-17F);
  EXPECT_NE(result.failure_flags & 8U, 0U);
  EXPECT_EQ(result.metering_failures, 1U);
  EXPECT_EQ(result.first_failure_product, 1U);
}

NOLINT_TEST_F(ExposureGpuTest, SuitabilityAccountsForConsumerRgbAmplification)
{
  // Independent FP32 reference: an 8192 sample anchors candidate P at one;
  // AP contributes 1e-8 * 1e6 = .01 at the other scene pixel. Both local
  // narrowing checks pass without consumer gain, but half storage loses AP.
  const std::array scene_pixels { Pixel { 8192, 8192, 8192, 1 },
    Pixel { .01F, .01F, .01F, 1 } };
  const std::array<Pixel, 1> ap_pixel { Pixel { 1e-8F, 1e-8F, 1e-8F, 1 } };
  const auto scene_signal = MakeSignal(2U, 1U, scene_pixels);
  const auto ap_signal = MakeSignal(1U, 1U, ap_pixel);
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.key = 12.5F;
  settings.manual_ev = 0.0F;
  const auto config = SharedConfig(settings);
  const std::array gains { 0.0F, .5F, 1.0F, 1e6F, -1.0F,
    std::numeric_limits<float>::infinity(),
    std::numeric_limits<float>::quiet_NaN() };
  const auto capture = BeginOptionalCapture();
  for (const auto gain : gains) {
    SCOPED_TRACE(gain);
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    const auto frame = pass_->ResolveFrame(ctx_, config, { .use_fp32 = true });
    ASSERT_NE(frame, nullptr);
    const auto solved = RecordShared(scene_signal, config);
    ASSERT_TRUE(solved.executed);
    const std::array products { postprocess::ExposurePass::HdrProduct {
                                  .texture = scene_signal.texture.get(),
                                  .srv = scene_signal.srv,
                                  .id = 11U,
                                  .error_budget_share = .25F },
      postprocess::ExposurePass::HdrProduct {
        .texture = ap_signal.texture.get(),
        .srv = ap_signal.srv,
        .id = 6U,
        .transmittance = true,
        .error_budget_share = .25F,
        .consumer_rgb_gain = gain } };
    ASSERT_TRUE(pass_->EvaluateFp16Products(ctx_, frame, config, products, {}));
    const auto report = Read<HdrSuitabilityData>(
      *frame->suitability_buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(report.candidate_pre_exposure, 1.0F);
    EXPECT_EQ(report.checked_samples, 3U);
    if (!std::isfinite(gain) || gain < 0.0F) {
      EXPECT_EQ(report.failure_flags, 1U);
      EXPECT_EQ(report.rejected_samples, 1U);
      EXPECT_EQ(report.first_failure_product, 6U);
    } else if (gain == 1e6F) {
      EXPECT_EQ(report.failure_flags, 4U);
      EXPECT_EQ(report.image_failures, 1U);
      EXPECT_EQ(report.first_failure_product, 6U);
    } else {
      EXPECT_EQ(report.failure_flags, 0U);
    }
    EXPECT_EQ(ReadState(solved).displayed_scale, 1.0F);
  }
  if (capture)
    EXPECT_TRUE(capture->EndCapture());
}

NOLINT_TEST_F(ExposureGpuTest,
  SuitabilityIgnoresBelowBudgetComponentsButRespectsDisplayedGain)
{
  const std::array<Pixel, 1> pixel { Pixel { 1.0F, .5F, 0x1p-40F, 1.0F } };
  EXPECT_EQ(Qualify(MakeSignal(1U, 1U, pixel), true).failure_flags, 0U);
  const std::array wide { Pixel { 0x1p30F, 0x1p30F, 0x1p30F, 1.0F },
    Pixel { 0x1p-24F, 0x1p-24F, 0x1p-24F, 1.0F } };
  const auto signal = MakeSignal(2U, 1U, wide);
  EXPECT_EQ(Qualify(signal, false).failure_flags, 0U);
  auto bright = scene::ExposureSettings {};
  bright.manual_ev = -32.0F;
  EXPECT_NE(Qualify(signal, false, bright).failure_flags & 4U, 0U);
}

NOLINT_TEST_F(ExposureGpuTest, SuitabilityUsesMeterMaskAndCoverageWeights)
{
  const std::array wide { Pixel { 0x1p30F, 0x1p30F, 0x1p30F, 1.0F },
    Pixel { 0x1p-24F, 0x1p-24F, 0x1p-24F, 1.0F } };
  const std::array mask_pixels { Pixel { 1.0F, 0, 0, 1 },
    Pixel { 0, 0, 0, 1 } };
  const auto mask = MakeSignal(2U, 1U, mask_pixels);
  const auto signal = MakeSignal(2U, 1U, wide);
  auto required_dark = scene::ExposureSettings {};
  required_dark.black_influence = 1.0F;
  EXPECT_NE(Qualify(signal, true, required_dark).failure_flags & 8U, 0U);
  EXPECT_EQ(Qualify(signal, true, required_dark, &mask).failure_flags, 0U);
  const std::array<Pixel, 1> covered { Pixel { .25F, .25F, .25F, .5F } };
  EXPECT_EQ(
    Qualify(MakeSignal(1U, 1U, covered), true, {}, nullptr, true).failure_flags,
    0U);
}

NOLINT_TEST_F(
  ExposureGpuTest, PreEnvironmentRangePreservesStatusAndViewIsolation)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.key = 12.5F;
  settings.manual_ev = 3.0F;
  const auto config = SharedConfig(settings);
  std::array<Pixel, 27> pixels {};
  pixels.fill(Pixel { 1, 2, 3, 1 });
  pixels[8] = Pixel { -65536, 4, 5, 1 };
  pixels[26] = Pixel { 6, 7, 32768, 1 };
  const auto signal = MakeSignal(9U, 3U, pixels);
  const auto smaller = Uniform(.25F, 2U, 5U);
  const auto capture = BeginOptionalCapture();
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  const auto frame = pass_->ResolveFrame(ctx_, config, { .use_fp32 = false });
  ASSERT_NE(frame, nullptr);
  EXPECT_EQ(
    Read<FrameExposureData>(*frame->buffer, ResourceStates::kShaderResource)
      .pre_exposure,
    .125F);
  ASSERT_TRUE(RecordShared(signal, config).executed);
  // Distinct sentinels verify that this reduction does not clear producer
  // bounds.
  const std::array<HdrErrorBoundsData, 3> bounds { HdrErrorBoundsData {
                                                     .rgb_absolute = .125F },
    HdrErrorBoundsData { .rgb_absolute = .25F },
    HdrErrorBoundsData { .rgb_absolute = .5F } };
  auto upload = CreateUploadBuffer(SizeBytes { sizeof(bounds) });
  upload->Update(bounds.data(), sizeof(bounds), 0U);
  {
    auto recorder = AcquireRecorder("Pre-environment bound sentinels");
    EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
    ASSERT_TRUE(
      recorder->AdoptKnownResourceState(*frame->current_state->status_buffer));
    recorder->RequireResourceState(
      *frame->current_state->status_buffer, ResourceStates::kCopyDest);
    recorder->FlushBarriers();
    recorder->CopyBuffer(
      *frame->current_state->status_buffer, 80U, *upload, 0U, sizeof(bounds));
    recorder->RequireResourceStateFinal(
      *frame->current_state->status_buffer, ResourceStates::kUnorderedAccess);
  }
  const auto before = Read<ExposureStatusStorage>(
    *frame->current_state->status_buffer, ResourceStates::kUnorderedAccess);
  ASSERT_TRUE(pass_->CapturePreEnvironmentRange(
    ctx_, frame, *signal.texture, signal.srv));
  EXPECT_TRUE(pass_->HasPreEnvironmentRange(frame));
  const auto first = Read<ExposureStatusStorage>(
    *frame->current_state->status_buffer, ResourceStates::kUnorderedAccess);
  auto expected_status = before;
  expected_status.completed.flags |= 18U;
  expected_status.completed.first_failure_product = 11U;
  expected_status.completed.first_failure_kind |= 32U;
  // Only the required producer verdict may change; identities and the
  // independently uploaded producer-error sentinels remain byte-identical.
  EXPECT_EQ(std::memcmp(&expected_status, &first, 128U), 0);
  EXPECT_EQ(first.composition_input.maximum_pre_exposed_rgb, 65536.0F);
  EXPECT_EQ(first.composition_input.flags, 5U);
  EXPECT_EQ(first.composition_input.checked_pixels, 27U);
  EXPECT_EQ(first.composition_input.reserved, 0U);

  // A second view cannot overwrite the first view's retained input range.
  ctx_.current_view.view_id = ViewId { 2U };
  ctx_.current_view.view_state_handle = CompositionView::ViewStateHandle { 2U };
  const auto other = pass_->ResolveFrame(ctx_, config, { .use_fp32 = true });
  ASSERT_NE(other, nullptr);
  ASSERT_TRUE(pass_->CapturePreEnvironmentRange(
    ctx_, other, *smaller.texture, smaller.srv));
  const auto second = Read<ExposureStatusStorage>(
    *other->current_state->status_buffer, ResourceStates::kUnorderedAccess);
  EXPECT_EQ(second.composition_input.maximum_pre_exposed_rgb, .25F);
  EXPECT_EQ(second.composition_input.checked_pixels, 10U);
  EXPECT_EQ(second.composition_input.flags, 1U);
  EXPECT_EQ(Read<ExposureStatusStorage>(*frame->current_state->status_buffer,
              ResourceStates::kUnorderedAccess)
              .composition_input.maximum_pre_exposed_rgb,
    65536.0F);

  ctx_.current_view.view_id = ViewId { 1U };
  ctx_.current_view.view_state_handle = CompositionView::ViewStateHandle { 1U };
  auto& backend = static_cast<ExposureFailureGraphics&>(Backend());
  backend.fail_recorder_name = "Vortex Exposure PreEnvironment Range";
  EXPECT_FALSE(pass_->CapturePreEnvironmentRange(
    ctx_, frame, *smaller.texture, smaller.srv));
  EXPECT_FALSE(pass_->HasPreEnvironmentRange(frame));
  backend.fail_recorder_name.clear();
  ASSERT_TRUE(pass_->CapturePreEnvironmentRange(
    ctx_, frame, *smaller.texture, smaller.srv));
  const auto retry = Read<ExposureStatusStorage>(
    *frame->current_state->status_buffer, ResourceStates::kUnorderedAccess);
  EXPECT_EQ(retry.composition_input.maximum_pre_exposed_rgb, .25F);
  EXPECT_EQ(retry.composition_input.checked_pixels, 10U);
  EXPECT_EQ(retry.composition_input.flags, 1U);
  EXPECT_EQ(std::memcmp(&expected_status, &retry, 128U), 0);
  const std::array products { postprocess::ExposurePass::HdrProduct {
    .texture = smaller.texture.get(), .srv = smaller.srv, .id = 11U } };
  ASSERT_TRUE(pass_->EvaluateFp16Products(ctx_, frame, config, products, {}));
  ASSERT_TRUE(pass_->FinalizeFp16Suitability(ctx_, frame,
    { .product_layout_revision = 1U, .expected_products = 1U << 10U }));
  const auto finalized = Read<ExposureStatusStorage>(
    *frame->current_state->status_buffer, ResourceStates::kUnorderedAccess);
  EXPECT_EQ(std::memcmp(&retry.composition_input, &finalized.composition_input,
              sizeof(HdrCompositionInputData)),
    0);
  EXPECT_FALSE(pass_->CapturePreEnvironmentRange(
    ctx_, frame, *smaller.texture, kInvalidShaderVisibleIndex));
  EXPECT_FALSE(pass_->HasPreEnvironmentRange(frame));
  if (capture)
    EXPECT_TRUE(capture->EndCapture());
}

NOLINT_TEST_F(ExposureGpuTest, PreEnvironmentRangePreservesSignedTinyInputs)
{
  const auto config = SharedConfig(scene::ExposureSettings {});
  for (const auto bits : { 1U, 0x80000001U, 0x80000000U }) {
    SCOPED_TRACE(bits);
    const auto signal = Uniform(std::bit_cast<float>(bits));
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    const auto frame = pass_->ResolveFrame(ctx_, config, { .use_fp32 = true });
    ASSERT_NE(frame, nullptr);
    ASSERT_TRUE(pass_->CapturePreEnvironmentRange(
      ctx_, frame, *signal.texture, signal.srv));
    const auto status = Read<ExposureStatusStorage>(
      *frame->current_state->status_buffer, ResourceStates::kUnorderedAccess);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(
                status.composition_input.maximum_pre_exposed_rgb),
      bits & 0x7fffffffU);
    EXPECT_EQ(status.composition_input.flags, bits == 0x80000001U ? 5U : 1U);
    EXPECT_EQ(status.composition_input.checked_pixels, 1U);
  }
}

NOLINT_TEST_F(ExposureGpuTest, PreEnvironmentRangeReportsNonfiniteInput)
{
  const auto config = SharedConfig(scene::ExposureSettings {});
  for (unsigned channel = 0U; channel < 4U; ++channel) {
    SCOPED_TRACE(channel);
    std::array<Pixel, 9> pixels {};
    pixels.fill(Pixel { 2, 3, 4, 1 });
    pixels[8][channel] = channel % 2U == 0U
      ? std::numeric_limits<float>::infinity()
      : std::numeric_limits<float>::quiet_NaN();
    const auto signal = MakeSignal(9U, 1U, pixels);
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    const auto frame = pass_->ResolveFrame(ctx_, config, { .use_fp32 = true });
    ASSERT_NE(frame, nullptr);
    ASSERT_TRUE(pass_->CapturePreEnvironmentRange(
      ctx_, frame, *signal.texture, signal.srv));
    const auto status = Read<ExposureStatusStorage>(
      *frame->current_state->status_buffer, ResourceStates::kUnorderedAccess);
    EXPECT_EQ(status.composition_input.maximum_pre_exposed_rgb, 4.0F);
    EXPECT_EQ(status.composition_input.flags, 3U);
    EXPECT_EQ(status.composition_input.checked_pixels, 9U);
    EXPECT_EQ(status.completed.flags, 18U);
    EXPECT_EQ(status.completed.first_failure_product, 11U);
    EXPECT_EQ(status.completed.first_failure_kind, 1U);
    // Later attenuation may hide an invalid source. Finite final pixels must
    // not authorize adaptation after the earlier producer failure.
    const auto finite = Uniform(.25F);
    const auto solved = RecordShared(finite, config);
    ASSERT_TRUE(solved.executed);
    EXPECT_EQ(ReadState(solved).flags & 12U, 0U);
    EXPECT_NE(ReadState(solved).flags & 32U, 0U);
    const auto after = Read<ExposureStatusStorage>(
      *frame->current_state->status_buffer, ResourceStates::kUnorderedAccess);
    EXPECT_EQ(std::memcmp(&status.composition_input, &after.composition_input,
                sizeof(HdrCompositionInputData)),
      0);
  }
}

NOLINT_TEST_F(ExposureGpuTest, ProducerMaximumReuseMatchesCompleteScan)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 0.0F;
  const auto config = SharedConfig(settings);
  const auto meter = Uniform(.25F);
  for (const unsigned id : { 6U, 10U }) {
    for (const auto format : { Format::kRGBA32Float, Format::kRGBA16Float }) {
      for (const bool retained : { false, true }) {
        SCOPED_TRACE(::testing::Message()
          << "product=" << id << " format=" << static_cast<unsigned>(format)
          << " retained=" << retained);
        const std::array<Pixel, 1> values { Pixel { .25F, .5F, 2.0F, .5F } };
        const auto signal = MakeSignal(9U, 3U, values, 2U, format);
        const auto bounds = retained
          ? HdrErrorBoundsData { .rgb_relative = 1.0F / 128.0F,
              .rgb_absolute = 1.0F / 64.0F }
          : HdrErrorBoundsData {};
        const auto run = [&](const bool gradients, HdrSuitabilityData& report) {
          ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
          const auto frame
            = pass_->ResolveFrame(ctx_, config, { .use_fp32 = true });
          ASSERT_NE(frame, nullptr);
          ASSERT_TRUE(RecordShared(meter, config).executed);
          auto upload = CreateUploadBuffer(SizeBytes { sizeof(bounds) });
          upload->Update(&bounds, sizeof(bounds), 0U);
          {
            auto recorder = AcquireRecorder("Producer maximum bounds");
            EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
            ASSERT_TRUE(recorder->AdoptKnownResourceState(
              *frame->current_state->status_buffer));
            recorder->RequireResourceState(
              *frame->current_state->status_buffer, ResourceStates::kCopyDest);
            recorder->FlushBarriers();
            recorder->CopyBuffer(*frame->current_state->status_buffer,
              id == 6U ? 96U : 112U, *upload, 0U, sizeof(bounds));
            recorder->RequireResourceStateFinal(
              *frame->current_state->status_buffer,
              ResourceStates::kShaderResource);
          }
          const auto product = postprocess::ExposurePass::HdrProduct { .texture
            = signal.texture.get(),
            .srv = signal.srv,
            .id = id,
            .transmittance = true };
          if (gradients) {
            ASSERT_TRUE(pass_->GatherFilterGradients(ctx_, frame, product));
            const auto status = Read<ExposureStatusStorage>(
              *frame->current_state->status_buffer,
              ResourceStates::kShaderResource);
            EXPECT_EQ(status.filter_gradients[id == 6U ? 1U : 2U].flags, 1U);
          }
          ASSERT_TRUE(pass_->EvaluateFp16Products(
            ctx_, frame, config, std::span { &product, 1U }, {}));
          report = Read<HdrSuitabilityData>(
            *frame->suitability_buffer, ResourceStates::kShaderResource);
        };
        HdrSuitabilityData ordinary, reused;
        ASSERT_NO_FATAL_FAILURE(run(false, ordinary));
        ASSERT_NO_FATAL_FAILURE(run(true, reused));
        EXPECT_EQ(std::memcmp(&ordinary, &reused, sizeof(ordinary)), 0);
        EXPECT_EQ(reused.checked_products, 1U << (id - 1U));
        EXPECT_EQ(reused.checked_samples, 54U);
        EXPECT_EQ(reused.candidate_pre_exposure, 4096.0F);
        if (retained) {
          const auto exact_maximum = (2.0 + 1.0 / 64.0) / (1.0 - 1.0 / 128.0);
          EXPECT_GE(double(reused.maximum_scene_rgb), exact_maximum);
          EXPECT_LE(double(reused.maximum_scene_rgb), exact_maximum * 1.00001);
        } else {
          // One outward product rounds the exact peak 2 by four FP32 ULPs.
          EXPECT_EQ(std::bit_cast<std::uint32_t>(reused.maximum_scene_rgb),
            0x40000004U);
          EXPECT_EQ(reused.failure_flags, 0U);
        }
      }
    }
  }
}

NOLINT_TEST_F(ExposureGpuTest, ProducerMaximumReuseFallsBackForUnprovenRecords)
{
  enum class Fault {
    kMissing,
    kIncomplete,
    kInvalid,
    kInfiniteMaximum,
    kDifferentTexture,
    kSubmissionFailed,
    kTransmissionMismatch,
    kNonfiniteAlpha,
    kSignedRgb,
    kSkyAlpha
  };
  const auto faults = std::array { Fault::kMissing, Fault::kIncomplete,
    Fault::kInvalid, Fault::kInfiniteMaximum, Fault::kDifferentTexture,
    Fault::kSubmissionFailed, Fault::kTransmissionMismatch,
    Fault::kNonfiniteAlpha, Fault::kSignedRgb, Fault::kSkyAlpha };
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 0.0F;
  const auto config = SharedConfig(settings);
  const auto meter = Uniform(.25F);
  for (const auto fault : faults) {
    SCOPED_TRACE(static_cast<unsigned>(fault));
    const auto id = fault == Fault::kSkyAlpha ? 5U : 6U;
    const auto index = id == 5U ? 0U : 1U;
    const auto nonfinite_alpha = fault == Fault::kNonfiniteAlpha
      || fault == Fault::kSkyAlpha || fault == Fault::kTransmissionMismatch;
    const auto value = Pixel { fault == Fault::kSignedRgb ? -2.0F : 2.0F, .25F,
      .5F, nonfinite_alpha ? std::numeric_limits<float>::quiet_NaN() : .5F };
    const std::array<Pixel, 1> values { value };
    const auto signal = MakeSignal(9U, 1U, values);
    const auto different = Uniform(.25F, 9U, 1U);
    const auto run = [&](const bool gradients, HdrSuitabilityData& report) {
      ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
      const auto frame
        = pass_->ResolveFrame(ctx_, config, { .use_fp32 = true });
      ASSERT_NE(frame, nullptr);
      ASSERT_TRUE(RecordShared(meter, config).executed);
      const auto product = postprocess::ExposurePass::HdrProduct { .texture
        = signal.texture.get(),
        .srv = signal.srv,
        .id = id,
        .transmittance = id == 6U };
      if (gradients && fault != Fault::kMissing) {
        auto recorded = product;
        if (fault == Fault::kDifferentTexture) {
          recorded.texture = different.texture.get();
          recorded.srv = different.srv;
        }
        if (fault == Fault::kTransmissionMismatch) {
          recorded.transmittance = false;
        }
        ASSERT_TRUE(pass_->GatherFilterGradients(ctx_, frame, recorded));
        if (fault == Fault::kSubmissionFailed) {
          auto& backend = static_cast<ExposureFailureGraphics&>(Backend());
          backend.fail_recorder_name = "Vortex Exposure Filter Gradients";
          EXPECT_FALSE(pass_->GatherFilterGradients(ctx_, frame, product));
          backend.fail_recorder_name.clear();
          EXPECT_FALSE(pass_->HasFilterGradients(frame, id));
        }
        if (fault == Fault::kIncomplete || fault == Fault::kInvalid
          || fault == Fault::kInfiniteMaximum) {
          auto status
            = Read<ExposureStatusStorage>(*frame->current_state->status_buffer,
              ResourceStates::kShaderResource);
          // An unusable record must not supply even a finite cached maximum.
          status.product_reference_rgb_max[index] = 128.0F;
          if (fault == Fault::kIncomplete) {
            status.filter_gradients[index].checked_texels = 1U;
          } else if (fault == Fault::kInvalid) {
            status.filter_gradients[index].flags = 3U;
          } else {
            status.product_reference_rgb_max[index]
              = std::numeric_limits<float>::infinity();
          }
          auto upload = CreateUploadBuffer(SizeBytes { sizeof(status) });
          upload->Update(&status, sizeof(status), 0U);
          {
            auto recorder
              = AcquireRecorder("Producer maximum unavailable certificate");
            EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
            ASSERT_TRUE(recorder->AdoptKnownResourceState(
              *frame->current_state->status_buffer));
            recorder->RequireResourceState(
              *frame->current_state->status_buffer, ResourceStates::kCopyDest);
            recorder->FlushBarriers();
            recorder->CopyBuffer(*frame->current_state->status_buffer, 0U,
              *upload, 0U, sizeof(status));
            recorder->RequireResourceStateFinal(
              *frame->current_state->status_buffer,
              ResourceStates::kShaderResource);
          }
        }
      }
      ASSERT_TRUE(pass_->EvaluateFp16Products(
        ctx_, frame, config, std::span { &product, 1U }, {}));
      report = Read<HdrSuitabilityData>(
        *frame->suitability_buffer, ResourceStates::kShaderResource);
    };
    HdrSuitabilityData ordinary, guarded;
    ASSERT_NO_FATAL_FAILURE(run(false, ordinary));
    ASSERT_NO_FATAL_FAILURE(run(true, guarded));
    EXPECT_EQ(std::memcmp(&ordinary, &guarded, sizeof(ordinary)), 0);
    if (nonfinite_alpha) {
      EXPECT_NE(guarded.failure_flags & 1U, 0U);
      EXPECT_EQ(guarded.rejected_samples, 9U);
    }
  }
}

NOLINT_TEST_F(ExposureGpuTest, ProducerChecksPreserveOrderedFailuresAndCounts)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 0.0F;
  settings.key = 12.5F;
  const auto config = SharedConfig(settings);
  const auto meter = Uniform(.25F);
  const std::array<Pixel, 1> finite { Pixel { .25F, .5F, 2.0F, .5F } };
  const std::array<Pixel, 1> nonfinite { Pixel {
    .25F, .5F, 2.0F, std::numeric_limits<float>::quiet_NaN() } };
  const auto scene = MakeSignal(9U, 1U, finite);
  constexpr auto expected_products = (1U << 5U) | (1U << 9U) | (1U << 10U);
  const auto failure_bit
    = [](const unsigned id) { return id == 11U ? 1U : (id == 6U ? 2U : 4U); };
  for (const auto format : { Format::kRGBA32Float, Format::kRGBA16Float }) {
    const auto producer = MakeSignal(9U, 3U, finite, 2U, format);
    const auto invalid = MakeSignal(9U, 3U, nonfinite, 2U, format);
    auto order = std::array { 6U, 10U, 11U };
    do {
      for (const unsigned invalid_product : { 0U, 6U, 10U }) {
        const auto first_mask = invalid_product == 0U ? 0U : 7U;
        for (unsigned mask = first_mask; mask < 8U; ++mask) {
          SCOPED_TRACE(::testing::Message()
            << "format=" << static_cast<unsigned>(format)
            << " order=" << order[0] << ',' << order[1] << ',' << order[2]
            << " invalid=" << invalid_product << " gain-mask=" << mask);
          ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
          const auto frame
            = pass_->ResolveFrame(ctx_, config, { .use_fp32 = true });
          ASSERT_NE(frame, nullptr);
          ASSERT_TRUE(RecordShared(meter, config).executed);
          std::array<postprocess::ExposurePass::HdrProduct, 3> products;
          auto expected_first = invalid_product;
          auto expected_rejected = invalid_product == 0U ? 0U : 54U;
          for (unsigned index = 0U; index < order.size(); ++index) {
            const auto id = order[index];
            const auto& signal = id == 11U
              ? scene
              : (id == invalid_product ? invalid : producer);
            const auto bad_gain = (mask & failure_bit(id)) != 0U;
            products[index] = { .texture = signal.texture.get(),
              .srv = signal.srv,
              .id = id,
              .transmittance = id != 11U,
              .consumer_rgb_gain
              = bad_gain ? std::numeric_limits<float>::quiet_NaN() : 1.0F };
            if (id != invalid_product && bad_gain) {
              expected_rejected += id == 11U ? 9U : 54U;
              if (expected_first == 0U) {
                expected_first = id;
              }
            }
            if (id != 11U) {
              ASSERT_TRUE(
                pass_->GatherFilterGradients(ctx_, frame, products[index]));
            }
          }
          ASSERT_TRUE(
            pass_->EvaluateFp16Products(ctx_, frame, config, products, {}));
          const auto report = Read<HdrSuitabilityData>(
            *frame->suitability_buffer, ResourceStates::kShaderResource);
          EXPECT_EQ(report.first_failure_product, expected_first);
          EXPECT_EQ(report.failure_flags, expected_rejected == 0U ? 0U : 1U);
          EXPECT_EQ(report.rejected_samples, expected_rejected);
          EXPECT_EQ(report.checked_samples, invalid_product == 0U ? 117U : 63U);
          EXPECT_EQ(report.checked_products, expected_products);
          EXPECT_EQ(report.expected_products, expected_products);
          EXPECT_EQ(report.image_failures, 0U);
          EXPECT_EQ(report.metering_failures, 0U);
          EXPECT_EQ(report.overflow_failures, 0U);
          EXPECT_EQ(report.reserved, 0U);
          EXPECT_EQ(report.candidate_pre_exposure, 4096.0F);
          EXPECT_EQ(std::bit_cast<std::uint32_t>(report.maximum_scene_rgb),
            0x40000004U);
          const auto status
            = Read<ExposureStatusStorage>(*frame->current_state->status_buffer,
              ResourceStates::kShaderResource);
          for (const auto id : { 6U, 10U }) {
            const auto& bound = status.candidate_errors[id == 6U ? 1U : 2U];
            if (id == invalid_product) {
              EXPECT_TRUE(std::isinf(bound.rgb_absolute));
              EXPECT_TRUE(std::isinf(bound.transmittance_absolute));
            } else {
              for (const auto value : { bound.rgb_relative, bound.rgb_absolute,
                     bound.transmittance_relative,
                     bound.transmittance_absolute }) {
                EXPECT_TRUE(std::isfinite(value));
                EXPECT_GE(value, 0.0F);
                EXPECT_LE(value, 2e-5F);
              }
            }
          }
        }
      }
    } while (std::next_permutation(order.begin(), order.end()));
  }
}

NOLINT_TEST_F(ExposureGpuTest, CurrentScaleProducerChecksKeepOrderedFailures)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 0.0F;
  const auto config = SharedConfig(settings);
  const auto meter = Uniform(.25F);
  const auto signal = Uniform(.5F, 9U, 1U);
  for (const bool reverse : { false, true }) {
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    const auto frame = pass_->ResolveFrame(ctx_, config, { .use_fp32 = true });
    ASSERT_NE(frame, nullptr);
    ASSERT_TRUE(RecordShared(meter, config).executed);
    auto products = std::array {
      postprocess::ExposurePass::HdrProduct { .texture = signal.texture.get(),
        .srv = signal.srv,
        .id = 6U,
        .transmittance = true,
        .consumer_rgb_gain = std::numeric_limits<float>::quiet_NaN() },
      postprocess::ExposurePass::HdrProduct { .texture = signal.texture.get(),
        .srv = signal.srv,
        .id = 10U,
        .transmittance = true,
        .consumer_rgb_gain = std::numeric_limits<float>::quiet_NaN() }
    };
    if (reverse) {
      std::ranges::reverse(products);
    }
    ASSERT_TRUE(pass_->EvaluateFp16Products(ctx_, frame, config, products, {},
      postprocess::ExposurePass::SuitabilityScale::kCurrentFrame));
    const auto report = Read<HdrSuitabilityData>(
      *frame->conversion_buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(report.first_failure_product, products.front().id);
    EXPECT_EQ(report.failure_flags, 1U);
    EXPECT_EQ(report.rejected_samples, 18U);
    EXPECT_EQ(report.checked_samples, 18U);
    EXPECT_EQ(report.image_failures, 0U);
    EXPECT_EQ(report.metering_failures, 0U);
    EXPECT_EQ(report.overflow_failures, 0U);
    EXPECT_EQ(report.reserved, 0U);
    EXPECT_EQ(report.candidate_pre_exposure, 1.0F);
  }
}
NOLINT_TEST_F(ExposureGpuTest, ProducerChecksKeepDivergentLaneFailures)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 0.0F;
  settings.key = 12.5F;
  const auto config = SharedConfig(settings);
  const auto meter = Uniform(.25F);
  auto pixels = std::array<Pixel, 9> {};
  pixels.fill(Pixel { .25F, .5F, 2.0F, .5F });
  for (const auto index : { 1U, 3U }) {
    pixels[index] = { 0x1p-64F, 0.0F, 0.0F, .5F };
  }
  for (const auto index : { 2U, 5U, 8U }) {
    pixels[index][3] = std::numeric_limits<float>::quiet_NaN();
  }
  const auto signal = MakeSignal(9U, 1U, pixels);
  for (const auto id : { 6U, 10U }) {
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    const auto frame = pass_->ResolveFrame(ctx_, config, { .use_fp32 = true });
    ASSERT_NE(frame, nullptr);
    ASSERT_TRUE(RecordShared(meter, config).executed);
    const auto product
      = postprocess::ExposurePass::HdrProduct { .texture = signal.texture.get(),
          .srv = signal.srv,
          .id = id,
          .transmittance = true,
          .consumer_rgb_gain = 0x1p60F };
    ASSERT_TRUE(pass_->GatherFilterGradients(ctx_, frame, product));
    ASSERT_TRUE(pass_->EvaluateFp16Products(
      ctx_, frame, config, std::span { &product, 1U }, {}));
    const auto report = Read<HdrSuitabilityData>(
      *frame->suitability_buffer, ResourceStates::kShaderResource);
    // Three nonfinite texels are rejected by the maximum scan. Of six finite
    // texels, two lose RGB in half storage; their amplified contribution is
    // 1/16, so they exceed the independently specified absolute image budget.
    EXPECT_EQ(report.first_failure_product, id);
    EXPECT_EQ(report.failure_flags, 1U | 4U);
    EXPECT_EQ(report.rejected_samples, 3U);
    EXPECT_EQ(report.checked_samples, 6U);
    EXPECT_EQ(report.image_failures, 2U);
    EXPECT_EQ(report.metering_failures, 0U);
    EXPECT_EQ(report.overflow_failures, 0U);
    EXPECT_EQ(report.reserved, 0U);
  }
}
NOLINT_TEST_F(ExposureGpuTest, FilterGradientsEncloseReferenceNeighborsAndRetry)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.key = 12.5F;
  settings.manual_ev = 3.0F; // P=1/8; transmittance must remain unscaled.
  const auto config = SharedConfig(settings);
  const auto capture = BeginOptionalCapture();
  unsigned case_index = 0U;
  for (const auto format : { Format::kRGBA32Float, Format::kRGBA16Float })
    for (const auto shape :
      { std::array { 9U, 3U, 1U }, std::array { 9U, 3U, 2U },
        std::array { 1U, 1U, 2U }, std::array { 1U, 1U, 1U } })
      for (const bool retained : { false, true }) {
        SCOPED_TRACE(case_index);
        const auto [width, height, depth] = shape;
        std::vector<Pixel> pixels(width * height * depth);
        for (unsigned z = 0; z < depth; ++z)
          for (unsigned y = 0; y < height; ++y)
            for (unsigned x = 0; x < width; ++x)
              pixels[(z * height + y) * width + x]
                = Pixel { float(x) + float(y) / 4, float(y) * 2 + float(z) / 2,
                    float(z) * 4 + float(x) / 8, float(x + y + z) / 16 };
        const auto signal = MakeSignal(width, height, pixels, depth, format);
        const auto id = std::array { 5U, 6U, 10U }[case_index++ % 3U];
        const auto record_index = id == 5U ? 0U : id == 6U ? 1U : 2U;
        const bool transmission = id != 5U;
        const HdrErrorBoundsData bounds = retained
          ? HdrErrorBoundsData { .rgb_relative = 1.0F / 128,
              .rgb_absolute = .125F,
              .transmittance_relative = 1.0F / 64,
              .transmittance_absolute = 1.0F / 512 }
          : HdrErrorBoundsData {};
        ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
        const auto frame = pass_->ResolveFrame(ctx_, config, {});
        ASSERT_NE(frame, nullptr);
        auto upload = CreateUploadBuffer(SizeBytes { sizeof(bounds) });
        upload->Update(&bounds, sizeof(bounds), 0U);
        {
          auto recorder = AcquireRecorder("Filter gradient controlled bounds");
          EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
          ASSERT_TRUE(recorder->AdoptKnownResourceState(
            *frame->current_state->status_buffer));
          recorder->RequireResourceState(
            *frame->current_state->status_buffer, ResourceStates::kCopyDest);
          recorder->FlushBarriers();
          recorder->CopyBuffer(*frame->current_state->status_buffer,
            80U + record_index * 16U, *upload, 0U, sizeof(bounds));
          recorder->RequireResourceStateFinal(
            *frame->current_state->status_buffer,
            ResourceStates::kUnorderedAccess);
        }
        const auto read_status = [&] {
          return Read<ExposureStatusStorage>(
            *frame->current_state->status_buffer,
            ResourceStates::kUnorderedAccess);
        };
        const auto before = read_status();
        postprocess::ExposurePass::HdrProduct product { .texture
          = signal.texture.get(),
          .srv = signal.srv,
          .id = id,
          .transmittance = transmission };
        ASSERT_TRUE(pass_->GatherFilterGradients(ctx_, frame, product));
        EXPECT_TRUE(pass_->HasFilterGradients(frame, id));
        const auto after = read_status();
        EXPECT_EQ(std::memcmp(&before, &after, 160U), 0);
        const auto& gradient = after.filter_gradients[record_index];
        EXPECT_EQ(gradient.flags, 1U);
        EXPECT_EQ(gradient.checked_texels, pixels.size());
        std::array<double, 3> expected_rgb {}, expected_t {};
        const auto interval = [&](float stored, unsigned channel) {
          const double observed = double(stored) * (channel == 3U ? 1 : 8);
          const double relative = channel == 3U ? bounds.transmittance_relative
                                                : bounds.rgb_relative;
          const double absolute = channel == 3U ? bounds.transmittance_absolute
                                                : bounds.rgb_absolute;
          std::array result { std::max(
                                0.0, (observed - absolute) / (1 + relative)),
            (observed + absolute) / (1 - relative) };
          if (channel == 3U) {
            result[0] = std::min(1.0, result[0]);
            result[1] = std::min(1.0, result[1]);
          }
          return result;
        };
        for (unsigned z = 0; z < depth; ++z)
          for (unsigned y = 0; y < height; ++y)
            for (unsigned x = 0; x < width; ++x) {
              const auto index = (z * height + y) * width + x;
              const std::array coordinate { x, y, z };
              for (unsigned axis = 0; axis < 3; ++axis) {
                const bool boundary = coordinate[axis] + 1U >= shape[axis];
                if (boundary)
                  continue;
                const auto stride
                  = std::array { 1U, width, width * height }[axis];
                const auto neighbor = index + stride;
                for (unsigned c = 0; c < (transmission ? 4U : 3U); ++c) {
                  const auto a = interval(pixels[index][c], c);
                  const auto b = interval(pixels[neighbor][c], c);
                  auto& expected
                    = c == 3U ? expected_t[axis] : expected_rgb[axis];
                  expected
                    = std::max(expected, std::max(a[1] - b[0], b[1] - a[0]));
                }
              }
            }
        for (unsigned axis = 0; axis < 3; ++axis) {
          EXPECT_GE(double(gradient.rgb[axis]), expected_rgb[axis]);
          EXPECT_GE(double(gradient.transmittance[axis]), expected_t[axis]);
          EXPECT_LE(
            double(gradient.rgb[axis]), expected_rgb[axis] * 1.001 + 1e-6);
          EXPECT_LE(double(gradient.transmittance[axis]),
            expected_t[axis] * 1.001 + 1e-6);
          if (shape[axis] == 1U) {
            EXPECT_EQ(gradient.rgb[axis], 0.0F);
            EXPECT_EQ(gradient.transmittance[axis], 0.0F);
          }
        }
        for (unsigned peer = 0; peer < 3; ++peer)
          if (peer != record_index)
            EXPECT_EQ(
              std::memcmp(&before.filter_gradients[peer],
                &after.filter_gradients[peer], sizeof(HdrFilterGradientData)),
              0);
        const auto constant
          = Uniform(2.0F, retained ? 1U : 9U, retained ? 1U : 3U);
        ctx_.current_view.view_id = ViewId { 2U };
        ctx_.current_view.view_state_handle
          = CompositionView::ViewStateHandle { 2U };
        const auto other = pass_->ResolveFrame(ctx_, config, {});
        ASSERT_TRUE(pass_->GatherFilterGradients(ctx_, other,
          { .texture = constant.texture.get(),
            .srv = constant.srv,
            .id = id,
            .transmittance = transmission }));
        const auto retained_status = read_status();
        EXPECT_EQ(
          std::memcmp(&gradient,
            &retained_status.filter_gradients[record_index], sizeof(gradient)),
          0);
        ctx_.current_view.view_id = ViewId { 1U };
        ctx_.current_view.view_state_handle
          = CompositionView::ViewStateHandle { 1U };
        product.srv = kInvalidShaderVisibleIndex;
        EXPECT_FALSE(pass_->GatherFilterGradients(ctx_, frame, product));
        EXPECT_FALSE(pass_->HasFilterGradients(frame, id));
        product.srv = signal.srv;
        auto& backend = static_cast<ExposureFailureGraphics&>(Backend());
        backend.fail_recorder_name = "Vortex Exposure Filter Gradients";
        EXPECT_FALSE(pass_->GatherFilterGradients(ctx_, frame, product));
        EXPECT_FALSE(pass_->HasFilterGradients(frame, id));
        backend.fail_recorder_name.clear();
        ASSERT_TRUE(pass_->GatherFilterGradients(ctx_, frame,
          { .texture = constant.texture.get(),
            .srv = constant.srv,
            .id = id,
            .transmittance = transmission }));
        EXPECT_TRUE(pass_->HasFilterGradients(frame, id));
        const auto retried = read_status().filter_gradients[record_index];
        EXPECT_EQ(retried.flags, 1U);
        EXPECT_EQ(retried.checked_texels, retained ? 1U : 27U);
        EXPECT_EQ(retried.rgb, (std::array<float, 3> {}));
        EXPECT_EQ(retried.transmittance, (std::array<float, 3> {}));
      }
  if (capture)
    EXPECT_TRUE(capture->EndCapture());
}

NOLINT_TEST_F(
  ExposureGpuTest, FilterGradientsRejectInvalidIntervalsAndPreserveTinyGaps)
{
  struct Case {
    float rgb;
    float alpha;
    HdrErrorBoundsData bounds;
    bool transmission;
    bool valid;
  };
  const auto infinity = std::numeric_limits<float>::infinity();
  const auto nan = std::numeric_limits<float>::quiet_NaN();
  const std::array cases { Case {
                             std::bit_cast<float>(1U), .5F, {}, true, true },
    Case { 1, std::bit_cast<float>(1U), {}, true, true },
    Case { 1, 0, { .transmittance_absolute = std::bit_cast<float>(1U) }, true,
      true },
    Case { 1, std::numeric_limits<float>::min(),
      { .transmittance_absolute = std::bit_cast<float>(0x007fffffU) }, true,
      true },
    Case { 1, 0,
      { .transmittance_relative = .5F, .transmittance_absolute = -0.0F }, true,
      true },
    Case { std::bit_cast<float>(0x80000001U), .5F, {}, true, false },
    Case { infinity, .5F, {}, true, false }, Case { nan, .5F, {}, true, false },
    Case { 1, 1.125F, {}, true, false }, Case { 1, nan, {}, false, true },
    Case { 1, .5F, { .rgb_relative = 1 }, true, false },
    Case { 1, .5F, { .rgb_absolute = std::bit_cast<float>(0x80000001U) }, true,
      false },
    Case { 1, .5F, { .transmittance_relative = 1 }, true, false },
    Case { 1, .5F, { .transmittance_relative = 1 }, false, true },
    Case { std::numeric_limits<float>::max(), .5F, { .rgb_absolute = 1 }, true,
      false } };
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.key = 12.5F;
  settings.manual_ev = 0.0F;
  for (const auto& test : cases) {
    SCOPED_TRACE(test.rgb);
    const auto id = test.transmission ? 6U : 5U;
    const auto record_index = test.transmission ? 1U : 0U;
    const std::array pixels { Pixel { 0, 0, 0, 0 },
      Pixel { test.rgb, 0, 0, test.alpha } };
    const auto signal = MakeSignal(2U, 1U, pixels);
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    const auto frame = pass_->ResolveFrame(ctx_, SharedConfig(settings), {});
    ASSERT_NE(frame, nullptr);
    auto upload = CreateUploadBuffer(SizeBytes { sizeof(test.bounds) });
    upload->Update(&test.bounds, sizeof(test.bounds), 0U);
    {
      auto recorder = AcquireRecorder("Invalid gradient bounds fixture");
      EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
      ASSERT_TRUE(recorder->AdoptKnownResourceState(
        *frame->current_state->status_buffer));
      recorder->RequireResourceState(
        *frame->current_state->status_buffer, ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyBuffer(*frame->current_state->status_buffer,
        80U + record_index * 16U, *upload, 0U, sizeof(test.bounds));
      recorder->RequireResourceStateFinal(
        *frame->current_state->status_buffer, ResourceStates::kUnorderedAccess);
    }
    ASSERT_TRUE(pass_->GatherFilterGradients(ctx_, frame,
      { .texture = signal.texture.get(),
        .srv = signal.srv,
        .id = id,
        .transmittance = test.transmission }));
    const auto result = Read<ExposureStatusStorage>(
      *frame->current_state->status_buffer, ResourceStates::kUnorderedAccess)
                          .filter_gradients[record_index];
    EXPECT_EQ(result.flags, test.valid ? 1U : 3U);
    EXPECT_EQ(result.checked_texels, 2U);
    if (test.valid) {
      EXPECT_GE(double(result.rgb[0]), double(test.rgb));
      EXPECT_GT(result.rgb[0], 0.0F);
      EXPECT_EQ(result.rgb[1], 0.0F);
      EXPECT_EQ(result.rgb[2], 0.0F);
      if (test.transmission) {
        const double upper
          = (double(test.alpha) + test.bounds.transmittance_absolute)
          / (1.0 - test.bounds.transmittance_relative);
        EXPECT_GE(double(result.transmittance[0]), std::min(1.0, upper));
        if (upper == 0)
          EXPECT_EQ(result.transmittance[0], 0.0F);
      }
      if (!test.transmission)
        EXPECT_EQ(result.transmittance, (std::array<float, 3> {}));
    }
  }
}

NOLINT_TEST_F(ExposureGpuTest, OpaqueApErrorInvalidatesWhenInputCaptureChanges)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.key = 12.5F;
  settings.manual_ev = 0.0F;
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  const auto frame = pass_->ResolveFrame(ctx_, SharedConfig(settings), {});
  ASSERT_NE(frame, nullptr);
  const auto small_source = Uniform(1.0F);
  const auto large = Uniform(100.0F);
  ASSERT_TRUE(pass_->CapturePreEnvironmentRange(
    ctx_, frame, *small_source.texture, small_source.srv));
  const HdrErrorBoundsData bounds { .transmittance_absolute = .125F };
  auto upload = CreateUploadBuffer(SizeBytes { sizeof(bounds) });
  upload->Update(&bounds, sizeof(bounds), 0U);
  {
    auto recorder = AcquireRecorder("Opaque AP dependency bounds");
    EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
    ASSERT_TRUE(
      recorder->AdoptKnownResourceState(*frame->current_state->status_buffer));
    recorder->RequireResourceState(
      *frame->current_state->status_buffer, ResourceStates::kCopyDest);
    recorder->FlushBarriers();
    recorder->CopyBuffer(
      *frame->current_state->status_buffer, 96U, *upload, 0U, sizeof(bounds));
    recorder->RequireResourceStateFinal(
      *frame->current_state->status_buffer, ResourceStates::kUnorderedAccess);
  }
  const auto read_bound = [&] {
    return Read<ExposureStatusStorage>(
      *frame->current_state->status_buffer, ResourceStates::kUnorderedAccess)
      .opaque_ap_error;
  };
  ASSERT_TRUE(pass_->PropagateOpaqueApError(ctx_, frame, 1.0F));
  ASSERT_TRUE(pass_->HasOpaqueApError(frame));
  EXPECT_GE(read_bound().rgb_absolute, .125F);
  ASSERT_TRUE(
    pass_->CapturePreEnvironmentRange(ctx_, frame, *large.texture, large.srv));
  EXPECT_FALSE(pass_->HasOpaqueApError(frame));
  EXPECT_EQ(read_bound().valid, 0U);
  ASSERT_TRUE(pass_->PropagateOpaqueApError(ctx_, frame, 1.0F));
  EXPECT_GE(read_bound().rgb_absolute, 12.5F);

  for (const bool invalid_srv : { true, false }) {
    SCOPED_TRACE(invalid_srv);
    auto& backend = static_cast<ExposureFailureGraphics&>(Backend());
    if (!invalid_srv)
      backend.fail_recorder_name = "Vortex Exposure PreEnvironment Range";
    EXPECT_FALSE(
      pass_->CapturePreEnvironmentRange(ctx_, frame, *small_source.texture,
        invalid_srv ? kInvalidShaderVisibleIndex : small_source.srv));
    backend.fail_recorder_name.clear();
    EXPECT_FALSE(pass_->HasPreEnvironmentRange(frame));
    EXPECT_FALSE(pass_->HasOpaqueApError(frame));
    EXPECT_FALSE(pass_->PropagateOpaqueApError(ctx_, frame, 1.0F));
    ASSERT_TRUE(pass_->CapturePreEnvironmentRange(
      ctx_, frame, *small_source.texture, small_source.srv));
    EXPECT_FALSE(pass_->HasOpaqueApError(frame));
    EXPECT_EQ(read_bound().valid, 0U);
    ASSERT_TRUE(pass_->PropagateOpaqueApError(ctx_, frame, 1.0F));
    EXPECT_TRUE(pass_->HasOpaqueApError(frame));
    EXPECT_GE(read_bound().rgb_absolute, .125F);
    EXPECT_LT(read_bound().rgb_absolute, .126F);
  }
}

NOLINT_TEST_F(ExposureGpuTest, OpaqueApErrorUsesInputPeakAndActualGain)
{
  struct Case {
    float peak;
    float gain;
    HdrErrorBoundsData bounds;
    bool valid { true };
    float ev { 0.0F };
  };
  const auto infinity = std::numeric_limits<float>::infinity();
  const auto nan = std::numeric_limits<float>::quiet_NaN();
  const std::array cases { Case { 32, 8,
                             { .rgb_relative = .01F,
                               .rgb_absolute = .002F,
                               .transmittance_relative = .02F,
                               .transmittance_absolute = 1e-5F } },
    Case { 8192, 1, { .transmittance_absolute = 1e-4F } },
    Case { 1, 1e6F, { .rgb_absolute = 1e-8F } },
    Case { 16, 8, { .rgb_absolute = .002F, .transmittance_absolute = 1e-5F },
      true, 3 },
    Case { 0, 2, { .rgb_absolute = .125F } }, Case { 32, 1, {} },
    Case { 32, 0, { .rgb_absolute = infinity } },
    Case { 32, .00005F, { .rgb_absolute = infinity } },
    Case { 32, .0001F, { .rgb_absolute = .25F } },
    Case { 1e-30F, 1, { .transmittance_absolute = 1e-30F } },
    Case { 1, 1, { .rgb_absolute = std::bit_cast<float>(1U) } },
    Case { 1, 0x1p100F, { .rgb_absolute = std::bit_cast<float>(1U) } },
    Case { 1, 1, { .rgb_relative = std::bit_cast<float>(1U) } },
    Case { 1, infinity, {}, false }, Case { 1, -1, {}, false },
    Case { 1, nan, {}, false }, Case { 1, 1, { .rgb_relative = 1 }, false },
    Case { 1, 1, { .transmittance_relative = 1 }, false },
    Case { 1, 1, { .rgb_absolute = -1 }, false },
    Case { 1, 1, { .rgb_absolute = std::bit_cast<float>(0x80000001U) }, false },
    Case { 1, std::bit_cast<float>(0x80000001U), {}, false },
    Case { 1, 1, { .transmittance_absolute = nan }, false },
    Case { -1, 1, {}, false }, Case { infinity, 1, {}, false },
    Case {
      1, std::numeric_limits<float>::max(), { .rgb_absolute = 2 }, false } };
  const auto capture = BeginOptionalCapture();
  for (std::size_t index = 0U; index < cases.size(); ++index) {
    SCOPED_TRACE(index);
    const auto& test = cases[index];
    auto settings = scene::ExposureSettings {};
    settings.mode = engine::ExposureMode::kManual;
    settings.key = 12.5F;
    settings.manual_ev = test.ev;
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    const auto frame = pass_->ResolveFrame(ctx_, SharedConfig(settings), {});
    ASSERT_NE(frame, nullptr);
    EXPECT_FALSE(pass_->PropagateOpaqueApError(ctx_, frame, test.gain));
    const auto source = Uniform(test.peak);
    ASSERT_TRUE(pass_->CapturePreEnvironmentRange(
      ctx_, frame, *source.texture, source.srv));
    auto upload = CreateUploadBuffer(SizeBytes { sizeof(test.bounds) });
    upload->Update(&test.bounds, sizeof(test.bounds), 0U);
    {
      auto recorder = AcquireRecorder("Opaque AP controlled bounds");
      EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
      ASSERT_TRUE(recorder->AdoptKnownResourceState(
        *frame->current_state->status_buffer));
      recorder->RequireResourceState(
        *frame->current_state->status_buffer, ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyBuffer(*frame->current_state->status_buffer, 96U, *upload,
        0U, sizeof(test.bounds));
      recorder->RequireResourceStateFinal(
        *frame->current_state->status_buffer, ResourceStates::kUnorderedAccess);
    }
    const auto before = Read<ExposureStatusStorage>(
      *frame->current_state->status_buffer, ResourceStates::kUnorderedAccess);
    ASSERT_TRUE(pass_->PropagateOpaqueApError(ctx_, frame, test.gain));
    EXPECT_TRUE(pass_->HasOpaqueApError(frame));
    const auto after = Read<ExposureStatusStorage>(
      *frame->current_state->status_buffer, ResourceStates::kUnorderedAccess);
    EXPECT_EQ(std::memcmp(&before, &after, 144U), 0);
    const auto& actual = after.opaque_ap_error;
    EXPECT_EQ(actual.valid, test.valid ? 1U : 0U);
    EXPECT_EQ(actual.reserved, 0U);
    if (!test.valid) {
      EXPECT_TRUE(std::isinf(actual.rgb_absolute));
      continue;
    }
    // Independent double arithmetic over the authored binary32 inputs. These
    // cases have at most two products and a sum; the GPU rounds outward.
    const double relative = test.gain < .0001F
      ? 0
      : std::max(double(test.bounds.rgb_relative),
          double(test.bounds.transmittance_relative));
    const double absolute = test.gain < .0001F
      ? 0
      : double(test.gain) * test.bounds.rgb_absolute
        + std::ldexp(double(test.peak), int(test.ev))
          * test.bounds.transmittance_absolute;
    EXPECT_GE(double(actual.rgb_relative), relative);
    EXPECT_GE(double(actual.rgb_absolute), absolute);
    const auto promote = [](double value) {
      return value > 0 && value < std::numeric_limits<float>::min()
        ? double(std::numeric_limits<float>::min())
        : value;
    };
    const double operand_ceiling = test.gain < .0001F
      ? 0
      : promote(test.gain) * promote(test.bounds.rgb_absolute)
        + promote(std::ldexp(double(test.peak), int(test.ev)))
          * promote(test.bounds.transmittance_absolute);
    EXPECT_LE(double(actual.rgb_absolute),
      operand_ceiling * 1.00001
        + double(std::numeric_limits<float>::min()) * 1.00001);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(actual.rgb_relative),
      test.gain < .0001F
        ? 0U
        : std::max(std::bit_cast<std::uint32_t>(test.bounds.rgb_relative)
              & 0x7fffffffU,
            std::bit_cast<std::uint32_t>(test.bounds.transmittance_relative)
              & 0x7fffffffU));
    if (absolute == 0)
      EXPECT_EQ(actual.rgb_absolute, 0.0F);
    auto& backend = static_cast<ExposureFailureGraphics&>(Backend());
    backend.fail_recorder_name = "Vortex Exposure Opaque AP Error";
    EXPECT_FALSE(pass_->PropagateOpaqueApError(ctx_, frame, test.gain));
    EXPECT_FALSE(pass_->HasOpaqueApError(frame));
    backend.fail_recorder_name.clear();
    ASSERT_TRUE(pass_->PropagateOpaqueApError(ctx_, frame, test.gain));
    EXPECT_TRUE(pass_->HasOpaqueApError(frame));
  }
  if (capture)
    EXPECT_TRUE(capture->EndCapture());
}

NOLINT_TEST_F(
  ExposureGpuTest, MaterialTexturesPreserveTypedLinearRadianceInScenePaths)
{
  pass_.reset();
  renderer_->OnShutdown();
  auto renderer_config = RendererConfig {};
  renderer_config.upload_queue_key = QueueKeyFor().get();
  renderer_ = std::make_unique<Renderer>(GetGraphicsShared(), renderer_config,
    kPhase1DefaultRuntimeCapabilityFamilies
      | RendererCapabilityFamily::kDeferredShading
      | RendererCapabilityFamily::kLightingData
      | RendererCapabilityFamily::kFinalOutputComposition);
  owned_asset_loader_ = std::make_unique<vortex::testing::FakeAssetLoader>();
  owned_test_engine_
    = std::make_unique<::testing::NiceMock<ExposureTestEngine>>();
  ON_CALL(*owned_test_engine_, GetAssetLoader())
    .WillByDefault(::testing::Return(
      observer_ptr<content::IAssetLoader> { owned_asset_loader_.get() }));
  ASSERT_TRUE(renderer_->OnAttached(
    observer_ptr<IAsyncEngine> { owned_test_engine_.get() }));
  auto scene = std::make_shared<scene::Scene>("Material producer domain", 8U);
  scene->SetEnvironment(std::make_unique<scene::SceneEnvironment>());
  auto& post = scene->GetEnvironment()
                 ->AddSystem<scene::environment::PostProcessVolume>();
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 0;
  settings.key = 12.5F;
  post.SetExposureSettings(settings);
  post.SetToneMapper(engine::ToneMapper::kNone);
  post.SetDisplayGamma(1);
  post.SetBloomIntensity(0);
  auto camera = scene->CreateNode("Camera");
  auto lens = std::make_unique<scene::PerspectiveCamera>();
  auto view = View {};
  view.viewport = { .width = 1, .height = 1 };
  lens->SetViewport(view.viewport);
  ASSERT_TRUE(camera.AttachCamera(std::move(lens)));
  auto mesh_node = scene->CreateNode("Radiance triangle");
  std::vector<data::Vertex> vertices(3);
  const std::array positions { glm::vec3 { -2, -2, -1 },
    glm::vec3 { 2, -2, -1 }, glm::vec3 { 0, 2, -1 } };
  for (unsigned i = 0; i < 3; ++i) {
    vertices[i] = { .position = positions[i],
      .normal = { 0, 0, 1 },
      .texcoord = { .5F, .5F },
      .tangent = { 1, 0, 0 },
      .bitangent = { 0, 1, 0 },
      .color = { 1, 1, 1, 1 } };
  }
  std::shared_ptr<data::Mesh> mesh
    = data::MeshBuilder()
        .WithVertices(vertices)
        .WithIndices(std::vector<std::uint32_t> { 0, 1, 2 })
        .BeginSubMesh("Radiance", data::MaterialAsset::CreateDefault())
        .WithMeshView({ .first_index = 0,
          .index_count = 3,
          .first_vertex = 0,
          .vertex_count = 3 })
        .EndSubMesh()
        .Build();
  data::pak::geometry::GeometryAssetDesc geometry_desc {};
  geometry_desc.lod_count = 1;
  geometry_desc.bounding_box_min[0] = geometry_desc.bounding_box_min[1] = -2;
  geometry_desc.bounding_box_max[0] = geometry_desc.bounding_box_max[1] = 2;
  geometry_desc.bounding_box_min[2] = geometry_desc.bounding_box_max[2] = -1;
  mesh_node.GetRenderable().SetGeometry(std::make_shared<data::GeometryAsset>(
    data::AssetKey::FromVirtualPath("/Test/Exposure/Domain.ogeo"),
    geometry_desc, std::vector<std::shared_ptr<data::Mesh>> { mesh }));
  auto output = CreateRegisteredTexture({ .width = 1,
    .height = 1,
    .format = Format::kRGBA32Float,
    .is_render_target = true,
    .initial_state = ResourceStates::kCommon });
  auto framebuffer = Backend().CreateFramebuffer(
    FramebufferDesc {}.AddColorAttachment(output));
  struct Probe final : IViewExtension {
    Renderer& renderer;
    std::shared_ptr<const Texture> color;
    explicit Probe(Renderer& value)
      : renderer(value)
    {
    }
    postprocess::ExposurePass::FrameLease exposure;
    unsigned draws = 0;
    std::function<void(RenderContext&)> prepare;
    auto OnViewSetup(const ViewSetupContext& hook) -> void override
    {
      prepare(hook.render_context);
    }
    auto OnPostRenderViewGpu(const ViewRenderGpuContext& hook) -> void override
    {
      exposure = hook.render_context.current_view.frame_exposure;
      auto* owner
        = vortex::testing::RendererPublicationProbe::GetSceneRenderer(renderer);
      const auto& extracted
        = owner->GetSceneTextureExtracts().resolved_scene_color;
      color = extracted.valid ? extracted.texture->shared_from_this() : nullptr;

      const auto frame = hook.render_context.current_view.prepared_frame;
      draws = frame ? static_cast<unsigned>(frame->draw_metadata_bytes.size()
                        / sizeof(DrawMetadata))
                    : 0;
    }
  };
  auto probe = std::make_shared<Probe>(*renderer_);
  probe->prepare = [&](RenderContext& ctx) {
    auto* owner
      = vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_);
    auto* service
      = vortex::testing::RendererPublicationProbe::GetPostProcessService(
        *owner);
    service->SetResolvedConfig(SharedConfig(settings));
    ASSERT_NE(service->PrepareFrameExposure(ctx, false), nullptr);
  };

  renderer_->RegisterViewExtension(probe);
  auto frame = engine::FrameContext {};
  frame.SetScene(observer_ptr { scene.get() });
  unsigned sequence = 0, case_count = 0;
  namespace cook = content::import;
  struct Sample {
    Format source_format;
    ColorSpace source_space;
    Format stored_format;
    float source_value;
    double linear_value;
    bool range_failure { false };
  };
  std::vector<Sample> samples;
  for (const float value :
    { 0.0F, 0x1p-24F, .25F, 131072.0F, 0x1p32F, 0x1p33F })
    samples.push_back({ Format::kRGBA32Float, ColorSpace::kLinear,
      Format::kRGBA32Float, value, value, value > 0x1p32F });
  for (const float value : { 0x1p-24F, .25F, 65504.0F })
    samples.push_back({ Format::kRGBA32Float, ColorSpace::kLinear,
      Format::kRGBA16Float, value, value });
  const auto decode = [](double x) {
    return x <= .04045 ? x / 12.92 : std::pow((x + .055) / 1.055, 2.4);
  };
  samples.push_back({ Format::kRGBA8UNorm, ColorSpace::kSRGB,
    Format::kRGBA32Float, 128, decode(128.0 / 255) });
  samples.push_back({ Format::kRGBA8UNorm, ColorSpace::kSRGB,
    Format::kRGBA16Float, 128, .2158203125 });
  samples.push_back({ Format::kRGBA8UNorm, ColorSpace::kSRGB,
    Format::kRGBA8UNormSRGB, 128, decode(128.0 / 255) });
  samples.push_back({ Format::kRGBA8UNorm, ColorSpace::kLinear,
    Format::kRGBA32Float, 128, 128.0 / 255 });
  samples.push_back({ Format::kRGBA8UNorm, ColorSpace::kLinear,
    Format::kRGBA8UNorm, 128, 128.0 / 255 });
  samples.push_back({ Format::kRGBA8UNorm, ColorSpace::kLinear,
    Format::kRGBA8UNormSRGB, 128, decode(188.0 / 255) });
  for (const bool forward : { false, true })
    for (const bool unlit : { false, true })
      for (const auto domain :
        { data::MaterialDomain::kOpaque, data::MaterialDomain::kMasked,
          data::MaterialDomain::kAlphaBlended })
        for (const auto& sample : samples)
          for (const float ev : { -32.0F, 0.0F, 32.0F }) {
            // Exercise the full P interval with exact float inputs; encoding
            // controls need one P because their cooked source is unchanged.
            if (ev != 0
              && (sample.source_format != Format::kRGBA32Float
                || sample.stored_format != Format::kRGBA32Float))
              continue;
            SCOPED_TRACE(forward);
            SCOPED_TRACE(unlit);
            SCOPED_TRACE(static_cast<int>(domain));
            SCOPED_TRACE(static_cast<int>(sample.stored_format));
            SCOPED_TRACE(sample.source_value);
            SCOPED_TRACE(ev);
            probe->color.reset();
            probe->draws = 0;
            auto image = cook::ScratchImage::Create(
              { .width = 1, .height = 1, .format = sample.source_format });
            if (sample.source_format == Format::kRGBA32Float) {
              const Pixel pixel { sample.source_value, sample.source_value,
                sample.source_value, 1 };
              std::memcpy(image.GetMutablePixels(0, 0).data(), pixel.data(),
                sizeof(pixel));
            } else {
              const auto value = static_cast<std::uint8_t>(sample.source_value);
              const std::array<std::uint8_t, 4> pixel { value, value, value,
                255 };
              std::memcpy(image.GetMutablePixels(0, 0).data(), pixel.data(),
                sizeof(pixel));
            }
            auto import = cook::TextureImportDesc {};
            import.intent = cook::TextureIntent::kEmissive;
            import.source_color_space = sample.source_space;
            import.output_format = sample.stored_format;
            import.mip_policy = cook::MipPolicy::kNone;
            const auto cooked = cook::CookTexture(
              std::move(image), import, cook::D3D12PackingPolicy::Instance());
            ASSERT_TRUE(cooked.has_value()) << static_cast<int>(cooked.error());
            data::pak::core::TextureResourceDesc texture_desc {};
            texture_desc.data_offset = sizeof(texture_desc);
            texture_desc.size_bytes
              = static_cast<data::pak::core::DataBlobSizeT>(
                cooked->payload.size());
            texture_desc.texture_type
              = static_cast<std::uint8_t>(cooked->desc.texture_type);
            texture_desc.width = cooked->desc.width;
            texture_desc.height = cooked->desc.height;
            texture_desc.depth = cooked->desc.depth;
            texture_desc.array_layers = cooked->desc.array_layers;
            texture_desc.mip_levels = cooked->desc.mip_levels;
            texture_desc.format
              = static_cast<std::uint8_t>(cooked->desc.format);
            texture_desc.alignment = 256;
            texture_desc.content_hash = cooked->desc.content_hash;
            std::vector<std::uint8_t> payload(
              sizeof(texture_desc) + cooked->payload.size());
            std::memcpy(payload.data(), &texture_desc, sizeof(texture_desc));
            std::memcpy(payload.data() + sizeof(texture_desc),
              cooked->payload.data(), cooked->payload.size());
            const auto key = owned_asset_loader_->PreloadCookedTexture(payload);
            const bool base_color_source = forward && unlit;
            const float coverage
              = domain == data::MaterialDomain::kAlphaBlended ? .5F : 1.0F;
            data::pak::render::MaterialAssetDesc material_desc {};
            material_desc.material_domain = static_cast<std::uint8_t>(domain);
            material_desc.flags = data::pak::render::kMaterialFlag_DoubleSided;
            if (unlit)
              material_desc.flags |= data::pak::render::kMaterialFlag_Unlit;
            if (domain == data::MaterialDomain::kMasked)
              material_desc.flags |= data::pak::render::kMaterialFlag_AlphaTest;
            for (unsigned c = 0; c < 3; ++c)
              material_desc.base_color[c] = base_color_source ? 1.0F : 0.0F;
            material_desc.base_color[3] = coverage;
            material_desc.normal_scale = 1;
            material_desc.roughness = data::Unorm16 { 1 };
            material_desc.ambient_occlusion = data::Unorm16 { 1 };
            material_desc.uv_scale[0] = material_desc.uv_scale[1] = 1;
            for (auto& v : material_desc.emissive_factor)
              v = data::HalfFloat { base_color_source ? 0.0F : 1.0F };
            std::vector<content::ResourceKey> keys(6);
            keys[base_color_source ? 0 : 5] = key;
            auto material = std::make_shared<data::MaterialAsset>(
              data::AssetKey::FromVirtualPath(
                "/Test/Exposure/Domain" + std::to_string(case_count) + ".omat"),
              material_desc, std::vector<data::ShaderReference> {}, keys);
            mesh_node.GetRenderable().SetMaterialOverride(0, 0, material);
            settings.manual_ev = ev;
            post.SetExposureSettings(settings);
            scene->Update();
            for (unsigned warmup = 0; warmup < 5; ++warmup) {
              const auto slot = frame::Slot { sequence % 3 };
              Backend().BeginFrame(
                frame::SequenceNumber { sequence + 1 }, slot);
              frame.SetFrameSlot(
                slot, engine::internal::EngineTagFactory::Get());
              frame.SetFrameSequenceNumber(frame::SequenceNumber { ++sequence },
                engine::internal::EngineTagFactory::Get());
              renderer_->OnFrameStart(observer_ptr { &frame });
              auto facade = renderer_->ForOffscreenScene();
              facade.SetFrameSession({ .frame_slot = slot,
                .frame_sequence = frame::SequenceNumber { sequence },
                .delta_time_seconds = 0 });
              facade.SetSceneSource({ .scene = observer_ptr { scene.get() } });
              facade.SetViewIntent(
                Renderer::OffscreenSceneViewInput::FromCamera(
                  "Domain", ViewId { 100U }, view, camera)
                  .SetViewStateHandle(
                    CompositionView::ViewStateHandle { 100U }));
              facade.SetOutputTarget(
                { .framebuffer = observer_ptr { framebuffer.get() } });
              facade.SetPipeline(forward
                  ? Renderer::OffscreenPipelineInput::Forward()
                  : Renderer::OffscreenPipelineInput::Deferred());
              auto session = facade.Finalize();
              ASSERT_TRUE(session.has_value());
              ASSERT_TRUE(session->ExecuteInsideFrame(frame));
              renderer_->OnFrameEnd(observer_ptr { &frame });
              Backend().EndFrame(frame::SequenceNumber { sequence }, slot);
              WaitForQueueIdle();
            }
            ASSERT_EQ(probe->draws, 1U);
            ASSERT_NE(probe->color, nullptr);
            ASSERT_NE(probe->exposure, nullptr);
            const auto domain_data = Read<FrameExposureData>(
              *probe->exposure->buffer, ResourceStates::kShaderResource);
            const double p = std::exp2(-double(ev));
            EXPECT_EQ(domain_data.pre_exposure, p);
            const auto pixels = ReadFloatTexture(*probe->color);
            ASSERT_EQ(pixels.size(), 1U);
            const double expected = sample.linear_value * p * coverage;
            for (unsigned c = 0; c < 3; ++c) {
              if (sample.stored_format == Format::kRGBA8UNormSRGB) {
                // D3D 3.2.3.7 permits half a code of error in encoded sRGB
                // space. Check that bound and the frozen PBR image budget.
                // https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm
                const double encoded = sample.linear_value <= .0031308
                  ? sample.linear_value * 12.92
                  : 1.055 * std::pow(sample.linear_value, 1.0 / 2.4) - .055;
                const double code = std::round(encoded * 255);
                const double arithmetic = std::abs(expected) * 2e-5 + 0x1p-120;
                EXPECT_GE(pixels[0][c],
                  decode(std::max(0.0, code - .5) / 255) * p * coverage
                    - arithmetic);
                EXPECT_LE(pixels[0][c],
                  decode(std::min(255.0, code + .5) / 255) * p * coverage
                    + arithmetic);
                EXPECT_NEAR(pixels[0][c] / p, sample.linear_value * coverage,
                  .005 * sample.linear_value * coverage + 2e-5);
              } else {
                EXPECT_NEAR(
                  pixels[0][c], expected, std::abs(expected) * 2e-5 + 0x1p-120);
              }
            }
            EXPECT_FLOAT_EQ(pixels[0][3], coverage);
            const auto status = Read<ExposureCompletedStatus>(
              *probe->exposure->current_state->status_buffer,
              ResourceStates::kCopySource);
            if (sample.range_failure) {
              EXPECT_EQ(status.flags & 18U, 18U);
              EXPECT_NE(status.first_failure_kind & 32U, 0U);
              EXPECT_EQ(status.first_failure_product,
                domain == data::MaterialDomain::kAlphaBlended ? 4U
                  : forward                                   ? 4U
                                                              : 1U);
            } else
              EXPECT_EQ(status.flags & 16U, 0U);
            ++case_count;
          }
  probe->prepare = {};
  RecordProperty("material_endpoint_cases", case_count);
  WaitForQueueIdle();
}

// A single visible surface isolates light transport from scene composition.
class ExposureLightingGpuTest : public ExposureGpuTest {
protected:
  virtual auto AdditionalCapabilities() const -> CapabilitySet
  {
    return RendererCapabilityFamily::kNone;
  }
  struct Probe final : IViewExtension {
    Renderer& renderer;
    std::shared_ptr<const Texture> color;
    explicit Probe(Renderer& value)
      : renderer(value)
    {
    }
    postprocess::ExposurePass::FrameLease exposure;
    unsigned draws = 0;
    std::vector<float> raster_depths;
    bool early_depth_complete = false;
    std::function<void(RenderContext&)> prepare;
    std::function<void(
      const RenderContext&, const SceneTextureExtractRef&, unsigned)>
      inspect;
    auto OnViewSetup(const ViewSetupContext& hook) -> void override
    {
      prepare(hook.render_context);
    }
    auto OnPostRenderViewGpu(const ViewRenderGpuContext& hook) -> void override
    {
      exposure = hook.render_context.current_view.frame_exposure;
      auto* owner
        = vortex::testing::RendererPublicationProbe::GetSceneRenderer(renderer);
      const auto& extracted
        = owner->GetSceneTextureExtracts().resolved_scene_color;
      color = extracted.valid ? extracted.texture->shared_from_this() : nullptr;

      const auto prepared = hook.render_context.current_view.prepared_frame;
      draws = prepared
        ? static_cast<unsigned>(
            prepared->draw_metadata_bytes.size() / sizeof(DrawMetadata))
        : 0;
      raster_depths.clear();
      early_depth_complete
        = hook.render_context.current_view.IsEarlyDepthComplete();
      if (prepared) {
        for (const auto& draw :
          vortex::testing::RendererPublicationProbe::BasePassDrawCommands(
            *owner)) {
          if (draw.draw_index >= prepared->GetDrawMetadata().size())
            continue;
          const auto material
            = prepared->GetDrawMetadata()[draw.draw_index].material_handle;
          const auto item = std::ranges::find_if(
            prepared->render_items, [material](const auto& value) {
              return value.material_handle.get() == material;
            });
          ASSERT_NE(item, prepared->render_items.end());
          raster_depths.push_back(item->world_bounding_sphere.z + 1.0F);
        }
      }
      if (inspect)
        inspect(hook.render_context, extracted, draws);
    }
  };
  auto SetUp() -> void override
  {
    ExposureGpuTest::SetUp();
    pass_.reset();
    renderer_->OnShutdown();
    auto renderer_config = RendererConfig {};
    renderer_config.upload_queue_key = QueueKeyFor().get();
    renderer_ = std::make_unique<Renderer>(GetGraphicsShared(), renderer_config,
      kPhase1DefaultRuntimeCapabilityFamilies
        | RendererCapabilityFamily::kDeferredShading
        | RendererCapabilityFamily::kLightingData
        | RendererCapabilityFamily::kFinalOutputComposition
        | RendererCapabilityFamily::kEnvironmentLighting
        | AdditionalCapabilities());
    owned_asset_loader_ = std::make_unique<vortex::testing::FakeAssetLoader>();
    owned_test_engine_
      = std::make_unique<::testing::NiceMock<ExposureTestEngine>>();
    ON_CALL(*owned_test_engine_, GetAssetLoader())
      .WillByDefault(::testing::Return(
        observer_ptr<content::IAssetLoader> { owned_asset_loader_.get() }));
    ASSERT_TRUE(renderer_->OnAttached(
      observer_ptr<IAsyncEngine> { owned_test_engine_.get() }));
    renderer_->RegisterConsoleBindings(observer_ptr { &fixture_console });
    ASSERT_EQ(fixture_console.Execute("vtx.occlusion.enable false").status,
      console::ExecutionStatus::kOk);
    scene = std::make_shared<scene::Scene>("Lighting producer domain", 8U);
    scene->SetEnvironment(std::make_unique<scene::SceneEnvironment>());
    auto& post = scene->GetEnvironment()
                   ->AddSystem<scene::environment::PostProcessVolume>();
    settings = scene::ExposureSettings {};
    settings.mode = engine::ExposureMode::kManual;
    settings.manual_ev = 0;
    settings.key = 12.5F;
    post.SetExposureSettings(settings);
    post.SetToneMapper(engine::ToneMapper::kNone);
    post.SetDisplayGamma(1);
    post.SetBloomIntensity(0);
    camera = scene->CreateNode("Camera");
    auto lens = std::make_unique<scene::PerspectiveCamera>();
    view = View {};
    view.viewport = { .width = 1, .height = 1 };
    lens->SetViewport(view.viewport);
    ASSERT_TRUE(camera.AttachCamera(std::move(lens)));
    mesh_node = scene->CreateNode("Radiance triangle");
    std::vector<data::Vertex> vertices(3);
    const std::array positions { glm::vec3 { -2, -2, -1 },
      glm::vec3 { 2, -2, -1 }, glm::vec3 { 0, 2, -1 } };
    for (unsigned i = 0; i < 3; ++i) {
      vertices[i] = { .position = positions[i],
        .normal = { 0, 0, 1 },
        .texcoord = { .5F, .5F },
        .tangent = { 1, 0, 0 },
        .bitangent = { 0, 1, 0 },
        .color = { 1, 1, 1, 1 } };
    }
    std::shared_ptr<data::Mesh> mesh
      = data::MeshBuilder()
          .WithVertices(vertices)
          .WithIndices(std::vector<std::uint32_t> { 0, 1, 2 })
          .BeginSubMesh("Radiance", data::MaterialAsset::CreateDefault())
          .WithMeshView({ .first_index = 0,
            .index_count = 3,
            .first_vertex = 0,
            .vertex_count = 3 })
          .EndSubMesh()
          .Build();
    data::pak::geometry::GeometryAssetDesc geometry_desc {};
    geometry_desc.lod_count = 1;
    geometry_desc.bounding_box_min[0] = geometry_desc.bounding_box_min[1] = -2;
    geometry_desc.bounding_box_max[0] = geometry_desc.bounding_box_max[1] = 2;
    geometry_desc.bounding_box_min[2] = geometry_desc.bounding_box_max[2] = -1;
    mesh_node.GetRenderable().SetGeometry(std::make_shared<data::GeometryAsset>(
      data::AssetKey::FromVirtualPath("/Test/Exposure/Domain.ogeo"),
      geometry_desc, std::vector<std::shared_ptr<data::Mesh>> { mesh }));
    auto output = CreateRegisteredTexture({ .width = 1,
      .height = 1,
      .format = Format::kRGBA32Float,
      .is_render_target = true,
      .initial_state = ResourceStates::kCommon });
    framebuffer = Backend().CreateFramebuffer(
      FramebufferDesc {}.AddColorAttachment(output));
    probe = std::make_shared<Probe>(*renderer_);
    probe->prepare = [&](RenderContext& ctx) {
      ctx.current_view.depth_prepass_mode = depth_mode;
      auto* owner = vortex::testing::RendererPublicationProbe::GetSceneRenderer(
        *renderer_);
      auto* service
        = vortex::testing::RendererPublicationProbe::GetPostProcessService(
          *owner);
      service->SetResolvedConfig(SharedConfig(settings));
      ASSERT_NE(service->PrepareFrameExposure(ctx, false), nullptr);
    };

    renderer_->RegisterViewExtension(probe);
    frame.SetScene(observer_ptr { scene.get() });
  }

  auto MakeEmissiveMaterial(float value) -> std::shared_ptr<data::MaterialAsset>
  {
    data::pak::core::TextureResourceDesc desc {};
    desc.texture_type = static_cast<std::uint8_t>(TextureType::kTexture2D);
    desc.width = desc.height = desc.depth = desc.mip_levels = desc.array_layers
      = 1;
    desc.format = static_cast<std::uint8_t>(Format::kRGBA32Float);
    desc.alignment = 256;
    const auto key = owned_asset_loader_->MintSyntheticTextureKey();
    desc.content_hash = key.get();
    const Pixel pixel { value, value, value, 1 };
    std::vector<std::uint8_t> bytes(sizeof(pixel));
    std::memcpy(bytes.data(), pixel.data(), sizeof(pixel));
    const std::array layouts { data::pak::render::SubresourceLayout {
      .offset_bytes = 0,
      .row_pitch_bytes = sizeof(pixel),
      .size_bytes = sizeof(pixel) } };
    auto payload
      = vortex::testing::detail::BuildV4TexturePayload(desc, layouts, bytes);
    desc.size_bytes = static_cast<std::uint32_t>(payload.size());
    owned_asset_loader_->SetTexture(
      key, std::make_shared<data::TextureResource>(desc, std::move(payload)));
    data::pak::render::MaterialAssetDesc authored {};
    authored.flags = data::pak::render::kMaterialFlag_DoubleSided;
    authored.base_color[3] = 1;
    authored.normal_scale = 1;
    authored.roughness = data::Unorm16 { 1 };
    authored.ambient_occlusion = data::Unorm16 { 1 };
    authored.uv_scale[0] = authored.uv_scale[1] = 1;
    for (auto& component : authored.emissive_factor)
      component = data::HalfFloat { 1.0F };
    std::vector<content::ResourceKey> keys(6);
    keys[5] = key;
    return std::make_shared<data::MaterialAsset>(
      data::AssetKey::FromVirtualPath(
        "/Test/Exposure/Wide" + std::to_string(key.get()) + ".omat"),
      authored, std::vector<data::ShaderReference> {}, keys);
  }

  auto MeasureHdrAllocationAccounting(bool temporal) -> void;
  auto QualifySharedSceneLifecycle(bool forward) -> void;
  auto QualifyMixedPrecisionAuxiliaryHandoff(bool split_targets) -> void;

  auto UniformReferenceGain(float luminance) const -> double
  {
    // Independent one-pixel, full-percentile histogram oracle. Both adjacent
    // bins retain their rounded share of the fixed 4095 sample mass.
    if (luminance <= std::exp2(settings.min_log_luminance))
      return std::exp2(-double(settings.min_ev) + settings.compensation_ev)
        * (settings.target_luminance / .18) * (settings.key / 12.5);
    const double bin
      = std::clamp((std::log2(double(luminance)) - settings.min_log_luminance)
            / settings.log_luminance_range,
          0.0, 1.0)
      * 255;
    const double lower = std::floor(bin);
    const double upper_mass = std::floor((bin - lower) * 4095 + .5);
    const double measured_log = settings.min_log_luminance
      + (lower + upper_mass / 4095) * settings.log_luminance_range / 255;
    const double ev = std::clamp(measured_log - std::log2(.18),
      double(settings.min_ev), double(settings.max_ev));
    return std::exp2(-ev + settings.compensation_ev)
      * (settings.target_luminance / .18) * (settings.key / 12.5);
  }

  auto ExpectSurfaceExposure(float luminance, double expected_gain,
    const Texture& reference, ExposureStateData& state) -> void
  {
    state = Read<ExposureStateData>(
      *probe->exposure->current_state->buffer, ResourceStates::kShaderResource);
    const auto domain = Read<FrameExposureData>(
      *probe->exposure->buffer, ResourceStates::kShaderResource);
    EXPECT_TRUE(std::isfinite(domain.pre_exposure));
    EXPECT_GT(domain.pre_exposure, 0);
    EXPECT_GT(state.latent_scale, 0);
    if (expected_gain == 0)
      EXPECT_EQ(state.displayed_scale, 0);
    else {
      ASSERT_GT(state.displayed_scale, 0);
      EXPECT_NEAR(std::log2(double(state.displayed_scale)),
        std::log2(expected_gain), 4e-4);
    }
    if (luminance >= 0) {
      const auto hdr = ReadFloatTexture(reference);
      ASSERT_EQ(hdr.size(), 1U);
      for (unsigned c = 0; c < 3; ++c)
        EXPECT_NEAR(double(hdr[0][c]) / domain.pre_exposure, luminance,
          double(luminance) * 2e-5 + 0x1p-120);
    }
    const auto mapped = ReadFloatTexture(
      *framebuffer->GetDescriptor().color_attachments.front().texture);
    ASSERT_EQ(mapped.size(), 1U);
    const double expected = std::clamp(
      std::clamp(double(luminance) * expected_gain, 0.0, 1.0) - .5 / 255, 0.0,
      1.0);
    for (unsigned c = 0; c < 3; ++c)
      EXPECT_NEAR(mapped[0][c], expected, 2e-4);
    EXPECT_EQ(mapped[0][3], 1);
  }

  auto ReferenceAdaptedGain(
    double previous, double target, double seconds) const -> double
  {
    const double q = std::log2(previous);
    const double destination = std::log2(target);
    const double radius = std::abs(destination - q);
    const double speed
      = target < previous ? settings.speed_up : settings.speed_down;
    if (radius == 0 || speed == 0 || seconds == 0)
      return previous;
    const double distance = settings.transition_distance;
    const double crossing = std::max(radius - distance, 0.0) / speed;
    const double remaining = seconds <= crossing ? radius - speed * seconds
                                                 : std::min(radius, distance)
        * std::exp(-speed * (seconds - crossing) / distance);
    return std::exp2(destination - (destination > q ? 1 : -1) * remaining);
  }

  auto SetSurface(data::MaterialDomain domain, float emission = 0,
    bool rejected_mask = false) -> void
  {
    data::pak::render::MaterialAssetDesc desc {};
    desc.material_domain = static_cast<std::uint8_t>(domain);
    desc.flags = data::pak::render::kMaterialFlag_NoTextureSampling
      | data::pak::render::kMaterialFlag_DoubleSided;
    if (domain == data::MaterialDomain::kMasked)
      desc.flags |= data::pak::render::kMaterialFlag_AlphaTest;
    desc.base_color[0] = desc.base_color[1] = desc.base_color[2] = 1;
    desc.base_color[3] = rejected_mask                ? 0
      : domain == data::MaterialDomain::kAlphaBlended ? .5F
                                                      : 1.0F;
    for (auto& value : desc.emissive_factor)
      value = data::HalfFloat { emission };
    desc.normal_scale = 1;
    desc.roughness = data::Unorm16 { 1 };
    desc.ambient_occlusion = data::Unorm16 { 1 };
    desc.uv_scale[0] = desc.uv_scale[1] = 1;
    mesh_node.GetRenderable().SetMaterialOverride(0, 0,
      std::make_shared<data::MaterialAsset>(
        data::AssetKey::FromVirtualPath("/Test/Exposure/Lit"
          + std::to_string(static_cast<int>(domain)) + "-"
          + std::to_string(++material_sequence) + ".omat"),
        desc, std::vector<data::ShaderReference> {}));
  }

  auto RenderSurface(bool forward, float ev, unsigned frames = 5) -> void
  {
    auto timing = engine::ModuleTimingData {};
    timing.game_delta_time = time::CanonicalDuration {
      std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::duration<double> { frame_delta_seconds })
    };
    frame.SetModuleTimingData(
      timing, engine::internal::EngineTagFactory::Get());
    probe->color.reset();
    probe->draws = 0;
    settings.manual_ev = ev;
    scene->GetEnvironment()
      ->TryGetSystem<scene::environment::PostProcessVolume>()
      ->SetExposureSettings(settings);
    scene->Update();
    scene->SyncObservers();
    for (unsigned warmup = 0; warmup < frames; ++warmup) {
      const auto slot = frame::Slot { sequence % 3 };
      Backend().BeginFrame(frame::SequenceNumber { sequence + 1 }, slot);
      frame.SetFrameSlot(slot, engine::internal::EngineTagFactory::Get());
      frame.SetFrameSequenceNumber(frame::SequenceNumber { ++sequence },
        engine::internal::EngineTagFactory::Get());
      renderer_->OnFrameStart(observer_ptr { &frame });
      auto facade = renderer_->ForOffscreenScene();
      facade.SetFrameSession({ .frame_slot = slot,
        .frame_sequence = frame::SequenceNumber { sequence },
        .delta_time_seconds = frame_delta_seconds });
      facade.SetSceneSource({ .scene = observer_ptr { scene.get() } });
      facade.SetViewIntent(Renderer::OffscreenSceneViewInput::FromCamera(
        "Lighting", ViewId { surface_view_id }, view, camera)
          .SetViewStateHandle(persistent_surface_state
              ? CompositionView::ViewStateHandle { surface_view_id }
              : CompositionView::kInvalidViewStateHandle)
          .SetExposureSourceViewId(surface_source_id)
          .SetExposureOverride(surface_exposure_override));
      facade.SetOutputTarget(
        { .framebuffer = observer_ptr { framebuffer.get() } });
      facade.SetPipeline(forward
          ? Renderer::OffscreenPipelineInput::Forward()
          : Renderer::OffscreenPipelineInput::Deferred());
      auto session = facade.Finalize();
      ASSERT_TRUE(session.has_value());
      ASSERT_TRUE(session->ExecuteInsideFrame(frame));
      renderer_->OnFrameEnd(observer_ptr { &frame });
      Backend().EndFrame(frame::SequenceNumber { sequence }, slot);
      WaitForQueueIdle();
    }
    ASSERT_EQ(probe->draws, expected_draws);
    ASSERT_NE(probe->color, nullptr);
    ASSERT_NE(probe->exposure, nullptr);
    const auto domain_data = Read<FrameExposureData>(
      *probe->exposure->buffer, ResourceStates::kShaderResource);
    if (verify_manual_p && settings.mode == engine::ExposureMode::kManual)
      EXPECT_EQ(domain_data.pre_exposure, std::exp2(-double(ev)));
  }

  auto RenderPublishedSurface(bool forward) -> void
  {
    scene->Update();
    scene->SyncObservers();
    auto timing = engine::ModuleTimingData {};
    timing.game_delta_time = time::CanonicalDuration {
      std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::duration<double> { frame_delta_seconds })
    };
    frame.SetModuleTimingData(
      timing, engine::internal::EngineTagFactory::Get());
    frame.SetScene(observer_ptr { scene.get() });
    const auto slot = frame::Slot { sequence % 3U };
    Backend().BeginFrame(frame::SequenceNumber { ++sequence }, slot);
    frame.SetFrameSlot(slot, engine::internal::EngineTagFactory::Get());
    frame.SetFrameSequenceNumber(frame::SequenceNumber { sequence },
      engine::internal::EngineTagFactory::Get());
    renderer_->OnFrameStart(observer_ptr { &frame });
    auto input
      = CompositionView::ForScene(ViewId { surface_view_id }, view, camera);
    input.view_state_handle
      = CompositionView::ViewStateHandle { surface_view_id };
    input.exposure_source_view_id = surface_source_id;
    input.render_settings.exposure = settings;
    ASSERT_NE(renderer_->PublishRuntimeCompositionView(frame,
                { .composition_view = input,
                  .render_target = observer_ptr { framebuffer.get() },
                  .composite_source = observer_ptr { framebuffer.get() } },
                forward ? ShadingMode::kForward : ShadingMode::kDeferred),
      kInvalidViewId);
    auto loop = co::testing::TestEventLoop {};
    co::Run(loop, [&]() -> co::Co<void> {
      co_await renderer_->OnPreRender(observer_ptr { &frame });
      co_await renderer_->OnRender(observer_ptr { &frame });
    });
    renderer_->OnFrameEnd(observer_ptr { &frame });
    Backend().EndFrame(frame::SequenceNumber { sequence }, slot);
    WaitForQueueIdle();
    ASSERT_EQ(probe->draws, expected_draws);
    ASSERT_NE(probe->exposure, nullptr);
  }

  auto TearDown() -> void override
  {
    if (probe) {
      probe->prepare = {};
      probe->inspect = {};
      probe->color.reset();
      probe->exposure.reset();
    }
    probe.reset();
    framebuffer.reset();
    ExposureGpuTest::TearDown();
  }

  std::shared_ptr<scene::Scene> scene;
  scene::SceneNode camera;
  scene::SceneNode mesh_node;
  scene::ExposureSettings settings;
  View view;
  std::shared_ptr<Framebuffer> framebuffer;
  std::shared_ptr<Probe> probe;
  engine::FrameContext frame;
  unsigned sequence = 0;
  unsigned material_sequence = 0;
  unsigned expected_draws = 1;
  std::uint32_t surface_view_id = 100U;
  ViewId surface_source_id = kInvalidViewId;
  std::optional<scene::ExposureSettings> surface_exposure_override;
  bool verify_manual_p = true;
  bool persistent_surface_state = true;
  float frame_delta_seconds = 0;
  DepthPrePassMode depth_mode = DepthPrePassMode::kOpaqueAndMasked;
  console::Console fixture_console;
};

NOLINT_TEST_F(
  ExposureLightingGpuTest, SceneLifecycleHdrStartupCutsSeedsAndPause)
{
  verify_manual_p = false;
  probe->prepare = [](RenderContext&) { };
  settings.mode = engine::ExposureMode::kAuto;
  settings.min_ev = -22;
  settings.max_ev = 30;
  settings.min_log_luminance = -24;
  settings.log_luminance_range = 56;
  settings.low_percentile = 0;
  settings.high_percentile = 1;
  settings.speed_up = .75F;
  settings.speed_down = .5F;
  ASSERT_TRUE(scene::ResolveExposureSettings(settings).has_value());
  std::shared_ptr<const Texture> reference;
  Format format = Format::kUnknown;
  probe->inspect = [&](const RenderContext& ctx,
                     const SceneTextureExtractRef& color, unsigned) {
    EXPECT_FLOAT_EQ(ctx.delta_time, frame_delta_seconds);
    format = color.texture->GetDescriptor().format;
    auto* owner
      = vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_);
    reference = owner->GetResolvedSceneColorTexture();
  };
  ExposureStateData state;
  unsigned cases = 0;
  unsigned checks = 0;
  for (const bool forward : { false, true })
    for (const float value : { 0.0F, 0x1p-24F, .25F, 0x1p32F }) {
      SCOPED_TRACE(
        ::testing::Message() << "forward=" << forward << " source=" << value);
      const float changed = value == 0 ? .25F
        : value == 0x1p32F             ? 0x1p30F
                                       : value * 4;
      const auto initial_material = MakeEmissiveMaterial(value);
      const auto changed_material = MakeEmissiveMaterial(changed);
      surface_view_id = 9000;
      frame_delta_seconds = 0;
      // Asset readiness is established on another history before first use.
      mesh_node.GetRenderable().SetMaterialOverride(0, 0, changed_material);
      ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 4));
      mesh_node.GetRenderable().SetMaterialOverride(0, 0, initial_material);
      ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 4));
      surface_view_id = 1600 + cases;
      const auto handle = CompositionView::ViewStateHandle { surface_view_id };
      const auto render = [&](float input, double expected) {
        ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 1));
        ASSERT_NE(reference, nullptr);
        ASSERT_NO_FATAL_FAILURE(
          ExpectSurfaceExposure(input, expected, *reference, state));
        ++checks;
      };
      const double initial = UniformReferenceGain(value);
      const double target = UniformReferenceGain(changed);
      const auto capture = !forward && value == 0x1p32F
        ? BeginOptionalCapture()
        : observer_ptr<FrameCaptureController> {};
      ASSERT_NO_FATAL_FAILURE(render(value, initial));
      if (capture)
        EXPECT_TRUE(capture->EndCapture());
      EXPECT_EQ(format, Format::kRGBA32Float);
      EXPECT_EQ(state.flags & 31U, value <= 0x1p-24F ? 27U : 15U);
      EXPECT_EQ(state.fallback_reason, 0U);
      EXPECT_EQ(Read<FrameExposureData>(
                  *probe->exposure->buffer, ResourceStates::kShaderResource)
                  .pre_exposure,
        1);
      mesh_node.GetRenderable().SetMaterialOverride(0, 0, changed_material);
      ASSERT_NO_FATAL_FAILURE(render(changed, initial));
      EXPECT_NEAR(
        std::log2(double(state.target_scale)), std::log2(target), 4e-4);
      frame_delta_seconds = .25F;
      const double adapted = ReferenceAdaptedGain(initial, target, .25);
      ASSERT_NO_FATAL_FAILURE(render(changed, adapted));
      frame_delta_seconds = 0;
      ASSERT_TRUE(renderer_
          ->NotifyViewDiscontinuity(handle, ViewDiscontinuity::kCameraCut)
          .has_value());
      ASSERT_NO_FATAL_FAILURE(render(changed, target));
      EXPECT_EQ(format, Format::kRGBA32Float);
      EXPECT_EQ(state.requested_generation, state.applied_generation);
      EXPECT_NE(state.applied_generation[0], 0U);
      const float seed_ev = value <= 0x1p-24F ? -24.0F : 31.0F;
      const auto seed = renderer_->QueueExposureTransition(
        handle, ExposureTransitionPolicy::kSeedFromEv100, seed_ev);
      ASSERT_TRUE(seed.has_value());
      ASSERT_TRUE(renderer_
          ->NotifyViewDiscontinuity(handle, ViewDiscontinuity::kCameraCut)
          .has_value());
      frame_delta_seconds = .25F;
      const double seeded = std::exp2(-double(seed_ev));
      ASSERT_NO_FATAL_FAILURE(render(changed, seeded));
      EXPECT_EQ(state.applied_generation[0], seed->generation);
      const double after_seed = ReferenceAdaptedGain(seeded, target, .25);
      ASSERT_NO_FATAL_FAILURE(render(changed, after_seed));
      const auto preserve = renderer_->QueueExposureTransition(
        handle, ExposureTransitionPolicy::kPreserve);
      ASSERT_TRUE(preserve.has_value());
      ASSERT_TRUE(renderer_
          ->NotifyViewDiscontinuity(handle, ViewDiscontinuity::kCameraCut)
          .has_value());
      ASSERT_NO_FATAL_FAILURE(render(changed, after_seed));
      EXPECT_EQ(state.applied_generation[0], preserve->generation);
      surface_view_id += 500;
      const auto startup = renderer_->QueueExposureTransition(
        CompositionView::ViewStateHandle { surface_view_id },
        ExposureTransitionPolicy::kSeedFromEv100, -8.0F);
      ASSERT_TRUE(startup.has_value());
      ASSERT_TRUE(renderer_->RetryExposureTransition(*startup).has_value());
      ASSERT_NO_FATAL_FAILURE(render(changed, 256));
      EXPECT_EQ(format, Format::kRGBA32Float);
      EXPECT_EQ(state.applied_generation[0], startup->generation);
      ASSERT_TRUE(renderer_->RetryExposureTransition(*startup).has_value());
      ASSERT_NO_FATAL_FAILURE(
        render(changed, ReferenceAdaptedGain(256, target, .25)));
      EXPECT_EQ(state.applied_generation[0], startup->generation);
      reference.reset();
      ++cases;
    }
  for (const bool forward : { false, true }) {
    SCOPED_TRACE(
      ::testing::Message() << "unmetered startup forward=" << forward);
    surface_view_id = forward ? 2501U : 2500U;
    frame_delta_seconds = 0;
    SetSurface(data::MaterialDomain::kOpaque, -1);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 1));
    ASSERT_NO_FATAL_FAILURE(ExpectSurfaceExposure(-1, 1, *reference, state));
    EXPECT_EQ(state.flags & 2U, 0U);
    SetSurface(data::MaterialDomain::kOpaque, .25F);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 1));
    ASSERT_NO_FATAL_FAILURE(ExpectSurfaceExposure(
      .25F, UniformReferenceGain(.25F), *reference, state));
    EXPECT_NE(state.flags & 2U, 0U);
    surface_view_id += 100;
    SetSurface(data::MaterialDomain::kOpaque, -1);
    const auto seed = renderer_->QueueExposureTransition(
      CompositionView::ViewStateHandle { surface_view_id },
      ExposureTransitionPolicy::kSeedFromEv100, 6.0F);
    ASSERT_TRUE(seed.has_value());
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 1));
    ASSERT_NO_FATAL_FAILURE(
      ExpectSurfaceExposure(-1, 0x1p-6, *reference, state));
    EXPECT_EQ(state.flags & 12U, 0U);
    EXPECT_NE(state.flags & 2U, 0U);
    EXPECT_EQ(state.applied_generation[0], seed->generation);
    frame_delta_seconds = .25F;
    SetSurface(data::MaterialDomain::kOpaque, .25F);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 1));
    ASSERT_NO_FATAL_FAILURE(ExpectSurfaceExposure(.25F,
      ReferenceAdaptedGain(0x1p-6, UniformReferenceGain(.25F), .25), *reference,
      state));
    checks += 4;
  }
  probe->inspect = {};
  reference.reset();
  RecordProperty("unmetered_startup_paths", 2);
  RecordProperty("scene_hdr_lifecycle_cases", cases);
  RecordProperty("scene_hdr_lifecycle_frames_checked", checks);
}

NOLINT_TEST_F(ExposureLightingGpuTest,
  SceneLifecycleModesPhysicalCameraZeroTargetAndLockedRange)
{
  verify_manual_p = false;
  probe->prepare = [](RenderContext&) { };
  std::shared_ptr<const Texture> reference;
  probe->inspect
    = [&](const RenderContext& ctx, const SceneTextureExtractRef&, unsigned) {
        EXPECT_FLOAT_EQ(ctx.delta_time, frame_delta_seconds);
        reference = vortex::testing::RendererPublicationProbe::GetSceneRenderer(
          *renderer_)
                      ->GetResolvedSceneColorTexture();
      };
  ExposureStateData state;
  unsigned cases = 0;
  unsigned checks = 0;
  for (const bool forward : { false, true })
    for (const bool orthographic : { false, true }) {
      SCOPED_TRACE(::testing::Message()
        << "forward=" << forward << " orthographic=" << orthographic);
      scene::CameraExposure physical {
        .aperture_f = 2, .shutter_rate = 4, .iso = 100
      };
      if (orthographic) {
        auto lens = std::make_unique<scene::OrthographicCamera>();
        lens->SetViewport(view.viewport);
        lens->SetExtents(-1, 1, -1, 1, .1F, 10);
        lens->SetExposure(physical);
        ASSERT_TRUE(camera.ReplaceCamera(std::move(lens)));
      } else {
        auto lens = std::make_unique<scene::PerspectiveCamera>();
        lens->SetViewport(view.viewport);
        lens->SetExposure(physical);
        ASSERT_TRUE(camera.ReplaceCamera(std::move(lens)));
      }
      settings = scene::ExposureSettings {};
      settings.key = 12.5F;
      settings.mode = engine::ExposureMode::kManual;
      settings.min_ev = -22;
      settings.max_ev = 30;
      settings.min_log_luminance = -24;
      settings.log_luminance_range = 56;
      settings.low_percentile = 0;
      settings.high_percentile = 1;
      settings.speed_up = settings.speed_down = .5F;
      ASSERT_TRUE(scene::ResolveExposureSettings(settings).has_value());
      frame_delta_seconds = 0;
      surface_view_id = 9000;
      SetSurface(data::MaterialDomain::kOpaque, .25F);
      ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 4));
      surface_view_id = 3000 + cases;
      const auto handle = CompositionView::ViewStateHandle { surface_view_id };
      const auto render = [&](float input, double expected, float ev = 0) {
        ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, ev, 1));
        ASSERT_NE(reference, nullptr);
        ASSERT_NO_FATAL_FAILURE(
          ExpectSurfaceExposure(input, expected, *reference, state));
        ++checks;
      };
      ASSERT_NO_FATAL_FAILURE(render(.25F, 1));
      const auto rejected_seed = renderer_->QueueExposureTransition(
        handle, ExposureTransitionPolicy::kSeedFromEv100, 10.0F);
      ASSERT_TRUE(rejected_seed.has_value());
      ASSERT_NO_FATAL_FAILURE(render(.25F, 0x1p-4, 4));
      EXPECT_NE(state.flags & (1U << 12U), 0U);
      EXPECT_EQ((state.flags >> 16U) & 15U, 1U);
      ASSERT_NO_FATAL_FAILURE(render(.25F, .25, 2));
      const auto rejected = renderer_->InspectExposureTransition(handle);
      ASSERT_TRUE(rejected.has_value());
      EXPECT_EQ(rejected->phase, ExposureTransitionPhase::kRejected);
      EXPECT_EQ(rejected->error, ExposureTransitionError::kNotAuto);
      settings.mode = engine::ExposureMode::kAuto;
      frame_delta_seconds = .5F;
      const double target = UniformReferenceGain(.25F);
      ASSERT_NO_FATAL_FAILURE(render(.25F, .25));
      // The Manual start crosses D=1.5 during this half-second step.
      const double adapted = ReferenceAdaptedGain(.25, target, .5);
      ASSERT_NO_FATAL_FAILURE(render(.25F, adapted));
      frame_delta_seconds = 0;
      SetSurface(data::MaterialDomain::kOpaque, 1);
      ASSERT_NO_FATAL_FAILURE(render(1, adapted));
      settings.compensation_ev = 1;
      ASSERT_NO_FATAL_FAILURE(render(1, adapted));
      EXPECT_NEAR(std::log2(double(state.target_scale)),
        std::log2(UniformReferenceGain(1)), 4e-4);
      settings.compensation_ev = 0;
      settings.speed_up = settings.speed_down = 0;
      frame_delta_seconds = 1;
      ASSERT_NO_FATAL_FAILURE(render(1, adapted));
      settings.mode = engine::ExposureMode::kManualCamera;
      frame_delta_seconds = 0;
      ASSERT_NO_FATAL_FAILURE(render(1, 0x1p-4));
      physical.iso = 400;
      if (orthographic)
        camera.GetCameraAs<scene::OrthographicCamera>()->get().SetExposure(
          physical);
      else
        camera.GetCameraAs<scene::PerspectiveCamera>()->get().SetExposure(
          physical);
      ASSERT_NO_FATAL_FAILURE(render(1, .25));
      settings.enabled = false;
      const auto disabled_reset = renderer_->QueueExposureTransition(
        handle, ExposureTransitionPolicy::kRemeter);
      ASSERT_TRUE(disabled_reset.has_value());
      ASSERT_NO_FATAL_FAILURE(render(1, 1));
      EXPECT_NE(state.flags & (1U << 12U), 0U);
      EXPECT_EQ((state.flags >> 16U) & 15U, 1U);
      settings.enabled = true;
      settings.mode = engine::ExposureMode::kAuto;
      settings.target_luminance = 0;
      ASSERT_NO_FATAL_FAILURE(render(1, 0));
      EXPECT_EQ(renderer_->InspectExposureTransition(handle)->phase,
        ExposureTransitionPhase::kRejected);
      EXPECT_NE(state.flags & 64U, 0U);
      settings.target_luminance = .18F;
      ASSERT_NO_FATAL_FAILURE(render(1, UniformReferenceGain(1)));
      settings.target_luminance = 0;
      ASSERT_NO_FATAL_FAILURE(render(1, 0));
      const auto zero_seed = renderer_->QueueExposureTransition(
        handle, ExposureTransitionPolicy::kSeedFromEv100, 6.0F);
      ASSERT_TRUE(zero_seed.has_value());
      ASSERT_NO_FATAL_FAILURE(render(1, 0));
      EXPECT_FLOAT_EQ(state.latent_scale, 0x1p-6F);
      SetSurface(data::MaterialDomain::kOpaque, -1);
      settings.target_luminance = .18F;
      ASSERT_NO_FATAL_FAILURE(render(-1, UniformReferenceGain(1)));
      EXPECT_EQ(state.flags & 12U, 0U);
      settings.min_ev = settings.max_ev = 4;
      const auto remeter = renderer_->QueueExposureTransition(
        handle, ExposureTransitionPolicy::kRemeter);
      ASSERT_TRUE(remeter.has_value());
      ASSERT_NO_FATAL_FAILURE(render(-1, 0x1p-4));
      EXPECT_EQ(state.applied_generation[0], remeter->generation);
      SetSurface(data::MaterialDomain::kOpaque, .25F);
      const auto locked_seed = renderer_->QueueExposureTransition(
        handle, ExposureTransitionPolicy::kSeedFromEv100, -5.0F);
      ASSERT_TRUE(locked_seed.has_value());
      ASSERT_NO_FATAL_FAILURE(render(.25F, 32));
      EXPECT_EQ(state.applied_generation[0], locked_seed->generation);
      ASSERT_NO_FATAL_FAILURE(render(.25F, 0x1p-4));
      settings.target_luminance = 0;
      ASSERT_NO_FATAL_FAILURE(render(.25F, 0));
      EXPECT_FLOAT_EQ(state.latent_scale, 0x1p-4F);
      ++cases;
    }
  probe->inspect = {};
  reference.reset();
  RecordProperty("scene_mode_camera_cases", cases);
  RecordProperty("scene_mode_frames_checked", checks);
}

NOLINT_TEST_F(ExposureLightingGpuTest,
  OffscreenLifetimeReleasePreservesReadersAndFreshReuse)
{
  verify_manual_p = false;
  probe->prepare = [](RenderContext&) { };
  settings.mode = engine::ExposureMode::kAuto;
  settings.low_percentile = 0;
  settings.high_percentile = 1;
  std::shared_ptr<const Texture> reference;
  internal::PreviousViewHistoryCache::CurrentState camera_state;
  probe->inspect
    = [&](const RenderContext& ctx, const SceneTextureExtractRef&, unsigned) {
        reference = vortex::testing::RendererPublicationProbe::GetSceneRenderer(
          *renderer_)
                      ->GetResolvedSceneColorTexture();
        const auto& resolved = *ctx.current_view.resolved_view;
        camera_state = { .view_matrix = resolved.ViewMatrix(),
          .projection_matrix = resolved.ProjectionMatrix(),
          .stable_projection_matrix = resolved.StableProjectionMatrix(),
          .inverse_view_projection_matrix = resolved.InverseViewProjection(),
          .pixel_jitter = resolved.PixelJitter(),
          .viewport = resolved.Viewport() };
      };
  ExposureStateData state;
  for (const bool forward : { false, true }) {
    surface_view_id = forward ? 4201U : 4200U;
    const auto handle = CompositionView::ViewStateHandle { surface_view_id };
    SetSurface(data::MaterialDomain::kOpaque, .25F);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0));
    const auto seed = renderer_->QueueExposureTransition(
      handle, ExposureTransitionPolicy::kSeedFromEv100, 4.0F);
    ASSERT_TRUE(seed.has_value());
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 1));
    ASSERT_NO_FATAL_FAILURE(
      ExpectSurfaceExposure(.25F, 0x1p-4, *reference, state));
    const auto retained_state = probe->exposure->current_state;
    const auto retained_source = reference;
    const auto retained_domain = Read<FrameExposureData>(
      *probe->exposure->buffer, ResourceStates::kShaderResource);
    auto& service = OwnedExposureService();
    auto& history
      = vortex::testing::RendererPublicationProbe::PreviousViewHistory(
        *renderer_);
    EXPECT_TRUE(history.TouchCurrent(handle, camera_state).previous_valid);
    ASSERT_TRUE(
      renderer_->ReleaseOffscreenViewState(ViewId { surface_view_id }, handle));
    EXPECT_TRUE(
      renderer_->ReleaseOffscreenViewState(ViewId { surface_view_id }, handle));
    EXPECT_FALSE(
      vortex::testing::RendererPublicationProbe::HasExposureViewState(
        service, handle));
    EXPECT_FALSE(history.TouchCurrent(handle, camera_state).previous_valid);
    const auto old_retry = renderer_->RetryExposureTransition(*seed);
    ASSERT_FALSE(old_retry.has_value());
    EXPECT_EQ(old_retry.error(), ExposureTransitionError::kUnknownToken);
    SetSurface(data::MaterialDomain::kOpaque, 1);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 1));
    ASSERT_NO_FATAL_FAILURE(
      ExpectSurfaceExposure(1, UniformReferenceGain(1), *reference, state));
    EXPECT_NE(probe->exposure->current_state->owner_lifetime, seed->lifetime);
    EXPECT_FALSE(renderer_->RetryExposureTransition(*seed).has_value());
    EXPECT_EQ(Read<ExposureStateData>(
                *retained_state->buffer, ResourceStates::kShaderResource)
                .displayed_scale,
      0x1p-4F);
    EXPECT_NE(retained_source, nullptr);
    const auto retained_pixel = ReadFloatTexture(*retained_source);
    ASSERT_EQ(retained_pixel.size(), 1U);
    for (unsigned channel = 0; channel < 3; ++channel)
      EXPECT_EQ(
        retained_pixel[0][channel], .25F * retained_domain.pre_exposure);
    reference.reset();
  }
  EXPECT_FALSE(renderer_->ReleaseOffscreenViewState(
    kInvalidViewId, CompositionView::ViewStateHandle { 4200U }));
  EXPECT_FALSE(renderer_->ReleaseOffscreenViewState(
    ViewId { 4200U }, CompositionView::kInvalidViewStateHandle));
  probe->inspect = {};
  reference.reset();
  RecordProperty("offscreen_release_paths", 2);
}

NOLINT_TEST_F(ExposureLightingGpuTest, PublishedSceneIdleBoundaryAndRecreation)
{
  verify_manual_p = false;
  probe->prepare = [](RenderContext&) { };
  settings.mode = engine::ExposureMode::kAuto;
  settings.low_percentile = 0;
  settings.high_percentile = 1;
  std::shared_ptr<const Texture> reference;
  probe->inspect
    = [&](const RenderContext&, const SceneTextureExtractRef&, unsigned) {
        reference = vortex::testing::RendererPublicationProbe::GetSceneRenderer(
          *renderer_)
                      ->GetResolvedSceneColorTexture();
      };
  ExposureStateData state;
  for (const bool forward : { false, true }) {
    SetSurface(data::MaterialDomain::kOpaque, .25F);
    surface_view_id = 9000;
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0));
    surface_view_id = forward ? 4301U : 4300U;
    const auto intent = ViewId { surface_view_id };
    const auto handle = CompositionView::ViewStateHandle { surface_view_id };
    ASSERT_NO_FATAL_FAILURE(RenderPublishedSurface(forward));
    ASSERT_NO_FATAL_FAILURE(ExpectSurfaceExposure(
      .25F, UniformReferenceGain(.25F), *reference, state));
    const auto seed = renderer_->QueueExposureTransition(
      handle, ExposureTransitionPolicy::kSeedFromEv100, 4.0F);
    ASSERT_TRUE(seed.has_value());
    ASSERT_NO_FATAL_FAILURE(RenderPublishedSurface(forward));
    const auto published = renderer_->ResolvePublishedRuntimeViewId(intent);
    ASSERT_NE(published, kInvalidViewId);
    ASSERT_NO_FATAL_FAILURE(
      ExpectSurfaceExposure(.25F, 0x1p-4, *reference, state));
    EXPECT_FALSE(renderer_->ReleaseOffscreenViewState(published, handle));
    EXPECT_EQ(renderer_->ResolvePublishedRuntimeViewId(intent), published);
    const auto retained = probe->exposure->current_state;
    frame.RemoveView(published);
    const auto last_seen = sequence;
    frame.SetFrameSequenceNumber(frame::SequenceNumber { last_seen + 60U },
      engine::internal::EngineTagFactory::Get());
    EXPECT_TRUE(renderer_->PruneStalePublishedRuntimeViews(frame).empty());
    EXPECT_EQ(renderer_->ResolvePublishedRuntimeViewId(intent), published);
    EXPECT_TRUE(vortex::testing::RendererPublicationProbe::HasExposureViewState(
      OwnedExposureService(), handle));
    frame.SetFrameSequenceNumber(frame::SequenceNumber { last_seen + 61U },
      engine::internal::EngineTagFactory::Get());
    const auto removed = renderer_->PruneStalePublishedRuntimeViews(frame);
    ASSERT_EQ(removed.size(), 1U);
    EXPECT_EQ(removed.front(), intent);
    EXPECT_EQ(renderer_->ResolvePublishedRuntimeViewId(intent), kInvalidViewId);
    EXPECT_FALSE(
      vortex::testing::RendererPublicationProbe::HasExposureViewState(
        OwnedExposureService(), handle));
    EXPECT_FALSE(renderer_->RetryExposureTransition(*seed).has_value());
    sequence = last_seen + 61U;
    SetSurface(data::MaterialDomain::kOpaque, 1);
    ASSERT_NO_FATAL_FAILURE(RenderPublishedSurface(forward));
    ASSERT_NO_FATAL_FAILURE(
      ExpectSurfaceExposure(1, UniformReferenceGain(1), *reference, state));
    EXPECT_NE(probe->exposure->current_state->owner_lifetime, seed->lifetime);
    EXPECT_EQ(Read<ExposureStateData>(
                *retained->buffer, ResourceStates::kShaderResource)
                .displayed_scale,
      0x1p-4F);
    renderer_->RemovePublishedRuntimeView(frame, intent);
    EXPECT_FALSE(
      vortex::testing::RendererPublicationProbe::HasExposureViewState(
        OwnedExposureService(), handle));
    reference.reset();
  }
  probe->inspect = {};
  RecordProperty("published_idle_boundary_paths", 2);
}

NOLINT_TEST_F(
  ExposureLightingGpuTest, ReplacedSceneRemetersUnlessExplicitPreserveOverrides)
{
  verify_manual_p = false;
  probe->prepare = [](RenderContext&) { };
  settings.mode = engine::ExposureMode::kAuto;
  settings.low_percentile = 0;
  settings.high_percentile = 1;
  std::shared_ptr<const Texture> reference;
  probe->inspect
    = [&](const RenderContext& ctx, const SceneTextureExtractRef&, unsigned) {
        EXPECT_FLOAT_EQ(ctx.delta_time, frame_delta_seconds);
        reference = vortex::testing::RendererPublicationProbe::GetSceneRenderer(
          *renderer_)
                      ->GetResolvedSceneColorTexture();
      };
  ExposureStateData state;
  const auto geometry = mesh_node.GetRenderable().GetGeometry();
  const auto replace_world = [&](float emission) {
    scene = std::make_shared<scene::Scene>("Replacement world", 8U);
    scene->SetEnvironment(std::make_unique<scene::SceneEnvironment>());
    auto& post = scene->GetEnvironment()
                   ->AddSystem<scene::environment::PostProcessVolume>();
    post.SetExposureSettings(settings);
    post.SetToneMapper(engine::ToneMapper::kNone);
    post.SetDisplayGamma(1);
    post.SetBloomIntensity(0);
    camera = scene->CreateNode("Camera");
    auto lens = std::make_unique<scene::PerspectiveCamera>();
    lens->SetViewport(view.viewport);
    ASSERT_TRUE(camera.AttachCamera(std::move(lens)));
    mesh_node = scene->CreateNode("Replacement radiance");
    mesh_node.GetRenderable().SetGeometry(geometry);
    SetSurface(data::MaterialDomain::kOpaque, emission);
    frame.SetScene(observer_ptr { scene.get() });
  };
  for (const bool forward : { false, true }) {
    frame_delta_seconds = 0;
    SetSurface(data::MaterialDomain::kOpaque, .25F);
    surface_view_id = 9000;
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0));
    surface_view_id = forward ? 4401U : 4400U;
    const auto handle = CompositionView::ViewStateHandle { surface_view_id };
    ASSERT_NO_FATAL_FAILURE(RenderPublishedSurface(forward));
    ASSERT_NO_FATAL_FAILURE(ExpectSurfaceExposure(
      .25F, UniformReferenceGain(.25F), *reference, state));
    const auto previous = scene;
    ASSERT_NO_FATAL_FAILURE(replace_world(4));
    ASSERT_NO_FATAL_FAILURE(RenderPublishedSurface(forward));
    ASSERT_NO_FATAL_FAILURE(
      ExpectSurfaceExposure(4, UniformReferenceGain(4), *reference, state));
    const auto reset = renderer_->InspectExposureTransition(handle);
    ASSERT_TRUE(reset.has_value());
    EXPECT_EQ(reset->request.policy, ExposureTransitionPolicy::kRemeter);
    EXPECT_EQ(state.applied_generation[0], reset->request.generation);
    const auto held_world = scene;
    const double held_gain = UniformReferenceGain(4);
    const auto preserve = renderer_->QueueExposureTransition(
      handle, ExposureTransitionPolicy::kPreserve);
    ASSERT_TRUE(preserve.has_value());
    ASSERT_NO_FATAL_FAILURE(replace_world(1));
    frame_delta_seconds = .25F;
    ASSERT_NO_FATAL_FAILURE(RenderPublishedSurface(forward));
    ASSERT_NO_FATAL_FAILURE(
      ExpectSurfaceExposure(1, held_gain, *reference, state));
    EXPECT_EQ(state.applied_generation[0], preserve->generation);
    ASSERT_NO_FATAL_FAILURE(RenderPublishedSurface(forward));
    ASSERT_NO_FATAL_FAILURE(ExpectSurfaceExposure(1,
      ReferenceAdaptedGain(held_gain, UniformReferenceGain(1), .25), *reference,
      state));
    renderer_->RemovePublishedRuntimeView(frame, ViewId { surface_view_id });
    reference.reset();
  }
  probe->inspect = {};
  RecordProperty("world_replacement_paths", 2);
}

auto ExposureLightingGpuTest::QualifySharedSceneLifecycle(bool forward) -> void
{
  verify_manual_p = false;
  probe->prepare = [](RenderContext&) { };
  settings.mode = engine::ExposureMode::kAuto;
  settings.low_percentile = 0;
  settings.high_percentile = 1;
  settings.speed_up = settings.speed_down = 1;
  SetSurface(data::MaterialDomain::kOpaque, .25F);
  const auto source_mesh = mesh_node;
  auto consumer_mesh = scene->CreateNode("Consumer radiance");
  consumer_mesh.GetRenderable().SetGeometry(
    mesh_node.GetRenderable().GetGeometry());
  consumer_mesh.GetTransform().SetLocalPosition({ 20, 0, 0 });
  mesh_node = consumer_mesh;
  SetSurface(data::MaterialDomain::kOpaque, 1);
  mesh_node = source_mesh;
  auto consumer_camera = scene->CreateNode("Consumer camera");
  auto lens = std::make_unique<scene::PerspectiveCamera>();
  lens->SetViewport(view.viewport);
  ASSERT_TRUE(consumer_camera.AttachCamera(std::move(lens)));
  consumer_camera.GetTransform().SetLocalPosition({ 20, 0, 0 });
  consumer_camera.GetCameraAs<scene::PerspectiveCamera>()->get().SetExposure(
    { .aperture_f = 2, .shutter_rate = 4, .iso = 100 });
  camera.GetCameraAs<scene::PerspectiveCamera>()->get().SetExposure(
    { .aperture_f = 2, .shutter_rate = 4, .iso = 100 });
  // Prepared metadata includes both meshes; per-view pixels verify visibility.
  expected_draws = 2;
  surface_view_id = 9000;
  ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0));
  const auto source_camera = camera;
  camera = consumer_camera;
  surface_view_id = 9001;
  ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0));
  ASSERT_GT(ReadFloatTexture(*probe->color, true).front()[0], 0);
  camera = source_camera;
  const std::array ids { ViewId { 4500U }, ViewId { 4501U } };
  const std::array handles { CompositionView::ViewStateHandle { 4500U },
    CompositionView::ViewStateHandle { 4501U } };
  const std::array cameras { camera, consumer_camera };
  auto consumer_output = CreateRegisteredTexture({ .width = 1,
    .height = 1,
    .format = Format::kRGBA32Float,
    .is_render_target = true,
    .initial_state = ResourceStates::kCommon });
  const std::array targets { framebuffer,
    Backend().CreateFramebuffer(
      FramebufferDesc {}.AddColorAttachment(consumer_output)) };
  auto consumer_settings = settings;
  // Deliberately different consumer settings must not alter the borrowed gain.
  consumer_settings.compensation_ev = 3;
  std::array<postprocess::ExposurePass::FrameLease, 2> exposures;
  std::array<std::shared_ptr<const Texture>, 2> references;
  std::vector<unsigned> rendered_order;
  auto* owner
    = vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_);
  probe->inspect = [&](const RenderContext& ctx,
                     const SceneTextureExtractRef& color, unsigned draws) {
    const unsigned index
      = ctx.current_view.view_state_handle == handles[0] ? 0 : 1;
    exposures[index] = color.exposure;
    references[index] = owner->GetResolvedSceneColorTexture();
    rendered_order.push_back(index);
    EXPECT_EQ(draws, 2U);
  };
  bool shared = true;
  bool continuity_frame = false;
  bool render_source = true;
  bool diagnostic = false;
  auto profile = CompositionView::ViewFeatureProfile::kDefault;
  unsigned frames_checked = 0;
  unsigned source_first = 0;
  unsigned consumer_first = 0;
  const auto run = [&](double source_gain, double consumer_gain,
                     float consumer_luminance = 1) {
    SCOPED_TRACE(::testing::Message()
      << "scene frame=" << frames_checked << " source=" << source_gain
      << " consumer=" << consumer_gain);
    const auto capture = frames_checked == 0 ? BeginOptionalCapture() : nullptr;
    rendered_order.clear();
    exposures = {};
    references = {};
    scene->Update();
    scene->SyncObservers();
    auto timing = engine::ModuleTimingData {};
    timing.game_delta_time = time::CanonicalDuration {
      std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::duration<double> { frame_delta_seconds })
    };
    frame.SetModuleTimingData(
      timing, engine::internal::EngineTagFactory::Get());
    const auto slot = frame::Slot { sequence % 3U };
    Backend().BeginFrame(frame::SequenceNumber { ++sequence }, slot);
    frame.SetFrameSlot(slot, engine::internal::EngineTagFactory::Get());
    frame.SetFrameSequenceNumber(frame::SequenceNumber { sequence },
      engine::internal::EngineTagFactory::Get());
    renderer_->OnFrameStart(observer_ptr { &frame });
    const bool reversed = frames_checked % 2 != 0;
    const auto publish = [&](unsigned index) {
      if (index == 0 && !render_source)
        return;
      auto input = CompositionView::ForScene(ids[index], view, cameras[index]);
      input.view_state_handle = handles[index];
      input.exposure_source_view_id
        = index == 1 && shared ? ids[0] : kInvalidViewId;
      input.render_settings.exposure
        = index == 0 ? settings : consumer_settings;
      input.render_settings.shader_debug_mode = index == 0 && diagnostic
        ? ShaderDebugMode::kWorldNormals
        : ShaderDebugMode::kDisabled;
      input.feature_profile
        = index == 1 ? profile : CompositionView::ViewFeatureProfile::kDefault;
      // Optional depth dependencies order the native runtime path without
      // replacing either view's color; depth extraction is not required here.
      if (render_source) {
        if (index == unsigned(reversed))
          input.produced_aux_outputs.push_back(
            { .id = CompositionView::AuxOutputId { 4502U },
              .kind = CompositionView::AuxOutputKind::kDepthTexture,
              .debug_name = "Shared exposure order" });
        else
          input.consumed_aux_outputs.push_back(
            { .id = CompositionView::AuxOutputId { 4502U },
              .kind = CompositionView::AuxOutputKind::kDepthTexture,
              .required = false });
      }
      ASSERT_NE(renderer_->PublishRuntimeCompositionView(frame,
                  { .composition_view = input,
                    .render_target = observer_ptr { targets[index].get() },
                    .composite_source = observer_ptr { targets[index].get() } },
                  forward ? ShadingMode::kForward : ShadingMode::kDeferred),
        kInvalidViewId);
    };
    const bool consumer_first_publication = reversed
      && renderer_->ResolvePublishedRuntimeViewId(ids[0]) != kInvalidViewId;
    ASSERT_NO_FATAL_FAILURE(publish(consumer_first_publication ? 1 : 0));
    ASSERT_NO_FATAL_FAILURE(publish(consumer_first_publication ? 0 : 1));
    auto loop = co::testing::TestEventLoop {};
    co::Run(loop, [&]() -> co::Co<void> {
      co_await renderer_->OnPreRender(observer_ptr { &frame });
      co_await renderer_->OnRender(observer_ptr { &frame });
    });
    renderer_->OnFrameEnd(observer_ptr { &frame });
    Backend().EndFrame(frame::SequenceNumber { sequence }, slot);
    WaitForQueueIdle();
    if (capture)
      EXPECT_TRUE(capture->EndCapture());
    if (render_source) {
      EXPECT_EQ(rendered_order,
        (reversed ? std::vector<unsigned> { 1, 0 }
                  : std::vector<unsigned> { 0, 1 }));
      reversed ? ++consumer_first : ++source_first;
    } else
      EXPECT_EQ(rendered_order, std::vector<unsigned> { 1 });
    for (unsigned index = 0; index < 2; ++index) {
      if (index == 0 && (!render_source || diagnostic))
        continue;
      ASSERT_NE(exposures[index], nullptr);
      ASSERT_NE(references[index], nullptr);
      const auto saved_target = framebuffer;
      framebuffer = targets[index];
      probe->exposure = exposures[index];
      ExposureStateData state;
      ExpectSurfaceExposure(index == 0 ? .25F : consumer_luminance,
        index == 0 ? source_gain : consumer_gain, *references[index], state);
      framebuffer = saved_target;
      if (index == 1)
        EXPECT_EQ((state.flags & 128U) != 0,
          shared
            || (continuity_frame && consumer_settings.enabled
              && consumer_settings.mode == engine::ExposureMode::kAuto));
    }
    continuity_frame = false;
    ++frames_checked;
  };
  const double auto_gain = UniformReferenceGain(.25F);
  ASSERT_NO_FATAL_FAILURE(
    run(auto_gain, 1)); // Unpublished owner's EV0 fallback.
  ASSERT_NO_FATAL_FAILURE(run(auto_gain, auto_gain));
  mesh_node = consumer_mesh;
  SetSurface(data::MaterialDomain::kOpaque, 4);
  mesh_node = source_mesh;
  ASSERT_NO_FATAL_FAILURE(run(auto_gain, auto_gain, 4));
  mesh_node = consumer_mesh;
  SetSurface(data::MaterialDomain::kOpaque, 1);
  mesh_node = source_mesh;
  settings.target_luminance = 0;
  ASSERT_NO_FATAL_FAILURE(run(0, auto_gain));
  ASSERT_NO_FATAL_FAILURE(run(0, 0));
  settings.target_luminance = .18F;
  ASSERT_NO_FATAL_FAILURE(run(auto_gain, 0));
  ASSERT_NO_FATAL_FAILURE(run(auto_gain, auto_gain));
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 3;
  ASSERT_NO_FATAL_FAILURE(run(.125, auto_gain));
  ASSERT_NO_FATAL_FAILURE(run(.125, .125));
  settings.mode = engine::ExposureMode::kManualCamera;
  ASSERT_NO_FATAL_FAILURE(run(.0625, .125));
  ASSERT_NO_FATAL_FAILURE(run(.0625, .0625));
  settings.enabled = false;
  ASSERT_NO_FATAL_FAILURE(run(1, .0625));
  ASSERT_NO_FATAL_FAILURE(run(1, 1));
  settings.enabled = true;
  settings.mode = engine::ExposureMode::kAuto;
  ASSERT_NO_FATAL_FAILURE(run(1, 1)); // Resume with last displayed fixed gain.
  const auto seed = renderer_->QueueExposureTransition(
    handles[0], ExposureTransitionPolicy::kSeedFromEv100, 4.0F);
  ASSERT_TRUE(seed.has_value());
  diagnostic = true;
  for (unsigned i = 0; i < 3; ++i)
    ASSERT_NO_FATAL_FAILURE(run(1, 1));
  const auto pending = renderer_->InspectExposureTransition(handles[0]);
  ASSERT_TRUE(pending.has_value());
  EXPECT_EQ(pending->phase, ExposureTransitionPhase::kQueued);
  diagnostic = false;
  ASSERT_NO_FATAL_FAILURE(run(.0625, 1));
  ASSERT_NO_FATAL_FAILURE(run(.0625, .0625));
  EXPECT_EQ(Read<ExposureStateData>(*exposures[0]->current_state->buffer,
              ResourceStates::kShaderResource)
              .applied_generation[0],
    seed->generation);
  for (const auto variant :
    { CompositionView::ViewFeatureProfile::kNoEnvironment,
      CompositionView::ViewFeatureProfile::kNoShadowing,
      CompositionView::ViewFeatureProfile::kNoVolumetrics }) {
    profile = variant;
    ASSERT_NO_FATAL_FAILURE(run(.0625, .0625));
  }
  profile = CompositionView::ViewFeatureProfile::kDefault;
  frame.RemoveView(renderer_->ResolvePublishedRuntimeViewId(ids[0]));
  render_source = false;
  sequence += 61U;
  ASSERT_NO_FATAL_FAILURE(run(.0625, .0625));
  EXPECT_TRUE(renderer_->PruneStalePublishedRuntimeViews(frame).empty());
  EXPECT_NE(renderer_->ResolvePublishedRuntimeViewId(ids[0]), kInvalidViewId);
  EXPECT_TRUE(vortex::testing::RendererPublicationProbe::HasExposureViewState(
    OwnedExposureService(), handles[0]));
  // The source is absent from this frame but still registered until explicit
  // removal.
  renderer_->RemovePublishedRuntimeView(ids[0]);
  shared = false;
  continuity_frame = true;
  consumer_settings.compensation_ev = 0;
  frame_delta_seconds = .25F;
  ASSERT_NO_FATAL_FAILURE(run(0, .0625));
  const double adapted
    = ReferenceAdaptedGain(.0625, UniformReferenceGain(1), .25);
  ASSERT_NO_FATAL_FAILURE(run(0, adapted));
  EXPECT_FALSE(renderer_->RetryExposureTransition(*seed).has_value());
  renderer_->RemovePublishedRuntimeView(frame, ids[1]);
  // Recreate the pair for each source-loss policy, retaining the same scene.
  for (unsigned variant = 0; variant < 6; ++variant) {
    SCOPED_TRACE(::testing::Message() << "source-loss variant=" << variant);
    settings.enabled = true;
    settings.mode = engine::ExposureMode::kManual;
    settings.manual_ev = 4;
    settings.target_luminance = .18F;
    consumer_settings = settings;
    consumer_settings.mode = variant == 2 ? engine::ExposureMode::kManual
      : variant == 3                      ? engine::ExposureMode::kManualCamera
                                          : engine::ExposureMode::kAuto;
    consumer_settings.manual_ev = 2;
    consumer_settings.enabled = variant != 4;
    consumer_settings.target_luminance = variant == 5 ? 0 : .18F;
    render_source = shared = true;
    frame_delta_seconds = 0;
    ASSERT_NO_FATAL_FAILURE(run(.0625, .0625));
    ASSERT_NO_FATAL_FAILURE(run(.0625, .0625));
    if (variant == 0) {
      settings.mode = engine::ExposureMode::kAuto;
      settings.target_luminance = 0;
      ASSERT_NO_FATAL_FAILURE(run(0, .0625));
      ASSERT_NO_FATAL_FAILURE(run(0, 0));
    }
    renderer_->RemovePublishedRuntimeView(frame, ids[0]);
    render_source = shared = false;
    continuity_frame = true;
    settings.target_luminance = .18F;
    frame_delta_seconds = .25F;
    const double first = variant == 0 || variant == 5 ? 0
      : variant == 2                                  ? .25
      : variant == 4                                  ? 1
                                                      : .0625;
    ASSERT_NO_FATAL_FAILURE(run(0, first));
    const double next = variant <= 1
      ? ReferenceAdaptedGain(.0625, UniformReferenceGain(1), .25)
      : first;
    ASSERT_NO_FATAL_FAILURE(run(0, next));
    renderer_->RemovePublishedRuntimeView(frame, ids[1]);
  }
  probe->inspect = {};
  RecordProperty("shared_scene_frames", frames_checked);
  RecordProperty("source_first_frames", source_first);
  RecordProperty("consumer_first_frames", consumer_first);
}

NOLINT_TEST_F(ExposureLightingGpuTest, SharedSceneLifecycleForward)
{
  QualifySharedSceneLifecycle(true);
}

NOLINT_TEST_F(ExposureLightingGpuTest, SharedSceneLifecycleDeferred)
{
  QualifySharedSceneLifecycle(false);
}

NOLINT_TEST_F(
  ExposureLightingGpuTest, OffscreenExposureOverridePreservesSceneIntent)
{
  verify_manual_p = false;
  probe->prepare = [](RenderContext&) { };
  settings.enabled = false;
  settings.speed_up = settings.speed_down = 7;
  frame_delta_seconds = .25F;
  std::shared_ptr<const Texture> reference;
  probe->inspect
    = [&](const RenderContext&, const SceneTextureExtractRef&, unsigned) {
        reference = vortex::testing::RendererPublicationProbe::GetSceneRenderer(
          *renderer_)
                      ->GetResolvedSceneColorTexture();
      };
  SetSurface(data::MaterialDomain::kOpaque, .25F);
  for (const bool forward : { false, true }) {
    SCOPED_TRACE(forward ? "forward" : "deferred");
    surface_view_id = forward ? 4801U : 4800U;
    auto local = scene::ExposureSettings {};
    local.key = 12.5F;
    local.mode = engine::ExposureMode::kManual;
    local.manual_ev = 4;
    local.compensation_ev = 1;
    surface_exposure_override = local;
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0));
    ASSERT_NE(reference, nullptr);
    auto state = ExposureStateData {};
    ASSERT_NO_FATAL_FAILURE(
      ExpectSurfaceExposure(.25F, .125, *reference, state));

    local.mode = engine::ExposureMode::kAuto;
    local.compensation_ev = 0;
    local.low_percentile = 0;
    local.high_percentile = 1;
    local.speed_up = .5F;
    local.speed_down = 1;
    surface_exposure_override = local;
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 1));
    ASSERT_NO_FATAL_FAILURE(
      ExpectSurfaceExposure(.25F, .125, *reference, state));
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 1));
    ASSERT_NO_FATAL_FAILURE(
      ExpectSurfaceExposure(.25F, .125 * std::exp2(.25), *reference, state));

    const auto& inherited
      = scene->GetEnvironment()
          ->TryGetSystem<scene::environment::PostProcessVolume>()
          ->GetExposureSettings();
    EXPECT_FALSE(inherited.enabled);
    EXPECT_EQ(inherited.speed_up, 7);
    EXPECT_EQ(inherited.speed_down, 7);
    surface_exposure_override.reset();
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 1));
    ASSERT_NO_FATAL_FAILURE(ExpectSurfaceExposure(.25F, 1, *reference, state));
    EXPECT_TRUE(renderer_->ReleaseOffscreenViewState(ViewId { surface_view_id },
      CompositionView::ViewStateHandle { surface_view_id }));
  }
}

NOLINT_TEST_F(
  ExposureLightingGpuTest, StatelessSceneAutoRemetersEveryInvocation)
{
  verify_manual_p = false;
  probe->prepare = [](RenderContext&) { };
  settings.mode = engine::ExposureMode::kAuto;
  settings.low_percentile = 0;
  settings.high_percentile = 1;
  settings.min_log_luminance = -24;
  settings.log_luminance_range = 56;
  settings.min_ev = -22;
  settings.max_ev = 30;
  const auto bright = MakeEmissiveMaterial(0x1p32F);
  frame_delta_seconds = .25F;
  std::shared_ptr<const Texture> reference;
  probe->inspect = [&](const RenderContext& ctx,
                     const SceneTextureExtractRef& color, unsigned) {
    EXPECT_EQ(ctx.current_view.view_state_handle,
      CompositionView::kInvalidViewStateHandle);
    EXPECT_EQ(color.texture->GetDescriptor().format, Format::kRGBA32Float);
    reference
      = vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_)
          ->GetResolvedSceneColorTexture();
  };
  persistent_surface_state = false;
  unsigned checks = 0;
  for (const bool forward : { false, true }) {
    // Warm the texture binding without introducing persistent exposure history.
    mesh_node.GetRenderable().SetMaterialOverride(0, 0, bright);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0));
    for (const float luminance :
      { 0.0F, 0x1p-24F, .25F, 4.0F, 0x1p32F, 1.0F }) {
      if (luminance == 0x1p32F)
        mesh_node.GetRenderable().SetMaterialOverride(0, 0, bright);
      else
        SetSurface(data::MaterialDomain::kOpaque, luminance);
      const auto capture
        = !forward && luminance == 0x1p32F ? BeginOptionalCapture() : nullptr;
      ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 1));
      if (capture)
        EXPECT_TRUE(capture->EndCapture());
      ASSERT_NE(reference, nullptr);
      ExposureStateData state;
      ASSERT_NO_FATAL_FAILURE(ExpectSurfaceExposure(
        luminance, UniformReferenceGain(luminance), *reference, state));
      const auto domain = Read<FrameExposureData>(
        *probe->exposure->buffer, ResourceStates::kShaderResource);
      EXPECT_EQ(domain.pre_exposure, 1);
      EXPECT_FALSE(
        vortex::testing::RendererPublicationProbe::HasExposureViewState(
          OwnedExposureService(), CompositionView::kInvalidViewStateHandle));
      EXPECT_FALSE(
        vortex::testing::RendererPublicationProbe::HasExposureViewState(
          OwnedExposureService(),
          CompositionView::ViewStateHandle { surface_view_id }));
      ++checks;
    }
    for (const float invalid :
      { -1.0F, std::numeric_limits<float>::quiet_NaN() }) {
      SetSurface(data::MaterialDomain::kOpaque, invalid);
      ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 1));
      ExposureStateData state;
      ASSERT_NO_FATAL_FAILURE(ExpectSurfaceExposure(-1, 1, *reference, state));
      EXPECT_EQ(state.flags & 12U, 0U);
      const auto report = Read<ExposureCompletedStatus>(
        *probe->exposure->current_state->status_buffer,
        ResourceStates::kCopySource);
      EXPECT_NE(report.flags & 16U, 0U);
      EXPECT_NE(report.first_failure_kind, 0U);
      settings.target_luminance = 0;
      ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 1));
      ASSERT_NO_FATAL_FAILURE(ExpectSurfaceExposure(-1, 0, *reference, state));
      settings.target_luminance = .18F;
      SetSurface(data::MaterialDomain::kOpaque, .25F);
      ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 1));
      ASSERT_NO_FATAL_FAILURE(ExpectSurfaceExposure(
        .25F, UniformReferenceGain(.25F), *reference, state));
      checks += 3;
    }
  }
  probe->inspect = {};
  RecordProperty("stateless_scene_frames", checks);
}

NOLINT_TEST_F(
  ExposureLightingGpuTest, SceneDeviceRecoveryRejectsPriorEligibility)
{
  verify_manual_p = false;
  probe->prepare = [](RenderContext&) { };
  settings.mode = engine::ExposureMode::kAuto;
  settings.low_percentile = 0;
  settings.high_percentile = 1;
  std::shared_ptr<const Texture> reference;
  SceneTextureExtractRef current;
  probe->inspect
    = [&](const RenderContext&, const SceneTextureExtractRef& color, unsigned) {
        current = color;
        reference = vortex::testing::RendererPublicationProbe::GetSceneRenderer(
          *renderer_)
                      ->GetResolvedSceneColorTexture();
      };
  for (const bool forward : { false, true }) {
    surface_view_id = forward ? 4601U : 4600U;
    const auto handle = CompositionView::ViewStateHandle { surface_view_id };
    SetSurface(data::MaterialDomain::kOpaque, .25F);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 8));
    EXPECT_EQ(current.texture->GetDescriptor().format, Format::kRGBA16Float);
    const auto old = renderer_->QueueExposureTransition(
      handle, ExposureTransitionPolicy::kSeedFromEv100, 4.0F);
    ASSERT_TRUE(old.has_value());
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 1));
    ExposureStateData state;
    ASSERT_NO_FATAL_FAILURE(
      ExpectSurfaceExposure(.25F, .0625, *reference, state));
    EXPECT_EQ(state.applied_generation[0], old->generation);
    // Queue newer intent before polling the completed old GPU submission.
    const auto newer = renderer_->QueueExposureTransition(
      handle, ExposureTransitionPolicy::kSeedFromEv100, 3.0F);
    ASSERT_TRUE(newer.has_value());
    ASSERT_TRUE(renderer_
        ->NotifyViewDiscontinuity(handle, ViewDiscontinuity::kDeviceRecovery)
        .has_value());
    SetSurface(data::MaterialDomain::kOpaque, 1);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 1));
    EXPECT_EQ(current.texture->GetDescriptor().format, Format::kRGBA32Float);
    ASSERT_NO_FATAL_FAILURE(ExpectSurfaceExposure(1, .125, *reference, state));
    EXPECT_EQ(state.applied_generation[0], newer->generation);
    EXPECT_EQ(renderer_->InspectExposureTransition(handle)->request, *newer);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 8));
    EXPECT_EQ(current.texture->GetDescriptor().format, Format::kRGBA16Float);
    ASSERT_TRUE(renderer_
        ->NotifyViewDiscontinuity(handle, ViewDiscontinuity::kDeviceRecovery)
        .has_value());
    SetSurface(data::MaterialDomain::kOpaque, 4);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 1));
    EXPECT_EQ(current.texture->GetDescriptor().format, Format::kRGBA32Float);
    ASSERT_NO_FATAL_FAILURE(
      ExpectSurfaceExposure(4, UniformReferenceGain(4), *reference, state));
    const auto domain = Read<FrameExposureData>(
      *probe->exposure->buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(domain.pre_exposure, 1);
    const auto recovery = renderer_->InspectExposureTransition(handle);
    ASSERT_TRUE(recovery.has_value());
    EXPECT_GT(recovery->request.generation, newer->generation);
    EXPECT_EQ(recovery->request.policy, ExposureTransitionPolicy::kRemeter);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 8));
    EXPECT_EQ(current.texture->GetDescriptor().format, Format::kRGBA16Float);
    EXPECT_EQ(renderer_->InspectExposureTransition(handle)->phase,
      ExposureTransitionPhase::kApplied);
    EXPECT_TRUE(
      renderer_->ReleaseOffscreenViewState(ViewId { surface_view_id }, handle));
  }
  probe->inspect = {};
  RecordProperty("device_recovery_scene_paths", 2);
}

NOLINT_TEST_F(ExposureLightingGpuTest,
  ExposureStatusReadbacksReuseWithinViewLifetimeAndInvalidateOnRecovery)
{
  using PublicationProbe = vortex::testing::RendererPublicationProbe;
  probe->prepare = [](RenderContext&) { };
  settings.mode = engine::ExposureMode::kAuto;
  settings.low_percentile = 0;
  settings.high_percentile = 1;
  surface_view_id = 4820U;
  const auto handle = CompositionView::ViewStateHandle { surface_view_id };
  SetSurface(data::MaterialDomain::kOpaque, .25F);
  scene->GetEnvironment()
    ->TryGetSystem<scene::environment::PostProcessVolume>()
    ->SetExposureSettings(settings);
  scene->Update();
  scene->SyncObservers();

  // Only normal frame-slot synchronization drives completion. No queue-idle
  // waits, mapped image inspection or explicit status polling drive reuse.
  const auto render_frame = [&]() -> void {
    const auto slot = frame::Slot { sequence % frame::kFramesInFlight.get() };
    Backend().BeginFrame(frame::SequenceNumber { ++sequence }, slot);
    frame.SetFrameSlot(slot, engine::internal::EngineTagFactory::Get());
    frame.SetFrameSequenceNumber(frame::SequenceNumber { sequence },
      engine::internal::EngineTagFactory::Get());
    renderer_->OnFrameStart(observer_ptr { &frame });
    auto facade = renderer_->ForOffscreenScene();
    facade.SetFrameSession({ .frame_slot = slot,
      .frame_sequence = frame::SequenceNumber { sequence },
      .delta_time_seconds = 0.0F });
    facade.SetSceneSource({ .scene = observer_ptr { scene.get() } });
    facade.SetViewIntent(Renderer::OffscreenSceneViewInput::FromCamera(
      "Status readback reuse", ViewId { surface_view_id }, view, camera)
        .SetViewStateHandle(handle));
    facade.SetOutputTarget(
      { .framebuffer = observer_ptr { framebuffer.get() } });
    facade.SetPipeline(Renderer::OffscreenPipelineInput::Forward());
    auto session = facade.Finalize();
    ASSERT_TRUE(session.has_value());
    ASSERT_TRUE(session->ExecuteInsideFrame(frame));
    renderer_->OnFrameEnd(observer_ptr { &frame });
    Backend().EndFrame(frame::SequenceNumber { sequence }, slot);
  };
  const auto same_owner = [](const auto& left, const auto& right) {
    return !left.owner_before(right) && !right.owner_before(left);
  };
  std::weak_ptr<const void> stable_pool;
  std::vector<PublicationProbe::ExposureReadbackIdentity> identities;
  unsigned reused_submissions = 0U;
  PostProcessService* service = nullptr;
  constexpr auto stable_frames = 4U * frame::kFramesInFlight.get();
  for (unsigned iteration = 0U; iteration < stable_frames; ++iteration) {
    SCOPED_TRACE(iteration);
    ASSERT_NO_FATAL_FAILURE(render_frame());
    auto* owner = PublicationProbe::GetSceneRenderer(*renderer_);
    ASSERT_NE(owner, nullptr);
    service = PublicationProbe::GetPostProcessService(*owner);
    ASSERT_NE(service, nullptr);
    const auto reuse
      = PublicationProbe::ExposureStatusReuseForView(*service, handle);
    ASSERT_FALSE(reuse.pool.expired());
    ASSERT_GT(reuse.pending.size() + reuse.available.size(), 0U);
    EXPECT_LE(reuse.pending.size() + reuse.available.size(),
      frame::kFramesInFlight.get());
    if (iteration == 0U) {
      stable_pool = reuse.pool;
    } else {
      EXPECT_TRUE(same_owner(stable_pool, reuse.pool));
    }
    for (const auto& current : reuse.pending) {
      ASSERT_FALSE(current.readback.expired());
      const auto seen
        = std::ranges::find_if(identities, [&](const auto& prior) {
            return same_owner(prior.readback, current.readback);
          });
      if (seen == identities.end()) {
        identities.push_back(current);
      } else if (current.frame_sequence > seen->frame_sequence) {
        // A new ticket on the same ownership identity proves actual reuse,
        // rather than observing one incomplete readback in successive frames.
        ++reused_submissions;
        seen->frame_sequence = current.frame_sequence;
      }
    }
  }
  EXPECT_GT(reused_submissions, 0U);
  EXPECT_LE(identities.size(), frame::kFramesInFlight.get());
  ASSERT_NE(service, nullptr);
  ASSERT_FALSE(stable_pool.expired());

  ASSERT_TRUE(renderer_
      ->NotifyViewDiscontinuity(handle, ViewDiscontinuity::kDeviceRecovery)
      .has_value());
  ASSERT_NO_FATAL_FAILURE(render_frame());
  const auto recovered
    = PublicationProbe::ExposureStatusReuseForView(*service, handle);
  EXPECT_TRUE(stable_pool.expired());
  ASSERT_FALSE(recovered.pool.expired());
  EXPECT_FALSE(same_owner(stable_pool, recovered.pool));
  EXPECT_LE(recovered.pending.size() + recovered.available.size(),
    frame::kFramesInFlight.get());

  ASSERT_TRUE(
    renderer_->ReleaseOffscreenViewState(ViewId { surface_view_id }, handle));
  EXPECT_TRUE(recovered.pool.expired());
  const auto removed
    = PublicationProbe::ExposureStatusReuseForView(*service, handle);
  EXPECT_TRUE(removed.pool.expired());
  EXPECT_TRUE(removed.pending.empty());
  EXPECT_TRUE(removed.available.empty());
  const auto remaining
    = PublicationProbe::ExposureStatusCounts(*service, handle);
  EXPECT_EQ(remaining.first, 0U);
  EXPECT_EQ(remaining.second, 0U);
  RecordProperty(
    "status_readback_stable_frames", static_cast<int>(stable_frames));
  RecordProperty(
    "status_readback_reused_submissions", static_cast<int>(reused_submissions));
  RecordProperty(
    "status_readback_unique_identities", static_cast<int>(identities.size()));
}

NOLINT_TEST_F(
  ExposureLightingGpuTest, SceneDelayedStatusCannotAuthorizeStalePrecision)
{
  using Probe = vortex::testing::RendererPublicationProbe;
  verify_manual_p = false;
  probe->prepare = [](RenderContext&) { };
  settings.mode = engine::ExposureMode::kAuto;
  settings.low_percentile = 0;
  settings.high_percentile = 1;
  settings.speed_up = settings.speed_down = 1;
  frame_delta_seconds = .25F;
  SceneTextureExtractRef current;
  std::shared_ptr<const Texture> reference;
  probe->inspect
    = [&](const RenderContext&, const SceneTextureExtractRef& color, unsigned) {
        current = color;
        reference
          = Probe::GetSceneRenderer(*renderer_)->GetResolvedSceneColorTexture();
      };
  unsigned delayed_frames = 0;
  for (const bool forward : { false, true }) {
    settings.compensation_ev = 0;
    surface_view_id = 9000;
    SetSurface(data::MaterialDomain::kOpaque, .25F);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0));
    surface_view_id = forward ? 4701U : 4700U;
    const auto handle = CompositionView::ViewStateHandle { surface_view_id };
    auto& service = OwnedExposureService();
    const auto seed = renderer_->QueueExposureTransition(
      handle, ExposureTransitionPolicy::kSeedFromEv100, 4.0F);
    ASSERT_TRUE(seed.has_value());
    Probe::ExposureStatusJobs held;
    double expected = .0625;
    ExposureStateData state;
    for (unsigned iteration = 0; iteration < 6; ++iteration) {
      ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 1));
      EXPECT_EQ(current.texture->GetDescriptor().format, Format::kRGBA32Float);
      ASSERT_NO_FATAL_FAILURE(
        ExpectSurfaceExposure(.25F, expected, *reference, state));
      EXPECT_EQ(state.applied_generation[0], seed->generation);
      EXPECT_EQ(renderer_->InspectExposureTransition(handle)->phase,
        ExposureTransitionPhase::kQueued);
      // Coalesce delayed delivery to the latest real completed ticket. No
      // status bytes are fabricated, and at most two ticket batches coexist.
      held = Probe::TakeExposureStatuses(service, handle);
      ASSERT_EQ(held.size(), 1U);
      ASSERT_NE(held.front().readback, nullptr);
      expected
        = ReferenceAdaptedGain(expected, UniformReferenceGain(.25F), .25);
      ++delayed_frames;
    }
    const auto eligible = Read<ExposureCompletedStatus>(
      *held.front().state->status_buffer, ResourceStates::kCopySource);
    EXPECT_EQ(eligible.flags & 7U, 5U);
    EXPECT_EQ(eligible.fp16_eligible_streak, 2U);
    Probe::RestoreExposureStatuses(service, handle, std::move(held));
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 1));
    EXPECT_EQ(current.texture->GetDescriptor().format, Format::kRGBA16Float);
    ASSERT_NO_FATAL_FAILURE(
      ExpectSurfaceExposure(.25F, expected, *reference, state));
    EXPECT_EQ(renderer_->InspectExposureTransition(handle)->phase,
      ExposureTransitionPhase::kApplied);

    // A delayed eligible packet cannot authorize a newer request generation.
    held = Probe::TakeExposureStatuses(service, handle);
    ASSERT_EQ(held.size(), 1U);
    const auto newer = renderer_->QueueExposureTransition(
      handle, ExposureTransitionPolicy::kSeedFromEv100, 8.0F);
    ASSERT_TRUE(newer.has_value());
    Probe::RestoreExposureStatuses(service, handle, std::move(held));
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 1));
    EXPECT_EQ(current.texture->GetDescriptor().format, Format::kRGBA32Float);
    ASSERT_NO_FATAL_FAILURE(
      ExpectSurfaceExposure(.25F, 0x1p-8, *reference, state));
    EXPECT_EQ(renderer_->InspectExposureTransition(handle)->request, *newer);
    EXPECT_EQ(renderer_->InspectExposureTransition(handle)->phase,
      ExposureTransitionPhase::kQueued);

    // Nor can it certify changed settings, even if polled before preparation.
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 8));
    EXPECT_EQ(current.texture->GetDescriptor().format, Format::kRGBA16Float);
    const auto before
      = Read<ExposureStateData>(*current.exposure->current_state->buffer,
        ResourceStates::kShaderResource);
    held = Probe::TakeExposureStatuses(service, handle);
    ASSERT_EQ(held.size(), 1U);
    settings.compensation_ev = 1;
    Probe::RestoreExposureStatuses(service, handle, std::move(held));
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 1));
    EXPECT_EQ(current.texture->GetDescriptor().format, Format::kRGBA32Float);
    ASSERT_NO_FATAL_FAILURE(ExpectSurfaceExposure(.25F,
      ReferenceAdaptedGain(
        before.displayed_scale, UniformReferenceGain(.25F), .25),
      *reference, state));
    EXPECT_NE(state.settings_revision, before.settings_revision);

    // Retirement must reject old readers even after the same handle is reused.
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 8));
    EXPECT_EQ(current.texture->GetDescriptor().format, Format::kRGBA16Float);
    held = Probe::TakeExposureStatuses(service, handle);
    ASSERT_EQ(held.size(), 1U);
    const auto retired_lifetime = held.front().lifetime;
    EXPECT_TRUE(
      renderer_->ReleaseOffscreenViewState(ViewId { surface_view_id }, handle));
    const auto replacement = renderer_->QueueExposureTransition(
      handle, ExposureTransitionPolicy::kSeedFromEv100, 6.0F);
    ASSERT_TRUE(replacement.has_value());
    EXPECT_NE(replacement->lifetime, retired_lifetime);
    settings.compensation_ev = 0;
    Probe::RestoreExposureStatuses(service, handle, std::move(held));
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 1));
    EXPECT_EQ(current.texture->GetDescriptor().format, Format::kRGBA32Float);
    ASSERT_NO_FATAL_FAILURE(
      ExpectSurfaceExposure(.25F, 0x1p-6, *reference, state));
    EXPECT_EQ(
      renderer_->InspectExposureTransition(handle)->request, *replacement);
    EXPECT_FALSE(renderer_->RetryExposureTransition(*newer).has_value());
    EXPECT_TRUE(
      renderer_->ReleaseOffscreenViewState(ViewId { surface_view_id }, handle));
  }
  probe->inspect = {};
  RecordProperty("delayed_scene_frames", delayed_frames);
  RecordProperty("stale_scene_status_cases", 6);
}

class ExposureProfilingOverheadTest : public ExposureLightingGpuTest {
protected:
  enum class BaselineRecipe { kControlled, kMixed, kIndoorOutdoor };
  auto MeasureReleaseBaseline(BaselineRecipe recipe) -> void;

  auto BackendConfigJson() const -> std::string override
  {
    return R"({"enable_debug_layer":false})";
  }
  auto AdditionalCapabilities() const -> CapabilitySet override
  {
    return RendererCapabilityFamily::kDiagnosticsAndProfiling;
  }
};

class ExposureIndoorOutdoorBenchmarkTest
  : public ExposureProfilingOverheadTest {
protected:
  auto AdditionalCapabilities() const -> CapabilitySet override
  {
    return ExposureProfilingOverheadTest::AdditionalCapabilities()
      | RendererCapabilityFamily::kShadowing;
  }
};
NOLINT_TEST_F(ExposureProfilingOverheadTest, DISABLED_ReleaseCollectionOnOff)
{
#ifndef NDEBUG
  FAIL() << "This performance measurement requires Release.";
#else
  // Reuse C01: the existing two-view accounting recipe. This run removes its
  // correctness readbacks and explicit queue drains from the timed loop.
  constexpr unsigned width = 1920U;
  constexpr unsigned height = 1080U;
  view.viewport = { .width = float(width), .height = float(height) };
  camera.GetCameraAs<scene::PerspectiveCamera>()->get().SetViewport(
    view.viewport);
  SetSurface(data::MaterialDomain::kOpaque, .25F);
  auto& sky
    = scene->GetEnvironment()->AddSystem<scene::environment::SkyAtmosphere>();
  sky.SetEnabled(true);
  sky.SetRayleighScatteringRgb({ 0, 0, 0 });
  sky.SetMieScatteringRgb({ 0, 0, 0 });
  sky.SetMieAbsorptionRgb({ 0, 0, 0 });
  sky.SetOzoneAbsorptionRgb({ 0, 0, 0 });
  auto& fog = scene->GetEnvironment()->AddSystem<scene::environment::Fog>();
  fog.SetEnabled(true);
  fog.SetEnableHeightFog(true);
  fog.SetEnableVolumetricFog(true);
  fog.SetExtinctionSigmaTPerMeter(0);
  ASSERT_EQ(
    fixture_console.Execute("vtx.volumetric_fog.temporal_reprojection false")
      .status,
    console::ExecutionStatus::kOk);
  ASSERT_EQ(fixture_console.Execute("vtx.volumetric_fog.jitter false").status,
            console::ExecutionStatus::kOk);
  probe->prepare = [](RenderContext& context) {
    context.current_view.with_atmosphere = true;
    context.current_view.with_height_fog = true;
  };
  scene->Update();
  scene->SyncObservers();
  std::array<std::shared_ptr<Framebuffer>, 2> targets;
  for (unsigned index = 0U; index < targets.size(); ++index) {
    auto output
      = CreateRegisteredTexture({ .width = width >> index,
                                  .height = height >> index,
                                  .format = Format::kRGBA32Float,
                                  .is_render_target = true,
                                  .initial_state = ResourceStates::kCommon });
    targets[index] = Backend().CreateFramebuffer(
      FramebufferDesc {}.AddColorAttachment(output));
  }
  auto timing = frame.GetModuleTimingData();
  timing.game_delta_time
    = time::CanonicalDuration { std::chrono::nanoseconds { 16'666'667 } };
  frame.SetModuleTimingData(timing, engine::internal::EngineTagFactory::Get());
  auto& diagnostics = renderer_->GetDiagnosticsService();
  diagnostics.SetEnabledFeatures(DiagnosticsFeature::kGpuTimeline);
  const auto directory = std::filesystem::path { OXYGEN_EXPOSURE_WORKSPACE }
    / "out/build-ninja/analysis/vortex/exposure-lightbench/slice51";
  std::filesystem::create_directories(directory);
  const auto recording = directory / "overhead-on-Release.gpu.json";
  using Clock = std::chrono::steady_clock;
  struct Sample {
    unsigned frame_sequence;
    double wall_ms;
    double frame_start_ms;
    double submission_ms;
  };
  const auto milliseconds = [](const auto duration) {
    return std::chrono::duration<double, std::milli>(duration).count();
  };
  constexpr auto sample_count = 3600U;
  auto require_fp16 = false;
  auto seen_views = std::array<bool, 2> {};
  probe->inspect = [&](const RenderContext& context,
                     const SceneTextureExtractRef& color, const unsigned draws) {
    const auto index = context.current_view.view_state_handle.get() - 500U;
    CHECK_F(index < seen_views.size());
    if (require_fp16) {
      CHECK_F(color.valid && color.texture != nullptr && draws == 1U);
      CHECK_F(color.texture->GetDescriptor().format == Format::kRGBA16Float);
    }
    seen_views[index] = true;
  };
  const auto render = [&](const bool start_recording) -> Sample {
    const auto started = Clock::now();
    seen_views.fill(false);
    const auto slot = frame::Slot { sequence % 3U };
    const auto frame_sequence = frame::SequenceNumber { ++sequence };
    Backend().BeginFrame(frame_sequence, slot);
    const auto after_frame_start = Clock::now();
    frame.SetFrameSlot(slot, engine::internal::EngineTagFactory::Get());
    frame.SetFrameSequenceNumber(frame_sequence,
                                 engine::internal::EngineTagFactory::Get());
    renderer_->OnFrameStart(observer_ptr { &frame });
    if (start_recording) {
      CHECK_F(diagnostics.RequestGpuTimelineRecording(recording, sample_count));
    }
    for (unsigned index = 0U; index < targets.size(); ++index) {
      auto sized_view = view;
      sized_view.viewport.width = float(width >> index);
      sized_view.viewport.height = float(height >> index);
      auto input = CompositionView::ForScene(ViewId { 500U + index },
                                             sized_view, camera);
      input.view_state_handle
        = CompositionView::ViewStateHandle { 500U + index };
      input.render_settings.exposure = settings;
      CHECK_F(renderer_->PublishRuntimeCompositionView(
                frame,
                { .composition_view = input,
                  .render_target = observer_ptr { targets[index].get() },
                  .composite_source = observer_ptr { targets[index].get() } })
              != kInvalidViewId);
    }
    auto loop = co::testing::TestEventLoop {};
    co::Run(loop, [&]() -> co::Co<void> {
      co_await renderer_->OnPreRender(observer_ptr { &frame });
      co_await renderer_->OnRender(observer_ptr { &frame });
      co_await renderer_->OnCompositing(observer_ptr { &frame });
    });
    if (require_fp16) {
      CHECK_F(seen_views[0] && seen_views[1]);
    }
    renderer_->OnFrameEnd(observer_ptr { &frame });
    Backend().EndFrame(frame_sequence, slot);
    const auto ended = Clock::now();
    return { sequence, milliseconds(ended - started),
             milliseconds(after_frame_start - started),
             milliseconds(ended - after_frame_start) };
  };
  for (const bool enabled : { false, true }) {
    diagnostics.SetGpuTimelineEnabled(enabled);
    require_fp16 = false;
    const auto warm_start = Clock::now();
    unsigned warm_frames = 0U;
    while (warm_frames < 300U
           || Clock::now() - warm_start < std::chrono::seconds { 10 }) {
      static_cast<void>(render(false));
      ++warm_frames;
    }
    std::vector<Sample> samples;
    samples.reserve(sample_count);
    require_fp16 = true;
    const auto sample_start = Clock::now();
    auto previous_end = sample_start;
    for (unsigned index = 0U; index < sample_count; ++index) {
      auto sample = render(enabled && index == 0U);
      const auto ended = Clock::now();
      sample.wall_ms = milliseconds(ended - previous_end);
      previous_end = ended;
      samples.push_back(sample);
    }
    const auto elapsed
      = std::chrono::duration<double>(Clock::now() - sample_start).count();
    // One ordinary frame publishes the final GPU capture. Its time is outside
    // the declared steady-state population, and no explicit GPU wait is added.
    const auto label = enabled ? "on" : "off";
    const auto finish_sample = render(false);
    RecordProperty(std::string { label } + "_finish_wall_ms",
      std::to_string(finish_sample.wall_ms));
    RecordProperty(std::string { label } + "_finish_submission_ms",
      std::to_string(finish_sample.submission_ms));
    const auto path
      = directory / (std::string { "overhead-" } + label + "-Release.csv");
    auto stream = std::ofstream(path);
    stream << "frame_seq,wall_ms,frame_start_ms,submission_ms\n";
    for (const auto& sample : samples) {
      stream << sample.frame_sequence << ',' << sample.wall_ms << ','
             << sample.frame_start_ms << ',' << sample.submission_ms << '\n';
    }
    stream.close();
    RecordProperty(std::string { label } + "_sample_count", sample_count);
    RecordProperty(std::string { label } + "_elapsed_seconds",
                   std::to_string(elapsed));
    RecordProperty(std::string { label } + "_raw_samples", path.string());
    EXPECT_GE(elapsed, 30.0);
  }
  auto stream = std::ifstream(recording);
  const auto report = nlohmann::json::parse(stream);
  EXPECT_EQ(report.at("complete"), true);
  EXPECT_EQ(report.at("timing_valid"), true);
  const auto adapter = Backend().GetCurrentDevice()->GetAdapterLuid();
  RecordProperty("adapter_luid_low", std::to_string(adapter.LowPart));
  RecordProperty("adapter_luid_high", std::to_string(adapter.HighPart));
  RecordProperty("workload",
                 "C01: 1920x1080 + 960x540, Manual EV0, emissive triangle, "
                 "vacuum atmosphere, zero-extinction fog, temporal off, fixed "
                 "dt 1/60, native offscreen");
  RecordProperty(
    "scope",
    "Collection/export on versus runtime off. Scope wrappers exist in both. "
    "Wall interval includes ordinary frame-start queue waits; submission span "
    "includes driver calls and recording enqueue work. The separate finalization "
    "frame includes writer drain. Not a presented FPS or exposure CPU-budget "
    "acceptance test.");
#endif
}
NOLINT_TEST_F(ExposureProfilingOverheadTest, DISABLED_ReleaseControlledBaseline)
{
  MeasureReleaseBaseline(BaselineRecipe::kControlled);
}

NOLINT_TEST_F(ExposureProfilingOverheadTest, DISABLED_ReleaseMixedBaseline)
{
  MeasureReleaseBaseline(BaselineRecipe::kMixed);
}

NOLINT_TEST_F(
  ExposureIndoorOutdoorBenchmarkTest, DISABLED_ReleaseIndoorOutdoorBaseline)
{
  MeasureReleaseBaseline(BaselineRecipe::kIndoorOutdoor);
}

auto ExposureProfilingOverheadTest::MeasureReleaseBaseline(
  const BaselineRecipe kind) -> void
{
#ifndef NDEBUG
  static_cast<void>(kind);
  FAIL() << "This performance measurement requires Release.";
#else
  const auto moving = kind == BaselineRecipe::kIndoorOutdoor;
  const auto mixed_scene = kind != BaselineRecipe::kControlled;
  const auto option = [](const char* name, std::string fallback) {
    char* value = nullptr;
    std::size_t size = 0U;
    const auto result = _dupenv_s(&value, &size, name);
    const auto owned
      = std::unique_ptr<char, decltype(&std::free)>(value, &std::free);
    if (result != 0) {
      throw std::runtime_error(std::string { "Cannot read " } + name);
    }
    return value ? std::string { value } : std::move(fallback);
  };
  const auto workload = option("OXYGEN_EXPOSURE_BASELINE_CASE",
    moving ? "I01" : (mixed_scene ? "M01" : "C01"));
  if (moving) {
    ASSERT_TRUE(workload == "I01" || workload == "I02")
      << "Indoor/outdoor baseline requires I01 or I02";
  } else if (mixed_scene) {
    ASSERT_TRUE(workload == "M01" || workload == "M02" || workload == "M03"
      || workload == "M04")
      << "Mixed baseline requires M01, M02, M03 or M04";
  } else {
    ASSERT_TRUE(workload == "C01" || workload == "C02")
      << "Controlled baseline requires C01 or C02";
  }
  const auto precision
    = option("OXYGEN_EXPOSURE_BASELINE_PRECISION", "production");
  ASSERT_TRUE(precision == "production" || precision == "fp32")
    << "OXYGEN_EXPOSURE_BASELINE_PRECISION must be production or fp32";
  const auto fp32_reference = precision == "fp32";
  const auto width_text = option("OXYGEN_EXPOSURE_TIMING_WIDTH", "1920");
  ASSERT_TRUE(width_text == "1920" || width_text == "3840")
    << "OXYGEN_EXPOSURE_TIMING_WIDTH must be 1920 or 3840";
  const auto frames_text = option("OXYGEN_EXPOSURE_BASELINE_FRAMES", "3600");
  unsigned sample_count = 0U;
  const auto parsed = std::from_chars(
    frames_text.data(), frames_text.data() + frames_text.size(), sample_count);
  ASSERT_TRUE(parsed.ec == std::errc {}
    && parsed.ptr == frames_text.data() + frames_text.size()
    && sample_count >= 1800U && sample_count <= 20000U)
    << "OXYGEN_EXPOSURE_BASELINE_FRAMES must be an integer in [1800, 20000]";
  if (moving) {
    ASSERT_EQ(sample_count % 1200U, 0U)
      << "Indoor/outdoor measurements require complete 1200-frame cycles";
  }
  const auto run_id = option("OXYGEN_EXPOSURE_BASELINE_RUN", "run01");
  ASSERT_TRUE(!run_id.empty() && run_id.size() <= 64U
    && std::ranges::all_of(run_id,
      [](const char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
          || (c >= '0' && c <= '9') || c == '-' || c == '_';
      }))
    << "OXYGEN_EXPOSURE_BASELINE_RUN must contain 1-64 ASCII letters, digits, "
       "hyphens or underscores";
  ASSERT_TRUE(CapturePath().empty())
    << "Native baseline measurements do not permit RenderDoc capture";
  const auto width = width_text == "1920" ? 1920U : 3840U;
  const auto height = width * 9U / 16U;
  const auto temporal
    = moving || workload == "C02" || workload == "M02" || workload == "M04";
  const auto forward
    = workload == "M03" || workload == "M04" || workload == "I02";
  const auto view_count
    = workload == "M01" || workload == "M04" || workload == "I01" ? 1U : 2U;
  const auto expected_format
    = fp32_reference || temporal ? Format::kRGBA32Float : Format::kRGBA16Float;
  const auto directory = std::filesystem::path { OXYGEN_EXPOSURE_WORKSPACE }
    / "out/build-ninja/analysis/vortex/exposure-lightbench/slice51";
  std::filesystem::create_directories(directory);
  const auto stem
    = std::string { moving ? "indoor-"
                           : (mixed_scene ? "mixed-" : "controlled-") }
    + workload + "-" + width_text + (fp32_reference ? "-fp32" : "") + "-"
    + run_id + "-Release";
  const auto gpu_path = directory / (stem + ".gpu.json");
  const auto cpu_path = directory / (stem + ".cpu.csv");
  const auto manifest_path = directory / (stem + ".json");
  ASSERT_FALSE(std::filesystem::exists(gpu_path)
    || std::filesystem::exists(cpu_path)
    || std::filesystem::exists(manifest_path))
    << "Choose a new run ID; existing baseline evidence is not overwritten";

  view.viewport = { .width = float(width), .height = float(height) };
  auto cameras = std::array { camera, camera };
  if (mixed_scene) {
    ASSERT_TRUE(scene->DestroyNode(mesh_node));
    ASSERT_TRUE(scene->DestroyNode(camera));
    const auto recipe
      = vortex::testing::PopulateMixedExposureBenchmarkScene(*scene, moving);
    cameras = { recipe.main_camera, recipe.secondary_camera };
    for (unsigned index = 0U; index < view_count; ++index) {
      auto& lens
        = cameras[index].GetCameraAs<scene::PerspectiveCamera>()->get();
      auto viewport = view.viewport;
      viewport.width = float(width >> index);
      viewport.height = float(height >> index);
      lens.SetViewport(viewport);
      lens.SetAspectRatio(float(width) / float(height));
    }
    settings = scene::ExposureSettings {};
    settings.key = 12.5F;
    if (workload == "M03") {
      settings.mode = engine::ExposureMode::kManual;
      settings.manual_ev = 14.5F;
    }
    scene->GetEnvironment()
      ->TryGetSystem<scene::environment::PostProcessVolume>()
      ->SetExposureSettings(settings);
  } else {
    // Preserve the historical controlled camera, including aspect 1 and FOV 1.
    auto& lens = camera.GetCameraAs<scene::PerspectiveCamera>()->get();
    ASSERT_FLOAT_EQ(lens.GetAspectRatio(), 1.0F);
    ASSERT_FLOAT_EQ(lens.GetFieldOfView(), 1.0F);
    lens.SetViewport(view.viewport);
    SetSurface(data::MaterialDomain::kOpaque, .25F);
    auto& sky
      = scene->GetEnvironment()->AddSystem<scene::environment::SkyAtmosphere>();
    sky.SetEnabled(true);
    sky.SetRayleighScatteringRgb({ 0, 0, 0 });
    sky.SetMieScatteringRgb({ 0, 0, 0 });
    sky.SetMieAbsorptionRgb({ 0, 0, 0 });
    sky.SetOzoneAbsorptionRgb({ 0, 0, 0 });
    auto& fog = scene->GetEnvironment()->AddSystem<scene::environment::Fog>();
    fog.SetEnabled(true);
    fog.SetEnableHeightFog(true);
    fog.SetEnableVolumetricFog(true);
    fog.SetExtinctionSigmaTPerMeter(0);
  }
  ASSERT_EQ(
    fixture_console
      .Execute(temporal ? "vtx.volumetric_fog.temporal_reprojection true"
                        : "vtx.volumetric_fog.temporal_reprojection false")
      .status,
    console::ExecutionStatus::kOk);
  ASSERT_EQ(fixture_console.Execute("vtx.volumetric_fog.jitter false").status,
    console::ExecutionStatus::kOk);
  const auto quality_commands = std::array {
    "vtx.sky_atmosphere.aerial_perspective_lut.width 64",
    "vtx.sky_atmosphere.aerial_perspective_lut.depth_resolution 32",
    "vtx.sky_atmosphere.aerial_perspective_lut.depth_km 96.0",
    "vtx.sky_atmosphere.aerial_perspective_lut.sample_count_max_per_slice 2.0",
    "vtx.volumetric_fog.history_miss_supersample_count 4",
    "vtx.volumetric_fog.directional_shadows true"
  };
  for (const auto* command : quality_commands) {
    ASSERT_EQ(
      fixture_console.Execute(command).status, console::ExecutionStatus::kOk)
      << command;
  }
  probe->prepare = [](RenderContext& context) {
    context.current_view.with_atmosphere = true;
    context.current_view.with_height_fog = true;
  };
  scene->Update();
  scene->SyncObservers();

  auto& backend = static_cast<ExposureFailureGraphics&>(Backend());
  backend.track_resources = true;
  // GetResourceAllocationInfo is reserved for the two untimed snapshots.
  backend.account_texture_allocations = false;
  auto targets = std::vector<std::shared_ptr<Framebuffer>>(view_count);
  for (unsigned index = 0U; index < targets.size(); ++index) {
    auto output = CreateRegisteredTexture({ .width = width >> index,
      .height = height >> index,
      .format = Format::kRGBA32Float,
      .debug_name = std::string { mixed_scene ? "MixedBaseline.Output"
                                              : "ControlledBaseline.Output" }
        + std::to_string(index),
      .is_render_target = true,
      .initial_state = ResourceStates::kCommon });
    targets[index] = Backend().CreateFramebuffer(
      FramebufferDesc {}.AddColorAttachment(output));
  }
  const auto snapshot = [&]() {
    auto textures = nlohmann::json::array();
    auto buffers = nlohmann::json::array();
    auto unique = std::unordered_set<ID3D12Resource*> {};
    std::uint64_t texture_bytes = 0U;
    std::uint64_t buffer_bytes = 0U;
    for (const auto& weak : backend.tracked_textures) {
      const auto texture = weak.lock();
      if (!texture) {
        continue;
      }
      auto* native = texture->GetNativeResource()->AsPointer<ID3D12Resource>();
      if (!native || !unique.insert(native).second) {
        continue;
      }
      const auto shape = native->GetDesc();
      const auto bytes = backend.GetCurrentDevice()
                           ->GetResourceAllocationInfo(0U, 1U, &shape)
                           .SizeInBytes;
      const auto& desc = texture->GetDescriptor();
      textures.push_back({ { "name", desc.debug_name },
        { "format", static_cast<unsigned>(desc.format) },
        { "width", desc.width }, { "height", desc.height },
        { "depth", desc.depth }, { "array_layers", desc.array_size },
        { "mips", desc.mip_levels }, { "samples", desc.sample_count },
        { "placement_bytes", bytes },
        { "exposure_hdr",
          ExposureFailureGraphics::IsExposureHdrTexture(desc.debug_name) } });
      texture_bytes += bytes;
    }
    for (const auto& weak : backend.tracked_buffers) {
      const auto buffer = weak.lock();
      if (!buffer) {
        continue;
      }
      auto* native = buffer->GetNativeResource()->AsPointer<ID3D12Resource>();
      if (!native || !unique.insert(native).second) {
        continue;
      }
      const auto shape = native->GetDesc();
      const auto bytes = backend.GetCurrentDevice()
                           ->GetResourceAllocationInfo(0U, 1U, &shape)
                           .SizeInBytes;
      const auto& desc = buffer->GetDescriptor();
      buffers.push_back({ { "name", desc.debug_name },
        { "logical_bytes", desc.size_bytes }, { "placement_bytes", bytes } });
      buffer_bytes += bytes;
    }
    const auto* owner
      = vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_);
    const auto [families, leased]
      = vortex::testing::RendererPublicationProbe::SceneTexturePoolCounts(
        *owner);
    return nlohmann::json { { "textures", std::move(textures) },
      { "buffers", std::move(buffers) },
      { "texture_placement_bytes", texture_bytes },
      { "buffer_placement_bytes", buffer_bytes },
      { "texture_creation_count", backend.tracked_textures.size() },
      { "buffer_creation_count", backend.tracked_buffers.size() },
      { "scene_texture_families", families },
      { "leased_scene_texture_families", leased } };
  };

  auto timing = frame.GetModuleTimingData();
  constexpr auto simulation_dt_ns = 16'666'667;
  timing.game_delta_time
    = time::CanonicalDuration { std::chrono::nanoseconds { simulation_dt_ns } };
  frame.SetModuleTimingData(timing, engine::internal::EngineTagFactory::Get());
  auto& diagnostics = renderer_->GetDiagnosticsService();
  diagnostics.SetHdrFp32ReferenceEnabled(fp32_reference);
  diagnostics.SetEnabledFeatures(DiagnosticsFeature::kGpuTimeline);
  diagnostics.SetGpuTimelineEnabled(true);
  using Clock = std::chrono::steady_clock;
  struct Sample {
    unsigned frame_sequence;
    double wall_ms;
    double frame_start_ms;
    double submission_ms;
    std::array<unsigned, 2> formats;
    std::array<unsigned, 2> draws;
    std::array<unsigned, 2> path_phases;
    std::array<unsigned, 2> history_reprojected;
    std::array<unsigned, 2> history_reset;
  };
  const auto milliseconds = [](const auto duration) {
    return std::chrono::duration<double, std::milli>(duration).count();
  };
  auto require_ready = false;
  auto seen_views = std::array<bool, 2> {};
  auto formats = std::array<unsigned, 2> {};
  auto draw_counts = std::array<unsigned, 2> {};
  auto warm_draw_counts = std::array<unsigned, 2> {};
  auto path_phases = std::array<unsigned, 2> {};
  auto history_reprojected = std::array<unsigned, 2> {};
  auto history_reset = std::array<unsigned, 2> {};
  auto capture_endpoint = false;
  std::array<SceneTextureExtractRef, 2> endpoint_hdr;
  std::array<postprocess::ExposurePass::FrameLease, 2> endpoint_exposure;
  const auto update_path = [&](const unsigned sample_frame) {
    for (unsigned index = 0U; index < view_count; ++index) {
      const auto phase = (sample_frame + index * 600U) % 1200U;
      path_phases[index] = phase;
      auto progress = 0.0F;
      if (phase >= 300U && phase < 600U) {
        progress = float(phase - 300U) / 300.0F;
      } else if (phase >= 600U && phase < 900U) {
        progress = 1.0F;
      } else if (phase >= 900U) {
        progress = 1.0F - float(phase - 900U) / 300.0F;
      }
      progress = progress * progress * (3.0F - 2.0F * progress);
      const auto position = glm::vec3 { index == 0U ? 2.25F : 2.6F,
        -8.0F + 9.5F * progress, 1.2F };
      cameras[index].GetTransform().SetLocalPosition(position);
      const auto camera_view = glm::lookAt(
        position, glm::vec3 { -.75F, 0.0F, .25F }, space::move::Up);
      cameras[index].GetTransform().SetLocalRotation(
        glm::quat_cast(glm::inverse(camera_view)));
    }
    scene->Update();
    scene->SyncObservers();
  };
  probe->inspect = [&](const RenderContext& context,
                     const SceneTextureExtractRef& color,
                     const unsigned draws) {
    const auto index = context.current_view.view_state_handle.get() - 500U;
    CHECK_F(index < seen_views.size());
    CHECK_F(!seen_views[index]);
    seen_views[index] = true;
    draw_counts[index] = draws;
    if (capture_endpoint) {
      CHECK_F(color.valid && color.texture != nullptr);
      endpoint_hdr[index] = color;
      endpoint_exposure[index] = context.current_view.frame_exposure;
    }
    if (require_ready) {
      CHECK_F(color.valid && color.texture != nullptr && color.exposure);
      if (moving) {
        CHECK_F(draws > 0U && draws <= 11U);
        const auto* owner
          = vortex::testing::RendererPublicationProbe::GetSceneRenderer(
            *renderer_);
        const auto* shadows
          = vortex::testing::RendererPublicationProbe::GetShadowService(*owner);
        CHECK_NOTNULL_F(shadows);
        CHECK_NOTNULL_F(
          shadows->InspectShadowSurface(context.current_view.view_id));
        CHECK_F(shadows->ResolveShadowFrameSlot(context.current_view.view_id)
          != kInvalidShaderVisibleIndex);
        const auto& shadow_state = shadows->GetLastRenderState();
        CHECK_F(
          shadow_state.frame_sequence == frame::SequenceNumber { sequence });
        CHECK_F(shadow_state.rendered_cascade_count > 0U);
        CHECK_F(shadow_state.shadow_caster_draw_count > 0U);
        const auto& environment = owner->GetLastEnvironmentLightingState();
        CHECK_F(environment.stage14_integrated_light_scattering_valid);
        CHECK_F(environment.stage14_volumetric_fog_executed);
        CHECK_F(environment.stage14_volumetric_fog_temporal_history_requested);
        history_reprojected[index]
          = environment
              .stage14_volumetric_fog_temporal_history_reprojection_executed
          ? 1U
          : 0U;
        history_reset[index]
          = environment.stage14_volumetric_fog_temporal_history_reset ? 1U : 0U;
        const auto phase = path_phases[index];
        if ((phase >= 10U && phase < 300U) || (phase >= 610U && phase < 900U)) {
          CHECK_F(history_reprojected[index] == 1U);
        }
      } else if (mixed_scene) {
        CHECK_F(draws > 0U && draws <= 5U);
        CHECK_F(draws == warm_draw_counts[index]);
      } else {
        CHECK_F(draws == 1U);
      }
      CHECK_F(color.texture->GetDescriptor().width == width >> index);
      CHECK_F(color.texture->GetDescriptor().height == height >> index);
      if (!mixed_scene || fp32_reference) {
        CHECK_F(color.texture->GetDescriptor().format == expected_format);
      }
    }
    formats[index] = color.texture
      ? static_cast<unsigned>(color.texture->GetDescriptor().format)
      : static_cast<unsigned>(Format::kUnknown);
  };
  const auto render = [&](const bool start_recording,
                        const unsigned sample_frame = 0U) -> Sample {
    const auto started = Clock::now();
    seen_views.fill(false);
    const auto slot = frame::Slot { sequence % 3U };
    const auto frame_sequence = frame::SequenceNumber { ++sequence };
    Backend().BeginFrame(frame_sequence, slot);
    const auto after_frame_start = Clock::now();
    frame.SetFrameSlot(slot, engine::internal::EngineTagFactory::Get());
    frame.SetFrameSequenceNumber(
      frame_sequence, engine::internal::EngineTagFactory::Get());
    if (moving) {
      update_path(sample_frame);
    }
    renderer_->OnFrameStart(observer_ptr { &frame });
    if (start_recording) {
      CHECK_F(diagnostics.RequestGpuTimelineRecording(gpu_path, sample_count));
    }
    for (unsigned index = 0U; index < targets.size(); ++index) {
      auto sized_view = view;
      sized_view.viewport.width = float(width >> index);
      sized_view.viewport.height = float(height >> index);
      auto input = CompositionView::ForScene(
        ViewId { 500U + index }, sized_view, cameras[index]);
      input.view_state_handle
        = CompositionView::ViewStateHandle { 500U + index };
      input.render_settings.exposure = settings;
      CHECK_F(renderer_->PublishRuntimeCompositionView(frame,
                { .composition_view = input,
                  .render_target = observer_ptr { targets[index].get() },
                  .composite_source = observer_ptr { targets[index].get() } },
                forward ? ShadingMode::kForward : ShadingMode::kDeferred)
        != kInvalidViewId);
    }
    auto loop = co::testing::TestEventLoop {};
    co::Run(loop, [&]() -> co::Co<void> {
      co_await renderer_->OnPreRender(observer_ptr { &frame });
      co_await renderer_->OnRender(observer_ptr { &frame });
      co_await renderer_->OnCompositing(observer_ptr { &frame });
    });
    if (require_ready) {
      for (unsigned index = 0U; index < view_count; ++index) {
        CHECK_F(seen_views[index]);
      }
    }
    renderer_->OnFrameEnd(observer_ptr { &frame });
    Backend().EndFrame(frame_sequence, slot);
    const auto ended = Clock::now();
    return { sequence, milliseconds(ended - started),
      milliseconds(after_frame_start - started),
      milliseconds(ended - after_frame_start), formats, draw_counts,
      path_phases, history_reprojected, history_reset };
  };
  const auto warm_start = Clock::now();
  unsigned warm_frames = 0U;
  const auto minimum_warm_frames = moving ? 1200U : 300U;
  while (warm_frames < minimum_warm_frames
    || Clock::now() - warm_start < std::chrono::seconds { 10 }
    || (moving && warm_frames % 1200U != 0U)) {
    static_cast<void>(render(false, moving ? warm_frames : 0U));
    ++warm_frames;
  }
  const auto warm_seconds
    = std::chrono::duration<double>(Clock::now() - warm_start).count();
  warm_draw_counts = draw_counts;
  const auto before = snapshot();
  auto samples = std::vector<Sample> {};
  samples.reserve(sample_count);
  require_ready = true;
  const auto sample_start = Clock::now();
  auto previous_end = sample_start;
  for (unsigned index = 0U; index < sample_count; ++index) {
    auto sample = render(index == 0U, index);
    const auto ended = Clock::now();
    sample.wall_ms = milliseconds(ended - previous_end);
    previous_end = ended;
    samples.push_back(sample);
  }
  const auto sample_seconds
    = std::chrono::duration<double>(Clock::now() - sample_start).count();
  const auto after = snapshot();
  // Resolve the last recording outside the declared sample population. This
  // explicit drain is not part of the timed renderer loop or a frame-rate
  // claim.
  WaitForQueueIdle();
  capture_endpoint = moving;
  const auto finalization = render(false, sample_count);
  backend.track_resources = false;
  auto endpoints = nlohmann::json::array();
  const auto save_endpoint = [&](const unsigned path_frame) {
    WaitForQueueIdle();
    for (unsigned index = 0U; index < view_count; ++index) {
      const auto& extract = endpoint_hdr[index];
      CHECK_NOTNULL_F(extract.texture);
      CHECK_NOTNULL_F(endpoint_exposure[index].get());
      const auto domain = Read<FrameExposureData>(
        *endpoint_exposure[index]->buffer, ResourceStates::kShaderResource);
      const auto* hdr_texture = extract.texture;
      auto used_fallback = false;
      if (extract.fallback != nullptr) {
        CHECK_NOTNULL_F(extract.exposure.get());
        const auto report
          = Read<HdrSuitabilityData>(*extract.exposure->conversion_buffer,
            ResourceStates::kShaderResource);
        constexpr auto scene_product_mask = 1U << 10U;
        const auto accepted = report.failure_flags == 0U
          && report.checked_products == scene_product_mask
          && report.expected_products == scene_product_mask;
        if (!accepted) {
          hdr_texture = extract.fallback;
          used_fallback = true;
        }
      }
      const auto hdr = ReadFloatTexture(*hdr_texture, true);
      const auto pixels = ReadFloatTexture(
        *targets[index]->GetDescriptor().color_attachments.front().texture);
      auto scene_luminance = 0.0;
      auto display_luminance = 0.0;
      CHECK_F(hdr.size() == pixels.size() && !pixels.empty());
      for (std::size_t pixel = 0U; pixel < pixels.size(); ++pixel) {
        for (unsigned channel = 0U; channel < 3U; ++channel) {
          CHECK_F(std::isfinite(hdr[pixel][channel]));
          CHECK_F(std::isfinite(pixels[pixel][channel]));
        }
        const auto luminance = [](const Pixel& value) {
          return .2126 * value[0] + .7152 * value[1] + .0722 * value[2];
        };
        scene_luminance += luminance(hdr[pixel]) * domain.one_over_pre_exposure;
        display_luminance += luminance(pixels[pixel]);
      }
      scene_luminance /= double(pixels.size());
      display_luminance /= double(pixels.size());
      CHECK_F(scene_luminance > 0.0 && display_luminance > 0.0);
      const auto filename = stem + "-endpoint-" + std::to_string(path_frame)
        + "-view-" + std::to_string(index) + ".rgba32f";
      const auto path = directory / filename;
      CHECK_F(!std::filesystem::exists(path));
      auto output = std::ofstream(path, std::ios::binary);
      CHECK_F(output.is_open());
      output.write(reinterpret_cast<const char*>(pixels.data()),
        static_cast<std::streamsize>(pixels.size() * sizeof(Pixel)));
      output.close();
      CHECK_F(output.good());
      endpoints.push_back({ { "file", filename }, { "view_index", index },
        { "width", width >> index }, { "height", height >> index },
        { "path_phase", path_phases[index] }, { "frame_sequence", sequence },
        { "pre_exposure", domain.pre_exposure },
        { "used_fp32_fallback", used_fallback },
        { "mean_scene_luminance", scene_luminance },
        { "mean_display_luminance", display_luminance },
        { "encoding",
          "Little-endian float32 RGBA, row-major; final renderer output, no "
          "additional normalization" } });
      endpoint_hdr[index] = {};
      endpoint_exposure[index].reset();
    }
  };
  if (moving) {
    save_endpoint(0U);
    capture_endpoint = false;
    for (unsigned path_frame = 1U; path_frame <= 600U; ++path_frame) {
      capture_endpoint = path_frame == 600U;
      static_cast<void>(render(false, path_frame));
    }
    save_endpoint(600U);
  }
  probe->inspect = {};

  auto cpu = std::ofstream(cpu_path, std::ios::binary);
  ASSERT_TRUE(cpu.is_open());
  cpu << std::setprecision(17)
      << "frame_seq,simulation_dt_ns,wall_ms,frame_start_ms,submission_ms,"
         "main_format,secondary_format,main_draws,secondary_draws,"
         "main_path_phase,secondary_path_phase,main_history_reprojected,"
         "secondary_history_reprojected,main_history_reset,secondary_history_"
         "reset\n";
  for (const auto& sample : samples) {
    cpu << sample.frame_sequence << ',' << simulation_dt_ns << ','
        << sample.wall_ms << ',' << sample.frame_start_ms << ','
        << sample.submission_ms << ',' << sample.formats[0] << ','
        << sample.formats[1] << ',' << sample.draws[0] << ',' << sample.draws[1]
        << ',' << sample.path_phases[0] << ',' << sample.path_phases[1] << ','
        << sample.history_reprojected[0] << ',' << sample.history_reprojected[1]
        << ',' << sample.history_reset[0] << ',' << sample.history_reset[1]
        << '\n';
  }
  cpu.close();
  ASSERT_TRUE(cpu.good());
  auto gpu_stream = std::ifstream(gpu_path);
  ASSERT_TRUE(gpu_stream.is_open());
  const auto gpu = nlohmann::json::parse(gpu_stream);
  const auto adapter = backend.GetCurrentDevice()->GetAdapterLuid();
  const auto manifest = nlohmann::json { { "schema_version", 1 },
    { "workload", workload }, { "run_id", run_id },
    { "configuration", "Release" }, { "width", width }, { "height", height },
    { "view_count", view_count }, { "prepared_draw_counts", warm_draw_counts },
    { "secondary_width", view_count == 2U ? width / 2U : 0U },
    { "secondary_height", view_count == 2U ? height / 2U : 0U },
    { "view_ids",
      view_count == 2U ? std::vector { 500, 501 } : std::vector { 500 } },
    { "view_state_handles",
      view_count == 2U ? std::vector { 500, 501 } : std::vector { 500 } },
    { "shading", forward ? "Forward" : "Deferred" },
    { "exposure",
      { { "mode",
          settings.mode == engine::ExposureMode::kAuto ? "Auto" : "Manual" },
        { "manual_ev", settings.manual_ev }, { "key", settings.key },
        { "metering", "Average" }, { "min_ev", settings.min_ev },
        { "max_ev", settings.max_ev }, { "speed_up", settings.speed_up },
        { "speed_down", settings.speed_down } } },
    { "precision", precision },
    { "precision_scope",
      fp32_reference ? "Format-only FP32 control; certification remains enabled"
                     : "Production admission; certification remains enabled" },
    { "recipe",
      moving ? "MultiView mixed exposure with six-piece sun-shadowed enclosure"
             : (mixed_scene ? "MultiView mixed exposure"
                            : "Emissive triangle 0.25, vacuum atmosphere, "
                              "zero-extinction fog") },
    { "camera_path",
      moving ? "1200 frames: 300 exterior hold, 300 smoothstep entry, 300 "
               "interior hold, 300 smoothstep exit; secondary phase +600"
             : "Static" },
    { "camera_aspect", mixed_scene ? double(width) / height : 1.0 },
    { "camera_fov_radians", mixed_scene ? double(glm::radians(45.0F)) : 1.0 },
    { "tone_mapper", "None" }, { "display_gamma", 1 },
    { "quality_commands", quality_commands }, { "temporal_fog", temporal },
    { "jitter", false }, { "simulation_dt_ns", simulation_dt_ns },
    { "frame_slots", 3 }, { "warmup_frames", warm_frames },
    { "warmup_seconds", warm_seconds }, { "sample_count", sample_count },
    { "sample_seconds", sample_seconds },
    { "first_frame_seq", samples.front().frame_sequence },
    { "last_frame_seq", samples.back().frame_sequence },
    { "adapter_luid_low", adapter.LowPart },
    { "adapter_luid_high", adapter.HighPart },
    { "cpu_samples", cpu_path.filename().string() },
    { "gpu_samples", gpu_path.filename().string() },
    { "gpu_complete", gpu.at("complete") },
    { "gpu_timing_valid", gpu.at("timing_valid") },
    { "resources_before", before }, { "resources_after", after },
    { "resource_scope",
      "Resources created after fixture setup, including "
      "outputs; native placement requirements, not committed heap residency. "
      "No retained extracts beyond normal renderer/probe ownership." },
    { "finalization_frame_seq", finalization.frame_sequence },
    { "finalization_wall_ms", finalization.wall_ms },
    { "untimed_endpoint_images", endpoints },
    { "scope",
      "Native offscreen workload; no presented FPS claim. "
      "Frame-start duration includes backend waits; submission is a "
      "CPU/driver/recording span, not pure active CPU time. Correctness "
      "readbacks and explicit drains are absent from measured frames. "
      "Source/binary/shader hashes and clock/thermal samples belong to the "
      "external frozen-checkpoint runner." } };
  auto output = std::ofstream(manifest_path, std::ios::binary);
  ASSERT_TRUE(output.is_open());
  output << manifest.dump(2) << '\n';
  output.close();
  ASSERT_TRUE(output.good());
  RecordProperty("baseline_manifest", manifest_path.string());
  EXPECT_GE(sample_seconds, 30.0);
  EXPECT_EQ(gpu.at("complete"), true);
  EXPECT_EQ(gpu.at("timing_valid"), true);
  EXPECT_EQ(gpu.at("first_frame_seq"), samples.front().frame_sequence);
  ASSERT_EQ(gpu.at("frames").size(), sample_count);
  auto frame_ids = std::unordered_set<unsigned> {};
  for (const auto& measured : gpu.at("frames")) {
    EXPECT_TRUE(
      frame_ids.insert(measured.at("frame_seq").get<unsigned>()).second);
  }
  for (const auto& sample : samples) {
    EXPECT_TRUE(frame_ids.contains(sample.frame_sequence));
  }
#endif
}
auto ExposureLightingGpuTest::MeasureHdrAllocationAccounting(bool temporal)
  -> void
{
  const auto environment = [](const char* name, const char* fallback) {
    char* text = nullptr;
    std::size_t size = 0U;
    if (_dupenv_s(&text, &size, name) != 0 || !text) {
      return std::string { fallback };
    }
    const auto owned
      = std::unique_ptr<char, decltype(&std::free)>(text, &std::free);
    return std::string { owned.get() };
  };
  const auto width_text = environment("OXYGEN_EXPOSURE_TIMING_WIDTH", "1920");
  std::uint32_t width = 0U;
  const auto parsed = std::from_chars(
    width_text.data(), width_text.data() + width_text.size(), width);
  ASSERT_EQ(parsed.ec, std::errc {});
  ASSERT_EQ(parsed.ptr, width_text.data() + width_text.size());
  ASSERT_TRUE(width == 1920U || width == 3840U);
  const auto precision
    = environment("OXYGEN_EXPOSURE_BASELINE_PRECISION", "production");
  ASSERT_TRUE(precision == "production" || precision == "fp32");
  const bool fp32_reference = precision == "fp32";
  const auto height = width * 9U / 16U;
  verify_manual_p = false;
  renderer_->GetDiagnosticsService().SetHdrFp32ReferenceEnabled(fp32_reference);
  view.viewport = { .width = float(width), .height = float(height) };
  camera.GetCameraAs<scene::PerspectiveCamera>()->get().SetViewport(
    view.viewport);
  SetSurface(data::MaterialDomain::kOpaque, .25F);
  auto& sky
    = scene->GetEnvironment()->AddSystem<scene::environment::SkyAtmosphere>();
  sky.SetEnabled(true);
  sky.SetRayleighScatteringRgb({ 0, 0, 0 });
  sky.SetMieScatteringRgb({ 0, 0, 0 });
  sky.SetMieAbsorptionRgb({ 0, 0, 0 });
  sky.SetOzoneAbsorptionRgb({ 0, 0, 0 });
  auto& fog = scene->GetEnvironment()->AddSystem<scene::environment::Fog>();
  fog.SetEnabled(true);
  fog.SetEnableHeightFog(true);
  fog.SetEnableVolumetricFog(true);
  fog.SetExtinctionSigmaTPerMeter(0);
  ASSERT_EQ(
    fixture_console
      .Execute(temporal ? "vtx.volumetric_fog.temporal_reprojection true"
                        : "vtx.volumetric_fog.temporal_reprojection false")
      .status,
    console::ExecutionStatus::kOk);
  ASSERT_EQ(fixture_console.Execute("vtx.volumetric_fog.jitter false").status,
    console::ExecutionStatus::kOk);
  probe->prepare = [](RenderContext& context) {
    context.current_view.with_atmosphere = true;
    context.current_view.with_height_fog = true;
  };
  scene->Update();
  scene->SyncObservers();

  auto& backend = static_cast<ExposureFailureGraphics&>(Backend());
  backend.tracked_textures.clear();
  backend.tracked_buffers.clear();
  backend.peak_texture_bytes = backend.peak_hdr_bytes = 0U;
  backend.peak_buffer_bytes = backend.peak_placement_bytes = 0U;
  backend.peak_engine_placement_bytes = 0U;
  backend.allocation_peaks = nlohmann::json::object();
  backend.allocation_peak_history = nlohmann::json::array();
  backend.accounting_phase = "fixed_fixture_outputs";
  backend.track_resources = true;
  backend.account_texture_allocations = true;
  std::array<std::shared_ptr<Texture>, 2> outputs;
  std::array<std::shared_ptr<Framebuffer>, 2> targets;
  std::array<std::shared_ptr<Texture>, 2> consumer_outputs;
  std::array<std::shared_ptr<Framebuffer>, 2> consumer_targets;
  struct DepthReadback {
    std::shared_ptr<graphics::Buffer> buffer;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint {};
    UINT64 row_bytes { 0U };
    UINT64 alias_stride { 0U };
  };
  std::array<DepthReadback, 2> depth_readbacks;
  auto consumer = postprocess::TonemapPass(*renderer_);
  auto cleanup = ScopeGuard([&]() noexcept {
    backend.defer_tonemap_recorders = false;
    Backend().SubmitDeferredCommandLists();
    WaitForQueueIdle();
    backend.deferred_tonemap_recordings.clear();
    backend.account_texture_allocations = false;
    backend.track_resources = false;
    probe->inspect = {};
    probe->color.reset();
    probe->exposure.reset();
    targets = {};
    consumer_targets = {};
    for (auto* collection : { &outputs, &consumer_outputs }) {
      for (auto& texture : *collection) {
        if (texture) {
          Backend().GetResourceRegistry().UnRegisterResource(*texture);
          Backend().RegisterDeferredRelease(std::move(texture));
        }
      }
    }
    for (auto& readback : depth_readbacks) {
      if (readback.buffer) {
        Backend().GetResourceRegistry().UnRegisterResource(*readback.buffer);
        Backend().RegisterDeferredRelease(std::move(readback.buffer));
      }
    }
  });
  for (unsigned index = 0U; index < 2U; ++index) {
    outputs[index] = Backend().CreateTexture({ .width = width >> index,
      .height = height >> index,
      .format = Format::kRGBA32Float,
      .debug_name = "LifecycleAccounting.Output" + std::to_string(index),
      .is_render_target = true,
      .initial_state = ResourceStates::kCommon });
    ASSERT_NE(outputs[index], nullptr);
    Backend().GetResourceRegistry().Register(outputs[index]);
    targets[index] = Backend().CreateFramebuffer(
      FramebufferDesc {}.AddColorAttachment(outputs[index]));
    consumer_outputs[index] = Backend().CreateTexture({ .width = 1U,
      .height = 1U,
      .format = Format::kRGBA32Float,
      .debug_name
      = "LifecycleAccounting.DelayedConsumer" + std::to_string(index),
      .is_render_target = true,
      .initial_state = ResourceStates::kCommon });
    ASSERT_NE(consumer_outputs[index], nullptr);
    Backend().GetResourceRegistry().Register(consumer_outputs[index]);
    consumer_targets[index] = Backend().CreateFramebuffer(
      FramebufferDesc {}.AddColorAttachment(consumer_outputs[index]));
  }
  const auto without_diagnostics = [&](auto&& action) {
    const bool tracked = std::exchange(backend.track_resources, false);
    const bool accounted
      = std::exchange(backend.account_texture_allocations, false);
    auto restore = ScopeGuard([&]() noexcept {
      backend.track_resources = tracked;
      backend.account_texture_allocations = accounted;
    });
    return action();
  };
  struct ViewRecord {
    ViewId id;
    SceneTextureExtractRef color;
    std::array<SceneTextureExtractRef, 2> depths;
    unsigned draws;
  };
  std::unordered_map<CompositionView::ViewStateHandle, ViewRecord> current;
  std::array<ViewRecord, 2> retained;
  auto phases = nlohmann::json::array();
  auto frames = nlohmann::json::array();
  probe->inspect = [&](const RenderContext& context,
                     const SceneTextureExtractRef& color, unsigned draws) {
    const auto* owner
      = vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_);
    CHECK_NOTNULL_F(owner);
    const auto& extracts = owner->GetSceneTextureExtracts();
    current.insert_or_assign(context.current_view.view_state_handle,
      ViewRecord { context.current_view.view_id, color,
        { extracts.resolved_scene_depth, extracts.prev_scene_depth }, draws });
    frames.push_back({ { "sequence", context.frame_sequence.get() },
      { "phase", backend.accounting_phase },
      { "view", context.current_view.view_id.get() },
      { "handle", context.current_view.view_state_handle.get() },
      { "format",
        static_cast<unsigned>(color.texture->GetDescriptor().format) },
      { "width", color.texture->GetDescriptor().width },
      { "height", color.texture->GetDescriptor().height },
      { "draws", draws } });
  };
  const auto snapshot = [&](const std::string& phase) {
    auto record = backend.MeasureTrackedPlacement();
    record["phase"] = phase;
    record["sequence"] = sequence;
    record["slot"] = frame.GetFrameSlot().get();
    record["views"] = nlohmann::json::array();
    record["retained_extracts"] = std::count_if(retained.begin(),
      retained.end(),
      [](const auto& item) { return item.color.retained_texture != nullptr; });
    record["retained_depth_aliases"] = std::accumulate(retained.begin(),
      retained.end(), 0U, [](unsigned count, const auto& item) {
        return count
          + static_cast<unsigned>(std::count_if(
            item.depths.begin(), item.depths.end(), [](const auto& depth) {
              return depth.retained_texture != nullptr;
            }));
      });
    const auto* owner
      = vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_);
    if (owner) {
      const auto [families, leased]
        = vortex::testing::RendererPublicationProbe::SceneTexturePoolCounts(
          *owner);
      record["pool_families"] = families;
      record["leased_families"] = leased;
    }
    without_diagnostics([&] {
      for (const auto& [handle, item] : current) {
        const auto& exposure = item.color.exposure;
        CHECK_NOTNULL_F(exposure.get());
        const auto state = Read<ExposureStateData>(
          *exposure->current_state->buffer, ResourceStates::kShaderResource);
        const auto domain = Read<FrameExposureData>(
          *exposure->buffer, ResourceStates::kShaderResource);
        const auto status = Read<ExposureStatusStorage>(
          *exposure->current_state->status_buffer, ResourceStates::kCopySource);
        const auto report = Read<HdrSuitabilityData>(
          *exposure->suitability_buffer, ResourceStates::kShaderResource);
        record["views"].push_back({ { "handle", handle.get() },
          { "view", item.id.get() }, { "draws", item.draws },
          { "format",
            static_cast<unsigned>(item.color.texture->GetDescriptor().format) },
          { "P", domain.pre_exposure }, { "gain", state.displayed_scale },
          { "lifetime", exposure->current_state->owner_lifetime },
          { "state_words",
            std::bit_cast<std::array<std::uint32_t, sizeof(state) / 4U>>(
              state) },
          { "frame_words",
            std::bit_cast<std::array<std::uint32_t, sizeof(domain) / 4U>>(
              domain) },
          { "status_words",
            std::bit_cast<std::array<std::uint32_t, sizeof(status) / 4U>>(
              status) },
          { "suitability_words",
            std::bit_cast<std::array<std::uint32_t, sizeof(report) / 4U>>(
              report) } });
      }
    });
    phases.push_back(record);
    return record;
  };
  const auto render_frame = [&](unsigned view_count, unsigned layout, float ev,
                              const std::string& phase) {
    backend.accounting_phase = phase;
    ++backend.accounting_iteration;
    const auto slot = frame::Slot { sequence % frame::kFramesInFlight.get() };
    Backend().BeginFrame(frame::SequenceNumber { ++sequence }, slot);
    frame.SetFrameSlot(slot, engine::internal::EngineTagFactory::Get());
    frame.SetFrameSequenceNumber(frame::SequenceNumber { sequence },
      engine::internal::EngineTagFactory::Get());
    renderer_->OnFrameStart(observer_ptr { &frame });
    for (unsigned index = 0U; index < view_count; ++index) {
      const auto output_index = index ^ layout;
      auto sized_view = view;
      sized_view.viewport.width = float(width >> output_index);
      sized_view.viewport.height = float(height >> output_index);
      auto input = CompositionView::ForScene(
        ViewId { 500U + index }, sized_view, camera);
      input.view_state_handle
        = CompositionView::ViewStateHandle { 500U + index };
      auto exposure = settings;
      exposure.manual_ev = ev;
      input.render_settings.exposure = exposure;
      ASSERT_NE(
        renderer_->PublishRuntimeCompositionView(frame,
          { .composition_view = input,
            .render_target = observer_ptr { targets[output_index].get() },
            .composite_source = observer_ptr { targets[output_index].get() } }),
        kInvalidViewId);
    }
    if (view_count != 0U) {
      auto loop = co::testing::TestEventLoop {};
      co::Run(loop, [&]() -> co::Co<void> {
        co_await renderer_->OnPreRender(observer_ptr { &frame });
        co_await renderer_->OnRender(observer_ptr { &frame });
      });
    }
    renderer_->OnFrameEnd(observer_ptr { &frame });
    Backend().EndFrame(frame::SequenceNumber { sequence }, slot);
    WaitForQueueIdle();
  };
  const auto remove_views = [&] {
    for (unsigned index = 0U; index < 2U; ++index) {
      renderer_->RemovePublishedRuntimeView(frame, ViewId { 500U + index });
    }
    current.clear();
    probe->color.reset();
    probe->exposure.reset();
  };
  const auto srv = [&](const Texture& texture) {
    const auto index
      = Backend().GetResourceRegistry().FindShaderVisibleIndex(texture,
        TextureViewDescription { .view_type = ResourceViewType::kTexture_SRV,
          .visibility = DescriptorVisibility::kShaderVisible,
          .format = texture.GetDescriptor().format,
          .dimension = texture.GetDescriptor().texture_type,
          .sub_resources = TextureSubResourceSet::EntireTexture() });
    CHECK_F(index.has_value());
    return *index;
  };
  const auto population = [](const nlohmann::json& snapshot) {
    std::vector<std::string> rows;
    for (const auto* kind : { "textures", "buffers" }) {
      for (auto row : snapshot.at(kind)) {
        row.erase("id");
        row.erase("registered");
        rows.push_back(std::string(kind) + row.dump());
      }
    }
    std::ranges::sort(rows);
    return rows;
  };
  const auto ensure_depth_readback = [&](unsigned index, const Texture& depth) {
    auto& readback = depth_readbacks[index];
    if (readback.buffer) {
      ASSERT_EQ(
        readback.footprint.Footprint.Width, depth.GetDescriptor().width);
      ASSERT_EQ(
        readback.footprint.Footprint.Height, depth.GetDescriptor().height);
      return;
    }
    auto* source = depth.GetNativeResource()->AsPointer<ID3D12Resource>();
    const auto desc = source->GetDesc();
    UINT rows = 0U;
    UINT64 bytes = 0U;
    Backend().GetCurrentDevice()->GetCopyableFootprints(&desc, 0U, 1U, 0U,
      &readback.footprint, &rows, &readback.row_bytes, &bytes);
    ASSERT_EQ(rows, depth.GetDescriptor().height);
    ASSERT_GT(bytes, 0U);
    ASSERT_NE(bytes, (std::numeric_limits<UINT64>::max)());
    constexpr UINT64 alignment = D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT;
    readback.alias_stride = (bytes + alignment - 1U) & ~(alignment - 1U);
    readback.buffer
      = Backend().CreateBuffer({ .size_bytes = 2U * readback.alias_stride,
        .usage = BufferUsage::kNone,
        .memory = BufferMemory::kReadBack,
        .debug_name
        = "LifecycleAccounting.DepthReadback" + std::to_string(index) });
    ASSERT_NE(readback.buffer, nullptr);
    Backend().GetResourceRegistry().Register(readback.buffer);
  };
  const auto copy_depth = [&](graphics::CommandRecorder& recorder,
                            const Texture& depth, unsigned index,
                            unsigned alias) {
    const auto& readback = depth_readbacks[index];
    CHECK_NOTNULL_F(readback.buffer.get());
    if (!recorder.IsResourceTracked(depth)) {
      CHECK_F(recorder.AdoptKnownResourceState(depth));
    }
    if (!recorder.IsResourceTracked(*readback.buffer)
      && !recorder.AdoptKnownResourceState(*readback.buffer)) {
      recorder.BeginTrackingResourceState(
        *readback.buffer, ResourceStates::kCopyDest);
    }
    recorder.RequireResourceState(depth, ResourceStates::kCopySource);
    recorder.RequireResourceState(*readback.buffer, ResourceStates::kCopyDest);
    recorder.FlushBarriers();
    D3D12_TEXTURE_COPY_LOCATION source {};
    source.pResource = depth.GetNativeResource()->AsPointer<ID3D12Resource>();
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    source.SubresourceIndex = 0U;
    D3D12_TEXTURE_COPY_LOCATION destination {};
    destination.pResource
      = readback.buffer->GetNativeResource()->AsPointer<ID3D12Resource>();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint = readback.footprint;
    destination.PlacedFootprint.Offset += alias * readback.alias_stride;
    const auto recording = recorder.GetCommandListForInspection();
    const auto* native
      = static_cast<const graphics::d3d12::CommandList*>(recording.get());
    native->GetCommandList()->CopyTextureRegion(
      &destination, 0U, 0U, 0U, &source, nullptr);
  };
  const auto depth_sample = [&](unsigned index, unsigned alias) {
    const auto& readback = depth_readbacks[index];
    const auto width = readback.footprint.Footprint.Width;
    CHECK_EQ_F(readback.row_bytes % width, 0U);
    const auto stride = readback.row_bytes / width;
    CHECK_GE_F(stride, sizeof(std::uint32_t));
    const auto* bytes = static_cast<const std::byte*>(readback.buffer->Map());
    CHECK_NOTNULL_F(bytes);
    std::uint32_t bits = 0U;
    std::memcpy(&bits,
      bytes + readback.footprint.Offset + alias * readback.alias_stride
        + (readback.footprint.Footprint.Height / 2U)
          * readback.footprint.Footprint.RowPitch
        + (width / 2U) * stride,
      sizeof(bits));
    readback.buffer->UnMap();
    return bits;
  };
  static_cast<void>(snapshot("cold_fixture"));
  ASSERT_NO_FATAL_FAILURE(render_frame(1U, 0U, 0.0F, "bootstrap_one"));
  static_cast<void>(snapshot("bootstrap_one"));
  for (unsigned warm = 1U; warm < 8U; ++warm) {
    ASSERT_NO_FATAL_FAILURE(render_frame(1U, 0U, 0.0F, "qualify_one"));
  }
  static_cast<void>(snapshot("steady_one"));
  std::vector<std::string> retired_population;
  auto consumer_results = nlohmann::json::array();
  auto depth_results = nlohmann::json::array();
  for (unsigned cycle = 0U; cycle < 3U; ++cycle) {
    SCOPED_TRACE(cycle);
    const auto prefix = "cycle" + std::to_string(cycle) + "_";
    for (unsigned warm = 0U; warm < 8U; ++warm) {
      ASSERT_NO_FATAL_FAILURE(
        render_frame(2U, 0U, 0.0F, prefix + "qualify_two"));
    }
    for (unsigned index = 0U; index < 2U; ++index) {
      const auto handle = CompositionView::ViewStateHandle { 500U + index };
      ASSERT_TRUE(current.contains(handle));
      retained[index] = current.at(handle);
      ASSERT_GT(retained[index].draws, 0U);
      ASSERT_TRUE(retained[index].color.valid);
      ASSERT_NE(retained[index].color.retained_texture, nullptr);
      for (const auto& depth : retained[index].depths) {
        ASSERT_TRUE(depth.valid);
        ASSERT_NE(depth.texture, nullptr);
        ASSERT_NE(depth.retained_texture, nullptr);
      }
      ASSERT_EQ(
        retained[index].depths[0].texture, retained[index].depths[1].texture);
      EXPECT_FALSE(retained[index].depths[0].retained_texture.owner_before(
        retained[index].depths[1].retained_texture));
      EXPECT_FALSE(retained[index].depths[1].retained_texture.owner_before(
        retained[index].depths[0].retained_texture));
      if (fp32_reference) {
        EXPECT_EQ(retained[index].color.texture->GetDescriptor().format,
          Format::kRGBA32Float);
      } else if (!temporal) {
        EXPECT_EQ(retained[index].color.texture->GetDescriptor().format,
          Format::kRGBA16Float);
      }
      ASSERT_TRUE(renderer_
          ->NotifyViewDiscontinuity(handle, ViewDiscontinuity::kCameraCut)
          .has_value());
    }
    backend.accounting_phase = prefix + "depth_reference";
    std::array<std::uint32_t, 2> reference_depth;
    for (unsigned index = 0U; index < 2U; ++index) {
      const auto& depth = *retained[index].depths[0].texture;
      ASSERT_NO_FATAL_FAILURE(ensure_depth_readback(index, depth));
      {
        auto recorder = AcquireRecorder("Lifecycle immutable depth reference");
        copy_depth(*recorder, depth, index, 0U);
        recorder->RequireResourceStateFinal(
          depth, ResourceStates::kShaderResource);
      }
    }
    WaitForQueueIdle();
    for (unsigned index = 0U; index < 2U; ++index) {
      reference_depth[index] = depth_sample(index, 0U);
      const auto value = std::bit_cast<float>(reference_depth[index]);
      ASSERT_TRUE(std::isfinite(value));
      ASSERT_GT(value, 0.0F);
      ASSERT_LT(value, 1.0F);
    }
    static_cast<void>(snapshot(prefix + "steady_two_retained"));
    ASSERT_NO_FATAL_FAILURE(render_frame(2U, 0U, 1.0F, prefix + "recovery"));
    static_cast<void>(snapshot(prefix + "recovery_two_retained"));
    for (unsigned warm = 0U; warm < 8U; ++warm) {
      ASSERT_NO_FATAL_FAILURE(render_frame(2U, 1U, 1.0F, prefix + "resized"));
    }
    static_cast<void>(snapshot(prefix + "resized_two_retained"));
    std::array<ExposureStateData, 2> saved_states;
    std::array<FrameExposureData, 2> saved_domains;
    std::array<std::uint64_t, 2> saved_lifetimes;
    std::array<std::weak_ptr<const Texture>, 2> wrapper_lifetimes;
    std::array<std::weak_ptr<const Texture>, 2> resource_lifetimes;
    std::array<std::weak_ptr<const Texture>, 2> depth_wrapper_lifetimes;
    std::array<std::weak_ptr<const Texture>, 2> depth_resource_lifetimes;
    without_diagnostics([&] {
      for (unsigned index = 0U; index < 2U; ++index) {
        const auto& source = retained[index].color;
        saved_states[index]
          = Read<ExposureStateData>(*source.exposure->current_state->buffer,
            ResourceStates::kShaderResource);
        saved_domains[index] = Read<FrameExposureData>(
          *source.exposure->buffer, ResourceStates::kShaderResource);
        saved_lifetimes[index] = source.exposure->current_state->owner_lifetime;
        wrapper_lifetimes[index] = source.retained_texture;
        resource_lifetimes[index] = source.texture->shared_from_this();
        depth_wrapper_lifetimes[index]
          = retained[index].depths[0].retained_texture;
        depth_resource_lifetimes[index]
          = retained[index].depths[0].texture->shared_from_this();
      }
    });
    backend.accounting_phase = prefix + "queued_consumers";
    backend.deferred_tonemap_recordings.clear();
    {
      backend.defer_tonemap_recorders = true;
      auto restore = ScopeGuard(
        [&]() noexcept { backend.defer_tonemap_recorders = false; });
      auto consume_context = RenderContext {};
      consume_context.frame_sequence = frame::SequenceNumber { sequence };
      consume_context.frame_slot = frame.GetFrameSlot();
      auto* owner = vortex::testing::RendererPublicationProbe::GetSceneRenderer(
        *renderer_);
      ASSERT_NE(owner, nullptr);
      for (unsigned index = 0U; index < 2U; ++index) {
        const auto& source = retained[index].color;
        const auto& exposure = source.exposure;
        consume_context.current_view.view_id = ViewId { 900U + index };
        ASSERT_TRUE(consumer
            .Record(consume_context, owner->GetSceneTextures(),
              { .scene_signal = source.texture,
                .exposure_buffer = exposure->current_state->buffer.get(),
                .frame_exposure_buffer = exposure->buffer.get(),
                .scene_signal_srv = srv(*source.texture),
                .exposure_buffer_srv = exposure->current_state->srv_index,
                .frame_exposure_srv = exposure->srv_index,
                .post_target
                = observer_ptr<const Framebuffer> { consumer_targets[index]
                    .get() },
                .tone_mapper = engine::ToneMapper::kNone,
                .gamma = 1.0F,
                .scene_fallback = source.fallback,
                .scene_fallback_srv = source.fallback
                  ? srv(*source.fallback)
                  : kInvalidShaderVisibleIndex,
                .conversion_report
                = source.fallback ? exposure->conversion_buffer.get() : nullptr,
                .conversion_report_srv = source.fallback
                  ? exposure->conversion_srv
                  : kInvalidShaderVisibleIndex })
            .executed);
      }
    }
    ASSERT_EQ(backend.deferred_tonemap_recordings.size(), 2U);
    std::shared_ptr<const graphics::CommandList> depth_recording;
    {
      auto recorder
        = AcquireDeferredRecorder("Lifecycle retained depth aliases");
      for (unsigned index = 0U; index < 2U; ++index) {
        for (unsigned alias = 0U; alias < 2U; ++alias) {
          copy_depth(
            *recorder, *retained[index].depths[alias].texture, index, alias);
        }
        recorder->RequireResourceStateFinal(
          *retained[index].depths[0].texture, ResourceStates::kShaderResource);
      }
      depth_recording = recorder->GetCommandListForInspection();
    }
    ASSERT_NE(depth_recording, nullptr);
    ASSERT_FALSE(depth_recording->IsSubmitted());
    for (const auto& recording : backend.deferred_tonemap_recordings) {
      ASSERT_FALSE(recording->IsSubmitted());
    }
    remove_views();
    static_cast<void>(snapshot(prefix + "removed_retained_pending"));
    ASSERT_NO_FATAL_FAILURE(render_frame(2U, 1U, 2.0F, prefix + "readded"));
    ASSERT_FALSE(depth_recording->IsSubmitted());
    for (const auto& recording : backend.deferred_tonemap_recordings) {
      ASSERT_FALSE(recording->IsSubmitted());
    }
    for (unsigned index = 0U; index < 2U; ++index) {
      const auto handle = CompositionView::ViewStateHandle { 500U + index };
      ASSERT_TRUE(current.contains(handle));
      EXPECT_NE(
        current.at(handle).color.exposure->current_state->owner_lifetime,
        saved_lifetimes[index]);
    }
    without_diagnostics([&] {
      for (unsigned index = 0U; index < 2U; ++index) {
        const auto& source = retained[index].color;
        const auto state
          = Read<ExposureStateData>(*source.exposure->current_state->buffer,
            ResourceStates::kShaderResource);
        const auto domain = Read<FrameExposureData>(
          *source.exposure->buffer, ResourceStates::kShaderResource);
        EXPECT_EQ(std::memcmp(&state, &saved_states[index], sizeof(state)), 0);
        EXPECT_EQ(
          std::memcmp(&domain, &saved_domains[index], sizeof(domain)), 0);
      }
    });
    static_cast<void>(snapshot(prefix + "readded_retained_pending"));
    const auto completion = SignalQueue();
    {
      auto recorder
        = AcquireDeferredRecorder("Lifecycle retained consumer completion");
      recorder->RecordQueueSignal(completion.get());
    }
    EXPECT_LT(GetQueue()->GetCompletedValue(), completion.get());
    retained = {};
    for (unsigned index = 0U; index < 2U; ++index) {
      EXPECT_TRUE(wrapper_lifetimes[index].expired());
      EXPECT_FALSE(resource_lifetimes[index].expired());
      EXPECT_TRUE(depth_wrapper_lifetimes[index].expired());
      EXPECT_FALSE(depth_resource_lifetimes[index].expired());
    }
    auto pending_snapshot = backend.MeasureTrackedPlacement();
    pending_snapshot["phase"] = prefix + "released_queued_before_submission";
    pending_snapshot["sequence"] = sequence;
    pending_snapshot["slot"] = frame.GetFrameSlot().get();
    pending_snapshot["retained_extracts"] = 0U;
    pending_snapshot["retained_depth_aliases"] = 0U;
    phases.push_back(std::move(pending_snapshot));
    EXPECT_LT(GetQueue()->GetCompletedValue(), completion.get());
    Backend().SubmitDeferredCommandLists();
    ASSERT_TRUE(depth_recording->IsSubmitted());
    for (const auto& recording : backend.deferred_tonemap_recordings) {
      ASSERT_TRUE(recording->IsSubmitted());
    }
    WaitForQueue(completion);
    WaitForQueueIdle();
    for (unsigned index = 0U; index < 2U; ++index) {
      for (unsigned alias = 0U; alias < 2U; ++alias) {
        const auto actual = depth_sample(index, alias);
        EXPECT_EQ(actual, reference_depth[index]);
        depth_results.push_back({ { "cycle", cycle }, { "view", index },
          { "alias", alias }, { "reference_bits", reference_depth[index] },
          { "actual_bits", actual },
          { "width", depth_readbacks[index].footprint.Footprint.Width },
          { "height", depth_readbacks[index].footprint.Footprint.Height } });
      }
    }
    depth_recording.reset();
    without_diagnostics([&] {
      for (unsigned index = 0U; index < 2U; ++index) {
        const auto pixel = ReadFloatTexture(*consumer_outputs[index]);
        ASSERT_EQ(pixel.size(), 1U);
        // A 1x1 output samples the source center. Ordered dithering uses that
        // source pixel, so the half-size view can have a different Bayer rank.
        // Construct the 4x4 rank independently from its interleaved bit pairs.
        const auto sample_x = (width >> index) / 2U;
        const auto sample_y = (height >> index) / 2U;
        const auto bayer_rank = 8U * ((sample_x ^ sample_y) & 1U)
          + 4U * (sample_y & 1U) + 2U * (((sample_x ^ sample_y) >> 1U) & 1U)
          + ((sample_y >> 1U) & 1U);
        const double expected = .25 + (double(bayer_rank) / 16.0 - .5) / 255.0;
        EXPECT_EQ(saved_states[index].displayed_scale, 1.0F);
        ASSERT_GT(saved_domains[index].pre_exposure, 0.0F);
        for (unsigned channel = 0U; channel < 3U; ++channel) {
          EXPECT_NEAR(pixel.front()[channel], expected, 2e-5);
        }
        EXPECT_EQ(pixel.front()[3], 1.0F);
        consumer_results.push_back({ { "cycle", cycle }, { "view", index },
          { "expected", expected }, { "actual", pixel.front() },
          { "source_pixel", { sample_x, sample_y } },
          { "bayer_rank", bayer_rank },
          { "P", saved_domains[index].pre_exposure },
          { "gain", saved_states[index].displayed_scale },
          { "retired_lifetime", saved_lifetimes[index] },
          { "requested_generation", saved_states[index].requested_generation },
          { "applied_generation", saved_states[index].applied_generation } });
      }
    });
    backend.deferred_tonemap_recordings.clear();
    remove_views();
    static_cast<void>(snapshot(prefix + "released_before_fence"));
    for (unsigned drain = 0U; drain < 2U * frame::kFramesInFlight.get();
      ++drain) {
      ASSERT_NO_FATAL_FAILURE(
        render_frame(0U, 0U, 0.0F, prefix + "retirement"));
    }
    for (const auto& lifetime : resource_lifetimes) {
      EXPECT_TRUE(lifetime.expired());
    }
    for (const auto& lifetime : depth_resource_lifetimes) {
      EXPECT_TRUE(lifetime.expired());
    }
    const auto retired = snapshot(prefix + "retired_cached");
    EXPECT_EQ(retired.at("leased_families").get<std::size_t>(), 0U);
    const auto signature = population(retired);
    if (cycle != 0U) {
      EXPECT_EQ(signature, retired_population)
        << "Repeated visits to the same descriptors must return to the same "
           "cached population";
    }
    retired_population = signature;
  }
  backend.account_texture_allocations = false;
  backend.track_resources = false;
  probe->inspect = {};
  const auto adapter = backend.GetCurrentDevice()->GetAdapterLuid();
  const auto report = nlohmann::json { { "schema_version", 1U },
    { "precision", precision }, { "width", width }, { "height", height },
    { "temporal_fog", temporal }, { "cycles", 3U }, { "frame_slots", 3U },
    { "scope",
      "Creation-time unique live texture/buffer placement; fixed fixture "
      "outputs included and named, diagnostic readbacks excluded. "
      "Queue-drained lifecycle schedule with explicitly deferred retained "
      "consumers; not heap commitment, residency, arbitrary concurrency or "
      "timed performance." },
    { "layout",
      "Two fixed large/half outputs; layout1 swaps their view assignments" },
    { "phases", phases }, { "frames", frames },
    { "allocation_peaks", backend.allocation_peaks },
    { "allocation_peak_history", backend.allocation_peak_history },
    { "delayed_consumers", consumer_results },
    { "delayed_depth_consumers", depth_results },
    { "adapter_luid_low", adapter.LowPart },
    { "adapter_luid_high", adapter.HighPart } };
  RecordProperty("allocation_lifecycle_report", report.dump());
  RecordProperty("main_width", width);
  RecordProperty("main_height", height);
  RecordProperty("concurrent_views", 2);
  RecordProperty("temporal_fog", temporal ? 1 : 0);
  RecordProperty("precision", precision);
  RecordProperty(
    "peak_traced_texture_bytes", std::to_string(backend.peak_texture_bytes));
  RecordProperty("peak_hdr_bytes", std::to_string(backend.peak_hdr_bytes));
  RecordProperty(
    "peak_buffer_bytes", std::to_string(backend.peak_buffer_bytes));
  RecordProperty(
    "peak_placement_bytes", std::to_string(backend.peak_placement_bytes));
  RecordProperty("peak_engine_placement_bytes",
    std::to_string(backend.peak_engine_placement_bytes));
  RecordProperty("peak_hdr_iteration", backend.peak_hdr_iteration);
  RecordProperty("adapter_luid_low", std::to_string(adapter.LowPart));
  RecordProperty("adapter_luid_high", std::to_string(adapter.HighPart));
}

NOLINT_TEST_F(
  ExposureLightingGpuTest, DISABLED_ProductionHdrAllocationAccounting)
{
  MeasureHdrAllocationAccounting(false);
}

NOLINT_TEST_F(ExposureLightingGpuTest,
  DISABLED_ProductionHdrAllocationAccountingWithTemporalFog)
{
  MeasureHdrAllocationAccounting(true);
}

NOLINT_TEST_F(ExposureLightingGpuTest,
  ProductionWideRangeRetainsAdaptationAndQualifiesSharedViewsIndependently)
{
  verify_manual_p = false;
  probe->prepare = [](RenderContext&) { };
  frame_delta_seconds = .1F;
  settings.mode = engine::ExposureMode::kAuto;
  settings.min_log_luminance = -24;
  settings.log_luminance_range = 56;
  settings.speed_up = settings.speed_down = .25F;
  view.viewport = { .width = 2, .height = 1 };
  auto lens = std::make_unique<scene::OrthographicCamera>();
  lens->SetExtents(-1, 1, -.5F, .5F, .1F, 10);
  lens->SetViewport(view.viewport);
  ASSERT_TRUE(camera.ReplaceCamera(std::move(lens)));
  auto output = CreateRegisteredTexture({ .width = 2,
    .height = 1,
    .format = Format::kRGBA32Float,
    .is_render_target = true,
    .initial_state = ResourceStates::kCommon });
  framebuffer = Backend().CreateFramebuffer(
    FramebufferDesc {}.AddColorAttachment(output));
  mesh_node.GetTransform().SetLocalScale({ .25F, 1, 1 });
  mesh_node.GetTransform().SetLocalPosition({ -.5F, 0, 0 });
  auto dim = scene->CreateNode("Required dark signal");
  dim.GetRenderable().SetGeometry(mesh_node.GetRenderable().GetGeometry());
  dim.GetTransform().SetLocalScale({ .25F, 1, 1 });
  dim.GetTransform().SetLocalPosition({ .5F, 0, 0 });
  expected_draws = 2;
  const auto bright = MakeEmissiveMaterial(0x1p30F);
  const auto changed_bright = MakeEmissiveMaterial(0x1p29F);
  const auto dark = MakeEmissiveMaterial(0x1p-16F);
  const auto ordinary = MakeEmissiveMaterial(.25F);
  const auto set_pair = [&](const auto& left, const auto& right) {
    mesh_node.GetRenderable().SetMaterialOverride(0, 0, left);
    dim.GetRenderable().SetMaterialOverride(0, 0, right);
  };
  SceneTextureExtractRef current;
  probe->inspect = [&](const RenderContext& ctx,
                     const SceneTextureExtractRef& color, unsigned) {
    EXPECT_FLOAT_EQ(ctx.delta_time, frame_delta_seconds);
    current = color;
  };
  const auto read_state = [&] {
    return Read<ExposureStateData>(*current.exposure->current_state->buffer,
      ResourceStates::kShaderResource);
  };
  // Make both material texture bindings resident before measuring adaptation.
  set_pair(changed_bright, dark);
  ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 10));
  set_pair(bright, dark);
  ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 10));
  const auto initialized = renderer_->QueueExposureTransition(
    CompositionView::ViewStateHandle { 100U },
    ExposureTransitionPolicy::kRemeter);
  ASSERT_TRUE(initialized.has_value());
  ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 2));
  auto before = read_state();
  EXPECT_EQ(current.texture->GetDescriptor().format, Format::kRGBA32Float);
  EXPECT_EQ(before.flags & 12U, 12U);
  const auto pixels = ReadFloatTexture(*current.texture);
  ASSERT_EQ(pixels.size(), 2U);
  EXPECT_EQ(pixels[0], (Pixel { 0x1p30F, 0x1p30F, 0x1p30F, 1 }));
  EXPECT_EQ(pixels[1], (Pixel { 0x1p-16F, 0x1p-16F, 0x1p-16F, 1 }));
  set_pair(changed_bright, dark);
  for (unsigned index = 0; index < 8; ++index) {
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 1));
    const auto state = read_state();
    EXPECT_EQ(current.texture->GetDescriptor().format, Format::kRGBA32Float);
    EXPECT_EQ(state.flags & 12U, 12U);
    EXPECT_GT(state.displayed_scale, before.displayed_scale);
    EXPECT_LT(state.displayed_scale, state.target_scale);
    EXPECT_EQ(state.requested_generation, before.requested_generation);
    before = state;
  }
  set_pair(ordinary, ordinary);
  ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 10));
  EXPECT_EQ(current.texture->GetDescriptor().format, Format::kRGBA16Float);
  EXPECT_EQ(read_state().requested_generation, before.requested_generation);

  auto source_settings = settings;
  source_settings.mode = engine::ExposureMode::kManual;
  source_settings.manual_ev = 0;
  const auto source_handle = CompositionView::ViewStateHandle { 800U };
  const auto root = PublishExposureOwner(
    frame, ViewId { 800U }, source_handle, source_settings);
  ctx_.scene = observer_ptr { scene.get() };
  ctx_.current_view.view_id = root;
  ctx_.current_view.view_state_handle = source_handle;
  sequence_ = sequence;
  ServicePixel(OwnedExposureService(), Uniform(.25F, 4U, 4U), source_settings);
  sequence = static_cast<unsigned>(sequence_);
  surface_source_id = ViewId { 800U };
  for (unsigned frame_index = 0; frame_index < 8; ++frame_index) {
    for (unsigned order = 0; order < 2; ++order) {
      const bool wide = (order + frame_index) % 2 == 0;
      surface_view_id = wide ? 110U : 111U;
      set_pair(wide ? bright : ordinary, wide ? dark : ordinary);
      ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 1));
      EXPECT_EQ(read_state().displayed_scale, 1.0F);
      if (frame_index >= 5)
        EXPECT_EQ(current.texture->GetDescriptor().format,
          wide ? Format::kRGBA32Float : Format::kRGBA16Float);
      EXPECT_FALSE(renderer_
          ->InspectExposureTransition(
            CompositionView::ViewStateHandle { surface_view_id })
          .has_value());
    }
  }
  EXPECT_FALSE(renderer_->InspectExposureTransition(source_handle).has_value());
  probe->inspect = {};
  current = {};
  RecordProperty("wide_range_stable_adaptation_frames", 8);
  RecordProperty("contrasting_shared_consumer_frames", 16);
}

NOLINT_TEST_F(ExposureLightingGpuTest,
  ProductionRecoveryRetainsHistoryAndResetsOnlyAutoOwner)
{
  verify_manual_p = false;
  probe->prepare = [](RenderContext&) { };
  frame_delta_seconds = .1F;
  const auto root_handle = CompositionView::ViewStateHandle { 800U };
  auto source_settings = settings;
  const auto root = PublishExposureOwner(
    frame, ViewId { 800U }, root_handle, source_settings);
  ctx_.scene = observer_ptr { scene.get() };
  ctx_.current_view.view_id = root;
  ctx_.current_view.view_state_handle = root_handle;
  ServicePixel(OwnedExposureService(), Uniform(.25F, 4U, 4U), source_settings);
  sequence = static_cast<unsigned>(sequence_);
  SceneTextureExtractRef current;
  probe->inspect = [&](const RenderContext& ctx,
                     const SceneTextureExtractRef& color, unsigned) {
    EXPECT_FLOAT_EQ(ctx.delta_time, frame_delta_seconds);
    current = color;
  };
  const auto read_state = [&] {
    return Read<ExposureStateData>(*current.exposure->current_state->buffer,
      ResourceStates::kShaderResource);
  };
  for (unsigned mode = 0; mode < 5; ++mode) {
    SCOPED_TRACE(mode);
    surface_view_id = 100U + mode;
    const auto handle = CompositionView::ViewStateHandle { surface_view_id };
    settings.enabled = mode != 2;
    settings.mode
      = mode == 1 ? engine::ExposureMode::kManual : engine::ExposureMode::kAuto;
    surface_source_id = mode == 3 ? ViewId { 800U } : kInvalidViewId;
    SetSurface(data::MaterialDomain::kOpaque, .25F);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 10));
    ASSERT_EQ(current.texture->GetDescriptor().format, Format::kRGBA16Float);
    const auto before = read_state();
    const auto old_request = renderer_->InspectExposureTransition(handle);
    const auto generation = old_request ? old_request->request.generation : 0U;
    SetSurface(data::MaterialDomain::kOpaque, -1);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 1));
    ASSERT_EQ(current.texture->GetDescriptor().format, Format::kRGBA16Float);
    auto status = Read<ExposureCompletedStatus>(
      *current.exposure->current_state->status_buffer,
      ResourceStates::kCopySource);
    EXPECT_NE(status.flags & 16U, 0U);
    const auto failed = read_state();
    EXPECT_EQ(failed.displayed_scale, before.displayed_scale);
    EXPECT_EQ(failed.latent_scale, before.latent_scale);
    if (mode == 4)
      ASSERT_TRUE(renderer_
          ->QueueExposureTransition(handle, ExposureTransitionPolicy::kPreserve)
          .has_value());
    for (unsigned frame_index = 0; frame_index < 6; ++frame_index) {
      ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 1));
      EXPECT_EQ(current.texture->GetDescriptor().format, Format::kRGBA32Float);
      const auto held = read_state();
      EXPECT_EQ(held.displayed_scale, before.displayed_scale);
      EXPECT_EQ(held.latent_scale, before.latent_scale);
      const auto request = renderer_->InspectExposureTransition(handle);
      EXPECT_EQ(request ? request->request.generation : 0U,
        generation + (mode == 0 || mode == 4 ? 1U : 0U));
      if (mode == 0) {
        ASSERT_TRUE(request.has_value());
        EXPECT_EQ(request->request.policy, ExposureTransitionPolicy::kRemeter);
        EXPECT_EQ(request->phase, ExposureTransitionPhase::kQueued);
      }
    }
    SetSurface(data::MaterialDomain::kOpaque, .5F);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 1));
    const auto repaired = read_state();
    if (mode == 0) {
      EXPECT_EQ(repaired.requested_generation, repaired.applied_generation);
      EXPECT_EQ(repaired.displayed_scale, repaired.target_scale);
      EXPECT_LT(repaired.displayed_scale, before.displayed_scale);
    }
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 10));
    EXPECT_EQ(current.texture->GetDescriptor().format, Format::kRGBA16Float);
    const auto request = renderer_->InspectExposureTransition(handle);
    EXPECT_EQ(request ? request->request.generation : 0U,
      generation + (mode == 0 || mode == 4 ? 1U : 0U));
    EXPECT_FALSE(renderer_->InspectExposureTransition(root_handle).has_value());
  }
  probe->inspect = {};
  current = {};
  RecordProperty("recovery_owner_modes", 5);
}

NOLINT_TEST_F(ExposureLightingGpuTest,
  ProductionConversionRecoveryHonorsNewerExplicitTransition)
{
  verify_manual_p = false;
  probe->prepare = [](RenderContext&) { };
  settings.mode = engine::ExposureMode::kAuto;
  frame_delta_seconds = .1F;
  SceneTextureExtractRef current;
  probe->inspect = [&](const RenderContext& ctx,
                     const SceneTextureExtractRef& color, unsigned) {
    EXPECT_FLOAT_EQ(ctx.delta_time, frame_delta_seconds);
    current = color;
  };
  for (const bool explicit_request : { false, true }) {
    SCOPED_TRACE(explicit_request);
    surface_view_id = explicit_request ? 101U : 100U;
    const auto handle = CompositionView::ViewStateHandle { surface_view_id };
    SetSurface(data::MaterialDomain::kOpaque, .25F);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 10));
    ASSERT_EQ(current.texture->GetDescriptor().format, Format::kRGBA16Float);
    const auto old = renderer_->InspectExposureTransition(handle);
    const auto generation = old ? old->request.generation : 0U;
    SetSurface(data::MaterialDomain::kOpaque, 65504);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 1));
    ASSERT_EQ(current.texture->GetDescriptor().format, Format::kRGBA16Float);
    const auto status = Read<ExposureCompletedStatus>(
      *current.exposure->current_state->status_buffer,
      ResourceStates::kCopySource);
    EXPECT_NE(status.flags & 32U, 0U);
    EXPECT_EQ(status.flags & 16U, 0U);
    const auto before_recovery
      = Read<ExposureStateData>(*current.exposure->current_state->buffer,
        ResourceStates::kShaderResource);
    EXPECT_EQ(before_recovery.flags & 12U, 12U);
    if (explicit_request)
      ASSERT_TRUE(renderer_
          ->QueueExposureTransition(
            handle, ExposureTransitionPolicy::kSeedFromEv100, -3.0F)
          .has_value());
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 1));
    EXPECT_EQ(current.texture->GetDescriptor().format, Format::kRGBA32Float);
    const auto request = renderer_->InspectExposureTransition(handle);
    EXPECT_EQ(request ? request->request.generation : 0U,
      generation + (explicit_request ? 1U : 0U));
    if (explicit_request) {
      ASSERT_TRUE(request.has_value());
      EXPECT_EQ(
        request->request.policy, ExposureTransitionPolicy::kSeedFromEv100);
    }
    const auto state
      = Read<ExposureStateData>(*current.exposure->current_state->buffer,
        ResourceStates::kShaderResource);
    EXPECT_EQ(state.applied_generation, state.requested_generation);
    if (explicit_request)
      EXPECT_EQ(state.displayed_scale, 8.0F);
    else {
      EXPECT_LT(state.displayed_scale, before_recovery.displayed_scale);
      EXPECT_GT(state.displayed_scale, state.target_scale);
    }
  }
  probe->inspect = {};
  current = {};
}

auto ExposureLightingGpuTest::QualifyMixedPrecisionAuxiliaryHandoff(
  bool split_targets) -> void
{
  verify_manual_p = false;
  probe->prepare = [](RenderContext&) { };
  SetSurface(data::MaterialDomain::kOpaque, .25F);
  scene->Update();
  scene->SyncObservers();
  std::array<std::shared_ptr<Texture>, 3> outputs;
  std::array<std::shared_ptr<Framebuffer>, 3> targets;
  std::array<std::shared_ptr<Framebuffer>, 3> scene_targets;
  for (unsigned i = 0; i < 3; ++i) {
    outputs[i] = CreateRegisteredTexture({ .width = 1,
      .height = 1,
      .format = Format::kRGBA32Float,
      .is_render_target = true,
      .initial_state = ResourceStates::kCommon });
    targets[i] = Backend().CreateFramebuffer(
      FramebufferDesc {}.AddColorAttachment(outputs[i]));
    scene_targets[i] = targets[i];
    if (split_targets) {
      auto scene_output = CreateRegisteredTexture({ .width = 1,
        .height = 1,
        .format = Format::kRGBA32Float,
        .is_render_target = true,
        .initial_state = ResourceStates::kCommon });
      scene_targets[i] = Backend().CreateFramebuffer(
        FramebufferDesc {}.AddColorAttachment(scene_output));
    }
  }
  std::unordered_map<CompositionView::ViewStateHandle, SceneTextureExtractRef>
    snapshots;
  probe->inspect = [&](const RenderContext& ctx,
                     const SceneTextureExtractRef& color, unsigned) {
    snapshots.insert_or_assign(ctx.current_view.view_state_handle, color);
  };
  unsigned checked = 0;
  for (unsigned iteration = 0; iteration < 12; ++iteration) {
    SCOPED_TRACE(iteration);
    const auto slot = frame::Slot { sequence % 3U };
    Backend().BeginFrame(frame::SequenceNumber { ++sequence }, slot);
    frame.SetFrameSlot(slot, engine::internal::EngineTagFactory::Get());
    frame.SetFrameSequenceNumber(frame::SequenceNumber { sequence },
      engine::internal::EngineTagFactory::Get());
    renderer_->OnFrameStart(observer_ptr { &frame });
    const auto order = iteration % 2 == 0 ? std::array { 1U, 2U, 0U }
                                          : std::array { 2U, 1U, 0U };
    for (const auto index : order) {
      auto input
        = CompositionView::ForScene(ViewId { 500U + index }, view, camera);
      input.view_state_handle = index == 1
        ? CompositionView::kInvalidViewStateHandle
        : CompositionView::ViewStateHandle { 500U + index };
      auto exposure = settings;
      exposure.manual_ev = index == 0 ? (iteration >= 8 ? 3.0F : 0.0F)
        : index == 1                  ? 1.0F
                                      : 2.0F;
      input.render_settings.exposure = exposure;
      if (index == 0) {
        input.view_kind = CompositionView::ViewKind::kAuxiliary;
        input.produced_aux_outputs.push_back(
          { .id = CompositionView::AuxOutputId { 7001U },
            .kind = CompositionView::AuxOutputKind::kColorTexture,
            .debug_name = "Exposure lifetime producer" });
      } else if (index == 1) {
        input.consumed_aux_outputs.push_back(
          { .id = CompositionView::AuxOutputId { 7001U },
            .kind = CompositionView::AuxOutputKind::kColorTexture,
            .required = true });
      }
      ASSERT_NE(
        renderer_->PublishRuntimeCompositionView(frame,
          { .composition_view = input,
            .render_target = observer_ptr { scene_targets[index].get() },
            .composite_source = observer_ptr { targets[index].get() } }),
        kInvalidViewId);
    }
    auto loop = co::testing::TestEventLoop {};
    co::Run(loop, [&]() -> co::Co<void> {
      co_await renderer_->OnPreRender(observer_ptr { &frame });
      co_await renderer_->OnRender(observer_ptr { &frame });
    });
    renderer_->OnFrameEnd(observer_ptr { &frame });
    Backend().EndFrame(frame::SequenceNumber { sequence }, slot);
    WaitForQueueIdle();
    if (iteration < 6)
      continue;
    const double producer_gain = iteration >= 8 ? .125 : 1;
    const std::array expected { .25 * producer_gain - .5 / 255,
      .25 * producer_gain - .5 / 255, .25 * .25 - .5 / 255 };
    for (unsigned index = 0; index < 3; ++index) {
      const auto pixels = ReadFloatTexture(*outputs[index]);
      ASSERT_EQ(pixels.size(), 1U);
      for (unsigned c = 0; c < 3; ++c)
        EXPECT_NEAR(pixels[0][c], expected[index], 2e-5);
      EXPECT_EQ(pixels[0][3], 1);
      ++checked;
    }
    EXPECT_EQ(snapshots.at(CompositionView::kInvalidViewStateHandle)
                .texture->GetDescriptor()
                .format,
      Format::kRGBA32Float);
    if (iteration >= 7)
      EXPECT_EQ(snapshots.at(CompositionView::ViewStateHandle { 502U })
                  .texture->GetDescriptor()
                  .format,
        Format::kRGBA16Float);
    if (iteration == 7 || iteration == 11)
      EXPECT_EQ(snapshots.at(CompositionView::ViewStateHandle { 500U })
                  .texture->GetDescriptor()
                  .format,
        Format::kRGBA16Float);
    if (iteration == 8)
      EXPECT_EQ(snapshots.at(CompositionView::ViewStateHandle { 500U })
                  .texture->GetDescriptor()
                  .format,
        Format::kRGBA32Float);
  }
  probe->inspect = {};
  snapshots.clear();
  RecordProperty("mixed_family_auxiliary_outputs", checked);
}

NOLINT_TEST_F(ExposureLightingGpuTest,
  MixedPrecisionFamilyAuxiliaryHandoffUsesMappedProducerOutput)
{
  QualifyMixedPrecisionAuxiliaryHandoff(false);
}

NOLINT_TEST_F(ExposureLightingGpuTest,
  SplitSceneAndCompositeTargetsPreserveAuxiliaryMappedOutput)
{
  QualifyMixedPrecisionAuxiliaryHandoff(true);
}

NOLINT_TEST_F(ExposureLightingGpuTest,
  QueuedDepthAliasesRetainSnapshotAcrossViewResizeAndRetirement)
{
  verify_manual_p = false;
  probe->prepare = [](RenderContext&) { };
  SceneRenderer* owner = nullptr;
  std::array<SceneTextureExtractRef, 2> latest;
  const Texture* live_depth = nullptr;
  probe->inspect
    = [&](const RenderContext&, const SceneTextureExtractRef&, unsigned) {
        owner = vortex::testing::RendererPublicationProbe::GetSceneRenderer(
          *renderer_);
        ASSERT_NE(owner, nullptr);
        const auto& extracts = owner->GetSceneTextureExtracts();
        latest = { extracts.resolved_scene_depth, extracts.prev_scene_depth };
        live_depth = &owner->GetSceneTextures().GetSceneDepth();
      };
  const auto resize_output = [&](unsigned width, unsigned height) {
    view.viewport = { .width = float(width), .height = float(height) };
    auto& lens = camera.GetCameraAs<scene::PerspectiveCamera>()->get();
    lens.SetViewport(view.viewport);
    lens.SetAspectRatio(float(width) / float(height));
    auto output = CreateRegisteredTexture({ .width = width,
      .height = height,
      .format = Format::kRGBA32Float,
      .is_render_target = true,
      .initial_state = ResourceStates::kCommon });
    framebuffer = Backend().CreateFramebuffer(
      FramebufferDesc {}.AddColorAttachment(output));
  };

  struct DepthCopy {
    std::shared_ptr<graphics::Buffer> readback;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
    UINT64 row_bytes;
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
    copy.readback = CreateReadbackBuffer(SizeBytes { bytes }, "Queued depth");
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
      = static_cast<const graphics::d3d12::CommandList*>(recording.get());
    native_recording->GetCommandList()->CopyTextureRegion(
      &destination, 0U, 0U, 0U, &source_location, nullptr);
    return copy;
  };
  const auto depth_words = [](const DepthCopy& copy) {
    const auto width = copy.footprint.Footprint.Width;
    const auto height = copy.footprint.Footprint.Height;
    CHECK_EQ_F(copy.row_bytes % width, 0U);
    const auto texel_stride = copy.row_bytes / width;
    CHECK_GE_F(texel_stride, sizeof(std::uint32_t));
    const auto* bytes = static_cast<const std::byte*>(copy.readback->Map());
    CHECK_NOTNULL_F(bytes);
    std::vector<std::uint32_t> words(width * height);
    for (unsigned y = 0U; y < height; ++y) {
      for (unsigned x = 0U; x < width; ++x) {
        std::memcpy(&words[y * width + x],
          bytes + copy.footprint.Offset + y * copy.footprint.Footprint.RowPitch
            + x * texel_stride,
          sizeof(words.front()));
      }
    }
    copy.readback->UnMap();
    return words;
  };
  const auto read_depth = [&](const Texture& depth) {
    DepthCopy copy {};
    {
      auto recorder = AcquireRecorder("Depth snapshot reference");
      copy = copy_depth(*recorder, depth);
      recorder->RequireResourceStateFinal(depth, ResourceStates::kShaderResource);
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
  ASSERT_EQ(latest[0].texture, latest[1].texture);
  EXPECT_FALSE(
    latest[0].retained_texture.owner_before(latest[1].retained_texture));
  EXPECT_FALSE(
    latest[1].retained_texture.owner_before(latest[0].retained_texture));
  auto retained = latest;
  const auto reference = read_depth(*retained[0].texture);
  ASSERT_EQ(reference.size(), 64U);
  for (const auto bits : reference) {
    const auto depth = std::bit_cast<float>(bits);
    ASSERT_TRUE(std::isfinite(depth));
    ASSERT_GT(depth, 0.0F);
    ASSERT_LT(depth, 1.0F);
  }
  std::weak_ptr<const Texture> extract_lifetime = retained[0].retained_texture;
  std::weak_ptr<const Texture> resource_lifetime
    = retained[0].texture->shared_from_this();

  surface_view_id = 101U;
  mesh_node.GetTransform().SetLocalPosition({ 0.0F, 0.0F, -1.0F });
  ASSERT_NO_FATAL_FAILURE(RenderSurface(true, 0, 1U));
  ASSERT_NE(latest[0].texture, retained[0].texture);
  EXPECT_NE(read_depth(*latest[0].texture), reference);
  surface_view_id = 100U;
  resize_output(16U, 8U);
  ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 1U));
  ASSERT_EQ(latest[0].texture->GetDescriptor().width, 16U);
  ASSERT_EQ(retained[0].texture->GetDescriptor().width, 8U);
  owner->RemoveViewState(
    ViewId { 100U }, CompositionView::ViewStateHandle { 100U });
  owner->RemoveViewState(
    ViewId { 101U }, CompositionView::ViewStateHandle { 101U });
  probe->inspect = {};
  latest = {};

  const auto slot = frame::Slot { sequence % 3U };
  Backend().BeginFrame(frame::SequenceNumber { ++sequence }, slot);
  frame.SetFrameSlot(slot, engine::internal::EngineTagFactory::Get());
  frame.SetFrameSequenceNumber(frame::SequenceNumber { sequence },
    engine::internal::EngineTagFactory::Get());
  renderer_->OnFrameStart(observer_ptr { &frame });
  const auto completion = SignalQueue();
  std::array<DepthCopy, 2> copies;
  {
    auto recorder = AcquireDeferredRecorder("Retained depth alias consumers");
    for (unsigned index = 0U; index < copies.size(); ++index) {
      copies[index] = copy_depth(*recorder, *retained[index].texture);
    }
    // Both aliases share one tracked resource; finalize after its last copy.
    recorder->RequireResourceStateFinal(
      *retained[0].texture, ResourceStates::kShaderResource);
    recorder->RecordQueueSignal(completion.get());
  }
  // Deferred submission proves the consumers have not executed when the last
  // extract wrappers disappear. GPU-safe retirement must keep their source.
  retained = {};
  EXPECT_TRUE(extract_lifetime.expired());
  EXPECT_FALSE(resource_lifetime.expired());
  EXPECT_LT(GetQueue()->GetCompletedValue(), completion.get());
  SubmitDeferredRecorders();
  renderer_->OnFrameEnd(observer_ptr { &frame });
  Backend().EndFrame(frame::SequenceNumber { sequence }, slot);
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
  probe->prepare = [](RenderContext&) { };
  SceneTextureExtractRef latest;
  probe->inspect
    = [&](const RenderContext&, const SceneTextureExtractRef& color, unsigned) {
        latest = color;
      };
  SetSurface(data::MaterialDomain::kOpaque, .25F);
  ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0));
  for (unsigned retry = 0;
    retry < 8 && latest.texture->GetDescriptor().format != Format::kRGBA16Float;
    ++retry)
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 1));
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
  const auto retain = [&](float value, float gain, bool checked = true) {
    auto output = CreateRegisteredTexture({ .width = 1,
      .height = 1,
      .format = Format::kRGBA32Float,
      .is_render_target = true,
      .initial_state = ResourceStates::kCommon });
    auto target = Backend().CreateFramebuffer(
      FramebufferDesc {}.AddColorAttachment(output));
    pending.push_back(
      { latest, value, gain, checked, std::move(output), std::move(target) });
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
    ViewId { 100U }, CompositionView::ViewStateHandle { 100U });
  owner->RemoveViewState(
    ViewId { 101U }, CompositionView::ViewStateHandle { 101U });
  latest = {};
  const auto srv = [&](const Texture& texture) {
    const auto view_desc
      = TextureViewDescription { .view_type = ResourceViewType::kTexture_SRV,
          .visibility = DescriptorVisibility::kShaderVisible,
          .format = texture.GetDescriptor().format,
          .dimension = texture.GetDescriptor().texture_type,
          .sub_resources = TextureSubResourceSet::EntireTexture() };
    const auto index = Backend().GetResourceRegistry().FindShaderVisibleIndex(
      texture, view_desc);
    EXPECT_TRUE(index.has_value());
    return index.value_or(kInvalidShaderVisibleIndex);
  };
  const auto consumer_slot = frame::Slot { sequence % 3U };
  Backend().BeginFrame(frame::SequenceNumber { ++sequence }, consumer_slot);
  frame.SetFrameSlot(consumer_slot, engine::internal::EngineTagFactory::Get());
  frame.SetFrameSequenceNumber(frame::SequenceNumber { sequence },
    engine::internal::EngineTagFactory::Get());
  renderer_->OnFrameStart(observer_ptr { &frame });
  // Poison only the rejected half destination. The checked consumer must read
  // the retained bright FP32 fallback; an unconditional control must read
  // black.
  const auto zeros = std::array<std::byte, 256> {};
  auto upload = CreateUploadBuffer(SizeBytes { zeros.size() });
  upload->Update(zeros.data(), zeros.size(), 0U);
  {
    auto recorder = AcquireRecorder("Rejected half consumer sentinel");
    auto& rejected_half = *pending[1].source.texture;
    EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
    ASSERT_TRUE(recorder->AdoptKnownResourceState(rejected_half));
    recorder->RequireResourceState(rejected_half, ResourceStates::kCopyDest);
    recorder->FlushBarriers();
    recorder->CopyBufferToTexture(*upload,
      { .buffer_offset = 0U,
        .buffer_row_pitch = 256U,
        .buffer_slice_pitch = 256U,
        .dst_slice = { .width = 1U, .height = 1U, .depth = 1U } },
      rejected_half);
    recorder->RequireResourceStateFinal(
      rejected_half, ResourceStates::kShaderResource);
  }
  auto consumer = postprocess::TonemapPass(*renderer_);
  auto consume_context = RenderContext {};
  consume_context.frame_sequence = frame::SequenceNumber { sequence };
  consume_context.frame_slot = consumer_slot;
  for (unsigned index = 0; index < pending.size(); ++index) {
    auto& job = pending[index];
    ASSERT_TRUE(job.source.valid);
    ASSERT_NE(job.source.retained_texture, nullptr);
    const auto& exposure = job.source.exposure;
    ASSERT_NE(exposure, nullptr);
    const auto* fallback = job.checked ? job.source.fallback : nullptr;
    consume_context.current_view.view_id = ViewId { 900U + index };
    const auto result
      = consumer.Record(consume_context, owner->GetSceneTextures(),
        {
          .scene_signal = job.source.texture,
          .exposure_buffer = exposure->current_state->buffer.get(),
          .frame_exposure_buffer = exposure->buffer.get(),
          .scene_signal_srv = srv(*job.source.texture),
          .exposure_buffer_srv = exposure->current_state->srv_index,
          .frame_exposure_srv = exposure->srv_index,
          .post_target = observer_ptr<const Framebuffer> { job.target.get() },
          .tone_mapper = engine::ToneMapper::kNone,
          .gamma = 1,
          .scene_fallback = fallback,
          .scene_fallback_srv
          = fallback ? srv(*fallback) : kInvalidShaderVisibleIndex,
          .conversion_report
          = fallback ? exposure->conversion_buffer.get() : nullptr,
          .conversion_report_srv
          = fallback ? exposure->conversion_srv : kInvalidShaderVisibleIndex,
        });
    ASSERT_TRUE(result.executed);
  }
  // Release the last extraction owners after submission, before the frame's
  // fence completes. All consumers are queued before any readback waits.
  for (auto& job : pending)
    job.source = {};
  renderer_->OnFrameEnd(observer_ptr { &frame });
  Backend().EndFrame(frame::SequenceNumber { sequence }, consumer_slot);
  WaitForQueueIdle();
  for (const auto& job : pending) {
    const auto pixel = ReadFloatTexture(*job.output);
    ASSERT_EQ(pixel.size(), 1U);
    const double expected = std::clamp(
      std::clamp(double(job.scene_value) * job.displayed_gain, 0.0, 1.0)
        - .5 / 255,
      0.0, 1.0);
    for (unsigned c = 0; c < 3; ++c)
      EXPECT_NEAR(pixel[0][c], expected, 2e-5);
    EXPECT_EQ(pixel[0][3], 1.0F);
  }
  RecordProperty("queued_mixed_hdr_consumers", pending.size());
  probe->inspect = {};
  pending.clear();
}

NOLINT_TEST_F(ExposureLightingGpuTest,
  ProductionAdmissionRejectsCurrentAtmosphereLayoutChanges)
{
  verify_manual_p = false;
  settings.mode = engine::ExposureMode::kAuto;
  SetSurface(data::MaterialDomain::kOpaque, .25F);
  auto& sky
    = scene->GetEnvironment()->AddSystem<scene::environment::SkyAtmosphere>();
  sky.SetEnabled(true);
  // Exact vacuum transfer isolates layout compatibility from conservative
  // interpolation budgets of a nonuniform atmosphere.
  sky.SetRayleighScatteringRgb({ 0, 0, 0 });
  sky.SetMieScatteringRgb({ 0, 0, 0 });
  sky.SetMieAbsorptionRgb({ 0, 0, 0 });
  sky.SetOzoneAbsorptionRgb({ 0, 0, 0 });
  auto& fog = scene->GetEnvironment()->AddSystem<scene::environment::Fog>();
  fog.SetEnabled(true);
  fog.SetEnableHeightFog(true);
  fog.SetEnableVolumetricFog(true);
  fog.SetExtinctionSigmaTPerMeter(0);
  ASSERT_EQ(
    fixture_console.Execute("vtx.volumetric_fog.temporal_reprojection false")
      .status,
    console::ExecutionStatus::kOk);
  ASSERT_EQ(fixture_console.Execute("vtx.volumetric_fog.jitter false").status,
    console::ExecutionStatus::kOk);
  probe->prepare = [](RenderContext& ctx) {
    ctx.current_view.with_atmosphere = true;
    ctx.current_view.with_height_fog = true;
  };
  struct Snapshot {
    SceneTextureExtractRef color;
    postprocess::ExposurePass::FrameLease frame;
    glm::uvec3 aerial;
    Format aerial_format;
    Format sky_format = Format::kUnknown;
    Format fog_format = Format::kUnknown;
  };
  std::unordered_map<std::uint32_t, Snapshot> snapshots;
  probe->inspect = [&](const RenderContext& ctx,
                     const SceneTextureExtractRef& color, unsigned) {
    auto* owner
      = vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_);
    const auto textures
      = vortex::testing::RendererPublicationProbe::EnvironmentTextures(
        *owner, ctx.current_view.view_id);
    Snapshot snapshot { color, ctx.current_view.frame_exposure, {},
      Format::kUnknown };
    for (const auto& texture : textures) {
      const auto& desc = texture->GetDescriptor();
      if (desc.debug_name.find("SkyView") != std::string::npos)
        snapshot.sky_format = desc.format;
      if (desc.debug_name.find("IntegratedLightScattering")
        != std::string::npos)
        snapshot.fog_format = desc.format;
      if (desc.texture_type == TextureType::kTexture3D
        && desc.debug_name.find("Aerial") != std::string::npos) {
        snapshot.aerial = { desc.width, desc.height, desc.depth };
        snapshot.aerial_format = desc.format;
      }
    }
    snapshots.insert_or_assign(
      static_cast<std::uint32_t>(ctx.current_view.view_id.get()),
      std::move(snapshot));
  };
  const auto settle = [&](std::uint32_t id) {
    surface_view_id = id;
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0));
    for (unsigned retry = 0; retry < 10
      && snapshots.at(id).color.texture->GetDescriptor().format
        != Format::kRGBA16Float;
      ++retry) {
      const auto& f = snapshots.at(id).frame;
      const auto status = Read<ExposureCompletedStatus>(
        *f->current_state->status_buffer, ResourceStates::kCopySource);
      const auto report = Read<HdrSuitabilityData>(
        *f->suitability_buffer, ResourceStates::kShaderResource);
      std::printf("admission_layout frame=%u flags=%u product=%u kind=%u "
                  "streak=%u candidate=%g report=%u products=%u\n",
        sequence, status.flags, status.first_failure_product,
        status.first_failure_kind, status.fp16_eligible_streak,
        report.candidate_pre_exposure, report.failure_flags,
        report.checked_products);
      ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 1));
    }
    ASSERT_EQ(snapshots.at(id).color.texture->GetDescriptor().format,
      Format::kRGBA16Float);
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 1));
    const auto& current = snapshots.at(id);
    EXPECT_EQ(
      current.color.texture->GetDescriptor().format, Format::kRGBA16Float);
    EXPECT_EQ(current.aerial_format, Format::kRGBA16Float);
    EXPECT_EQ(current.sky_format, Format::kRGBA16Float);
    EXPECT_EQ(current.fog_format, Format::kRGBA16Float);
    const auto conversion = Read<HdrSuitabilityData>(
      *current.frame->conversion_buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(conversion.failure_flags, 0U);
  };
  ASSERT_NO_FATAL_FAILURE(settle(100));
  ASSERT_NO_FATAL_FAILURE(settle(101));
  struct Change {
    const char* command;
    glm::uvec3 extent;
  };
  const std::array changes {
    Change {
      "vtx.sky_atmosphere.aerial_perspective_lut.width 16", { 16, 16, 32 } },
    Change {
      "vtx.sky_atmosphere.aerial_perspective_lut.width 128", { 128, 128, 32 } },
    Change { "vtx.sky_atmosphere.aerial_perspective_lut.depth_resolution 16",
      { 128, 128, 16 } },
    Change { "vtx.sky_atmosphere.aerial_perspective_lut.depth_resolution 64",
      { 128, 128, 64 } }
  };
  unsigned cases = 0;
  for (const auto& change : changes) {
    SCOPED_TRACE(change.command);
    ASSERT_EQ(fixture_console.Execute(change.command).status,
      console::ExecutionStatus::kOk);
    // The first view must reject its stale certificate before a second view
    // can refresh the shared cache and conceal a stale-layout selection.
    for (const auto id : { 100U, 101U }) {
      surface_view_id = id;
      const auto before = Read<ExposureStateData>(
        *snapshots.at(id).frame->current_state->buffer,
        ResourceStates::kShaderResource);
      ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 1));
      const auto& current = snapshots.at(id);
      EXPECT_EQ(
        current.color.texture->GetDescriptor().format, Format::kRGBA32Float);
      EXPECT_EQ(current.aerial_format, Format::kRGBA32Float);
      EXPECT_EQ(current.sky_format, Format::kRGBA32Float);
      EXPECT_EQ(current.fog_format, Format::kRGBA32Float);
      EXPECT_EQ(current.aerial, change.extent);
      EXPECT_EQ(current.frame->qualified_candidate, nullptr);
      const auto after = Read<ExposureStateData>(
        *current.frame->current_state->buffer, ResourceStates::kShaderResource);
      EXPECT_EQ(after.displayed_scale, before.displayed_scale);
      EXPECT_EQ(after.latent_scale, before.latent_scale);
      EXPECT_EQ(after.requested_generation, before.requested_generation);
      EXPECT_EQ(after.applied_generation, before.applied_generation);
      EXPECT_NE(after.flags & 1U, 0U);
      ++cases;
    }
    ASSERT_NO_FATAL_FAILURE(settle(100));
    ASSERT_NO_FATAL_FAILURE(settle(101));
  }
  surface_view_id = 100;
  const auto before_failure
    = Read<ExposureStateData>(*snapshots.at(100).frame->current_state->buffer,
      ResourceStates::kShaderResource);
  fog.SetExtinctionSigmaTPerMeter(.01F);
  fog.SetVolumetricFogEmissive({ 65504, 65504, 65504 });
  ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 1));
  const auto failed = snapshots.at(100).frame;
  const auto failure = Read<ExposureCompletedStatus>(
    *failed->current_state->status_buffer, ResourceStates::kCopySource);
  EXPECT_EQ(snapshots.at(100).fog_format, Format::kRGBA16Float);
  EXPECT_EQ(failure.flags & 18U, 18U);
  EXPECT_EQ(failure.first_failure_product, 10U);
  EXPECT_NE(failure.first_failure_kind & 2U, 0U);
  const auto held = Read<ExposureStateData>(
    *failed->current_state->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(held.displayed_scale, before_failure.displayed_scale);
  EXPECT_EQ(held.latent_scale, before_failure.latent_scale);
  EXPECT_EQ(held.flags & 12U, 0U);
  ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 1));
  EXPECT_EQ(snapshots.at(100).color.texture->GetDescriptor().format,
    Format::kRGBA32Float);
  EXPECT_EQ(snapshots.at(100).fog_format, Format::kRGBA32Float);
  fog.SetExtinctionSigmaTPerMeter(0);
  fog.SetVolumetricFogEmissive({ 0, 0, 0 });
  ASSERT_NO_FATAL_FAILURE(settle(100));
  ASSERT_NO_FATAL_FAILURE(settle(101));
  probe->inspect = {};
  snapshots.clear();
  RecordProperty("current_layout_resize_view_cases", cases);
  RecordProperty("normal_half_store_failure_cases", 1);
}

NOLINT_TEST_F(
  ExposureLightingGpuTest, ProductionAdmissionInvalidatesBorrowerOnSourceChange)
{
  verify_manual_p = false;
  probe->prepare = [](RenderContext&) { };
  SetSurface(data::MaterialDomain::kOpaque, .25F);
  auto& service = OwnedExposureService();
  auto source_settings = settings;
  source_settings.manual_ev = 0;
  const auto root_a = PublishExposureOwner(frame, ViewId { 800U },
    CompositionView::ViewStateHandle { 800U }, source_settings);
  ctx_.scene = observer_ptr { scene.get() };
  const auto publish_source
    = [&](ViewId view_id, CompositionView::ViewStateHandle handle,
        scene::ExposureSettings authored) {
        ctx_.current_view.view_id = view_id;
        ctx_.current_view.view_state_handle = handle;
        ctx_.current_view.exposure_view_state_handle
          = CompositionView::kInvalidViewStateHandle;
        sequence_ = sequence;
        ServicePixel(service, Uniform(.25F, 4U, 4U), authored);
        sequence = static_cast<unsigned>(sequence_);
      };
  publish_source(
    root_a, CompositionView::ViewStateHandle { 800U }, source_settings);
  surface_source_id = ViewId { 800U };
  SceneTextureExtractRef current;
  postprocess::ExposurePass::FrameLease exposure;
  probe->inspect = [&](const RenderContext& ctx,
                     const SceneTextureExtractRef& color, unsigned) {
    current = color;
    exposure = ctx.current_view.frame_exposure;
  };
  const auto settle = [&] {
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0));
    for (unsigned retry = 0; retry < 8
      && current.texture->GetDescriptor().format != Format::kRGBA16Float;
      ++retry)
      ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 1));
    ASSERT_EQ(current.texture->GetDescriptor().format, Format::kRGBA16Float);
    const auto state = Read<ExposureStateData>(
      *exposure->current_state->buffer, ResourceStates::kShaderResource);
    EXPECT_NE(state.flags & 128U, 0U);
  };
  ASSERT_NO_FATAL_FAILURE(settle());
  const auto old_candidate = exposure->qualified_candidate;
  ASSERT_NE(old_candidate, nullptr);
  source_settings.manual_ev = 4;
  const auto root_b = PublishExposureOwner(frame, ViewId { 801U },
    CompositionView::ViewStateHandle { 801U }, source_settings);
  publish_source(
    root_b, CompositionView::ViewStateHandle { 801U }, source_settings);
  surface_source_id = ViewId { 801U };
  ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 1));
  EXPECT_EQ(current.texture->GetDescriptor().format, Format::kRGBA32Float);
  EXPECT_EQ(exposure->qualified_candidate, nullptr);
  auto state = Read<ExposureStateData>(
    *exposure->current_state->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(state.displayed_scale, 0x1p-4F);
  ASSERT_NO_FATAL_FAILURE(settle());
  EXPECT_NE(exposure->qualified_candidate, old_candidate);
  const auto request = renderer_->QueueExposureTransition(
    CompositionView::ViewStateHandle { 801U },
    ExposureTransitionPolicy::kPreserve);
  ASSERT_TRUE(request.has_value());
  ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 1));
  EXPECT_EQ(current.texture->GetDescriptor().format, Format::kRGBA32Float);
  EXPECT_EQ(exposure->qualified_candidate, nullptr);
  publish_source(
    root_b, CompositionView::ViewStateHandle { 801U }, source_settings);
  ASSERT_NO_FATAL_FAILURE(settle());
  state = Read<ExposureStateData>(
    *exposure->current_state->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(state.displayed_scale, 0x1p-4F);
  probe->inspect = {};
  current = {};
  exposure.reset();
}

NOLINT_TEST_F(
  ExposureLightingGpuTest, Fp32ReferenceSwitchPreservesSceneExposureAndHistory)
{
  verify_manual_p = false;
  probe->prepare = [](RenderContext&) { };
  settings.mode = engine::ExposureMode::kAuto;
  settings.low_percentile = 0.0F;
  settings.high_percentile = 1.0F;
  SetSurface(data::MaterialDomain::kOpaque, .25F);
  auto& diagnostics = renderer_->GetDiagnosticsService();
  ASSERT_FALSE(diagnostics.IsHdrFp32ReferenceEnabled());
  SceneTextureExtractRef current;
  unsigned current_draws = 0U;
  probe->inspect = [&](const RenderContext& context,
                     const SceneTextureExtractRef& color,
                     const unsigned draws) {
    current_draws = draws;
    EXPECT_FLOAT_EQ(context.delta_time, frame_delta_seconds);
    const auto* owner
      = vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_);
    EXPECT_EQ(owner->GetSceneTextures().GetSceneColor().GetDescriptor().format,
      Format::kRGBA32Float);
    current = color;
  };
  for (const bool forward : { false, true }) {
    SCOPED_TRACE(::testing::Message() << "forward=" << forward);
    surface_view_id = forward ? 9301U : 9300U;
    frame_delta_seconds = 0.0F;
    const auto handle = CompositionView::ViewStateHandle { surface_view_id };
    const auto seed = renderer_->QueueExposureTransition(
      handle, ExposureTransitionPolicy::kSeedFromEv100, 3.0F);
    ASSERT_TRUE(seed.has_value());
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 8));
    ASSERT_EQ(current_draws, 1U);
    ASSERT_TRUE(current.valid);
    ASSERT_NE(current.texture, nullptr);
    ASSERT_EQ(current.texture->GetDescriptor().format, Format::kRGBA16Float);
    ASSERT_NE(current.exposure, nullptr);
    ASSERT_NE(current.exposure->qualified_candidate, nullptr);
    const auto before_domain = Read<FrameExposureData>(
      *current.exposure->buffer, ResourceStates::kShaderResource);
    const auto before
      = Read<ExposureStateData>(*current.exposure->current_state->buffer,
        ResourceStates::kShaderResource);
    EXPECT_EQ(before_domain.flags, 0U);
    ASSERT_NE(before_domain.pre_exposure, 1.0F);
    EXPECT_EQ(before.displayed_scale, .125F);
    EXPECT_EQ(before.latent_scale, .125F);
    EXPECT_NE(before.target_scale, before.displayed_scale);
    EXPECT_EQ(before.requested_generation, before.applied_generation);
    EXPECT_EQ(before.applied_generation[0], seed->generation);
    EXPECT_NEAR(std::log2(double(before.target_scale)),
      std::log2(UniformReferenceGain(.25F)), 4e-4);
    const auto output_texture
      = framebuffer->GetDescriptor().color_attachments.front().texture;
    const auto before_output = ReadFloatTexture(*output_texture);
    ASSERT_EQ(before_output.size(), 1U);
    constexpr auto expected_output = .25F * .125F - .5F / 255.0F;
    for (unsigned channel = 0U; channel < 3U; ++channel) {
      EXPECT_NEAR(before_output[0][channel], expected_output, 2e-4F);
    }
    for (const bool reference : { true, false }) {
      SCOPED_TRACE(::testing::Message() << "reference=" << reference);
      diagnostics.SetHdrFp32ReferenceEnabled(reference);
      EXPECT_EQ(diagnostics.IsHdrFp32ReferenceEnabled(), reference);
      ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 1));
      ASSERT_EQ(current_draws, 1U);
      ASSERT_TRUE(current.valid);
      ASSERT_NE(current.texture, nullptr);
      ASSERT_EQ(current.texture->GetDescriptor().format,
        reference ? Format::kRGBA32Float : Format::kRGBA16Float);
      ASSERT_NE(current.exposure, nullptr);
      ASSERT_NE(current.exposure->qualified_candidate, nullptr);
      ASSERT_NE(current.exposure->selected_history, nullptr);
      const auto domain = Read<FrameExposureData>(
        *current.exposure->buffer, ResourceStates::kShaderResource);
      const auto state
        = Read<ExposureStateData>(*current.exposure->current_state->buffer,
          ResourceStates::kShaderResource);
      EXPECT_EQ(domain.flags, reference ? 1U : 0U);
      EXPECT_EQ(domain.pre_exposure, before_domain.pre_exposure);
      EXPECT_EQ(
        domain.one_over_pre_exposure, before_domain.one_over_pre_exposure);
      EXPECT_EQ(state.displayed_scale, .125F);
      EXPECT_EQ(state.latent_scale, .125F);
      EXPECT_EQ(state.target_scale, before.target_scale);
      EXPECT_EQ(state.latent_target_scale, before.latent_target_scale);
      EXPECT_EQ(state.raw_metered_luminance, before.raw_metered_luminance);
      EXPECT_EQ(state.raw_metered_ev, before.raw_metered_ev);
      EXPECT_EQ(state.settings_revision, before.settings_revision);
      EXPECT_EQ(state.requested_generation, before.requested_generation);
      EXPECT_EQ(state.applied_generation, before.applied_generation);
      EXPECT_EQ(state.product_layout_revision, before.product_layout_revision);
      EXPECT_GE(state.fp16_eligible_streak, 2U);
      EXPECT_EQ(state.fallback_reason, before.fallback_reason);
      const auto pixels = ReadFloatTexture(*current.texture, !reference);
      ASSERT_EQ(pixels.size(), 1U);
      const auto output = ReadFloatTexture(*output_texture);
      ASSERT_EQ(output.size(), 1U);
      for (unsigned channel = 0U; channel < 3U; ++channel) {
        EXPECT_NEAR(
          pixels[0][channel] / domain.pre_exposure, .25F, .005F * .25F + 2e-5F);
        EXPECT_NEAR(output[0][channel], expected_output, 2e-4F);
        EXPECT_NEAR(output[0][channel], before_output[0][channel], 1.0F / 255);
      }
    }
    frame_delta_seconds = .25F;
    ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0, 1));
    ASSERT_EQ(current_draws, 1U);
    const auto adapted
      = Read<ExposureStateData>(*current.exposure->current_state->buffer,
        ResourceStates::kShaderResource);
    const auto expected_gain
      = ReferenceAdaptedGain(.125, UniformReferenceGain(.25F), .25);
    EXPECT_NEAR(std::log2(double(adapted.displayed_scale)),
      std::log2(expected_gain), 4e-4);
    EXPECT_GT(adapted.displayed_scale, .125F);
    EXPECT_EQ(adapted.applied_generation, before.applied_generation);
    EXPECT_EQ(adapted.requested_generation, before.requested_generation);
    EXPECT_TRUE(
      renderer_->ReleaseOffscreenViewState(ViewId { surface_view_id }, handle));
    current = {};
  }
  probe->inspect = {};
}
NOLINT_TEST_F(
  ExposureLightingGpuTest, ProductionAdmissionPinsCandidateAndRetainsFallback)
{
  verify_manual_p = false;
  probe->prepare = [](RenderContext&) { };
  SetSurface(data::MaterialDomain::kOpaque, .25F);
  struct Record {
    SceneTextureExtractRef color;
    postprocess::ExposurePass::FrameLease frame;
    Format accumulation;
    unsigned draws;
  };
  std::vector<Record> records;
  probe->inspect = [&](const RenderContext& ctx,
                     const SceneTextureExtractRef& color, unsigned draws) {
    auto* owner
      = vortex::testing::RendererPublicationProbe::GetSceneRenderer(*renderer_);
    records.push_back({ color, ctx.current_view.frame_exposure,
      owner->GetSceneTextures().GetSceneColor().GetDescriptor().format,
      draws });
  };
  ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0));
  for (unsigned retry = 0; retry < 6
    && records.back().color.texture->GetDescriptor().format
      != Format::kRGBA16Float;
    ++retry)
    ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 0, 1));
  ASSERT_FALSE(records.empty());
  EXPECT_EQ(records.front().color.texture->GetDescriptor().format,
    Format::kRGBA32Float);
  ASSERT_EQ(
    records.back().color.texture->GetDescriptor().format, Format::kRGBA16Float);
  const auto half = records.back();
  ASSERT_NE(half.color.fallback, nullptr);
  ASSERT_NE(half.color.source_lease, nullptr);
  ASSERT_NE(half.frame->qualified_candidate, nullptr);
  const auto domain = Read<FrameExposureData>(
    *half.frame->buffer, ResourceStates::kShaderResource);
  const auto qualified = Read<ExposureStateData>(
    *half.frame->qualified_candidate->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(domain.pre_exposure, qualified.fp16_candidate_pre_exposure);
  const auto report = Read<HdrSuitabilityData>(
    *half.frame->conversion_buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(report.failure_flags, 0U);
  EXPECT_EQ(report.checked_products, 1024U);
  const auto before = Read<ExposureStateData>(
    *half.frame->current_state->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(before.displayed_scale, 1.0F);
  const auto pixels = ReadFloatTexture(*half.color.texture, true);
  ASSERT_EQ(pixels.size(), 1U);
  EXPECT_NEAR(pixels[0][0] / domain.pre_exposure, .25F, .005F * .25F + 2e-5F);
  for (const auto& r : records)
    EXPECT_EQ(r.accumulation, Format::kRGBA32Float);
  const auto saved_fallback = ReadFloatTexture(*half.color.fallback);
  records.clear();
  ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 2, 1));
  EXPECT_EQ(records.front().color.texture->GetDescriptor().format,
    Format::kRGBA32Float);
  ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 2));
  EXPECT_EQ(
    records.back().color.texture->GetDescriptor().format, Format::kRGBA16Float);
  EXPECT_EQ(ReadFloatTexture(*half.color.fallback), saved_fallback);
  EXPECT_EQ(ReadFloatTexture(*half.color.texture, true), pixels);
  const auto after
    = Read<ExposureStateData>(*records.back().frame->current_state->buffer,
      ResourceStates::kShaderResource);
  EXPECT_EQ(after.displayed_scale, .25F);
  const auto capture = BeginOptionalCapture();
  ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 2, 1));
  if (capture) EXPECT_TRUE(capture->EndCapture());
  auto& backend = static_cast<ExposureFailureGraphics&>(Backend());
  backend.fail_recorder_name = "Vortex Checked SceneColor Conversion";
  ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 2, 1));
  backend.fail_recorder_name.clear();
  EXPECT_EQ(
    records.back().color.texture->GetDescriptor().format, Format::kRGBA32Float);
  EXPECT_EQ(records.back().color.fallback, nullptr);
  ASSERT_NO_FATAL_FAILURE(RenderSurface(false, 2, 1));
  EXPECT_EQ(
    records.back().color.texture->GetDescriptor().format, Format::kRGBA32Float);
  EXPECT_EQ(records.back().frame->qualified_candidate, nullptr);
  const auto after_failure
    = Read<ExposureStateData>(*records.back().frame->current_state->buffer,
      ResourceStates::kShaderResource);
  EXPECT_EQ(after_failure.displayed_scale, after.displayed_scale);
  EXPECT_EQ(after_failure.applied_generation, after.applied_generation);
  probe->inspect = {};
  records.clear();
}

NOLINT_TEST_F(
  ExposureLightingGpuTest, DirectLightRadiancePreservesSupportedRange)
{
  auto sun = scene->CreateNode("Directional");
  auto directional = std::make_unique<scene::DirectionalLight>();
  directional->Common().casts_shadows = false;
  directional->Common().color_rgb = { .25F, .5F, 1.0F };
  directional->SetEnvironmentContribution(true);
  ASSERT_TRUE(sun.AttachLight(std::move(directional)));
  sun.GetTransform().SetLocalRotation(
    glm::quat { .70710678F, .70710678F, 0, 0 });
  auto point_node = scene->CreateNode("Point");
  auto point = std::make_unique<scene::PointLight>();
  point->Common().casts_shadows = false;
  point->Common().color_rgb = { .25F, .5F, 1.0F };
  point->SetRange(100);
  ASSERT_TRUE(point_node.AttachLight(std::move(point)));
  auto spot_node = scene->CreateNode("Spot");
  auto spot = std::make_unique<scene::SpotLight>();
  spot->Common().casts_shadows = false;
  spot->Common().color_rgb = { .25F, .5F, 1.0F };
  spot->SetRange(100);
  ASSERT_TRUE(spot_node.AttachLight(std::move(spot)));
  spot_node.GetTransform().SetLocalRotation(
    glm::quat { .70710678F, .70710678F, 0, 0 });
  unsigned cases = 0;
  for (const bool forward : { false, true })
    for (const auto domain :
      { data::MaterialDomain::kOpaque, data::MaterialDomain::kMasked,
        data::MaterialDomain::kAlphaBlended }) {
      SetSurface(domain);
      const bool forward_shader
        = forward || domain == data::MaterialDomain::kAlphaBlended;
      const double coverage
        = domain == data::MaterialDomain::kAlphaBlended ? .5 : 1;
      // Independent double-precision on-axis GGX evaluation for a white,
      // nonmetal surface at roughness 1. This checks the current transport
      // equations; Slice 7 owns their differing photometric calibrations.
      const double pi = std::acos(-1.0);
      const double f0 = .04;
      const double oct = 1.0 / 1023;
      const double n = forward_shader ? 1.0
                                      : (1 - 2 * oct)
          / std::sqrt(2 * oct * oct + (1 - 2 * oct) * (1 - 2 * oct));
      const double g = 2 * n / (n + 1);
      const double specular = f0 * g * g / (4 * pi * n * n);
      const double brdf = ((1 - f0) / (forward_shader ? 1 : pi) + specular) * n;
      // R8 UNORM stores the .5 specular value at either adjacent code. Carry
      // its half-code uncertainty through this linear-in-F0 expression.
      const double brdf_error = forward_shader
        ? 0
        : .04 / 255 * std::abs(g * g / (4 * pi * n * n) - 1 / pi) * n;
      for (unsigned kind = 0; kind < 3; ++kind) {
        auto d = sun.GetLightAs<scene::DirectionalLight>();
        auto p = point_node.GetLightAs<scene::PointLight>();
        auto s = spot_node.GetLightAs<scene::SpotLight>();
        d->get().Common().affects_world = kind == 0;
        p->get().Common().affects_world = kind == 1;
        s->get().Common().affects_world = kind == 2;
        // Replace the authoring component to publish a light mutation and
        // invalidate the resolver's cached directional membership.
        ASSERT_TRUE(sun.ReplaceLight(
          std::make_unique<scene::DirectionalLight>(d->get())));
        d = sun.GetLightAs<scene::DirectionalLight>();
        const double attenuation = kind == 0 ? (forward_shader ? 1 / pi : 1)
          : forward_shader                   ? .99 * .99
                                             : std::pow(1 - 1e-8, 2) / 2;
        const double coefficient = brdf * attenuation;
        for (const double radiance :
          { 0.0, 0x1p-24, .25, 131072.0, 0x1p32 * .999, 0x1p35 })
          for (const float ev : { -32.0F, 0.0F, 32.0F }) {
            SCOPED_TRACE(forward);
            SCOPED_TRACE(static_cast<int>(domain));
            SCOPED_TRACE(kind);
            SCOPED_TRACE(radiance);
            SCOPED_TRACE(ev);
            const float intensity = static_cast<float>(radiance / coefficient);
            d->get().SetIntensityLux(intensity);
            p->get().SetLuminousFluxLm(intensity);
            s->get().SetLuminousFluxLm(intensity);
            ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, ev));
            const auto pixels = ReadFloatTexture(*probe->color);
            ASSERT_EQ(pixels.size(), 1U);
            for (unsigned channel = 0; channel < 3; ++channel) {
              const double expected = intensity * coefficient * coverage
                * std::exp2(double(channel) - 2 - ev);
              EXPECT_NEAR(pixels[0][channel], expected,
                std::abs(expected) * (2e-5 + brdf_error / brdf) + 0x1p-120);
            }
            EXPECT_FLOAT_EQ(pixels[0][3], static_cast<float>(coverage));
            const auto status = Read<ExposureCompletedStatus>(
              *probe->exposure->current_state->status_buffer,
              ResourceStates::kCopySource);
            if (radiance > 0x1p32) {
              EXPECT_EQ(status.flags & 18U, 18U);
              EXPECT_NE(status.first_failure_kind & 32U, 0U);
              EXPECT_EQ(status.first_failure_product,
                domain == data::MaterialDomain::kAlphaBlended || forward ? 4U
                                                                         : 2U);
            } else
              EXPECT_EQ(status.flags & 16U, 0U);
            ++cases;
          }
      }
    }
  RecordProperty("direct_light_endpoint_cases", cases);
}

NOLINT_TEST_F(ExposureLightingGpuTest,
  SourceFailureSurvivesPositiveLightingAndPreservesAutoHistory)
{
  auto point_node = scene->CreateNode("Cancellation control");
  auto light = std::make_unique<scene::PointLight>();
  light->Common().casts_shadows = false;
  light->SetRange(100);
  light->SetLuminousFluxLm(100);
  ASSERT_TRUE(point_node.AttachLight(std::move(light)));
  auto fill_node = scene->CreateNode("Positive fill");
  auto fill = std::make_unique<scene::PointLight>();
  fill->Common().casts_shadows = false;
  fill->SetRange(100);
  ASSERT_TRUE(fill_node.AttachLight(std::move(fill)));
  settings.mode = engine::ExposureMode::kAuto;
  frame_delta_seconds = .1F;
  unsigned cases = 0;
  for (const bool prepass : { true, false })
    for (const bool invalid_light : { false, true })
      for (const bool forward : { false, true })
        for (const auto domain :
          { data::MaterialDomain::kOpaque, data::MaterialDomain::kMasked,
            data::MaterialDomain::kAlphaBlended }) {
          SCOPED_TRACE(forward);
          SCOPED_TRACE(static_cast<int>(domain));
          SCOPED_TRACE(invalid_light);
          SCOPED_TRACE(prepass);
          depth_mode = prepass ? DepthPrePassMode::kOpaqueAndMasked
                               : DepthPrePassMode::kDisabled;
          point_node.GetLightAs<scene::PointLight>()->get().SetLuminousFluxLm(
            invalid_light ? 0.0F : 100.0F);
          fill_node.GetLightAs<scene::PointLight>()->get().SetLuminousFluxLm(
            invalid_light ? 200.0F : 0.0F);
          SetSurface(domain, 1);
          ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0));
          const auto before
            = Read<ExposureStateData>(*probe->exposure->current_state->buffer,
              ResourceStates::kShaderResource);
          auto status = Read<ExposureCompletedStatus>(
            *probe->exposure->current_state->status_buffer,
            ResourceStates::kCopySource);
          EXPECT_EQ(status.flags & 16U, 0U);
          EXPECT_EQ(before.flags & 12U, 12U);
          if (invalid_light)
            point_node.GetLightAs<scene::PointLight>()->get().SetLuminousFluxLm(
              -100.0F);
          else
            SetSurface(domain, -1);
          ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0));
          const auto pixels = ReadFloatTexture(*probe->color);
          ASSERT_EQ(pixels.size(), 1U);
          for (unsigned c = 0; c < 3; ++c) {
            ASSERT_TRUE(std::isfinite(pixels[0][c]));
            EXPECT_GT(pixels[0][c], 0);
          }
          status = Read<ExposureCompletedStatus>(
            *probe->exposure->current_state->status_buffer,
            ResourceStates::kCopySource);
          EXPECT_EQ(status.flags & 18U, 18U);
          EXPECT_EQ(status.first_failure_kind & 32U, 32U);
          EXPECT_EQ(status.first_failure_product,
            forward || domain == data::MaterialDomain::kAlphaBlended ? 4U
              : invalid_light                                        ? 2U
                                                                     : 1U);
          const auto after
            = Read<ExposureStateData>(*probe->exposure->current_state->buffer,
              ResourceStates::kShaderResource);
          EXPECT_EQ(after.displayed_scale, before.displayed_scale);
          EXPECT_EQ(after.latent_scale, before.latent_scale);
          EXPECT_EQ(after.flags & 12U, 0U);
          EXPECT_EQ(after.flags & 32U, 32U);
          ++cases;
          if (domain != data::MaterialDomain::kOpaque) {
            SetSurface(domain, invalid_light ? 1.0F : -1.0F, true);
            ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0));
            const auto empty = ReadFloatTexture(*probe->color);
            ASSERT_EQ(empty.size(), 1U);
            EXPECT_EQ(empty[0], (Pixel { 0, 0, 0, 0 }));
            status = Read<ExposureCompletedStatus>(
              *probe->exposure->current_state->status_buffer,
              ResourceStates::kCopySource);
            EXPECT_EQ(status.flags & 16U, 0U);
            ++cases;
          }
        }
  RecordProperty("source_cancellation_and_rejection_cases", cases);
}

NOLINT_TEST_F(
  ExposureLightingGpuTest, EmissiveFailureRespectsFinishedDepthAndMaskHoles)
{
  auto blocker = scene->CreateNode("Foreground");
  blocker.GetRenderable().SetGeometry(mesh_node.GetRenderable().GetGeometry());
  blocker.GetTransform().SetLocalPosition({ 0, 0, .25F });
  expected_draws = 2;
  settings.mode = engine::ExposureMode::kAuto;
  const auto set_blocker
    = [&](data::MaterialDomain domain, float emission, bool hole = false) {
        const auto saved = mesh_node;
        mesh_node = blocker;
        SetSurface(domain, emission, hole);
        mesh_node = saved;
      };
  unsigned cases = 0;
  for (const bool prepass : { true, false })
    for (const bool forward : { false, true })
      for (const auto domain :
        { data::MaterialDomain::kOpaque, data::MaterialDomain::kMasked })
        for (const bool negative_first : { true, false }) {
          SCOPED_TRACE(prepass);
          SCOPED_TRACE(forward);
          SCOPED_TRACE(static_cast<int>(domain));
          SCOPED_TRACE(negative_first);
          depth_mode = prepass ? DepthPrePassMode::kOpaqueAndMasked
                               : DepthPrePassMode::kDisabled;
          mesh_node.GetTransform().SetLocalPosition({ 0, 0, 0 });
          SetSurface(domain, negative_first ? -1.0F : 1.0F);
          blocker.GetRenderable().SetMaterialOverride(
            0, 0, mesh_node.GetRenderable().ResolveSubmeshMaterial(0, 0));
          ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0));
          // Register one material first, then the other, to exercise each
          // actual material-sorted raster order; assert the resulting order.
          if (negative_first)
            set_blocker(domain, 1);
          else
            SetSurface(domain, -1);
          ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0));
          ASSERT_EQ(probe->raster_depths.size(), 2U);
          if (probe->raster_depths[0] != (negative_first ? 0.0F : .25F)) {
            // Keep the image identical while reversing which scene node owns
            // each surface; verify the renderer's resulting raster order.
            const auto negative
              = mesh_node.GetRenderable().ResolveSubmeshMaterial(0, 0);
            const auto positive
              = blocker.GetRenderable().ResolveSubmeshMaterial(0, 0);
            mesh_node.GetRenderable().SetMaterialOverride(0, 0, positive);
            blocker.GetRenderable().SetMaterialOverride(0, 0, negative);
            mesh_node.GetTransform().SetLocalPosition({ 0, 0, .25F });
            blocker.GetTransform().SetLocalPosition({ 0, 0, 0 });
            std::swap(mesh_node, blocker);
            ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0));
            ASSERT_EQ(probe->raster_depths.size(), 2U);
          }
          EXPECT_EQ(probe->early_depth_complete, prepass);
          EXPECT_EQ(probe->raster_depths[0], negative_first ? 0.0F : .25F);
          const auto pixels = ReadFloatTexture(*probe->color);
          ASSERT_EQ(pixels.size(), 1U);
          EXPECT_GT(pixels[0][0], 0);
          auto status = Read<ExposureCompletedStatus>(
            *probe->exposure->current_state->status_buffer,
            ResourceStates::kCopySource);
          EXPECT_EQ(status.flags & 16U, 0U);
          const auto meter
            = Read<ExposureStateData>(*probe->exposure->current_state->buffer,
              ResourceStates::kShaderResource);
          EXPECT_EQ(meter.flags & 12U, 12U);
          ++cases;
          mesh_node.GetTransform().SetLocalPosition({ 0, 0, .5F });
          ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0));
          ASSERT_EQ(probe->raster_depths.size(), 2U);
          status = Read<ExposureCompletedStatus>(
            *probe->exposure->current_state->status_buffer,
            ResourceStates::kCopySource);
          EXPECT_EQ(status.flags & 18U, 18U);
          EXPECT_EQ(status.first_failure_kind & 32U, 32U);
          EXPECT_EQ(status.first_failure_product, forward ? 4U : 1U);
          ++cases;
          if (domain == data::MaterialDomain::kMasked) {
            mesh_node.GetTransform().SetLocalPosition({ 0, 0, 0 });
            set_blocker(domain, 1, true);
            ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, 0));
            ASSERT_EQ(probe->raster_depths.size(), 2U);
            status = Read<ExposureCompletedStatus>(
              *probe->exposure->current_state->status_buffer,
              ResourceStates::kCopySource);
            EXPECT_EQ(status.flags & 18U, 18U);
            EXPECT_EQ(status.first_failure_product, forward ? 4U : 1U);
            ++cases;
          }
        }
  RecordProperty("emissive_depth_visibility_cases", cases);
}

NOLINT_TEST_F(ExposureLightingGpuTest, StaticSkyDiffusePreservesSupportedRange)
{
  auto& sky
    = scene->GetEnvironment()->AddSystem<scene::environment::SkyLight>();
  sky.SetEnabled(true);
  sky.SetSource(scene::environment::SkyLightSource::kSpecifiedCubemap);
  sky.SetLowerHemisphereIsSolidColor(false);
  sky.SetSpecularIntensity(0);
  sky.SetDiffuseIntensity(1);
  unsigned cases = 0;
  for (const Pixel source_color :
    { Pixel { .25F, .5F, 1, 1 }, Pixel { 0x1p-24F, .25F, 0x1p30F, 1 } }) {
    data::pak::core::TextureResourceDesc desc {};
    desc.texture_type = static_cast<std::uint8_t>(TextureType::kTextureCube);
    desc.width = desc.height = desc.depth = desc.mip_levels = 1;
    desc.array_layers = 6;
    desc.format = static_cast<std::uint8_t>(Format::kRGBA32Float);
    desc.alignment = 256;
    const auto key = owned_asset_loader_->MintSyntheticTextureKey();
    desc.content_hash = key.get();
    std::vector<std::uint8_t> data_region(6 * sizeof(Pixel));
    std::vector<data::pak::render::SubresourceLayout> layouts;
    for (unsigned face = 0; face < 6; ++face) {
      std::memcpy(data_region.data() + face * sizeof(Pixel),
        source_color.data(), sizeof(Pixel));
      layouts.push_back({ .offset_bytes = face * sizeof(Pixel),
        .row_pitch_bytes = sizeof(Pixel),
        .size_bytes = sizeof(Pixel) });
    }
    auto payload = vortex::testing::detail::BuildV4TexturePayload(
      desc, layouts, data_region);
    desc.size_bytes = static_cast<std::uint32_t>(payload.size());
    owned_asset_loader_->SetTexture(
      key, std::make_shared<data::TextureResource>(desc, std::move(payload)));
    sky.SetCubemapResource(key);
    for (const bool forward : { false, true })
      for (const auto domain :
        { data::MaterialDomain::kOpaque, data::MaterialDomain::kMasked,
          data::MaterialDomain::kAlphaBlended }) {
        SetSurface(domain);
        const double coverage
          = domain == data::MaterialDomain::kAlphaBlended ? .5 : 1;
        for (const float multiplier :
          { 0.0F, 0x1p-24F, 1.0F, 0x1p32F * .999F, 0x1p35F })
          for (const float ev : { -32.0F, 0.0F, 32.0F }) {
            SCOPED_TRACE(source_color[2]);
            SCOPED_TRACE(forward);
            SCOPED_TRACE(static_cast<int>(domain));
            SCOPED_TRACE(multiplier);
            SCOPED_TRACE(ev);
            sky.SetIntensityMul(multiplier);
            ASSERT_NO_FATAL_FAILURE(RenderSurface(forward, ev));
            const auto pixels = ReadFloatTexture(*probe->color);
            ASSERT_EQ(pixels.size(), 1U);
            // Isotropic radiance projects to the constant SH coefficient.
            // Lambert convolution / pi returns that same radiance,
            // independently of normal, cubemap orientation or canonical
            // normalization.
            for (unsigned channel = 0; channel < 3; ++channel) {
              const double expected = double(source_color[channel]) * multiplier
                * coverage * std::exp2(-double(ev));
              EXPECT_NEAR(pixels[0][channel], expected,
                std::abs(expected) * 2e-5 + 0x1p-120);
            }
            EXPECT_FLOAT_EQ(pixels[0][3], static_cast<float>(coverage));
            const auto status = Read<ExposureCompletedStatus>(
              *probe->exposure->current_state->status_buffer,
              ResourceStates::kCopySource);
            if (double(source_color[2]) * multiplier > 0x1p32) {
              EXPECT_EQ(status.flags & 18U, 18U);
              EXPECT_NE(status.first_failure_kind & 32U, 0U);
              EXPECT_EQ(status.first_failure_product,
                domain == data::MaterialDomain::kAlphaBlended || forward ? 4U
                                                                         : 3U);
            } else
              EXPECT_EQ(status.flags & 16U, 0U);
            ++cases;
          }
      }
  }
  RecordProperty("static_sky_endpoint_cases", cases);
}

NOLINT_TEST_F(ExposureGpuTest, DistantSkyRadiancePreservesLinearRange)
{
  pass_.reset();
  renderer_->OnShutdown();
  auto config = RendererConfig {};
  config.upload_queue_key = QueueKeyFor().get();
  renderer_ = std::make_unique<Renderer>(GetGraphicsShared(), config,
    kPhase1DefaultRuntimeCapabilityFamilies
      | RendererCapabilityFamily::kEnvironmentLighting);
  ctx_.current_view.with_atmosphere = true;
  auto view_data = ViewConstants::GpuData {};
  auto view_buffer
    = CreateUploadBuffer(SizeBytes { 256U }, BufferUsage::kConstant);
  view_buffer->Update(&view_data, sizeof(view_data), 0U);
  ctx_.view_constants = view_buffer;
  auto stable = environment::internal::StableAtmosphereState {};
  stable.atmosphere_revision = 1;
  stable.view_products.atmosphere.enabled = true;
  auto cache = environment::internal::AtmosphereLutCache(*renderer_);
  auto transmittance = environment::AtmosphereTransmittanceLutPass(*renderer_);
  auto scattering = environment::AtmosphereMultiScatteringLutPass(*renderer_);
  auto distant = environment::DistantSkyLightLutPass(*renderer_);
  const auto render = [&](const std::array<float, 2>& intensity) {
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    ctx_.frame_slot = frame::Slot { unsigned(sequence_ % 3) };
    stable.light_revision = sequence_;
    stable.view_products.atmosphere_light_count = 2;
    for (unsigned i = 0; i < 2; ++i) {
      auto& light = stable.view_products.atmosphere_lights[i];
      light.enabled = true;
      light.direction_to_light_ws = i == 0
        ? glm::vec3 { 0, 0, 1 }
        : glm::normalize(glm::vec3 { 1, 0, 1 });
      light.illuminance_rgb_lux = glm::vec3 { intensity[i] };
    }
    cache.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    cache.RefreshForState(stable);
    transmittance.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    scattering.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    distant.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    if (cache.NeedsTransmittanceBuild())
      EXPECT_TRUE(transmittance.Record(ctx_, stable, cache).executed);
    if (cache.NeedsMultiScatteringBuild())
      EXPECT_TRUE(scattering.Record(ctx_, stable, cache).executed);
    EXPECT_TRUE(distant.Record(ctx_, stable, cache).executed);
    return Read<Pixel>(
      *cache.GetDistantSkyLightBuffer(), ResourceStates::kShaderResource);
  };
  const auto first = render({ 1, 0 });
  const auto second = render({ 0, 1 });
  for (unsigned channel = 0; channel < 3; ++channel) {
    ASSERT_GT(first[channel], 0.0F);
    ASSERT_GT(second[channel], 0.0F);
  }
  unsigned cases = 0;
  // Linearity of the radiative-transfer equation provides an independent
  // scaling/additivity oracle. Unit-light native anchors are not an absolute
  // atmospheric-accuracy reference; no production helper computes expected RGB.
  for (const float gain : { 0.0F, 0x1p-24F, 1.0F, 0x1p32F, 0x1p38F })
    for (const bool dual : { false, true }) {
      SCOPED_TRACE(gain);
      SCOPED_TRACE(dual);
      const auto result = render({ gain, dual ? gain * .5F : 0.0F });
      for (unsigned channel = 0; channel < 3; ++channel) {
        const double expected = double(gain)
          * (double(first[channel]) + (dual ? .5 * second[channel] : 0));
        EXPECT_NEAR(
          result[channel], expected, std::abs(expected) * 2e-5 + 0x1p-120);
      }
      EXPECT_EQ(result[3], 0);
      ++cases;
    }
  ctx_.view_constants.reset();
  RecordProperty("distant_sky_range_cases", cases);
  WaitForQueueIdle();
}

NOLINT_TEST_F(ExposureGpuTest, DISABLED_ProducerRangeAndCanonicalTiming)
{
  pass_.reset();
  renderer_->OnShutdown();
  auto config = RendererConfig {};
  config.upload_queue_key = QueueKeyFor().get();
  renderer_ = std::make_unique<Renderer>(GetGraphicsShared(), config,
    kPhase1DefaultRuntimeCapabilityFamilies
      | RendererCapabilityFamily::kEnvironmentLighting);
  pass_ = std::make_unique<postprocess::ExposurePass>(*renderer_);
  auto timestamps = Backend().GetTimestampQueryProvider();
  ASSERT_NE(timestamps, nullptr);
  ASSERT_TRUE(timestamps->EnsureCapacity(2U));
  std::uint64_t frequency = 0U;
  ASSERT_TRUE(GetQueue()->TryGetTimestampFrequency(frequency));
  ASSERT_GT(frequency, 0U);
  const auto measure = [&](const char* name, unsigned width, unsigned height,
                         unsigned iteration,
                         const std::function<void()>& work) {
    WaitForQueueIdle();
    {
      auto recorder = AcquireRecorder("Producer native timing begin");
      ASSERT_TRUE(timestamps->WriteTimestamp(*recorder, 0U));
    }
    work();
    {
      auto recorder = AcquireRecorder("Producer native timing end");
      ASSERT_TRUE(timestamps->WriteTimestamp(*recorder, 1U));
      ASSERT_TRUE(timestamps->RecordResolve(*recorder, 2U));
    }
    WaitForQueueIdle();
    const auto ticks = timestamps->GetResolvedTicks();
    ASSERT_GE(ticks.size(), 2U);
    ASSERT_GE(ticks[1], ticks[0]);
    std::printf(
      "producer_native case=%s width=%u height=%u iteration=%u queue_ms=%.6f\n",
      name, width, height, iteration,
      double(ticks[1] - ticks[0]) * 1000.0 / double(frequency));
  };
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  const auto resolved = SharedConfig(settings);
  const auto capture = BeginOptionalCapture();
  for (const unsigned width : { 1920U, 3840U }) {
    const auto height = width * 9U / 16U;
    const auto signal = Uniform(1.0F, width, height);
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    const auto frame
      = pass_->ResolveFrame(ctx_, resolved, { .use_fp32 = true });
    ASSERT_NE(frame, nullptr);
    for (unsigned iteration = 0; iteration < 3; ++iteration)
      measure("final_range", width, height, iteration, [&] {
        ASSERT_TRUE(pass_->CheckSceneColorRange(
          ctx_, frame, *signal.texture, signal.srv));
      });
    const auto status = Read<ExposureCompletedStatus>(
      *frame->current_state->status_buffer, ResourceStates::kUnorderedAccess);
    EXPECT_EQ(status.flags & 16U, 0U);
  }
  ctx_.current_view.with_atmosphere = true;
  auto view = ViewConstants::GpuData {};
  auto constants
    = CreateUploadBuffer(SizeBytes { 256U }, BufferUsage::kConstant);
  constants->Update(&view, sizeof(view), 0U);
  ctx_.view_constants = constants;
  auto cache = environment::internal::AtmosphereLutCache(*renderer_);
  auto transmittance = environment::AtmosphereTransmittanceLutPass(*renderer_);
  auto multiple = environment::AtmosphereMultiScatteringLutPass(*renderer_);
  auto stable = environment::internal::StableAtmosphereState {};
  stable.view_products.atmosphere.enabled = true;
  for (unsigned iteration = 0; iteration < 3; ++iteration) {
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    stable.atmosphere_revision = sequence_;
    cache.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    cache.RefreshForState(stable);
    transmittance.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    multiple.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    measure("canonical_refresh", 256, 64, iteration, [&] {
      ASSERT_TRUE(transmittance.Record(ctx_, stable, cache).executed);
      ASSERT_TRUE(multiple.Record(ctx_, stable, cache).executed);
    });
  }
  if (capture)
    EXPECT_TRUE(capture->EndCapture());
  ctx_.view_constants.reset();
  WaitForQueueIdle();
}

NOLINT_TEST_F(ExposureGpuTest, DISABLED_ComposedAdmissionFullResolutionTiming)
{
  std::uint32_t width = 960U;
  char* width_text = nullptr;
  std::size_t width_size = 0U;
  if (_dupenv_s(&width_text, &width_size, "OXYGEN_EXPOSURE_TIMING_WIDTH") == 0
    && width_text) {
    width = static_cast<std::uint32_t>(std::strtoul(width_text, nullptr, 10));
    std::free(width_text);
  }
  ASSERT_GE(width, 64U);
  ASSERT_LE(width, 3840U);
  const std::uint32_t height = width * 9U / 16U;
  auto scene = scene::Scene("Admission timing", 1U);
  scene.SetEnvironment(std::make_unique<scene::SceneEnvironment>());
  auto& background
    = scene.GetEnvironment()->AddSystem<scene::environment::Background>();
  background.SetEnabled(true);
  background.SetColorRgb({ .25F, .25F, .25F });
  ctx_.scene = observer_ptr { &scene };
  auto timestamps = Backend().GetTimestampQueryProvider();
  ASSERT_NE(timestamps, nullptr);
  ASSERT_TRUE(timestamps->EnsureCapacity(2U));
  std::uint64_t frequency = 0U;
  ASSERT_TRUE(GetQueue()->TryGetTimestampFrequency(frequency));
  ASSERT_GT(frequency, 0U);
  struct Case {
    const char* name;
    engine::ToneMapper mapper;
    float gamma;
    float alpha;
    float alpha_error;
    float rgb_error;
    bool neutral;
    unsigned views;
  };
  const std::array cases {
    Case {
      "opaque", engine::ToneMapper::kAcesFitted, 2.2F, 1, 0, .0001F, false, 1 },
    Case { "uncertain", engine::ToneMapper::kAcesFitted, 2.2F, .7F, .0001F,
      .0001F, false, 1 },
    Case { "filmic", engine::ToneMapper::kFilmic, 2.2F, .7F, .0001F, .0001F,
      false, 1 },
    Case { "gamma", engine::ToneMapper::kAcesFitted, .8F, .7F, .0001F, .0001F,
      false, 1 },
    Case {
      "wide", engine::ToneMapper::kAcesFitted, 2.2F, .5F, .001F, 0, true, 1 },
    Case { "main_pip", engine::ToneMapper::kAcesFitted, 2.2F, .7F, .0001F,
      .0001F, false, 2 },
  };
  const auto anchor = Uniform(8192.0F);
  const auto capture = BeginOptionalCapture();
  unsigned case_index = 0U;
  for (const auto& test : cases) {
    SCOPED_TRACE(test.name);
    background.SetColorRgb(test.neutral ? Vec3 { 0.0F } : Vec3 { .25F });
    auto settings = scene::ExposureSettings {};
    settings.mode = engine::ExposureMode::kManual;
    settings.key = 12.5F;
    settings.manual_ev = 0.0F;
    const auto resolved
      = ResolvedPostProcessConfig::Resolve({ .exposure = settings,
        .tone_mapper = test.mapper,
        .enable_bloom = false,
        .gamma = test.gamma });
    ASSERT_TRUE(resolved.has_value());
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    struct View {
      Signal signal;
      postprocess::ExposurePass::FrameLease frame;
      ViewId id;
      CompositionView::ViewStateHandle handle;
      std::uint32_t width;
      std::uint32_t height;
    };
    std::vector<View> views;
    for (unsigned index = 0U; index < test.views; ++index) {
      const auto view_width = index == 0U ? width : width / 2U;
      const auto view_height = index == 0U ? height : height / 2U;
      const auto id = 100U + case_index * 2U + index;
      ctx_.current_view.view_id = ViewId { id };
      ctx_.current_view.view_state_handle
        = CompositionView::ViewStateHandle { id };
      const std::array<Pixel, 1> pixel { test.neutral
          ? Pixel { .5F, .5F, .5F, test.alpha }
          : Pixel { .18F * test.alpha, .24F * test.alpha, .35F * test.alpha,
              test.alpha } };
      auto signal = MakeSignal(view_width, view_height, pixel);
      const auto frame
        = pass_->ResolveFrame(ctx_, *resolved, { .use_fp32 = true });
      ASSERT_NE(frame, nullptr);
      ASSERT_TRUE(RecordShared(signal, *resolved).executed);
      const auto error
        = HdrSceneErrorData { .candidate_rgb_relative = test.rgb_error,
            .candidate_coverage_absolute = test.alpha_error,
            .candidate_pre_exposure = 1.0F,
            .checked_products = (1U << 10U) | 1U,
            .flags = 3U };
      auto upload = CreateUploadBuffer(SizeBytes { sizeof(error) });
      upload->Update(&error, sizeof(error), 0U);
      {
        auto recorder = AcquireRecorder("Admission timing certificate");
        EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
        ASSERT_TRUE(recorder->AdoptKnownResourceState(
          *frame->current_state->status_buffer));
        recorder->RequireResourceState(
          *frame->current_state->status_buffer, ResourceStates::kCopyDest);
        recorder->FlushBarriers();
        recorder->CopyBuffer(*frame->current_state->status_buffer,
          offsetof(ExposureStatusStorage, scene_error), *upload, 0U,
          sizeof(error));
        recorder->RequireResourceStateFinal(
          *frame->current_state->status_buffer,
          ResourceStates::kShaderResource);
      }
      views.push_back({ std::move(signal), frame, ctx_.current_view.view_id,
        ctx_.current_view.view_state_handle, view_width, view_height });
    }
    for (unsigned iteration = 0U; iteration < 3U; ++iteration) {
      WaitForQueueIdle();
      {
        auto recorder = AcquireRecorder("Admission native timing begin");
        ASSERT_TRUE(timestamps->WriteTimestamp(*recorder, 0U));
      }
      for (const auto& view : views) {
        ctx_.current_view.view_id = view.id;
        ctx_.current_view.view_state_handle = view.handle;
        const std::array products {
          postprocess::ExposurePass::HdrProduct {
            .texture = anchor.texture.get(), .srv = anchor.srv, .id = 1U },
          postprocess::ExposurePass::HdrProduct {
            .texture = view.signal.texture.get(),
            .srv = view.signal.srv,
            .id = 11U,
            .coverage = true,
            .composed_error = true },
        };
        ASSERT_TRUE(pass_->EvaluateFp16Products(
          ctx_, view.frame, *resolved, products, {}));
      }
      {
        auto recorder = AcquireRecorder("Admission native timing end");
        ASSERT_TRUE(timestamps->WriteTimestamp(*recorder, 1U));
        ASSERT_TRUE(timestamps->RecordResolve(*recorder, 2U));
      }
      WaitForQueueIdle();
      const auto ticks = timestamps->GetResolvedTicks();
      ASSERT_GE(ticks.size(), 2U);
      ASSERT_GE(ticks[1], ticks[0]);
      std::array<std::uint32_t, 2> flags {};
      for (std::size_t index = 0; index < views.size(); ++index) {
        const auto& view = views[index];
        const auto report = Read<HdrSuitabilityData>(
          *view.frame->suitability_buffer, ResourceStates::kShaderResource);
        EXPECT_EQ(report.checked_samples, view.width * view.height + 1U);
        EXPECT_EQ(report.failure_flags & ~4U, 0U);
        flags[index] = report.failure_flags;
      }
      // Queue timestamps include inter-dispatch barriers and any CPU submission
      // gaps. Replay event counters separately measure dispatch execution only.
      std::printf("admission_native case=%s width=%u height=%u views=%u "
                  "iteration=%u queue_ms=%.6f first_flags=%u second_flags=%u\n",
        test.name, width, height, test.views, iteration,
        double(ticks[1] - ticks[0]) * 1000.0 / double(frequency), flags[0],
        flags[1]);
    }
    ++case_index;
  }
  if (capture)
    EXPECT_TRUE(capture->EndCapture());
  ctx_.scene.reset();
}

NOLINT_TEST_F(ExposureGpuTest, SceneDisplayAdmissionChecksGammaAndBackground)
{
  struct Case {
    const char* name;
    float radiance;
    float alpha;
    float gamma;
    float alpha_error;
    bool background;
    float background_value;
    std::uint32_t failure;
    engine::ToneMapper mapper { engine::ToneMapper::kNone };
  };
  const std::array cases {
    Case {
      "gamma reveals below-float-budget loss", 8e-6F, 1, 2.2F, 0, false, 0, 4 },
    Case {
      "linear display keeps loss insignificant", 8e-6F, 1, 1, 0, false, 0, 0 },
    Case { "white background exposes coverage error", 0, .5F, 2.2F, .1F, true,
      1, 4 },
    Case { "black image does not require unused coverage", 0, .5F, 2.2F, .1F,
      true, 0, 0 },
    Case {
      "half coverage changes dark background", 0, .9987F, 2.2F, 0, true, 1, 4 },
    Case { "ACES toe hides tiny radiance", 8e-6F, 1, 2.2F, 0, false, 0, 0,
      engine::ToneMapper::kAcesFitted },
    Case { "Filmic exposes tiny radiance", 8e-6F, 1, 2.2F, 0, false, 0, 4,
      engine::ToneMapper::kFilmic },
    Case { "Reinhard exposes tiny radiance", 8e-6F, 1, 2.2F, 0, false, 0, 4,
      engine::ToneMapper::kReinhard },
  };
  const auto capture = BeginOptionalCapture();
  for (const auto& test : cases) {
    SCOPED_TRACE(test.name);
    auto scene = scene::Scene("Display admission", 1U);
    scene.SetEnvironment(std::make_unique<scene::SceneEnvironment>());
    auto& background
      = scene.GetEnvironment()->AddSystem<scene::environment::Background>();
    background.SetEnabled(test.background);
    background.SetColorRgb(
      { test.background_value, test.background_value, test.background_value });
    ctx_.scene = observer_ptr { &scene };
    auto settings = scene::ExposureSettings {};
    settings.mode = engine::ExposureMode::kManual;
    settings.key = 12.5F;
    settings.manual_ev = 0.0F;
    const auto resolved
      = ResolvedPostProcessConfig::Resolve({ .exposure = settings,
        .tone_mapper = test.mapper,
        .enable_bloom = false,
        .gamma = test.gamma });
    ASSERT_TRUE(resolved.has_value());
    const std::array<Pixel, 1> pixels { Pixel {
      test.radiance, test.radiance, test.radiance, test.alpha } };
    const auto signal = MakeSignal(1U, 1U, pixels);
    const auto anchor = Uniform(0x1p33F);
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    const auto frame
      = pass_->ResolveFrame(ctx_, *resolved, { .use_fp32 = true });
    ASSERT_NE(frame, nullptr);
    ASSERT_TRUE(RecordShared(signal, *resolved).executed);
    const auto error
      = HdrSceneErrorData { .candidate_coverage_absolute = test.alpha_error,
          .candidate_pre_exposure = 0x1p-20F,
          .checked_products = (1U << 10U) | 1U,
          .flags = 3U };
    auto upload = CreateUploadBuffer(SizeBytes { sizeof(error) });
    upload->Update(&error, sizeof(error), 0U);
    {
      auto recorder = AcquireRecorder("Display admission certificate");
      EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
      ASSERT_TRUE(recorder->AdoptKnownResourceState(
        *frame->current_state->status_buffer));
      recorder->RequireResourceState(
        *frame->current_state->status_buffer, ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyBuffer(*frame->current_state->status_buffer,
        offsetof(ExposureStatusStorage, scene_error), *upload, 0U,
        sizeof(error));
      recorder->RequireResourceStateFinal(
        *frame->current_state->status_buffer, ResourceStates::kShaderResource);
    }
    const std::array products {
      postprocess::ExposurePass::HdrProduct {
        .texture = anchor.texture.get(), .srv = anchor.srv, .id = 1U },
      postprocess::ExposurePass::HdrProduct { .texture = signal.texture.get(),
        .srv = signal.srv,
        .id = 11U,
        .coverage = test.background,
        .composed_error = true }
    };
    ASSERT_TRUE(
      pass_->EvaluateFp16Products(ctx_, frame, *resolved, products, {}));
    const auto report = Read<HdrSuitabilityData>(
      *frame->suitability_buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(report.candidate_pre_exposure, 0x1p-20F);
    EXPECT_EQ(report.failure_flags, test.failure);
    EXPECT_EQ(report.metering_failures, 0U);
    ctx_.scene.reset();
  }
  if (capture)
    EXPECT_TRUE(capture->EndCapture());
}

NOLINT_TEST_F(ExposureGpuTest, ProducerStoreBoundsMatchSerialUnsignedMax)
{
  using Words = std::array<std::uint32_t, 4>;
  struct Sample {
    Pixel value;
    Pixel low;
    Pixel high;
  };
  const auto tiny = std::bit_cast<float>(1U);
  const auto infinity = std::numeric_limits<float>::infinity();
  const auto nan = std::numeric_limits<float>::quiet_NaN();
  const std::array samples {
    Sample { { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 } },
    Sample {
      { -0.0F, 0, -0.0F, 0 }, { -0.0F, 0, -0.0F, 0 }, { -0.0F, 0, -0.0F, 0 } },
    Sample { { tiny, tiny, tiny, .5F }, { tiny, tiny, tiny, .5F },
      { tiny, tiny, tiny, .5F } },
    Sample { { 1.0F / 3.0F, .125F, 3.75F, .375F },
      { 1.0F / 3.0F, .125F, 3.75F, .375F },
      { 1.0F / 3.0F, .125F, 3.75F, .375F } },
    Sample { { 2, 1, .5F, .5F }, { 1.875F, .875F, .25F, .375F },
      { 2.125F, 1.25F, .75F, .625F } },
    Sample { { 16, 8, 4, .75F }, { 15, 7, 3, .5F }, { 17, 9, 5, 1 } },
    Sample { { 0x1p-24F, 0x1p-14F, .125F, 1 }, { 0x1p-24F, 0x1p-14F, .125F, 1 },
      { 0x1p-24F, 0x1p-14F, .125F, 1 } },
    Sample { { 1e-20F, 1e-12F, 1e-4F, .875F }, { 0, 0, 0, .5F },
      { 2e-20F, 2e-12F, 2e-4F, 1 } },
  };
  enum class Pattern { kFull, kPartial, kSparse, kInvalidSparse, kZero };
  unsigned cases = 0U;
  unsigned wave_width = 0U;
  for (const unsigned product : { 5U, 6U, 10U }) {
    for (const unsigned fp16 : { 0U, 1U }) {
      for (const auto pattern : { Pattern::kFull, Pattern::kPartial,
             Pattern::kSparse, Pattern::kInvalidSparse, Pattern::kZero }) {
        SCOPED_TRACE(::testing::Message()
          << "product=" << product << " fp16=" << fp16
          << " pattern=" << static_cast<unsigned>(pattern));
        constexpr unsigned lanes = 64U;
        const auto index = product == 5U ? 0U : product == 6U ? 1U : 2U;
        std::array<Words, 4U + lanes * 4U> inputs {};
        inputs[0] = { product, fp16, std::bit_cast<std::uint32_t>(8.0F), 0U };
        std::array<Words, 3> expected { Words { 0x3d000000U, 0x3b800000U, 1U,
                                          0x3e000000U },
          Words { 0x3c000000U, 0x3b000000U, 2U, 0x3d800000U },
          Words { 0x3b000000U, 0x3a800000U, 3U, 0x3d000000U } };
        expected[index] = {};
        if (pattern == Pattern::kPartial) {
          expected[index] = { std::bit_cast<std::uint32_t>(.75F),
            std::bit_cast<std::uint32_t>(.25F), 0U,
            std::bit_cast<std::uint32_t>(.125F) };
        } else if (pattern == Pattern::kZero) {
          expected[index] = { 0x80000000U, 1U, 0x7f800000U, 0x3e800000U };
        }
        for (unsigned peer = 0U; peer < expected.size(); ++peer) {
          inputs[1U + peer] = expected[peer];
        }
        for (unsigned lane = 0U; lane < lanes; ++lane) {
          auto sample = samples[lane % samples.size()];
          unsigned control = 1U;
          if (pattern == Pattern::kPartial) {
            control = lane < 37U ? 1U : 0U;
          } else if (pattern == Pattern::kSparse) {
            control = lane % 7U == 3U ? 3U : 0U;
          } else if (pattern == Pattern::kInvalidSparse) {
            control = lane % 9U == 4U ? 1U : 0U;
            switch (lane % 4U) {
            case 0U: {
              sample.value[0] = nan;
              break;
            }
            case 1U: {
              sample.value[3] = infinity;
              break;
            }
            case 2U: {
              sample.low[1] = -infinity;
              break;
            }
            default: {
              sample.high[2] = nan;
              break;
            }
            }
          } else if (pattern == Pattern::kZero) {
            sample = samples[1U];
            control = 3U;
          }
          if (control == 0U) {
            // Inactive invalid coefficients must not join the reduction.
            sample.value = { nan, infinity, -infinity, nan };
          }
          const auto base = 4U + lane * 4U;
          inputs[base] = std::bit_cast<Words>(sample.value);
          inputs[base + 1U] = std::bit_cast<Words>(sample.low);
          inputs[base + 2U] = std::bit_cast<Words>(sample.high);
          inputs[base + 3U] = { control, 0U, 0U, 0U };
        }
        const auto result = RunToneProbe(
          std::as_bytes(std::span { inputs }), lanes, 16384U, false);
        ASSERT_EQ(result.size(), lanes);
        const auto word = [&](const unsigned offset) {
          return std::bit_cast<std::array<std::uint32_t, 8>>(
            result[offset / 32U])[(offset % 32U) / 4U];
        };
        const auto observed_wave_width = word(0U);
        ASSERT_GT(observed_wave_width, 0U);
        ASSERT_LE(observed_wave_width, 128U);
        if (wave_width == 0U) {
          wave_width = observed_wave_width;
        }
        EXPECT_EQ(observed_wave_width, wave_width);
        EXPECT_EQ(word(4U), lanes);
        EXPECT_EQ(word(8U), product);
        EXPECT_EQ(word(12U), fp16);
        unsigned active_count = 0U;
        for (unsigned lane = 0U; lane < lanes; ++lane) {
          const auto wave_lane = word(1152U + lane * 8U);
          ASSERT_LT(wave_lane, wave_width);
          const auto control = inputs[4U + lane * 4U + 3U][0];
          const bool active
            = (control & 1U) != 0U && ((control & 2U) == 0U || wave_lane != 0U);
          EXPECT_EQ(word(1156U + lane * 8U), active ? 1U : 0U);
          if (active) {
            ++active_count;
            for (unsigned component = 0U; component < 4U; ++component) {
              const auto coefficient = word(128U + lane * 16U + component * 4U);
              // Independent serial reduction: no GPU reduction helper here.
              expected[index][component]
                = std::max(expected[index][component], coefficient);
              if (pattern == Pattern::kZero) {
                EXPECT_EQ(coefficient, 0U);
              }
            }
          }
        }
        ASSERT_GT(active_count, 0U);
        if (pattern == Pattern::kPartial) {
          EXPECT_EQ(active_count, 37U);
        }
        for (unsigned peer = 0U; peer < expected.size(); ++peer) {
          for (unsigned component = 0U; component < 4U; ++component) {
            EXPECT_EQ(word(80U + peer * 16U + component * 4U),
              expected[peer][component]);
          }
        }
        const std::array canaries { 0x13579bdfU, 0x2468ace0U, 0x55aa55aaU,
          0xaa55aa55U };
        for (unsigned region = 0U; region < canaries.size(); ++region) {
          for (unsigned component = 0U; component < 4U; ++component) {
            EXPECT_EQ(
              word(16U + region * 16U + component * 4U), canaries[region]);
          }
        }
        ++cases;
      }
    }
  }
  RecordProperty("publication_cases", cases);
  RecordProperty("probe_wave_lane_count", wave_width);
  RecordProperty("full_group_wave_count", (64U + wave_width - 1U) / wave_width);
}

NOLINT_TEST_F(ExposureGpuTest, QuickToneBoundsCoverColorAndCoverageBoxes)
{
  const std::array levels { 0.0F, .001F, .01F, .05F, .18F, 1.0F, 4.0F, 100.0F };
  std::vector<std::array<float, 4>> inputs;
  for (const float red : levels)
    for (const float green : levels)
      for (const float blue : levels)
        for (const float alpha : { 0.0F, .25F, .7F, 1.0F }) {
          Pixel low { red, green, blue, alpha }, high = low;
          for (unsigned c = 0U; c < 3U; ++c) {
            const float radius = std::max(low[c] * .0001F, 1e-8F);
            low[c] = std::max(0.0F, low[c] - radius);
            high[c] += radius;
          }
          if (alpha > 0.0F && alpha < 1.0F) {
            low[3] -= .0001F;
            high[3] += .0001F;
          }
          inputs.push_back(low);
          inputs.push_back(high);
        }
  const auto count = static_cast<std::uint32_t>(inputs.size() / 2U);
  std::uint32_t mode = 3U; // ACES, gamma 2.2.
  char* mode_text = nullptr;
  std::size_t mode_size = 0U;
  if (_dupenv_s(&mode_text, &mode_size, "OXYGEN_EXPOSURE_TONE_PROBE_MODE") == 0
    && mode_text) {
    mode = static_cast<std::uint32_t>(std::strtoul(mode_text, nullptr, 10));
    std::free(mode_text);
    ASSERT_NE(mode & 1U, 0U);
  }
  const auto results
    = RunToneProbe(std::as_bytes(std::span { inputs }), count, mode);
  for (std::size_t i = 0; i < results.size(); ++i) {
    SCOPED_TRACE(i);
    const auto& result = results[i];
    EXPECT_EQ(result[3], 1.0F);
    for (unsigned c = 0U; c < 3U; ++c) {
      EXPECT_GE(result[c], 0.0F);
      EXPECT_LE(result[c], result[c + 4U]);
      EXPECT_LE(result[c + 4U], 1.0F);
    }
  }
}

NOLINT_TEST_F(
  ExposureGpuTest, HardwareFilterEnclosuresRetainFormatAndHistoryError)
{
  std::vector<std::array<float, 4>> inputs;
  const std::array levels { 0.0F, 0x1p-149F, 0x1p-126F, 0x1p-24F, 0x1p-14F, .5F,
    1.0F, 65504.0F };
  for (const auto value : levels)
    for (const auto relative : { 0.0F, 0x1p-11F, .01F })
      for (const auto gradient : { 0.0F, 0x1p-24F, .125F })
        for (const auto width : { 1.0F, 32.0F, 16384.0F })
          for (const auto half : { 0.0F, 1.0F }) {
            inputs.push_back({ value, relative, 0.0F, half });
            inputs.push_back({ gradient, gradient, gradient, value });
            inputs.push_back({ width, width, 32.0F, 0.0F });
          }
  // Captured half-sampler rounding at the midpoint of two exactly stored
  // half texels. There is no texel-store error to hide the filtering error.
  inputs.push_back({ .5419921875F, 0.0F, 0.0F, 1.0F });
  inputs.push_back({ 0.0F, 0.0F, .06494140625F, .541748046875F });
  inputs.push_back({ 1.0F, 1.0F, 32.0F, 0.0F });
  const auto count = static_cast<std::uint32_t>(inputs.size() / 3U);
  const auto results
    = RunToneProbe(std::as_bytes(std::span { inputs }), count, 32U);
  for (std::size_t i = 0U; i < results.size(); ++i) {
    SCOPED_TRACE(i);
    const auto& interval = results[i];
    const auto reference = double(inputs[i * 3U + 1U][3]);
    EXPECT_LE(double(interval[0]), reference);
    EXPECT_GE(double(interval[1]), reference);
    EXPECT_TRUE(std::isfinite(interval[0]));
    EXPECT_TRUE(std::isfinite(interval[1]));
    if (inputs[i * 3U][1] == 0.0F && inputs[i * 3U][3] == 0.0F) {
      EXPECT_EQ(std::bit_cast<std::uint32_t>(interval[0]),
        std::bit_cast<std::uint32_t>(inputs[i * 3U][0]));
      EXPECT_EQ(std::bit_cast<std::uint32_t>(interval[1]),
        std::bit_cast<std::uint32_t>(inputs[i * 3U][0]));
    }
  }
  RecordProperty("hardware_filter_interval_cases", count);
}

NOLINT_TEST_F(ExposureGpuTest, ToneBoundsEncloseSignedTinyArithmetic)
{
  using Case = std::array<std::uint32_t, 4>;
  std::vector<Case> cases;
  const auto one = std::bit_cast<std::uint32_t>(1.0F);
  const auto large = std::bit_cast<std::uint32_t>(0x1p100F);
  for (const auto sign : { 0U, 0x80000000U }) {
    for (const auto magnitude :
      { 0U, 1U, 0x007fffffU, 0x00800000U, 0x00800001U, 0x00800002U, 0x00800003U,
        0x00800004U, 0x00800005U, 0x00800006U, 0x00800007U, 0x00800008U,
        0x00800009U, 0x3f000000U, 0x3f800000U, 0x71800000U }) {
      const auto value = magnitude | sign;
      cases.push_back({ value, value, one, one });
      cases.push_back({ value, value, large, large });
    }
  }
  cases.push_back({ 1U, 1U, 0x00800000U, 0x00800000U });
  cases.push_back({ 0x80000001U, 0x80000001U, 0x80800000U, 0x80800000U });
  cases.push_back({ 0x80000001U, 1U, large, large });
  const auto results = RunToneProbe(std::as_bytes(std::span { cases }),
    static_cast<std::uint32_t>(cases.size()));
  for (std::size_t i = 0; i < cases.size(); ++i) {
    SCOPED_TRACE(i);
    const auto& result = results[i];
    const auto value = [&](unsigned index) {
      return double(std::bit_cast<float>(cases[i][index]));
    };
    EXPECT_LE(result[0], value(0));
    EXPECT_GE(result[1], value(0));
    for (unsigned left = 0; left < 2; ++left)
      for (unsigned right = 2; right < 4; ++right) {
        // Products of two finite binary32 values are exact in binary64.
        const double sum = value(left) + value(right);
        const double product = value(left) * value(right);
        EXPECT_LE(result[2], sum);
        EXPECT_GE(result[3], sum);
        EXPECT_LE(result[4], product);
        EXPECT_GE(result[5], product);
      }
    for (unsigned endpoint = 0; endpoint < 2; ++endpoint) {
      const double positive = std::max(0.0, value(endpoint));
      EXPECT_LE(result[6], positive * positive);
      EXPECT_GE(result[7], positive * positive);
    }
  }
}

NOLINT_TEST_F(
  ExposureGpuTest, ComposedSceneAdmissionUsesCandidateAndCoverageIntervals)
{
  constexpr auto scene_bit = 1U << 10U;
  struct Case {
    const char* name;
    Pixel pixel;
    HdrSceneErrorData error;
    std::uint32_t failure;
    bool metering { true };
    bool current_store { false };
    std::uint32_t required_products { 0U };
  };
  const auto complete = HdrSceneErrorData {
    .candidate_pre_exposure = 1.0F,
    .checked_products = scene_bit,
    .flags = 3U,
  };
  auto candidate_error = complete;
  candidate_error.candidate_rgb_absolute = .01F;
  auto retained_error = complete;
  retained_error.current_rgb_absolute = .01F;
  auto tiny_error = complete;
  tiny_error.candidate_rgb_absolute = 1e-8F;
  auto coverage_error = complete;
  coverage_error.candidate_coverage_absolute = .001F;
  auto dark_error = complete;
  // Half spacing immediately above 2^-12 is 2^-22; cross its half-way point.
  dark_error.candidate_rgb_absolute = 2e-7F;
  auto stale = complete;
  stale.candidate_pre_exposure = 2.0F;
  auto missing = complete;
  missing.flags = 1U;
  auto incomplete = complete;
  incomplete.checked_products = 0U;
  auto invalid = complete;
  invalid.candidate_rgb_absolute = std::numeric_limits<float>::infinity();
  auto current_only = invalid;
  current_only.flags = 1U;
  current_only.checked_products = scene_bit | (1U << 9U);
  current_only.candidate_pre_exposure = 2.0F;
  auto current_retained = current_only;
  current_retained.current_rgb_absolute = .01F;
  const std::array cases {
    Case { "exact", { 1, 1, 1, 1 }, complete, 0 },
    Case { "upstream candidate differs", { 1, 1, 1, 1 }, candidate_error, 12 },
    Case { "retained reference differs", { 1, 1, 1, 1 }, retained_error, 12 },
    Case { "insignificant candidate", { 1, 1, 1, 1 }, tiny_error, 0 },
    // The bounded runtime display check cannot certify this wide coverage
    // interval. Retain both its unresolved-image and definite mass failures.
    Case { "coverage changes mass and exceeds the cheap enclosure",
      { .5F, .5F, .5F, .5F }, coverage_error, 12 },
    Case { "dark cutoff crossed", { 0x1p-12F, 0x1p-12F, 0x1p-12F, 1 },
      dark_error, 8 },
    Case { "zero weight skips normalization", { 0, 0, 0, 0 }, complete, 0 },
    Case { "another candidate", { 1, 1, 1, 1 }, stale, 16 },
    Case { "missing candidate", { 1, 1, 1, 1 }, missing, 16 },
    Case { "incomplete product mask", { 1, 1, 1, 1 }, incomplete, 16 },
    Case { "invalid coefficient", { 1, 1, 1, 1 }, invalid, 16 },
    Case { "current resolve ignores prospective fields", { 1, 1, 1, 1 },
      current_only, 0, true, true, scene_bit | (1U << 9U) },
    Case { "current resolve requires all producers", { 1, 1, 1, 1 }, complete,
      16, true, true, scene_bit | (1U << 9U) },
    Case { "current resolve preserves retained errors", { 1, 1, 1, 1 },
      current_retained, 12, true, true, scene_bit | (1U << 9U) },
  };
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.key = 12.5F;
  settings.manual_ev = 0.0F;
  const auto config = SharedConfig(settings);
  const auto capture = BeginOptionalCapture();
  for (const auto& test : cases) {
    SCOPED_TRACE(test.name);
    // An exact 8192 anchor fixes candidate P=1 independently of the case.
    const std::array pixels { test.pixel, Pixel { 8192, 8192, 8192, 1 } };
    const auto signal = MakeSignal(2U, 1U, pixels);
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    const auto frame = pass_->ResolveFrame(ctx_, config, { .use_fp32 = true });
    ASSERT_NE(frame, nullptr);
    ASSERT_TRUE(RecordShared(signal, config).executed);
    const auto cleared = Read<ExposureStatusStorage>(
      *frame->current_state->status_buffer, ResourceStates::kCopySource);
    EXPECT_EQ(cleared.scene_error.flags, 0U);
    auto upload = CreateUploadBuffer(SizeBytes { sizeof(test.error) });
    upload->Update(&test.error, sizeof(test.error), 0U);
    {
      auto recorder = AcquireRecorder("Composed admission fixture");
      EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
      ASSERT_TRUE(recorder->AdoptKnownResourceState(
        *frame->current_state->status_buffer));
      recorder->RequireResourceState(
        *frame->current_state->status_buffer, ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyBuffer(*frame->current_state->status_buffer,
        offsetof(ExposureStatusStorage, scene_error), *upload, 0U,
        sizeof(test.error));
      recorder->RequireResourceStateFinal(
        *frame->current_state->status_buffer, ResourceStates::kShaderResource);
    }
    const std::array products { postprocess::ExposurePass::HdrProduct {
      .texture = signal.texture.get(),
      .srv = signal.srv,
      .id = 11U,
      .metering = test.metering,
      .coverage = true,
      .composed_error = true } };
    ASSERT_TRUE(pass_->EvaluateFp16Products(ctx_, frame, config, products,
      { .composition_products = test.required_products },
      test.current_store
        ? postprocess::ExposurePass::SuitabilityScale::kCurrentFrame
        : postprocess::ExposurePass::SuitabilityScale::kCandidate));
    const auto report = Read<HdrSuitabilityData>(
      *(test.current_store ? frame->conversion_buffer
                           : frame->suitability_buffer),
      ResourceStates::kShaderResource);
    EXPECT_EQ(report.candidate_pre_exposure, 1.0F);
    EXPECT_EQ(report.failure_flags, test.failure);
    EXPECT_EQ(report.checked_samples, 2U);
    if (test.current_store)
      continue;
    ASSERT_TRUE(pass_->FinalizeFp16Suitability(ctx_, frame,
      { .product_layout_revision = 1U,
        .expected_products = scene_bit,
        .invalidate_previous = true }));
    const auto status = Read<ExposureStatusStorage>(
      *frame->current_state->status_buffer, ResourceStates::kUnorderedAccess);
    EXPECT_EQ(
      status.completed.fp16_eligible_streak, test.failure == 0U ? 1U : 0U);
    EXPECT_EQ(status.scene_error.flags, test.error.flags);
  }
  if (capture)
    EXPECT_TRUE(capture->EndCapture());
}

NOLINT_TEST_F(
  ExposureGpuTest, FinalSceneRangePreservesOpaqueCertificateAndValidHistory)
{
  auto settings = scene::ExposureSettings {};
  settings.key = 12.5F;
  const auto prior = Run(Uniform(.25F), settings);
  const auto config = SharedConfig(settings);
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  const auto frame = pass_->ResolveFrame(ctx_, config, { .use_fp32 = true });
  ASSERT_NE(frame, nullptr);
  const auto opaque = Uniform(.5F, 4U, 4U);
  ASSERT_TRUE(pass_->CapturePreEnvironmentRange(
    ctx_, frame, *opaque.texture, opaque.srv));
  const auto before = Read<ExposureStatusStorage>(
    *frame->current_state->status_buffer, ResourceStates::kUnorderedAccess);
  const auto unsupported = Uniform(0x1p33F, 4U, 4U);
  ASSERT_TRUE(pass_->CheckSceneColorRange(
    ctx_, frame, *unsupported.texture, unsupported.srv));
  const auto after = Read<ExposureStatusStorage>(
    *frame->current_state->status_buffer, ResourceStates::kUnorderedAccess);
  EXPECT_EQ(std::memcmp(&before.composition_input, &after.composition_input,
              sizeof(before.composition_input)),
    0);
  EXPECT_TRUE(pass_->HasPreEnvironmentRange(frame));
  EXPECT_EQ(after.completed.flags & 18U, 18U);
  EXPECT_EQ(after.completed.first_failure_product, 11U);
  EXPECT_EQ(after.completed.first_failure_kind, 32U);
  const auto solved = RecordShared(unsupported, config);
  ASSERT_TRUE(solved.executed);
  const auto state = ReadState(solved);
  EXPECT_EQ(state.displayed_scale, prior.state.displayed_scale);
  EXPECT_EQ(state.latent_scale, prior.state.latent_scale);
  EXPECT_EQ(state.flags & 12U, 0U);
  EXPECT_NE(state.flags & 32U, 0U);
}

NOLINT_TEST_F(
  ExposureGpuTest, ProducerDomainDistinguishesFloatRangeFromHalfHeadroom)
{
  const auto infinity = std::numeric_limits<float>::infinity();
  const auto tiny = std::numeric_limits<float>::denorm_min();
  const std::array scene_values { 0.0F, -0.0F, tiny, -tiny, 0x1p-25F, 0x1p-24F,
    1.0F, std::nextafter(0x1p32F, 0.0F), 0x1p32F,
    std::nextafter(0x1p32F, infinity), -1.0F, infinity,
    std::numeric_limits<float>::quiet_NaN() };
  std::vector<std::array<float, 4>> inputs;
  std::vector<unsigned> expected;
  for (const float p :
    { 0x1p-32F, .1F, .6184799671173096F, 1.0F, 1.1F, 3.1415927F, 0x1p32F })
    for (const float scene : scene_values)
      for (unsigned channel = 0; channel < 3; ++channel)
        for (const bool half : { false, true }) {
          std::array<float, 4> value { 0, 0, 0, 1 };
          value[channel] = scene * p;
          inputs.push_back(value);
          inputs.push_back({ p, half ? 1.0F : 0.0F, 0, 0 });
          unsigned flags = 0;
          const double reference = double(value[channel]) / double(p);
          if (!std::isfinite(value[channel])) {
            flags = 1U;
          } else {
            // The oracle uses the supplied float's exact mathematical value;
            // it does not call production bound or classification helpers.
            if (reference < 0 || reference > 4294967296.0)
              flags |= 32U;
            if (half && std::abs(double(value[channel])) > 16376.0)
              flags |= 2U;
          }
          expected.push_back(flags);
        }
  for (const float invalid_p : { 0.0F, -1.0F, 0x1p-33F, 0x1p33F, infinity }) {
    inputs.push_back({ 1, 1, 1, 1 });
    inputs.push_back({ invalid_p, 0, 0, 0 });
    expected.push_back(std::isfinite(invalid_p) ? 32U : 1U);
  }
  const auto results = RunToneProbe(std::as_bytes(std::span { inputs }),
    static_cast<std::uint32_t>(expected.size()), 128U);
  ASSERT_EQ(results.size(), expected.size());
  for (std::size_t i = 0; i < expected.size(); ++i)
    EXPECT_EQ(results[i][0], float(expected[i])) << "case=" << i;
  RecordProperty("producer_domain_cases", expected.size());
}

NOLINT_TEST_F(ExposureGpuTest,
  FinalSceneRangeFailureInvalidatesAdmissionAndAllowsFreshRetry)
{
  auto service = PostProcessService(*renderer_);
  auto config = PostProcessConfig {};
  config.exposure.key = 12.5F;
  service.SetConfig(config);
  const auto signal = Uniform(1.0F, 4U, 4U);
  const auto requirements = postprocess::ExposurePass::EligibilityInputs {
    .product_layout_revision = 7U, .expected_products = 1024U
  };
  const std::array products { postprocess::ExposurePass::HdrProduct {
    .texture = signal.texture.get(),
    .srv = signal.srv,
    .id = 11U,
    .metering = true } };
  const auto transition = renderer_->QueueExposureTransition(
    ctx_.current_view.view_state_handle, ExposureTransitionPolicy::kRemeter);
  ASSERT_TRUE(transition.has_value());
  auto& backend = static_cast<ExposureFailureGraphics&>(Backend());
  for (unsigned step = 0; step < 5; ++step) {
    SCOPED_TRACE(step);
    WaitForQueueIdle();
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    ctx_.frame_slot = frame::Slot { unsigned(sequence_ % 3U) };
    service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    const auto candidate = service.SelectPrecisionCandidate(ctx_, requirements);
    EXPECT_EQ(candidate != nullptr, step == 2U || step == 4U);
    ctx_.current_view.frame_exposure = service.PrepareFrameExposure(ctx_, true);
    ASSERT_NE(ctx_.current_view.frame_exposure, nullptr);
    auto prepared = service.PrepareSceneExposure(ctx_.current_view.view_id,
      ctx_,
      { .scene_signal = signal.texture.get(), .scene_signal_srv = signal.srv });
    ASSERT_TRUE(prepared.has_value());
    const auto before = ReadState(prepared->exposure);
    if (step == 2U) {
      backend.fail_recorder_name = "Vortex Exposure Final Scene Range";
      EXPECT_FALSE(
        service.CheckSceneColorRange(ctx_, *signal.texture, signal.srv));
      backend.fail_recorder_name.clear();
      EXPECT_EQ(service.SelectPrecisionCandidate(ctx_, requirements), nullptr);
      EXPECT_FALSE(service.PrepareScenePrecision(ctx_, *prepared, products));
      EXPECT_FALSE(service.FinalizeScenePrecision(ctx_, *prepared));
      ASSERT_TRUE(
        service.CheckSceneColorRange(ctx_, *signal.texture, signal.srv));
      prepared = service.PrepareSceneExposure(ctx_.current_view.view_id, ctx_,
        { .scene_signal = signal.texture.get(),
          .scene_signal_srv = signal.srv });
      ASSERT_TRUE(prepared.has_value());
    }
    ASSERT_TRUE(service.PrepareScenePrecision(ctx_, *prepared, products));
    ASSERT_TRUE(service.FinalizeScenePrecision(ctx_, *prepared));
    const auto after = ReadState(prepared->exposure);
    EXPECT_EQ(after.displayed_scale, before.displayed_scale);
    EXPECT_EQ(after.latent_scale, before.latent_scale);
    EXPECT_EQ(after.applied_generation, before.applied_generation);
    EXPECT_EQ(after.fp16_eligible_streak, step == 0U || step == 2U ? 1U : 2U);
  }
  const auto status
    = renderer_->InspectExposureTransition(ctx_.current_view.view_state_handle);
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(status->applied_generation, transition->generation);
}

NOLINT_TEST_F(
  ExposureGpuTest, CanonicalAtmosphereStoresPreserveAmplifiedSmallTransfers)
{
  auto cache = environment::internal::AtmosphereLutCache(*renderer_);
  auto stable = environment::internal::StableAtmosphereState {};
  stable.view_products.atmosphere.enabled = true;
  stable.atmosphere_revision = 1U;
  cache.RefreshForState(stable);
  ASSERT_TRUE(cache.EnsureResources());
  constexpr float transfer = 1.0e-12F;
  constexpr float radiance = 1.88e9F;
  const std::array textures { cache.GetTransmittanceTexture(),
    cache.GetMultiScatteringTexture() };
  std::uint64_t allocation_bytes = 0U;
  std::uint64_t half_allocation_bytes = 0U;
  for (const auto& texture : textures) {
    ASSERT_NE(texture, nullptr);
    const auto& desc = texture->GetDescriptor();
    ASSERT_EQ(desc.format, Format::kRGBA32Float);
    auto native_desc
      = texture->GetNativeResource()->AsPointer<ID3D12Resource>()->GetDesc();
    auto* device
      = static_cast<ExposureFailureGraphics&>(Backend()).GetCurrentDevice();
    allocation_bytes
      += device->GetResourceAllocationInfo(0U, 1U, &native_desc).SizeInBytes;
    native_desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    half_allocation_bytes
      += device->GetResourceAllocationInfo(0U, 1U, &native_desc).SizeInBytes;
    const std::vector<Pixel> values(std::size_t(desc.width) * desc.height,
      Pixel { transfer, transfer, transfer, 0 });
    const auto bytes = std::as_bytes(std::span { values });
    auto upload = CreateUploadBuffer(SizeBytes { bytes.size() });
    upload->Update(bytes.data(), bytes.size(), 0U);
    auto recorder = AcquireRecorder("Canonical small transfer upload");
    EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
    if (!recorder->AdoptKnownResourceState(*texture))
      recorder->BeginTrackingResourceState(*texture, desc.initial_state);
    recorder->RequireResourceState(*texture, ResourceStates::kCopyDest);
    recorder->FlushBarriers();
    recorder->CopyBufferToTexture(*upload,
      { .buffer_offset = 0U,
        .buffer_row_pitch = desc.width * 16U,
        .buffer_slice_pitch = std::uint64_t(desc.width) * desc.height * 16U,
        .dst_slice
        = { .width = desc.width, .height = desc.height, .depth = 1U } },
      *texture);
    recorder->RequireResourceStateFinal(
      *texture, ResourceStates::kShaderResource);
  }
  const std::array<Pixel, 1> tiny_pixel { Pixel {
    transfer, transfer, transfer, 0 } };
  const auto half_control
    = MakeSignal(1U, 1U, tiny_pixel, 1U, Format::kRGBA16Float);
  const std::array<std::array<std::uint32_t, 4>, 3> inputs {
    std::array { cache.GetState().transmittance_lut_srv.get(),
      std::bit_cast<std::uint32_t>(radiance), 0U, 0U },
    std::array { cache.GetState().multi_scattering_lut_srv.get(),
      std::bit_cast<std::uint32_t>(radiance), 0U, 0U },
    std::array {
      half_control.srv.get(), std::bit_cast<std::uint32_t>(radiance), 0U, 0U }
  };
  const auto results
    = RunToneProbe(std::as_bytes(std::span { inputs }), 3U, 256U);
  for (unsigned i = 0; i < 2; ++i)
    for (unsigned c = 0; c < 3; ++c) {
      EXPECT_NEAR(results[i][c], double(transfer) * radiance,
        double(transfer) * radiance * 2e-5);
      EXPECT_EQ(results[i][4 + c], transfer);
    }
  EXPECT_EQ(
    results[2][0], 0.0F); // The old half format erases the required signal.
  for (const auto format : { Format::kRGBA16Float, Format::kRGBA32Float }) {
    ctx_.current_view.hdr_color_format = format;
    cache.RefreshForState(stable);
    EXPECT_EQ(cache.GetTransmittanceTexture(), textures[0]);
    EXPECT_EQ(cache.GetMultiScatteringTexture(), textures[1]);
  }
  RecordProperty(
    "canonical_fp32_placement_bytes", std::to_string(allocation_bytes));
  RecordProperty(
    "same_shape_fp16_placement_bytes", std::to_string(half_allocation_bytes));
  RecordProperty("canonical_generations_across_view_modes", 1);
  WaitForQueueIdle();
}

NOLINT_TEST_F(
  ExposureGpuTest, CanonicalAtmosphereProducersRetainTinyIlluminatedSignals)
{
  pass_.reset();
  renderer_->OnShutdown();
  auto config = RendererConfig {};
  config.upload_queue_key = QueueKeyFor().get();
  renderer_ = std::make_unique<Renderer>(GetGraphicsShared(), config,
    kPhase1DefaultRuntimeCapabilityFamilies
      | RendererCapabilityFamily::kEnvironmentLighting);
  ctx_.current_view.with_atmosphere = true;
  auto view_data = ViewConstants::GpuData {};
  auto view_buffer
    = CreateUploadBuffer(SizeBytes { 256U }, BufferUsage::kConstant);
  view_buffer->Update(&view_data, sizeof(view_data), 0U);
  ctx_.view_constants = view_buffer;
  auto cache = environment::internal::AtmosphereLutCache(*renderer_);
  auto transmittance = environment::AtmosphereTransmittanceLutPass(*renderer_);
  auto scattering = environment::AtmosphereMultiScatteringLutPass(*renderer_);
  auto stable = environment::internal::StableAtmosphereState {};
  auto& atmosphere = stable.view_products.atmosphere;
  atmosphere.enabled = true;
  atmosphere.planet_radius_m = 1000;
  atmosphere.atmosphere_height_m = 1000;
  atmosphere.rayleigh_scattering_rgb = {};
  atmosphere.mie_scattering_rgb = {};
  atmosphere.mie_absorption_rgb = {};
  atmosphere.ozone_absorption_rgb = { .0276F, .0001F, 0 };
  atmosphere.ozone_density_profile.layers[0]
    = { .width_m = 2000, .constant_term = 1 };
  atmosphere.ozone_density_profile.layers[1] = { .constant_term = 1 };
  const auto begin = [&](unsigned sequence) {
    ctx_.frame_sequence = frame::SequenceNumber { sequence };
    ctx_.frame_slot = frame::Slot { (sequence - 1U) % 3U };
    stable.atmosphere_revision = sequence;
    cache.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    cache.RefreshForState(stable);
    transmittance.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    scattering.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
  };
  begin(1U);
  ASSERT_TRUE(transmittance.Record(ctx_, stable, cache).executed);
  const auto trans_pixels = ReadFloatTexture(*cache.GetTransmittanceTexture());
  const auto& trans_desc = cache.GetTransmittanceTexture()->GetDescriptor();
  // Constant absorption makes Beer-Lambert independent of the integration
  // quadrature. Geometry is reconstructed in double from the documented LUT UV.
  const double horizon = std::sqrt(3.0);
  const double rho = (.5 / trans_desc.height) * horizon;
  const double radius = std::sqrt(1 + rho * rho);
  const double length
    = 2 - radius + (.5 / trans_desc.width) * (rho + horizon - (2 - radius));
  const double expected_trans
    = std::exp(-double(float(.0276F * 1000.0F)) * length);
  ASSERT_GT(expected_trans, 0.0);
  EXPECT_NEAR(trans_pixels[0][0], expected_trans, expected_trans * 2e-5);
  EXPECT_EQ(data::HalfFloat { trans_pixels[0][0] }.ToFloat(), 0.0F);
  const auto check_illumination = [&](const Texture& texture,
                                    ShaderVisibleIndex srv, unsigned pixel,
                                    unsigned channel, const Pixel& value) {
    const auto& desc = texture.GetDescriptor();
    const auto half
      = MakeSignal(1U, 1U, std::span { &value, 1U }, 1U, Format::kRGBA16Float);
    constexpr float gain = 1.88e9F;
    // Slot 3 is the renderer's registered linear-clamp sampler.
    const std::array<std::array<std::uint32_t, 4>, 4> inputs {
      std::array { srv.get(), 3U,
        std::bit_cast<std::uint32_t>(
          (float(pixel % desc.width) + .5F) / float(desc.width)),
        std::bit_cast<std::uint32_t>(
          (float(pixel / desc.width) + .5F) / float(desc.height)) },
      std::array { std::bit_cast<std::uint32_t>(gain), 0U, 0U, 0U },
      std::array { half.srv.get(), 3U, std::bit_cast<std::uint32_t>(.5F),
        std::bit_cast<std::uint32_t>(.5F) },
      std::array { std::bit_cast<std::uint32_t>(gain), 0U, 0U, 0U }
    };
    const auto result
      = RunToneProbe(std::as_bytes(std::span { inputs }), 2U, 512U);
    const double expected = double(value[channel]) * gain;
    EXPECT_GE(expected, 0x1p-24);
    EXPECT_LE(expected, 0x1p32);
    EXPECT_NEAR(result[0][channel], expected, expected * 2e-5);
    EXPECT_EQ(result[1][channel], 0.0F);
  };
  check_illumination(*cache.GetTransmittanceTexture(),
    cache.GetState().transmittance_lut_srv, 0U, 0U, trans_pixels[0]);
  atmosphere = environment::AtmosphereModel {};
  atmosphere.enabled = true;
  begin(2U);
  ASSERT_TRUE(transmittance.Record(ctx_, stable, cache).executed);
  ASSERT_TRUE(scattering.Record(ctx_, stable, cache).executed);
  const auto baseline = ReadFloatTexture(*cache.GetMultiScatteringTexture());
  unsigned brightest = 0U, channel = 0U;
  for (unsigned pixel = 0; pixel < baseline.size(); ++pixel)
    for (unsigned c = 0; c < 3; ++c)
      if (baseline[pixel][c] > baseline[brightest][channel]) {
        brightest = pixel;
        channel = c;
      }
  ASSERT_GT(baseline[brightest][channel], 0.0F);
  atmosphere.multi_scattering_factor = 0x1p-30F;
  begin(3U);
  ASSERT_TRUE(transmittance.Record(ctx_, stable, cache).executed);
  ASSERT_TRUE(scattering.Record(ctx_, stable, cache).executed);
  const auto tiny = ReadFloatTexture(*cache.GetMultiScatteringTexture());
  const double expected_scattering
    = double(baseline[brightest][channel]) * 0x1p-30;
  EXPECT_NEAR(
    tiny[brightest][channel], expected_scattering, expected_scattering * 2e-5);
  check_illumination(*cache.GetMultiScatteringTexture(),
    cache.GetState().multi_scattering_lut_srv, brightest, channel,
    tiny[brightest]);
  ctx_.view_constants.reset();
  WaitForQueueIdle();
}

NOLINT_TEST_F(
  ExposureGpuTest, ThinMediaArithmeticPreservesAmplifiedRequiredSignals)
{
  std::vector<std::array<float, 4>> inputs;
  for (const float depth : { -.1F, -.01F, -1e-8F, 0.0F, 0x1p-40F, 0x1p-24F,
         1e-5F, .00999F, .01F, .1F, 1.0F, 10.0F })
    for (const float source : { 0x1p-24F, 1.0F, 0x1p32F })
      inputs.push_back({ depth, source, 0, 0 });
  const auto output = RunToneProbe(std::as_bytes(std::span { inputs }),
    static_cast<std::uint32_t>(inputs.size()), 1024U);
  unsigned reproduced_losses = 0;
  for (std::size_t i = 0; i < inputs.size(); ++i) {
    const double opacity = -std::expm1(-double(inputs[i][0]));
    const double input_opacity = std::min(std::abs(double(inputs[i][0])), 1.0);
    const double optical_depth = input_opacity < 1.0 - double(1e-6F)
      ? -std::log1p(-input_opacity)
      : -std::log(double(1e-6F));
    const double radiance = opacity * double(inputs[i][1]);
    EXPECT_NEAR(output[i][0], opacity, std::abs(opacity) * 2e-5 + 0x1p-120);
    EXPECT_NEAR(output[i][1], optical_depth, optical_depth * 2e-5 + 0x1p-120);
    EXPECT_NEAR(output[i][2], radiance, std::abs(radiance) * 2e-5 + 0x1p-120);
    if (radiance >= 0x1p-24 && output[i][3] == 0) {
      EXPECT_GT(output[i][2], 0);
      ++reproduced_losses;
    }
  }
  EXPECT_GT(reproduced_losses, 0U);
}

NOLINT_TEST_F(ExposureGpuTest, SkyProducerPreservesThinBrightScattering)
{
  pass_.reset();
  renderer_->OnShutdown();
  auto config = RendererConfig {};
  config.upload_queue_key = QueueKeyFor().get();
  renderer_ = std::make_unique<Renderer>(GetGraphicsShared(), config,
    kPhase1DefaultRuntimeCapabilityFamilies
      | RendererCapabilityFamily::kEnvironmentLighting);
  pass_ = std::make_unique<postprocess::ExposurePass>(*renderer_);
  ctx_.current_view.with_atmosphere = true;
  ctx_.current_view.hdr_color_format = Format::kRGBA32Float;
  auto view_data = ViewConstants::GpuData {};
  auto view_buffer
    = CreateUploadBuffer(SizeBytes { 256U }, BufferUsage::kConstant);
  view_buffer->Update(&view_data, sizeof(view_data), 0U);
  ctx_.view_constants = view_buffer;
  auto cache = environment::internal::AtmosphereLutCache(*renderer_);
  auto transmittance = environment::AtmosphereTransmittanceLutPass(*renderer_);
  auto multiple = environment::AtmosphereMultiScatteringLutPass(*renderer_);
  auto sky = environment::AtmosphereSkyViewLutPass(*renderer_);
  auto stable = environment::internal::StableAtmosphereState {};
  auto& atmosphere = stable.view_products.atmosphere;
  atmosphere.enabled = true;
  atmosphere.planet_radius_m = 1000;
  atmosphere.atmosphere_height_m = 1000;
  atmosphere.rayleigh_scale_height_m = 1e15F;
  atmosphere.mie_scattering_rgb = {};
  atmosphere.mie_absorption_rgb = {};
  atmosphere.ozone_absorption_rgb = {};
  atmosphere.ground_albedo_rgb = {};
  atmosphere.multi_scattering_factor = 0;
  auto environment_view = EnvironmentViewData {};
  environment_view.sky_planet_translated_world_center_km_and_view_height_km
    = { 0, 0, -1, 1.25F };
  unsigned sequence = 0;
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  unsigned cases = 0;
  for (const unsigned light_slot : { 0U, 1U })
    for (const float illuminance : { .5e-6F, 1e-6F, 1.001e-6F, 1.88e9F })
      for (const float ev : { -32.0F, 0.0F, 32.0F }) {
        stable.view_products.atmosphere_lights = {};
        auto& light = stable.view_products.atmosphere_lights[light_slot];
        light.enabled = true;
        light.direction_to_light_ws = { 0, 0, 1 };
        light.illuminance_rgb_lux = glm::vec3 { illuminance };
        stable.view_products.atmosphere_light_count = light_slot + 1;
        settings.manual_ev = ev;
        const auto exposure_config = SharedConfig(settings);
        SCOPED_TRACE(light_slot);
        SCOPED_TRACE(illuminance);
        SCOPED_TRACE(ev);
        for (const float extinction :
          { 0.0F, 1e-12F, .999e-9F, 1e-9F, 1.001e-9F, 1e-6F }) {
          SCOPED_TRACE(extinction);
          atmosphere.rayleigh_scattering_rgb
            = glm::vec3 { extinction / 1000.0F };
          ctx_.frame_sequence = frame::SequenceNumber { ++sequence };
          ctx_.frame_slot = frame::Slot { (sequence - 1U) % 3U };
          const auto exposure
            = pass_->ResolveFrame(ctx_, exposure_config, { .use_fp32 = false });
          ASSERT_NE(exposure, nullptr);
          ctx_.current_view.frame_exposure = exposure;
          auto bindings = ViewFrameBindings {};
          bindings.frame_exposure_slot = exposure->srv_index;
          bindings.exposure_status_uav
            = exposure->current_state->status_uav_index;
          view_data.view_frame_bindings_bslot
            = BindlessViewFrameBindingsSlot { PublishFixtureData(bindings) };
          view_buffer->Update(&view_data, sizeof(view_data), 0U);
          stable.atmosphere_revision = sequence;
          cache.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
          cache.RefreshForState(stable);
          transmittance.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
          multiple.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
          sky.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
          const auto capture = light_slot == 1 && illuminance == .5e-6F
              && ev == -32 && extinction == 1e-6F
            ? BeginOptionalCapture()
            : observer_ptr<FrameCaptureController> {};
          ASSERT_TRUE(transmittance.Record(ctx_, stable, cache).executed);
          ASSERT_TRUE(multiple.Record(ctx_, stable, cache).executed);
          const auto produced
            = sky.Record(ctx_, environment_view, stable, cache);
          ASSERT_TRUE(produced.executed);
          const auto pixels = ReadFloatTexture(*produced.texture);
          // Row zero is exactly zenith. Constant-density, single Rayleigh
          // scattering toward a zenith sun has constant total light+view
          // attenuation along the .75 km ray: L = E * phase(1) * sigma * d *
          // exp(-sigma*d).
          const double sigma
            = double(atmosphere.rayleigh_scattering_rgb.x) * 1000;
          const double expected = double(light.illuminance_rgb_lux.x)
            * (3 / (8 * std::acos(-1.0))) * sigma * .75 * std::exp(-sigma * .75)
            * std::exp2(-double(ev));
          for (unsigned x = 0; x < produced.width; ++x)
            for (unsigned c = 0; c < 3; ++c)
              EXPECT_NEAR(pixels[x][c], expected, expected * 2e-5 + 0x1p-120);
          if (capture)
            EXPECT_TRUE(capture->EndCapture());
          const auto status = Read<ExposureCompletedStatus>(
            *exposure->current_state->status_buffer,
            ResourceStates::kCopySource);
          EXPECT_EQ(status.flags & 16U, 0U);
          ++cases;
        }
      }
  RecordProperty("sky_scattering_endpoint_cases", cases);
  ctx_.current_view.frame_exposure.reset();
  ctx_.view_constants.reset();
  WaitForQueueIdle();
}

NOLINT_TEST_F(ExposureGpuTest, ThinScatteringIntegralHasContinuousVacuumLimit)
{
  std::vector<Pixel> inputs;
  for (const float extinction : { 0.0F, 1e-12F, 0.999e-9F, 1e-9F, 1.001e-9F,
         1e-5F, .00999F, .01F, .1F, 1.0F, 100.0F })
    for (const float distance : { 0.0F, 1e-3F, 1.0F, 100.0F })
      for (const float source : { .0001496056465063816F, 1.0F, 0x1p32F })
        inputs.push_back({ extinction, distance, source, 0 });
  const auto output = RunToneProbe(std::as_bytes(std::span { inputs }),
    static_cast<std::uint32_t>(inputs.size()), 2048U);
  for (std::size_t i = 0; i < inputs.size(); ++i) {
    SCOPED_TRACE(i);
    const double extinction = inputs[i][0], distance = inputs[i][1];
    const double integral = extinction == 0
      ? distance
      : -std::expm1(-extinction * distance) / extinction;
    const double radiance = integral * inputs[i][2];
    EXPECT_NEAR(output[i][0], integral, integral * 2e-5 + 0x1p-120);
    EXPECT_NEAR(output[i][1], radiance, radiance * 2e-5 + 0x1p-120);
  }
  RecordProperty("continuous_integral_cases", inputs.size());
}

NOLINT_TEST_F(ExposureGpuTest, AuthoredLocalFogPreservesRadiometryThroughUpload)
{
  pass_.reset();
  renderer_->OnShutdown();
  auto renderer_config = RendererConfig {};
  renderer_config.upload_queue_key = QueueKeyFor().get();
  renderer_ = std::make_unique<Renderer>(GetGraphicsShared(), renderer_config,
    kPhase1DefaultRuntimeCapabilityFamilies
      | RendererCapabilityFamily::kEnvironmentLighting);
  pass_ = std::make_unique<postprocess::ExposurePass>(*renderer_);
  console::Console console;
  renderer_->RegisterConsoleBindings(observer_ptr { &console });
  ASSERT_EQ(console.Execute("vtx.local_fog.global_start_distance_m 0").status,
    console::ExecutionStatus::kOk);
  auto fog_scene
    = std::make_shared<scene::Scene>("Local fog radiometric domain", 8U);
  auto node = fog_scene->CreateNode("Authored local fog");
  auto impl = node.GetImpl();
  ASSERT_TRUE(impl.has_value());
  auto& fog = impl->get().AddComponent<scene::environment::LocalFogVolume>();
  fog.SetEnabled(true);
  fog.SetFogAlbedo({ 0, 0, 0 });
  fog.SetHeightFogOffset(0);
  node.GetTransform().SetLocalScale({ .2F, .2F, .2F });
  fog_scene->Update();
  auto resolved_params = ResolvedView::Params {};
  resolved_params.view_config.viewport = { .width = 1, .height = 1 };
  resolved_params.view_config.scissor = { .right = 1, .bottom = 1 };
  auto lens = scene::PerspectiveCamera {};
  lens.SetViewport(resolved_params.view_config.viewport);
  resolved_params.proj_matrix = lens.ProjectionMatrix();
  auto resolved = ResolvedView { resolved_params };
  ctx_.scene = observer_ptr { fog_scene.get() };
  ctx_.current_view.resolved_view = observer_ptr { &resolved };
  ctx_.current_view.with_local_fog = true;
  auto state = environment::internal::LocalFogVolumeState(*renderer_);
  auto compose = environment::LocalFogVolumeComposePass(*renderer_);
  auto textures = SceneTextures(Backend(),
    { .extent = { 1U, 1U },
      .enable_velocity = false,
      .scene_color_format = Format::kRGBA32Float });
  auto framebuffer = Backend().CreateFramebuffer(FramebufferDesc {}
      .AddColorAttachment(textures.GetSceneColorResource())
      .SetDepthAttachment({ .texture = textures.GetSceneDepthResource(),
        .format = textures.GetSceneDepth().GetDescriptor().format }));
  auto tiles = CreateRegisteredTexture({ .width = 1,
    .height = 1,
    .array_size = 2,
    .format = Format::kR32UInt,
    .texture_type = TextureType::kTexture2DArray,
    .is_shader_resource = true,
    .initial_state = ResourceStates::kCommon });
  auto tile_upload = CreateUploadBuffer(SizeBytes { 1024U });
  std::array<std::uint32_t, 256> tile_values {};
  tile_values[0] = 1U; // One volume, instance index zero in array slice one.
  tile_upload->Update(tile_values.data(), sizeof(tile_values), 0U);
  {
    auto recorder = AcquireRecorder("Local fog tile fixture upload");
    EnsureTracked(*recorder, tile_upload, ResourceStates::kGenericRead);
    EnsureTracked(*recorder, tiles, ResourceStates::kCommon);
    recorder->RequireResourceState(*tiles, ResourceStates::kCopyDest);
    recorder->FlushBarriers();
    recorder->CopyBufferToTexture(*tile_upload,
      { .buffer_offset = 0U,
        .buffer_row_pitch = 256U,
        .buffer_slice_pitch = 256U,
        .dst_slice = { .width = 1, .height = 1, .depth = 1 } },
      *tiles);
    recorder->RequireResourceStateFinal(
      *tiles, ResourceStates::kShaderResource);
  }
  auto& allocator = renderer_->GetGraphics()->GetDescriptorAllocator();
  const auto texture_srv = [&](const Texture& texture, TextureType type) {
    auto handle = allocator.AllocateRaw(
      ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
    const auto index = allocator.GetShaderVisibleIndex(handle);
    Backend().GetResourceRegistry().RegisterView(texture, std::move(handle),
      TextureViewDescription {
        .format = texture.GetDescriptor().format, .dimension = type });
    return index;
  };
  const auto tiles_slot = texture_srv(*tiles, TextureType::kTexture2DArray);
  auto scene_bindings = SceneTextureBindings {};
  scene_bindings.scene_depth_srv
    = texture_srv(textures.GetSceneDepth(), TextureType::kTexture2D).get();
  const auto scene_slot = PublishFixtureData(scene_bindings);
  const auto occupied_slot = PublishFixtureData(std::uint32_t { 0U });
  auto args = CreateUploadBuffer(SizeBytes { 16U });
  const std::array<std::uint32_t, 4> draw_args { 6U, 1U, 0U, 0U };
  args->Update(draw_args.data(), sizeof(draw_args), 0U);
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 0;
  const auto config = SharedConfig(settings);
  struct Case {
    float radial, height, falloff, emission;
    bool expected_range_failure { false };
  };
  const std::array cases { Case { 1, 0, 100, 0 },
    Case { 1, 0x1p-24F, 100, 0x1p-24F }, Case { 1, 0x1p-16F, 100, 0x1p-16F },
    Case { 1, 0x1p-14F, 100, 0x1p-14F }, Case { .45F, .25F, 100, 1 },
    Case { 1, 1, 100, 131072 }, Case { 0x1p-24F, 1, 100, 0x1p32F },
    Case { 0x1p-16F, 1, 100, 0x1p32F }, Case { 0x1p-14F, 1, 100, 0x1p32F },
    Case { 0x1p-24F, 0x1p-24F, 100, 0x1p32F }, Case { 1, 1, 10000, 1 },
    Case { 1, 1, 100, 0x1p37F, true } };
  unsigned sequence = 0;
  for (const auto& value : cases) {
    SCOPED_TRACE(sequence);
    fog.SetRadialFogExtinction(value.radial);
    fog.SetHeightFogExtinction(value.height);
    fog.SetHeightFogFalloff(value.falloff);
    fog.SetFogEmissive(
      { value.emission, value.emission / 2, value.emission / 4 });
    state.OnFrameStart(
      frame::SequenceNumber { ++sequence }, frame::Slot { 0U });
    auto products = state.Prepare(ctx_);
    ASSERT_TRUE(products.buffer_ready);
    ASSERT_EQ(products.instance_count, 1U);
    const std::array<std::array<std::uint32_t, 4>, 2> decode_input {
      std::array { products.instance_buffer_slot.get(), 0U, 0U, 0U },
      std::array { 0U, 0U, 0U, 0U }
    };
    const auto decoded = RunToneProbe(
      std::as_bytes(std::span { decode_input }), 1U, 4096U, false);
    EXPECT_EQ(decoded[0][0], value.radial);
    EXPECT_EQ(decoded[0][1], value.height);
    EXPECT_EQ(decoded[0][2], value.falloff * .01F);
    EXPECT_EQ(decoded[0][3], 0.0F);
    EXPECT_EQ(decoded[0][4], value.emission);
    EXPECT_EQ(decoded[0][5], value.emission / 2);
    EXPECT_EQ(decoded[0][6], value.emission / 4);
    EXPECT_EQ(decoded[0][7], 1.0F);
    // Reversed rays and small positive/negative changes of height exercise
    // the production signed integral, not a duplicate arithmetic helper.
    const std::array rays { Pixel { 1, -1, 1, 0 }, Pixel { 0, 1, 1, 0 },
      Pixel { .5F, 1, 1e-6F, 0 }, Pixel { .5F, -1, 1e-6F, 0 } };
    std::vector<std::array<std::uint32_t, 4>> inputs;
    for (const auto ray : rays) {
      inputs.push_back({ products.instance_buffer_slot.get(), 0U, 0U, 0U });
      inputs.push_back(std::bit_cast<std::array<std::uint32_t, 4>>(ray));
    }
    const auto actual = RunToneProbe(std::as_bytes(std::span { inputs }),
      static_cast<std::uint32_t>(rays.size()), 8192U, false);
    for (std::size_t i = 0; i < rays.size(); ++i) {
      SCOPED_TRACE(i);
      const double start = rays[i][0], direction = rays[i][1],
                   length = rays[i][2];
      const double falloff = double(value.falloff * .01F);
      const double radial_depth = double(value.radial) * .75
        * ((1 - start * start) * length - start * direction * length * length
          - length * length * length / 3);
      const double height_depth = double(value.height)
        * std::exp(-start * falloff)
        * -std::expm1(-direction * length * falloff) / (falloff * direction);
      const double combined
        = -std::expm1(-radial_depth) * -std::expm1(-height_depth);
      // The complement would lose very thin coverage in double too. At unit
      // scale the product of opacities is the exact unsaturated coverage.
      const double coverage = std::min(combined, 1.0 - 1e-6);
      EXPECT_NEAR(actual[i][3], coverage, coverage * 2e-5 + 0x1p-120);
      const double media_opacity = -std::expm1(-double(value.radial))
        * -std::expm1(-double(value.height));
      const double media_extinction = media_opacity < 1 - 1e-6
        ? -std::log1p(-media_opacity)
        : -std::log(1e-6);
      EXPECT_NEAR(
        actual[i][7], media_extinction, media_extinction * 2e-5 + 0x1p-120);
      for (unsigned c = 0; c < 3; ++c) {
        const double source = double(value.emission) / double(1U << c);
        EXPECT_NEAR(
          actual[i][c], source * coverage, source * coverage * 2e-5 + 0x1p-120);
        EXPECT_NEAR(actual[i][c + 4], source * media_extinction,
          source * media_extinction * 2e-5 + 0x1p-120);
      }
    }
    ctx_.frame_sequence = frame::SequenceNumber { sequence };
    const auto frame = pass_->ResolveFrame(ctx_, config, { .use_fp32 = true });
    ASSERT_NE(frame, nullptr);
    ctx_.current_view.frame_exposure = frame;
    auto bindings = ViewFrameBindings {};
    bindings.scene_texture_frame_slot = scene_slot;
    bindings.frame_exposure_slot = frame->srv_index;
    bindings.exposure_status_uav = frame->current_state->status_uav_index;
    auto view = ViewConstants::GpuData {};
    view.view_frame_bindings_bslot
      = BindlessViewFrameBindingsSlot { PublishFixtureData(bindings) };
    view.inverse_view_projection_matrix = glm::mat4 { 0.0F };
    view.inverse_view_projection_matrix[3] = { 0, 0, .5F, 1 };
    auto constants
      = CreateUploadBuffer(SizeBytes { 256U }, BufferUsage::kConstant);
    constants->Update(&view, sizeof(view), 0U);
    ctx_.view_constants = constants;
    {
      auto recorder = AcquireRecorder("Local fog compose initialization");
      for (const auto& texture :
        { textures.GetSceneColorResource(), textures.GetSceneDepthResource() })
        if (!recorder->AdoptKnownResourceState(*texture))
          recorder->BeginTrackingResourceState(
            *texture, texture->GetDescriptor().initial_state);
      EnsureTracked(*recorder, args, ResourceStates::kGenericRead);
      recorder->RequireResourceState(
        textures.GetSceneColor(), ResourceStates::kRenderTarget);
      recorder->RequireResourceState(
        textures.GetSceneDepth(), ResourceStates::kDepthWrite);
      recorder->FlushBarriers();
      recorder->ClearFramebuffer(
        *framebuffer, std::vector<std::optional<Color>> { Color {} }, .5F);
    }
    products.tile_data_ready = true;
    products.tile_data_texture_slot = tiles_slot;
    products.occupied_tile_buffer_slot = occupied_slot;
    products.tile_resolution_x = products.tile_resolution_y = 1;
    products.max_instances_per_tile = 1;
    products.occupied_tile_draw_args_buffer
      = observer_ptr<const Buffer> { args.get() };
    compose.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    const auto capture = value.emission == 131072
      ? BeginOptionalCapture()
      : observer_ptr<FrameCaptureController> {};
    ASSERT_TRUE(compose.Record(ctx_, textures, products).executed);
    if (capture)
      EXPECT_TRUE(capture->EndCapture());
    const auto pixels = ReadFloatTexture(textures.GetSceneColor());
    const double falloff = double(value.falloff * .01F);
    const double start = renderer_->GetLocalFogGlobalStartDistanceMeters();
    const double height_depth = double(value.height)
      * std::exp(-start * falloff) * -std::expm1(-(.5 - start) * falloff)
      / falloff;
    const double radial_depth = double(value.radial) * .75
      * ((.5 - .125 / 3) - (start - start * start * start / 3));
    const double coverage
      = -std::expm1(-radial_depth) * -std::expm1(-height_depth);
    for (unsigned c = 0; c < 3; ++c) {
      const double expected
        = double(value.emission) / double(1U << c) * coverage;
      EXPECT_NEAR(pixels[0][c], expected, expected * 2e-5 + 0x1p-120);
    }
    const auto status = Read<ExposureCompletedStatus>(
      *frame->current_state->status_buffer, ResourceStates::kUnorderedAccess);
    if (value.expected_range_failure) {
      ASSERT_GT(double(value.emission) * coverage, 0x1p32);
      EXPECT_EQ(status.first_failure_product, 9U);
      EXPECT_EQ(status.first_failure_kind, 32U);
      EXPECT_EQ(status.flags & 18U, 18U);
    } else {
      ASSERT_LE(double(value.emission) * coverage, 0x1p32);
      EXPECT_EQ(status.flags & 16U, 0U);
    }
    ctx_.current_view.frame_exposure.reset();
    ctx_.view_constants.reset();
  }
  RecordProperty("authored_local_fog_cases", cases.size() * 4);
  ctx_.current_view.resolved_view.reset();
  ctx_.scene.reset();
  WaitForQueueIdle();
}

NOLINT_TEST_F(ExposureGpuTest, ConsumerArithmeticContainsAttenuationAndRounding)
{
  std::vector<std::array<float, 4>> inputs;
  for (const float relative : { 0.0F, 0x1p-11F, .01F })
    for (const float absolute : { 0.0F, 0x1p-149F, 0x1p-120F, .001F })
      for (const float t_relative : { 0.0F, .002F })
        for (const float t_absolute : { 0.0F, 1e-8F })
          for (const float maximum : { 0.0F, 0x1p-24F, 1.0F, 0x1p32F })
            for (const float steps : { 0.0F, 128.0F, 4096.0F })
              for (const float inverse_p : { 0x1p-32F, 1.0F, 0x1p32F }) {
                inputs.push_back(
                  { relative, absolute, t_relative, t_absolute });
                inputs.push_back({ maximum, steps, inverse_p, 0.0F });
              }
  const auto count = static_cast<std::uint32_t>(inputs.size() / 2U);
  const auto results
    = RunToneProbe(std::as_bytes(std::span { inputs }), count, 64U);
  for (std::size_t i = 0; i < results.size(); ++i) {
    SCOPED_TRACE(i);
    const auto& b = inputs[i * 2U];
    const auto& c = inputs[i * 2U + 1U];
    const auto& result = results[i];
    for (const float value : result) {
      EXPECT_TRUE(std::isfinite(value));
      EXPECT_GE(value, 0.0F);
    }
    for (const double source_sign : { -1.0, 1.0 })
      for (const double t_sign : { -1.0, 1.0 }) {
        const double reference = double(c[0]) * .25;
        const double source = std::max(
          0.0, double(c[0]) + source_sign * (double(b[0]) * c[0] + b[1]));
        const double transmission
          = std::clamp(.25 + t_sign * (double(b[2]) * .25 + b[3]), 0.0, 1.0);
        const double error = std::abs(source * transmission - reference);
        EXPECT_LE(error, double(result[0]) * reference + result[1]);
        EXPECT_LE(error, double(result[2]) * reference + result[3]);
      }
    EXPECT_EQ(result[6], 0.0F);
    EXPECT_GE(double(result[7]), double(b[0]) * c[0] + b[1]);
  }
  RecordProperty("consumer_arithmetic_cases", count);
}

NOLINT_TEST_F(ExposureGpuTest, ComposedCoverageRequiresValidOpaqueDepth)
{
  struct Case {
    const char* name;
    float depth;
    bool reverse;
    bool supplied;
    unsigned width;
    float alpha;
    bool admitted;
  };
  const std::array cases {
    Case { "reverse opaque", .5F, true, true, 2U, 1, true },
    Case { "forward opaque", .5F, false, true, 2U, 1, true },
    Case { "reverse near", 1, true, true, 2U, 1, true },
    Case { "forward near", 0, false, true, 2U, 1, true },
    Case { "reverse far", 0, true, true, 2U, 1, false },
    Case { "forward far", 1, false, true, 2U, 1, false },
    Case { "uncertain horizon", .0005F, true, true, 2U, 1, false },
    Case { "no depth", .5F, true, false, 2U, 1, false },
    Case { "partial coverage", .5F, true, true, 2U, .5F, false },
    Case { "mismatched depth", .5F, false, true, 1U, 1, false },
    Case { "negative depth", -.1F, true, true, 2U, 1, false },
    Case { "depth above one", 1.1F, false, true, 2U, 1, false },
    Case { "NaN depth", std::numeric_limits<float>::quiet_NaN(), true,
      true, 2U, 1, false },
    Case { "infinite depth", std::numeric_limits<float>::infinity(), false,
      true, 2U, 1, false },
    Case { "negative subnormal depth", std::bit_cast<float>(0x80000001U),
      false, true, 2U, 1, false },
  };
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.key = 12.5F;
  settings.manual_ev = 0.0F;
  const auto config = SharedConfig(settings);
  const auto capture = BeginOptionalCapture();
  for (const auto& test : cases) {
    SCOPED_TRACE(test.name);
    const std::array pixels { Pixel { .5F, .5F, .5F, test.alpha },
      Pixel { 8192, 8192, 8192, 1 } };
    const auto signal = MakeSignal(2U, 1U, pixels);
    const auto depth = Uniform(test.depth, test.width, 1U);
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    const auto frame = pass_->ResolveFrame(ctx_, config, { .use_fp32 = true });
    ASSERT_NE(frame, nullptr);
    ASSERT_TRUE(RecordShared(signal, config).executed);
    const auto error = HdrSceneErrorData {
      .current_coverage_absolute = .001F,
      .candidate_pre_exposure = 1.0F,
      .checked_products = 1U << 10U,
      .flags = 1U,
    };
    auto upload = CreateUploadBuffer(SizeBytes { sizeof(error) });
    upload->Update(&error, sizeof(error), 0U);
    {
      auto recorder = AcquireRecorder("Opaque coverage fixture");
      EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
      ASSERT_TRUE(recorder->AdoptKnownResourceState(*frame->current_state->status_buffer));
      recorder->RequireResourceState(*frame->current_state->status_buffer, ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyBuffer(*frame->current_state->status_buffer,
        offsetof(ExposureStatusStorage, scene_error), *upload, 0U, sizeof(error));
      recorder->RequireResourceStateFinal(*frame->current_state->status_buffer, ResourceStates::kShaderResource);
    }
    const std::array products { postprocess::ExposurePass::HdrProduct {
      .texture = signal.texture.get(), .srv = signal.srv, .id = 11U,
      .metering = true, .coverage = true, .composed_error = true } };
    const auto composition = postprocess::ExposurePass::SceneComposition {
      .opaque_depth = test.supplied ? depth.texture.get() : nullptr,
      .opaque_depth_srv = test.supplied ? depth.srv : kInvalidShaderVisibleIndex,
      .reverse_z = test.reverse,
    };
    ASSERT_TRUE(pass_->EvaluateFp16Products(ctx_, frame, config, products,
      { .scene_composition = composition }, postprocess::ExposurePass::SuitabilityScale::kCurrentFrame));
    const auto result = Read<HdrSuitabilityData>(*frame->conversion_buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(result.failure_flags == 0U, test.admitted);
    if (!test.admitted) EXPECT_NE(result.failure_flags & 8U, 0U);
    EXPECT_EQ(result.checked_samples, 2U);
  }
  if (capture) EXPECT_TRUE(capture->EndCapture());
}

NOLINT_TEST_F(
  ExposureGpuTest, ProspectiveProductBoundsIncludeRetainedErrorAndSelectedScale)
{
  const std::array<float, 8> observed { 1.00075F, 1.0F / 3.0F, 0x1p-25F,
    3.0F * 0x1p-25F, 8190.0F, .25F, .5F, 0.0F };
  // Exact independent binary16 results at P=1, including ties and exponent
  // carry.
  const std::array<double, 8> narrowed { 1.0009765625, 1365.0 / 4096.0, 0.0,
    0x1p-23, 8192.0, .25, .5, 0.0 };
  std::array<Pixel, 8> pixels {};
  for (std::size_t i = 0; i < pixels.size(); ++i)
    pixels[i] = { observed[i], observed[i], observed[i], 1.0F - 0x1p-16F };
  const auto image = MakeSignal(4U, 2U, pixels);
  const auto volume = MakeSignal(2U, 2U, pixels, 2U);
  const auto anchor = Uniform(8192.0F);
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 0.0F;
  const auto config = SharedConfig(settings);
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  const auto frame = pass_->ResolveFrame(ctx_, config, { .use_fp32 = true });
  ASSERT_NE(frame, nullptr);
  ASSERT_TRUE(RecordShared(anchor, config).executed);
  const std::array products {
    postprocess::ExposurePass::HdrProduct {
      .texture = anchor.texture.get(), .srv = anchor.srv, .id = 11U },
    postprocess::ExposurePass::HdrProduct {
      .texture = image.texture.get(), .srv = image.srv, .id = 5U },
    postprocess::ExposurePass::HdrProduct { .texture = volume.texture.get(),
      .srv = volume.srv,
      .id = 6U,
      .transmittance = true },
    postprocess::ExposurePass::HdrProduct { .texture = volume.texture.get(),
      .srv = volume.srv,
      .id = 10U,
      .transmittance = true },
  };
  const auto capture = BeginOptionalCapture();
  std::array<HdrErrorBoundsData, 3> exact_result {};
  for (unsigned trial = 0U; trial < 4U; ++trial) {
    SCOPED_TRACE(trial);
    std::array<HdrErrorBoundsData, 3> prior {};
    if (trial == 1U)
      prior.fill({ .rgb_relative = .002F,
        .rgb_absolute = .01F,
        .transmittance_relative = .001F,
        .transmittance_absolute = .0001F });
    if (trial == 2U)
      prior[2].rgb_relative = 1.0F;
    auto upload = CreateUploadBuffer(SizeBytes { sizeof(prior) });
    upload->Update(prior.data(), sizeof(prior), 0U);
    {
      auto recorder = AcquireRecorder("Prospective bounds fixture");
      EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
      ASSERT_TRUE(recorder->AdoptKnownResourceState(
        *frame->current_state->status_buffer));
      recorder->RequireResourceState(
        *frame->current_state->status_buffer, ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyBuffer(*frame->current_state->status_buffer,
        offsetof(ExposureStatusStorage, producer_errors), *upload, 0U,
        sizeof(prior));
      recorder->RequireResourceStateFinal(
        *frame->current_state->status_buffer, ResourceStates::kShaderResource);
    }
    ASSERT_TRUE(pass_->EvaluateFp16Products(ctx_, frame, config, products, {}));
    const auto report = Read<HdrSuitabilityData>(
      *frame->suitability_buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(report.candidate_pre_exposure, 1.0F);
    const auto status = Read<ExposureStatusStorage>(
      *frame->current_state->status_buffer, ResourceStates::kShaderResource);
    for (std::size_t product = 0; product < prior.size(); ++product) {
      const auto& actual = status.candidate_errors[product];
      if (trial == 2U && product == 2U) {
        EXPECT_TRUE(std::isinf(actual.rgb_absolute));
        continue;
      }
      for (std::size_t i = 0; i < observed.size(); ++i) {
        const double low = std::max(0.0,
          (double(observed[i]) - prior[product].rgb_absolute)
            / (1.0 + prior[product].rgb_relative));
        const double high = (double(observed[i]) + prior[product].rgb_absolute)
          / (1.0 - prior[product].rgb_relative);
        for (const double reference : { low, high })
          EXPECT_LE(std::abs(narrowed[i] - reference),
            actual.rgb_relative * reference + actual.rgb_absolute);
      }
      if (product != 0U) {
        const double t = double(pixels[0][3]);
        const double low = std::max(0.0,
          (t - prior[product].transmittance_absolute)
            / (1.0 + prior[product].transmittance_relative));
        const double high = std::min(1.0,
          (t + prior[product].transmittance_absolute)
            / (1.0 - prior[product].transmittance_relative));
        for (const double reference : { low, high })
          EXPECT_LE(std::abs(1.0 - reference),
            actual.transmittance_relative * reference
              + actual.transmittance_absolute);
      }
    }
    if (trial == 0U)
      exact_result = status.candidate_errors;
    if (trial == 3U)
      EXPECT_EQ(std::memcmp(exact_result.data(), status.candidate_errors.data(),
                  sizeof(exact_result)),
        0);
  }
  if (capture)
    EXPECT_TRUE(capture->EndCapture());
}

NOLINT_TEST_F(ExposureGpuTest, SuitabilityRetainsProducerAndHistoryError)
{
  struct Case {
    float observed;
    HdrErrorBoundsData bounds;
    float gain;
    std::uint32_t failure;
    float anchor { 8192.0F };
    bool meter { false };
    bool huge_bound { false };
  };
  const std::array cases { Case { 1.01F, {}, 1, 0 },
    Case { 1.01F, { .rgb_absolute = .01F }, 1, 4 },
    Case { 1.01F, { .rgb_relative = .02F }, 1, 4 },
    Case { 1.01F, { .rgb_absolute = 1e-7F }, 1, 0 },
    Case { 0, { .rgb_absolute = 1e-8F }, 1, 0 },
    Case { 0, { .rgb_absolute = 1e-8F }, 1e6F, 4 },
    Case {
      1.01F, { .rgb_absolute = std::numeric_limits<float>::infinity() }, 1, 4 },
    Case { 1.01F, { .rgb_relative = 1.0F }, 1, 4 },
    Case { 1.01F, { .rgb_absolute = -.01F }, 1, 4 },
    Case { 1.01F, { .transmittance_absolute = .01F }, 1, 4 },
    Case { 1.01F, { .transmittance_relative = .02F }, 1, 4 },
    Case { 1.01F, { .transmittance_relative = 1.0F }, 1, 4 },
    Case { 8180.0F, { .rgb_absolute = 16.0F }, 1, 0, 1.0F },
    Case { 0, { .rgb_absolute = 1e-8F }, 1, 8, 8192.0F, true },
    Case { 0, { .rgb_absolute = std::bit_cast<float>(0x7f7ffff0U) }, 1, 4, 8192,
      false, true },
    Case { 0, { .rgb_absolute = std::bit_cast<float>(0x7f7ffff1U) }, 1, 4, 8192,
      false, true },
    Case { 0, { .rgb_absolute = std::bit_cast<float>(0x7f7ffff2U) }, 1, 4, 8192,
      false, true },
    Case { 0, { .rgb_absolute = std::bit_cast<float>(0x7f7ffff3U) }, 1, 4, 8192,
      false, true } };
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.key = 12.5F;
  settings.manual_ev = 0.0F;
  settings.black_influence = 1.0F;
  const auto config = SharedConfig(settings);
  const auto capture = BeginOptionalCapture();
  for (std::size_t index = 0; index < cases.size(); ++index) {
    SCOPED_TRACE(index);
    const auto& test = cases[index];
    const auto anchor = Uniform(test.anchor, 1U, 1U);
    const std::array<Pixel, 1> pixel { Pixel {
      test.observed, test.observed, test.observed, .5F } };
    const auto fog = MakeSignal(1U, 1U, pixel);
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    const auto frame = pass_->ResolveFrame(ctx_, config, { .use_fp32 = true });
    ASSERT_NE(frame, nullptr);
    const auto solved = RecordShared(anchor, config);
    ASSERT_TRUE(solved.executed);
    // Controlled GPU inputs, independent of the producer's bound arithmetic.
    // A bad unrelated sky certificate must not contaminate the fog record.
    std::array<HdrErrorBoundsData, 3> tail {};
    tail[0].rgb_absolute = std::numeric_limits<float>::infinity();
    tail[2] = test.bounds;
    auto upload = CreateUploadBuffer(SizeBytes { sizeof(tail) });
    upload->Update(tail.data(), sizeof(tail), 0U);
    {
      auto recorder = AcquireRecorder("Retained error fixture");
      EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
      ASSERT_TRUE(recorder->AdoptKnownResourceState(
        *frame->current_state->status_buffer));
      recorder->RequireResourceState(
        *frame->current_state->status_buffer, ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyBuffer(
        *frame->current_state->status_buffer, 80U, *upload, 0U, sizeof(tail));
      recorder->RequireResourceStateFinal(
        *frame->current_state->status_buffer, ResourceStates::kShaderResource);
    }
    const std::array products {
      postprocess::ExposurePass::HdrProduct {
        .texture = anchor.texture.get(), .srv = anchor.srv, .id = 11U },
      postprocess::ExposurePass::HdrProduct { .texture = fog.texture.get(),
        .srv = fog.srv,
        .id = 10U,
        .metering = test.meter,
        .transmittance = true,
        .consumer_rgb_gain = test.gain }
    };
    ASSERT_TRUE(pass_->EvaluateFp16Products(ctx_, frame, config, products, {}));
    const auto report = Read<HdrSuitabilityData>(
      *frame->suitability_buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(report.failure_flags, test.failure);
    EXPECT_EQ(report.image_failures, (test.failure & 4U) != 0U ? 1U : 0U);
    EXPECT_EQ(report.metering_failures, (test.failure & 8U) != 0U ? 1U : 0U);
    if (!test.huge_bound)
      EXPECT_EQ(report.candidate_pre_exposure, 1.0F);
    EXPECT_EQ(report.checked_samples, 2U);
    if (test.failure != 0U)
      EXPECT_EQ(report.first_failure_product, 10U);
    if (test.anchor == 1.0F)
      EXPECT_GE(report.maximum_scene_rgb, 8196.0F);
    ASSERT_TRUE(pass_->FinalizeFp16Suitability(ctx_, frame,
      { .product_layout_revision = 1U,
        .expected_products = (1U << 9U) | (1U << 10U) }));
    if (test.failure != 0U) {
      const auto status = Read<ExposureCompletedStatus>(
        *frame->current_state->status_buffer, ResourceStates::kUnorderedAccess);
      EXPECT_EQ(status.fp16_eligible_streak, 0U);
      EXPECT_EQ(status.flags & 4U, 0U);
    }
    EXPECT_EQ(ReadState(solved).displayed_scale, 1.0F);
  }
  if (capture)
    EXPECT_TRUE(capture->EndCapture());
}

NOLINT_TEST_F(ExposureGpuTest,
  SuitabilityRgbGainLeavesTransmissionUnscaledAndAvoidsCombinedGainOverflow)
{
  struct Case {
    float background;
    Pixel ap;
    float gain;
    float ev;
    std::uint32_t expected_failure;
  };
  // RGB gain must not amplify the tiny transmission error. In the second
  // case gain*S exceeds FP32, while (2^-100 * gain)*S is finite and its loss
  // must still be rejected.
  const std::array cases { Case { .125F, { 0, 0, 0, 1e-8F }, 1e6F, 0.0F, 0U },
    Case {
      8192.0F, { 0x1p-100F, 0x1p-100F, 0x1p-100F, 1 }, 0x1p100F, -32.0F, 4U } };
  for (const auto& test : cases) {
    SCOPED_TRACE(test.gain);
    const auto scene_signal = Uniform(test.background, 1U, 1U);
    const auto ap_signal = MakeSignal(1U, 1U, std::span { &test.ap, 1U });
    auto settings = scene::ExposureSettings {};
    settings.mode = engine::ExposureMode::kManual;
    settings.key = 12.5F;
    settings.manual_ev = test.ev;
    const auto config = SharedConfig(settings);
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    const auto frame = pass_->ResolveFrame(ctx_, config, { .use_fp32 = true });
    ASSERT_NE(frame, nullptr);
    ASSERT_TRUE(RecordShared(scene_signal, config).executed);
    const std::array products { postprocess::ExposurePass::HdrProduct {
                                  .texture = scene_signal.texture.get(),
                                  .srv = scene_signal.srv,
                                  .id = 11U },
      postprocess::ExposurePass::HdrProduct {
        .texture = ap_signal.texture.get(),
        .srv = ap_signal.srv,
        .id = 6U,
        .transmittance = true,
        .consumer_rgb_gain = test.gain } };
    ASSERT_TRUE(pass_->EvaluateFp16Products(ctx_, frame, config, products, {}));
    const auto report = Read<HdrSuitabilityData>(
      *frame->suitability_buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(report.failure_flags, test.expected_failure);
  }
}

NOLINT_TEST_F(
  ExposureGpuTest, SuitabilityRejectsNonfiniteAndMissingRequiredProducts)
{
  const auto nonfinite
    = Qualify(Uniform(std::numeric_limits<float>::infinity()), false);
  EXPECT_NE(nonfinite.failure_flags & 1U, 0U);
  EXPECT_EQ(nonfinite.rejected_samples, 1U);
  const auto signal = Uniform(.25F);
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  const auto config = SharedConfig();
  const auto frame = pass_->ResolveFrame(ctx_, config, { .use_fp32 = true });
  ASSERT_NE(frame, nullptr);
  const std::array products {
    postprocess::ExposurePass::HdrProduct {
      .texture = signal.texture.get(), .srv = signal.srv, .id = 1U },
    postprocess::ExposurePass::HdrProduct { .id = 6U }
  };
  ASSERT_TRUE(pass_->EvaluateFp16Products(ctx_, frame, config, products, {}));
  const auto missing = Read<HdrSuitabilityData>(
    *frame->suitability_buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(missing.failure_flags, 16U);
  EXPECT_EQ(missing.first_failure_product, 6U);
  EXPECT_EQ(missing.expected_products, 33U);
  EXPECT_EQ(missing.checked_products, 1U);
}

NOLINT_TEST_F(ExposureGpuTest, SuitabilityChecksVolumeRgbAndTransmittance)
{
  auto texture = CreateRegisteredTexture({ .width = 2U,
    .height = 1U,
    .depth = 2U,
    .format = Format::kRGBA32Float,
    .texture_type = TextureType::kTexture3D,
    .is_shader_resource = true,
    .initial_state = ResourceStates::kCommon });
  std::array<std::byte, 512U> bytes {};
  const Pixel value { .25F, .5F, .75F, .5F };
  for (unsigned z = 0U; z < 2U; ++z)
    for (unsigned x = 0U; x < 2U; ++x)
      std::memcpy(bytes.data() + z * 256U + x * sizeof(Pixel), value.data(),
        sizeof(Pixel));
  auto upload = CreateUploadBuffer(SizeBytes { bytes.size() });
  upload->Update(bytes.data(), bytes.size(), 0U);
  {
    auto recorder = AcquireRecorder("Suitability volume upload");
    EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
    EnsureTracked(*recorder, texture, ResourceStates::kCommon);
    recorder->RequireResourceState(*texture, ResourceStates::kCopyDest);
    recorder->FlushBarriers();
    recorder->CopyBufferToTexture(*upload,
      { .buffer_offset = 0U,
        .buffer_row_pitch = 256U,
        .buffer_slice_pitch = 256U,
        .dst_slice = { .width = 2U, .height = 1U, .depth = 2U } },
      *texture);
    recorder->RequireResourceStateFinal(
      *texture, ResourceStates::kShaderResource);
  }
  auto& allocator = renderer_->GetGraphics()->GetDescriptorAllocator();
  auto handle = allocator.AllocateRaw(
    ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
  const auto srv = allocator.GetShaderVisibleIndex(handle);
  Backend().GetResourceRegistry().RegisterView(*texture, std::move(handle),
    TextureViewDescription {
      .format = Format::kRGBA32Float, .dimension = TextureType::kTexture3D });
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 0.0F;
  const auto config = SharedConfig(settings);
  ctx_.frame_sequence = frame::SequenceNumber { 1U };
  const auto frame = pass_->ResolveFrame(ctx_, config, { .use_fp32 = true });
  ASSERT_NE(frame, nullptr);
  const std::array products { postprocess::ExposurePass::HdrProduct {
    .texture = texture.get(), .srv = srv, .id = 10U, .transmittance = true } };
  ASSERT_TRUE(pass_->EvaluateFp16Products(ctx_, frame, config, products, {}));
  const auto result = Read<HdrSuitabilityData>(
    *frame->suitability_buffer, ResourceStates::kShaderResource);
  EXPECT_GE(result.maximum_scene_rgb, .75F);
  EXPECT_LE(std::bit_cast<std::uint32_t>(result.maximum_scene_rgb),
    std::bit_cast<std::uint32_t>(.75F) + 4U);
  EXPECT_EQ(result.candidate_pre_exposure, 16384.0F);
  EXPECT_EQ(result.checked_samples, 4U);
  EXPECT_EQ(result.checked_products, 1U << 9U);
  EXPECT_EQ(result.failure_flags, 0U);
  const Pixel tiny_transmittance { 0.0F, 0.0F, 0.0F, 0x1p-25F };
  for (unsigned z = 0U; z < 2U; ++z)
    for (unsigned x = 0U; x < 2U; ++x)
      std::memcpy(bytes.data() + z * 256U + x * sizeof(Pixel),
        tiny_transmittance.data(), sizeof(Pixel));
  upload->Update(bytes.data(), bytes.size(), 0U);
  {
    auto recorder = AcquireRecorder("Suitability transmittance update");
    EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
    ASSERT_TRUE(recorder->AdoptKnownResourceState(*texture));
    recorder->RequireResourceState(*texture, ResourceStates::kCopyDest);
    recorder->FlushBarriers();
    recorder->CopyBufferToTexture(*upload,
      { .buffer_offset = 0U,
        .buffer_row_pitch = 256U,
        .buffer_slice_pitch = 256U,
        .dst_slice = { .width = 2U, .height = 1U, .depth = 2U } },
      *texture);
    recorder->RequireResourceStateFinal(
      *texture, ResourceStates::kShaderResource);
  }
  const auto bright = Uniform(0x1p30F);
  ctx_.frame_sequence = frame::SequenceNumber { 2U };
  const auto next = pass_->ResolveFrame(ctx_, config, { .use_fp32 = true });
  const std::array combined {
    postprocess::ExposurePass::HdrProduct {
      .texture = bright.texture.get(), .srv = bright.srv, .id = 1U },
    products[0]
  };
  ASSERT_TRUE(pass_->EvaluateFp16Products(ctx_, next, config, combined, {}));
  const auto failed = Read<HdrSuitabilityData>(
    *next->suitability_buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(failed.failure_flags, 4U);
  EXPECT_EQ(failed.first_failure_product, 10U);
  EXPECT_EQ(failed.image_failures, 4U);
}

NOLINT_TEST_F(
  ExposureGpuTest, SuitabilitySharesDiscardedDarkAndSyntheticFallbackSemantics)
{
  const std::array original { Pixel { 0x1p30F, 0x1p30F, 0x1p30F, 1.0F },
    Pixel { 0x1p-24F, 0x1p-24F, 0x1p-24F, 1.0F } };
  const std::array narrowed { original[0], Pixel { 0, 0, 0, 1 } };
  auto settings = scene::ExposureSettings {};
  settings.black_influence = 0.0F;
  const auto signal = MakeSignal(2U, 1U, original);
  EXPECT_EQ(Qualify(signal, true, settings).failure_flags, 0U);
  ResetHistory();
  const auto before = Run(signal, settings);
  ResetHistory();
  const auto after = Run(MakeSignal(2U, 1U, narrowed), settings);
  EXPECT_TRUE(std::equal(before.histogram.begin(),
    before.histogram.begin() + 256, after.histogram.begin()));
  EXPECT_EQ(before.histogram[257], after.histogram[257]);
  EXPECT_EQ(before.histogram[261], after.histogram[261]);
  EXPECT_EQ(before.state.displayed_scale, after.state.displayed_scale);
  EXPECT_EQ(before.state.raw_metered_ev, after.state.raw_metered_ev);
  ResetHistory();
  const auto dark_before = Run(Uniform(0x1p-24F), settings);
  ResetHistory();
  const auto dark_after = Run(Uniform(0.0F), settings);
  EXPECT_EQ(
    dark_before.state.displayed_scale, dark_after.state.displayed_scale);
  EXPECT_NE(dark_before.state.flags & 16U, 0U);
  EXPECT_NE(dark_after.state.flags & 16U, 0U);
  EXPECT_EQ(Qualify(Uniform(0x1p-24F), true, settings).failure_flags, 0U);
  settings.black_influence = 1.0F;
  EXPECT_NE(Qualify(signal, true, settings).failure_flags & 8U, 0U);
}

NOLINT_TEST_F(
  ExposureGpuTest, SuitabilityIgnoresCoverageMassChangesAfterDarkDiscard)
{
  auto scene = scene::Scene("DiscardedCoverage", 1U);
  scene.SetEnvironment(std::make_unique<scene::SceneEnvironment>());
  scene.GetEnvironment()
    ->AddSystem<scene::environment::Background>()
    .SetEnabled(true);
  ctx_.scene = observer_ptr { &scene };
  const std::array original { Pixel { 0x1p30F, 0x1p30F, 0x1p30F, 1.0F },
    Pixel { 0x1p-24F * .1F, 0x1p-24F * .1F, 0x1p-24F * .1F, .1F } };
  const std::array narrowed { original[0], Pixel { 0, 0, 0, .0999755859375F } };
  auto settings = scene::ExposureSettings {};
  settings.black_influence = 0.0F;
  const auto signal = MakeSignal(2U, 1U, original);
  EXPECT_EQ(Qualify(signal, true, settings, nullptr, true).failure_flags, 0U);
  ResetHistory();
  const auto before = Run(signal, settings);
  ResetHistory();
  const auto after = Run(MakeSignal(2U, 1U, narrowed), settings);
  EXPECT_TRUE(std::equal(before.histogram.begin(),
    before.histogram.begin() + 256, after.histogram.begin()));
  EXPECT_EQ(before.histogram[257], after.histogram[257]);
  EXPECT_EQ(before.histogram[261], after.histogram[261]);
  EXPECT_EQ(before.state.displayed_scale, after.state.displayed_scale);
  ctx_.scene.reset();
}

NOLINT_TEST_F(ExposureGpuTest,
  CheckedSceneColorConversionUsesCurrentScaleAndRejectsTheWholeImage)
{
  constexpr std::uint32_t width = 9U;
  constexpr std::uint32_t height = 3U;
  using PackedPixel = std::array<std::uint16_t, 4U>;
  constexpr PackedPixel sentinel { 0x3400U, 0x3800U, 0x3a00U, 0x3c00U };
  constexpr PackedPixel expected { 0x3000U, 0x3555U, 0x3800U, 0x3800U };
  const Pixel ordinary { .125F, 1.0F / 3.0F, .5F, .5F };
  const float sensitive = 256.4375F * 0x1p-24F;
  struct Case {
    const char* name;
    Pixel pixel;
    float ev;
    bool fp32;
    bool background;
    std::uint32_t failure;
    PackedPixel last_pixel;
    bool automatic { false };
    bool zero_meter_mask { false };
  };
  const std::array cases {
    Case { "ordinary", ordinary, 0, true, false, 0U, expected },
    Case { "typed half rounding",
      { 1.500732421875F, 1.500244140625F, 1.50048828125F, 1.0F }, 0, true,
      false, 0U, { 0x3e01U, 0x3e00U, 0x3e00U, 0x3c00U } },
    Case { "odd tie and exponent boundary",
      { 1.50146484375F, 8198.0F, 1.99951171875F, 1.0F }, 0, true, false, 0U,
      { 0x3e02U, 0x7001U, 0x4000U, 0x3c00U } },
    Case { "subnormal nearest even",
      { 1.75F * 0x1p-24F, 2.5F * 0x1p-24F, 3.5F * 0x1p-24F, 0.0F }, 0, true,
      false, 0U, { 2U, 2U, 4U, 0U }, false, true },
    Case { "overflow", { 0x1p20F, 0x1p20F, 0x1p20F, 1 }, 0, true, false, 2U,
      sentinel },
    Case { "nonfinite", { std::numeric_limits<float>::quiet_NaN(), 0, 0, 1 }, 0,
      true, false, 1U, sentinel },
    Case { "displayed dark loss", { 0x1p-30F, 0x1p-30F, 0x1p-30F, 1 }, -30,
      true, false, 4U, sentinel },
    Case { "nonunit P", ordinary, -2, false, false, 0U, expected },
    Case { "opaque zero alpha", { sensitive, sensitive, sensitive, 0 }, 0, true,
      false, 8U, sentinel, true },
    Case { "background zero alpha", { sensitive, sensitive, sensitive, 0 }, 0,
      true, true, 0U, { 0x0100U, 0x0100U, 0x0100U, 0U }, true },
    Case { "opaque partial alpha", { sensitive, sensitive, sensitive, .5F }, 0,
      true, false, 8U, sentinel, true },
    Case { "background partial alpha", { sensitive, sensitive, sensitive, .5F },
      0, true, true, 8U, sentinel, true },
  };
  for (const auto& test_case : cases) {
    SCOPED_TRACE(test_case.name);
    auto scene = scene::Scene("CheckedResolveCoverage", 1U);
    scene.SetEnvironment(std::make_unique<scene::SceneEnvironment>());
    scene.GetEnvironment()
      ->AddSystem<scene::environment::Background>()
      .SetEnabled(test_case.background);
    ctx_.scene = observer_ptr { &scene };
    auto settings = scene::ExposureSettings {};
    settings.mode = test_case.automatic ? engine::ExposureMode::kAuto
                                        : engine::ExposureMode::kManual;
    settings.manual_ev = test_case.ev;
    settings.min_log_luminance = -24.0F;
    auto config = SharedConfig(settings);
    std::vector<Pixel> pixels(width * height, ordinary);
    pixels.back() = test_case.pixel;
    const auto signal = MakeSignal(width, height, pixels);
    auto destination = CreateRegisteredTexture(TextureDesc { .width = width,
      .height = height,
      .format = Format::kRGBA16Float,
      .texture_type = TextureType::kTexture2D,
      .debug_name = "CheckedSceneColorDestination",
      .is_shader_resource = true,
      .is_uav = true,
      .initial_state = ResourceStates::kCommon });
    std::array<std::byte, height * 256U> initial {};
    for (unsigned y = 0U; y < height; ++y)
      for (unsigned x = 0U; x < width; ++x)
        std::memcpy(initial.data() + y * 256U + x * sizeof(PackedPixel),
          sentinel.data(), sizeof(PackedPixel));
    auto upload = CreateUploadBuffer(SizeBytes { initial.size() });
    upload->Update(initial.data(), initial.size(), 0U);
    {
      auto recorder = AcquireRecorder("Initialize checked resolve sentinel");
      EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
      EnsureTracked(*recorder, destination, ResourceStates::kCommon);
      recorder->RequireResourceState(*destination, ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyBufferToTexture(*upload,
        { .buffer_offset = 0U,
          .buffer_row_pitch = 256U,
          .buffer_slice_pitch = height * 256U,
          .dst_slice = { .width = width, .height = height, .depth = 1U } },
        *destination);
      recorder->RequireResourceStateFinal(
        *destination, ResourceStates::kShaderResource);
    }
    auto& allocator = renderer_->GetGraphics()->GetDescriptorAllocator();
    auto handle = allocator.AllocateRaw(
      ResourceViewType::kTexture_UAV, DescriptorVisibility::kShaderVisible);
    const auto uav = allocator.GetShaderVisibleIndex(handle);
    ASSERT_TRUE(Backend()
        .GetResourceRegistry()
        .RegisterView(*destination, std::move(handle),
          TextureViewDescription { .view_type = ResourceViewType::kTexture_UAV,
            .format = Format::kRGBA16Float,
            .dimension = TextureType::kTexture2D })
        ->IsValid());
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    const auto frame
      = pass_->ResolveFrame(ctx_, config, { .use_fp32 = test_case.fp32 });
    ASSERT_NE(frame, nullptr);
    ASSERT_TRUE(RecordShared(signal, config).executed);
    const auto capture = &test_case == &cases.front()
      ? BeginOptionalCapture()
      : observer_ptr<FrameCaptureController> {};
    const auto zero_mask = test_case.zero_meter_mask
      ? std::optional<Signal> { Uniform(0.0F) }
      : std::nullopt;
    ASSERT_TRUE(pass_->ConvertCheckedSceneColor(ctx_, frame, config,
      { .scene_signal = signal.texture.get(),
        .scene_signal_srv = signal.srv,
        .metering_mask = zero_mask ? zero_mask->texture.get() : nullptr,
        .metering_mask_srv
        = zero_mask ? zero_mask->srv : kInvalidShaderVisibleIndex },
      *destination, uav));
    if (capture)
      EXPECT_TRUE(capture->EndCapture());
    const auto result = Read<HdrSuitabilityData>(
      *frame->conversion_buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(result.candidate_pre_exposure, test_case.fp32 ? 1.0F : 4.0F);
    EXPECT_EQ(result.expected_products, 1U << 10U);
    EXPECT_EQ(result.checked_products, result.expected_products);
    const bool accepted = test_case.failure == 0U;
    if (accepted)
      EXPECT_EQ(result.failure_flags, 0U);
    else {
      EXPECT_NE(result.failure_flags & test_case.failure, 0U);
      EXPECT_EQ(result.first_failure_product, 11U);
    }
    auto readback
      = GetReadbackManager()->CreateTextureReadback("Checked resolve result");
    {
      auto recorder = AcquireRecorder("Read checked resolve result");
      ASSERT_TRUE(recorder->AdoptKnownResourceState(*destination));
      ASSERT_TRUE(
        readback->EnqueueCopy(*recorder, *destination, {}).has_value());
    }
    const auto mapped = readback->MapNow();
    ASSERT_TRUE(mapped.has_value());
    for (unsigned y = 0U; y < height; ++y)
      for (unsigned x = 0U; x < width; ++x) {
        PackedPixel actual {};
        std::memcpy(actual.data(),
          mapped->Data() + y * mapped->Layout().row_pitch.get()
            + x * sizeof(PackedPixel),
          sizeof(PackedPixel));
        EXPECT_EQ(actual,
          !accepted                               ? sentinel
            : y == height - 1U && x == width - 1U ? test_case.last_pixel
                                                  : expected);
      }
    ctx_.scene = {};
  }
}

NOLINT_TEST_F(ExposureGpuTest,
  PreparedSceneExposureIsSolvedOnceAndPinnedAcrossResolvedColorConsumption)
{
  for (const bool persistent : { true, false }) {
    SCOPED_TRACE(persistent);
    auto service = PostProcessService(*renderer_);
    ctx_.current_view.view_state_handle = persistent
      ? CompositionView::ViewStateHandle { 91U }
      : CompositionView::kInvalidViewStateHandle;
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    auto settings = scene::ExposureSettings {};
    settings.key = 12.5F;
    service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    [[maybe_unused]] const auto& accepted = service.CaptureViewExposureSettings(
      ctx_.current_view.view_id, ctx_.current_view.view_state_handle, settings);
    auto config = PostProcessConfig { .exposure = settings };
    config.tone_mapper = engine::ToneMapper::kNone;
    config.gamma = 1.0F;
    config.enable_bloom = false;
    config.bloom_intensity = 0.0F;
    service.SetResolvedConfig(service.BuildPassConfig(
      config, ctx_.current_view.view_id, ctx_.current_view.view_state_handle));
    ASSERT_NE(service.PrepareFrameExposure(ctx_, true), nullptr);
    const auto accumulation = Uniform(.25F, 4U, 4U);
    const auto resolved = Uniform(.5F, 4U, 4U);
    auto& recorder_names
      = static_cast<ExposureFailureGraphics&>(Backend()).recorder_names;
    recorder_names.clear();
    const auto prepared
      = service.PrepareSceneExposure(ctx_.current_view.view_id, ctx_,
        { .scene_signal = accumulation.texture.get(),
          .scene_signal_srv = accumulation.srv });
    ASSERT_TRUE(prepared.has_value());
    ASSERT_NE(prepared->exposure.state, nullptr);
    const auto before = Read<ExposureStateData>(
      *prepared->exposure.state->buffer, ResourceStates::kShaderResource);
    EXPECT_NEAR(before.displayed_scale, .72F, 2e-5F);
    EXPECT_NEAR(before.raw_metered_luminance, .25F, 2e-5F);
    // A different signal and mapper at Stage 22 must neither remeter nor
    // replace the configuration pinned when the accumulation was solved.
    EXPECT_NEAR(ServicePixel(service, resolved, settings, false, 0.0F, {},
                  engine::ToneMapper::kAcesFitted, false, &*prepared),
      .36F, 2e-5F);
    const auto after = Read<ExposureStateData>(
      *prepared->exposure.state->buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(std::memcmp(&before, &after, sizeof(before)), 0);
    EXPECT_EQ(std::count(recorder_names.begin(), recorder_names.end(),
                "Vortex Exposure"),
      1);
  }
}

NOLINT_TEST_F(ExposureGpuTest,
  TonemapSelectsCheckedHalfOrOriginalFloatWithoutChangingExposure)
{
  struct Case {
    const char* name;
    float value;
    float ev;
    bool fp32;
    bool overflow;
    float expected;
    bool missing_certificate { false };
    bool bloom { false };
  };
  const std::array cases {
    Case { "accepted half", 1.0F / 3.0F, 0, true, false, .333251953125F },
    Case { "overflow fallback", 1.0F / 3.0F, 0, true, true, 1.0F / 3.0F },
    Case { "dark loss fallback", 0x1p-30F, -30, true, false, 1.0F },
    Case { "future candidate cannot approve current conversion", 0x1p20F, 20,
      true, false, 1.0F },
    Case { "nonunit P", 1.0F / 3.0F, -2, false, false, .333251953125F },
    Case { "missing certificate fallback", 1.0F / 3.0F, 0, true, false,
      1.0F / 3.0F, true },
    Case { "accepted half with external bloom", 1.0F / 3.0F, 0, true, false,
      .333251953125F + .0625F, false, true },
    Case { "float fallback with external bloom", 1.0F / 3.0F, 0, true, true,
      1.0F / 3.0F + .0625F, false, true },
    Case { "nonunit P with external bloom", 1.0F / 3.0F, -2, false, false,
      .333251953125F + .0625F, false, true },
  };
  for (const auto& test_case : cases) {
    SCOPED_TRACE(test_case.name);
    auto service = PostProcessService(*renderer_);
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    auto settings = scene::ExposureSettings {};
    settings.mode = engine::ExposureMode::kManual;
    settings.manual_ev = test_case.ev;
    settings.key = 12.5F;
    [[maybe_unused]] const auto& accepted = service.CaptureViewExposureSettings(
      ctx_.current_view.view_id, ctx_.current_view.view_state_handle, settings);
    auto config = PostProcessConfig { .exposure = settings };
    config.tone_mapper = engine::ToneMapper::kNone;
    config.gamma = 1.0F;
    config.enable_bloom = test_case.bloom;
    config.bloom_intensity = test_case.bloom ? .5F : 0.0F;
    service.SetResolvedConfig(service.BuildPassConfig(
      config, ctx_.current_view.view_id, ctx_.current_view.view_state_handle));
    static_cast<void>(service.SelectPrecisionCandidate(
      ctx_, { .product_layout_revision = 1U, .expected_products = 1024U }));
    const auto frame = service.PrepareFrameExposure(ctx_, test_case.fp32);
    ASSERT_NE(frame, nullptr);
    std::array<Pixel, 16U> pixels;
    pixels.fill(
      Pixel { test_case.value, test_case.value, test_case.value, 1.0F });
    if (test_case.overflow)
      pixels.back() = Pixel { 0x1p20F, 0x1p20F, 0x1p20F, 1.0F };
    const auto accumulation = MakeSignal(4U, 4U, pixels);
    ctx_.current_view.frame_exposure = frame;
    if (!test_case.missing_certificate)
      ASSERT_TRUE(service.CapturePreEnvironmentRange(
        ctx_, *accumulation.texture, accumulation.srv));
    const auto prepared
      = service.PrepareSceneExposure(ctx_.current_view.view_id, ctx_,
        { .scene_signal = accumulation.texture.get(),
          .scene_signal_srv = accumulation.srv });
    ASSERT_TRUE(prepared.has_value());
    const auto before = Read<ExposureStateData>(
      *prepared->exposure.state->buffer, ResourceStates::kShaderResource);
    auto destination = CreateRegisteredTexture(TextureDesc { .width = 4U,
      .height = 4U,
      .format = Format::kRGBA16Float,
      .texture_type = TextureType::kTexture2D,
      .debug_name = "CheckedTonemapHalf",
      .is_shader_resource = true,
      .is_uav = true,
      .initial_state = ResourceStates::kCommon });
    constexpr std::array<std::uint16_t, 4U> sentinel { 0x3400U, 0x3400U,
      0x3400U, 0x3c00U };
    std::array<std::byte, 1024U> initial {};
    for (unsigned y = 0U; y < 4U; ++y)
      for (unsigned x = 0U; x < 4U; ++x)
        std::memcpy(initial.data() + y * 256U + x * sizeof(sentinel),
          sentinel.data(), sizeof(sentinel));
    auto upload = CreateUploadBuffer(SizeBytes { initial.size() });
    upload->Update(initial.data(), initial.size(), 0U);
    {
      auto recorder = AcquireRecorder("Initialize checked tonemap sentinel");
      EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
      EnsureTracked(*recorder, destination, ResourceStates::kCommon);
      recorder->RequireResourceState(*destination, ResourceStates::kCopyDest);
      recorder->FlushBarriers();
      recorder->CopyBufferToTexture(*upload,
        { .buffer_offset = 0U,
          .buffer_row_pitch = 256U,
          .buffer_slice_pitch = 1024U,
          .dst_slice = { .width = 4U, .height = 4U, .depth = 1U } },
        *destination);
      recorder->RequireResourceStateFinal(
        *destination, ResourceStates::kShaderResource);
    }
    const auto bind = [&](ResourceViewType type) {
      auto& allocator = renderer_->GetGraphics()->GetDescriptorAllocator();
      auto allocation
        = allocator.AllocateRaw(type, DescriptorVisibility::kShaderVisible);
      const auto index = allocator.GetShaderVisibleIndex(allocation);
      CHECK_F(Backend()
          .GetResourceRegistry()
          .RegisterView(*destination, std::move(allocation),
            TextureViewDescription { .view_type = type,
              .format = Format::kRGBA16Float,
              .dimension = TextureType::kTexture2D })
          ->IsValid());
      return index;
    };
    const auto uav = bind(ResourceViewType::kTexture_UAV);
    const Signal resolved { destination, bind(ResourceViewType::kTexture_SRV) };
    const auto capture = test_case.overflow
      ? BeginOptionalCapture()
      : observer_ptr<FrameCaptureController> {};
    const std::array products { postprocess::ExposurePass::HdrProduct {
      .texture = accumulation.texture.get(),
      .srv = accumulation.srv,
      .id = 11U,
      .metering = true,
      .composed_error = true } };
    if (!test_case.missing_certificate)
      ASSERT_TRUE(service.PrepareScenePrecision(ctx_, *prepared, products,
        postprocess::ExposurePass::SceneComposition {}));
    ASSERT_TRUE(service.ConvertSceneColor(ctx_, *prepared,
      { .scene_signal = accumulation.texture.get(),
        .scene_signal_srv = accumulation.srv },
      *destination, uav));
    const auto converted_state = Read<ExposureStateData>(
      *prepared->exposure.state->buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(std::memcmp(&before, &converted_state, sizeof(before)), 0);
    const auto conversion_before = Read<HdrSuitabilityData>(
      *frame->conversion_buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(service.FinalizeScenePrecision(ctx_, *prepared),
      !test_case.missing_certificate);
    const auto finalized = Read<ExposureStateData>(
      *prepared->exposure.state->buffer, ResourceStates::kShaderResource);
    const auto bloom = Uniform(
      float(.125 * (test_case.fp32 ? std::exp2(double(test_case.ev)) : 1.0)),
      4U, 4U);
    EXPECT_NEAR(
      ServicePixel(service, resolved, settings, false, 0.0F, {},
        engine::ToneMapper::kNone, false, &*prepared, &accumulation, frame,
        test_case.bloom ? &bloom : nullptr, test_case.bloom ? .5F : 0.0F),
      test_case.expected, 1e-7F);
    if (capture)
      EXPECT_TRUE(capture->EndCapture());
    const auto report = Read<HdrSuitabilityData>(
      *frame->conversion_buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(std::memcmp(&conversion_before, &report, sizeof(report)), 0);
    const bool rejected = test_case.overflow || test_case.value == 0x1p-30F
      || test_case.value == 0x1p20F || test_case.missing_certificate;
    EXPECT_EQ(report.failure_flags != 0U, rejected);
    if (!test_case.fp32 && rejected)
      EXPECT_EQ(finalized.fp16_eligible_streak, 0U);
    if (test_case.fp32 && test_case.overflow)
      EXPECT_EQ(finalized.fp16_eligible_streak, 1U);
    const auto after = Read<ExposureStateData>(
      *prepared->exposure.state->buffer, ResourceStates::kShaderResource);
    EXPECT_EQ(std::memcmp(&finalized, &after, sizeof(finalized)), 0);
    const auto* bindings = service.InspectBindings(ctx_.current_view.view_id);
    ASSERT_NE(bindings, nullptr);
    EXPECT_EQ(bindings->scene_fallback_srv, accumulation.srv);
    EXPECT_EQ(bindings->conversion_report_srv, frame->conversion_srv);
  }
}

NOLINT_TEST_F(
  ExposureGpuTest, RecycledPoisonedHalfRejectsConversionAndUsesOriginalFloat)
{
  auto pool = vortex::internal::RetainedTexturePool(GetGraphicsShared());
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  pool.OnFrameStart(ctx_.frame_sequence);
  auto& reclaimer = Backend().GetDeferredReclaimer();
  reclaimer.OnBeginFrame(ctx_.frame_slot);
  const auto desc = TextureDesc { .width = 4U,
    .height = 4U,
    .format = Format::kRGBA16Float,
    .texture_type = TextureType::kTexture2D,
    .debug_name = "RecycledCheckedTonemapHalf",
    .is_shader_resource = true,
    .is_uav = true,
    .initial_state = ResourceStates::kCommon };
  auto destination = pool.Acquire(ctx_.current_view.view_id, desc, true);
  ASSERT_NE(destination, nullptr);
  constexpr std::array<std::uint16_t, 4U> poison { 0x3400U, 0x3400U, 0x3400U,
    0x3c00U };
  std::array<std::byte, 1024U> initial {};
  for (unsigned y = 0U; y < 4U; ++y) {
    for (unsigned x = 0U; x < 4U; ++x) {
      std::memcpy(initial.data() + y * 256U + x * sizeof(poison), poison.data(),
        sizeof(poison));
    }
  }
  auto upload = CreateUploadBuffer(SizeBytes { initial.size() });
  upload->Update(initial.data(), initial.size(), 0U);
  {
    auto recorder = AcquireRecorder("Poison retained half before recycling");
    EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
    // The pool owns registration. The fixture's EnsureTracked(texture) would
    // retain an additional reader and prevent this deliberate reuse.
    recorder->BeginTrackingResourceState(*destination, ResourceStates::kCommon);
    recorder->RequireResourceState(*destination, ResourceStates::kCopyDest);
    recorder->FlushBarriers();
    recorder->CopyBufferToTexture(*upload,
      { .buffer_offset = 0U,
        .buffer_row_pitch = 256U,
        .buffer_slice_pitch = 1024U,
        .dst_slice = { .width = 4U, .height = 4U, .depth = 1U } },
      *destination);
    recorder->RequireResourceStateFinal(
      *destination, ResourceStates::kCopySource);
  }
  WaitForQueueIdle();
  const auto native = destination->GetNativeResource();
  auto* const physical_identity = destination.get();
  const std::weak_ptr<Texture> physical = destination->shared_from_this();
  ASSERT_EQ(
    Backend().TryGetKnownResourceState(native), ResourceStates::kCopySource);
  destination.reset();
  reclaimer.OnBeginFrame(ctx_.frame_slot);
  ASSERT_FALSE(physical.expired());
  ASSERT_FALSE(Backend().GetResourceRegistry().Contains(*physical.lock()));
  ASSERT_FALSE(GetQueue()->TryGetKnownResourceState(native).has_value());
  ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
  pool.OnFrameStart(ctx_.frame_sequence);
  destination = pool.Acquire(ctx_.current_view.view_id, desc, true);
  ASSERT_EQ(destination.get(), physical_identity);
  ASSERT_EQ(
    GetQueue()->TryGetKnownResourceState(native), ResourceStates::kCopySource);

  auto service = PostProcessService(*renderer_);
  service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 0.0F;
  settings.key = 12.5F;
  [[maybe_unused]] const auto& accepted = service.CaptureViewExposureSettings(
    ctx_.current_view.view_id, ctx_.current_view.view_state_handle, settings);
  auto config = PostProcessConfig { .exposure = settings };
  config.tone_mapper = engine::ToneMapper::kNone;
  config.gamma = 1.0F;
  config.enable_bloom = false;
  config.bloom_intensity = 0.0F;
  service.SetResolvedConfig(service.BuildPassConfig(
    config, ctx_.current_view.view_id, ctx_.current_view.view_state_handle));
  static_cast<void>(service.SelectPrecisionCandidate(
    ctx_, { .product_layout_revision = 1U, .expected_products = 1024U }));
  const auto frame = service.PrepareFrameExposure(ctx_, true);
  ASSERT_NE(frame, nullptr);
  ctx_.current_view.frame_exposure = frame;
  std::array<Pixel, 16U> pixels;
  pixels.fill(Pixel { 1.0F / 3.0F, 1.0F / 3.0F, 1.0F / 3.0F, 1.0F });
  pixels.back() = Pixel { 0x1p20F, 0x1p20F, 0x1p20F, 1.0F };
  const auto accumulation = MakeSignal(4U, 4U, pixels);
  ASSERT_TRUE(service.CapturePreEnvironmentRange(
    ctx_, *accumulation.texture, accumulation.srv));
  const auto prepared
    = service.PrepareSceneExposure(ctx_.current_view.view_id, ctx_,
      { .scene_signal = accumulation.texture.get(),
        .scene_signal_srv = accumulation.srv });
  ASSERT_TRUE(prepared.has_value());
  const auto solved = Read<ExposureStateData>(
    *prepared->exposure.state->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(solved.displayed_scale, 1.0F);
  const auto domain
    = Read<FrameExposureData>(*frame->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(domain.pre_exposure, 1.0F);
  EXPECT_EQ(domain.one_over_pre_exposure, 1.0F);
  const auto bind = [&](const ResourceViewType type) {
    auto& allocator = renderer_->GetGraphics()->GetDescriptorAllocator();
    auto allocation
      = allocator.AllocateRaw(type, DescriptorVisibility::kShaderVisible);
    const auto index = allocator.GetShaderVisibleIndex(allocation);
    CHECK_F(Backend()
        .GetResourceRegistry()
        .RegisterView(*destination, std::move(allocation),
          TextureViewDescription { .view_type = type,
            .format = Format::kRGBA16Float,
            .dimension = TextureType::kTexture2D })
        ->IsValid());
    return index;
  };
  const auto uav = bind(ResourceViewType::kTexture_UAV);
  const Signal resolved { destination, bind(ResourceViewType::kTexture_SRV) };
  const std::array products { postprocess::ExposurePass::HdrProduct {
    .texture = accumulation.texture.get(),
    .srv = accumulation.srv,
    .id = 11U,
    .metering = true,
    .composed_error = true } };
  ASSERT_TRUE(service.PrepareScenePrecision(
    ctx_, *prepared, products, postprocess::ExposurePass::SceneComposition {}));
  ASSERT_TRUE(service.ConvertSceneColor(ctx_, *prepared,
    { .scene_signal = accumulation.texture.get(),
      .scene_signal_srv = accumulation.srv },
    *destination, uav));
  EXPECT_EQ(GetQueue()->TryGetKnownResourceState(native),
    ResourceStates::kShaderResource);
  const auto converted = Read<ExposureStateData>(
    *prepared->exposure.state->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(std::memcmp(&solved, &converted, sizeof(solved)), 0);
  const auto report = Read<HdrSuitabilityData>(
    *frame->conversion_buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(report.candidate_pre_exposure, 1.0F);
  EXPECT_EQ(report.expected_products, 1024U);
  EXPECT_EQ(report.checked_products, 1024U);
  // Composed-scene qualification rejects an unrepresentable interval before
  // the point-sample overflow path. Only the deliberately oversized texel
  // must fail; rejection of every texel would hide an invalid certificate.
  EXPECT_EQ(report.failure_flags, 16U);
  EXPECT_EQ(report.image_failures, 1U);
  EXPECT_EQ(report.checked_samples, 16U);
  EXPECT_EQ(report.first_failure_product, 11U);
  ASSERT_TRUE(service.FinalizeScenePrecision(ctx_, *prepared));
  const auto finalized = Read<ExposureStateData>(
    *prepared->exposure.state->buffer, ResourceStates::kShaderResource);

  // Rejection must preserve the poisoned contents across the entire image.
  // The visible result below must therefore come from the original FP32 input.
  {
    auto readback
      = GetReadbackManager()->CreateTextureReadback("Recycled half poison");
    {
      auto recorder = AcquireRecorder("Read rejected recycled half");
      ASSERT_TRUE(recorder->AdoptKnownResourceState(*destination));
      ASSERT_TRUE(
        readback->EnqueueCopy(*recorder, *destination, {}).has_value());
    }
    const auto mapped = readback->MapNow();
    ASSERT_TRUE(mapped.has_value());
    for (unsigned y = 0U; y < 4U; ++y) {
      for (unsigned x = 0U; x < 4U; ++x) {
        std::array<std::uint16_t, 4U> actual {};
        std::memcpy(actual.data(),
          mapped->Data() + y * mapped->Layout().row_pitch.get()
            + x * sizeof(poison),
          sizeof(actual));
        EXPECT_EQ(actual, poison);
      }
    }
  }
  EXPECT_NEAR(
    ServicePixel(service, resolved, settings, false, 0.0F, {},
      engine::ToneMapper::kNone, false, &*prepared, &accumulation, frame),
    1.0F / 3.0F, 1e-7F)
    << "Consuming the stale half texture would return 0.25 instead";
  const auto after = Read<ExposureStateData>(
    *prepared->exposure.state->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(std::memcmp(&finalized, &after, sizeof(finalized)), 0);
  const auto report_after = Read<HdrSuitabilityData>(
    *frame->conversion_buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(std::memcmp(&report, &report_after, sizeof(report)), 0);
  const auto domain_after
    = Read<FrameExposureData>(*frame->buffer, ResourceStates::kShaderResource);
  EXPECT_EQ(std::memcmp(&domain, &domain_after, sizeof(domain)), 0);
  const auto* bindings = service.InspectBindings(ctx_.current_view.view_id);
  ASSERT_NE(bindings, nullptr);
  EXPECT_EQ(bindings->scene_fallback_srv, accumulation.srv);
  EXPECT_EQ(bindings->conversion_report_srv, frame->conversion_srv);
}

NOLINT_TEST_F(
  ExposureGpuTest, CheckedConversionWaitsForInitialMeteringMaskPolicy)
{
  for (const bool pending : { true, false }) {
    SCOPED_TRACE(pending);
    auto loader = vortex::testing::FakeAssetLoader {};
    auto service = PostProcessService(*renderer_,
      pending ? observer_ptr { &loader }
              : observer_ptr<vortex::testing::FakeAssetLoader> {});
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    auto requested = scene::ExposureSettings {};
    requested.metering_mask = pending ? loader.MintSyntheticTextureKey()
                                      : content::ResourceKey { 123U };
    const auto& captured
      = service.CaptureViewExposureSettings(ctx_.current_view.view_id,
        ctx_.current_view.view_state_handle, requested);
    EXPECT_EQ(captured.revision, 0U);
    EXPECT_EQ(captured.mask_status,
      pending ? PostProcessService::ExposureMaskStatus::kPending
              : PostProcessService::ExposureMaskStatus::kFailed);
    auto config = PostProcessConfig {};
    service.SetResolvedConfig(service.BuildPassConfig(
      config, ctx_.current_view.view_id, ctx_.current_view.view_state_handle));
    ASSERT_NE(service.PrepareFrameExposure(ctx_, true), nullptr);
    const auto source = Uniform(.25F, 4U, 4U);
    const auto inputs
      = PostProcessService::Inputs { .scene_signal = source.texture.get(),
          .scene_signal_srv = source.srv };
    const auto prepared
      = service.PrepareSceneExposure(ctx_.current_view.view_id, ctx_, inputs);
    ASSERT_TRUE(prepared.has_value());
    auto destination = CreateRegisteredTexture(TextureDesc { .width = 4U,
      .height = 4U,
      .format = Format::kRGBA16Float,
      .texture_type = TextureType::kTexture2D,
      .is_shader_resource = true,
      .is_uav = true,
      .initial_state = ResourceStates::kCommon });
    auto& allocator = renderer_->GetGraphics()->GetDescriptorAllocator();
    auto allocation = allocator.AllocateRaw(
      ResourceViewType::kTexture_UAV, DescriptorVisibility::kShaderVisible);
    const auto index = allocator.GetShaderVisibleIndex(allocation);
    ASSERT_TRUE(Backend()
        .GetResourceRegistry()
        .RegisterView(*destination, std::move(allocation),
          TextureViewDescription { .view_type = ResourceViewType::kTexture_UAV,
            .format = Format::kRGBA16Float,
            .dimension = TextureType::kTexture2D })
        ->IsValid());
    auto& backend = static_cast<ExposureFailureGraphics&>(Backend());
    backend.recorder_names.clear();
    EXPECT_FALSE(
      service.ConvertSceneColor(ctx_, *prepared, inputs, *destination, index));
    EXPECT_TRUE(backend.recorder_names.empty());
  }
}

NOLINT_TEST_F(ExposureGpuTest,
  FailedPreparationCannotReuseAnotherViewsSuccessfulOutputStatus)
{
  auto service = PostProcessService(*renderer_);
  const auto signal = Uniform(.25F, 4U, 4U);
  ctx_.current_view.view_id = ViewId { 92U };
  ctx_.current_view.view_state_handle
    = CompositionView::ViewStateHandle { 92U };
  EXPECT_NEAR(ServicePixel(service, signal), .18F, 2e-5F);
  ctx_.current_view.view_id = ViewId { 1U };
  ctx_.current_view.view_state_handle = CompositionView::ViewStateHandle { 1U };
  EXPECT_NEAR(ServicePixel(service, signal), .18F, 2e-5F);
  ASSERT_TRUE(service.GetLastExecutionState().wrote_visible_output);
  ctx_.current_view.view_id = ViewId { 92U };
  ctx_.current_view.view_state_handle
    = CompositionView::ViewStateHandle { 92U };
  ASSERT_NE(service.PrepareFrameExposure(ctx_, true), nullptr);
  auto& backend = static_cast<ExposureFailureGraphics&>(Backend());
  backend.fail_next_exposure_recorder = true;
  backend.fail_next_fallback_recorder = true;
  const auto prepared
    = service.PrepareSceneExposure(ctx_.current_view.view_id, ctx_,
      { .scene_signal = signal.texture.get(), .scene_signal_srv = signal.srv });
  EXPECT_FALSE(prepared.has_value());
  EXPECT_FALSE(service.GetLastExecutionState().wrote_visible_output);
  EXPECT_EQ(service.GetLastExecutionState().view_id, ctx_.current_view.view_id);
}

NOLINT_TEST_F(ExposureGpuTest,
  Fp16EligibilityRequiresStableCompleteFramesAndPreservesExposure)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 0.0F;
  auto config = SharedConfig(settings);
  const auto ordinary = Uniform(1.0F, 4U, 4U);
  const auto brighter = Uniform(2.0F, 4U, 4U);
  const auto invalid = Uniform(std::numeric_limits<float>::infinity(), 4U, 4U);
  auto token = renderer_->QueueExposureTransition(
    ctx_.current_view.view_state_handle, ExposureTransitionPolicy::kPreserve);
  ASSERT_TRUE(token.has_value());
  const auto step
    = [&](std::uint64_t sequence, std::uint64_t layout, std::uint64_t revision,
        const Signal& signal, std::uint32_t expected = 1024U,
        bool invalidate = false, bool metering_available = true) {
        config = SharedConfig(settings, {}, revision);
        return EligibilityStep(signal, config, sequence, layout, expected,
          invalidate, nullptr, *token, metering_available, sequence == 2U);
      };
  EXPECT_EQ(step(1U, 7U, 1U, ordinary).first.fp16_eligible_streak, 1U);
  const auto eligible = step(2U, 7U, 1U, ordinary);
  EXPECT_EQ(eligible.first.fp16_eligible_streak, 2U);
  EXPECT_EQ(eligible.first.flags & 256U, 256U);
  EXPECT_EQ(eligible.first.fp16_candidate_pre_exposure, 8192.0F);
  const auto failure = step(3U, 7U, 1U, invalid);
  EXPECT_EQ(failure.first.fp16_eligible_streak, 0U);
  EXPECT_NE(failure.second.flags & 2U, 0U);
  EXPECT_EQ(failure.second.first_failure_product, 11U);
  EXPECT_NE(failure.second.first_failure_kind & 1U, 0U);
  EXPECT_EQ(step(4U, 7U, 1U, ordinary).first.fp16_eligible_streak, 1U);
  EXPECT_EQ(step(5U, 7U, 1U, ordinary).first.fp16_eligible_streak, 2U);
  EXPECT_EQ(step(6U, 7U, 2U, ordinary).first.fp16_eligible_streak, 1U);
  EXPECT_EQ(step(7U, 7U, 2U, ordinary).first.fp16_eligible_streak, 2U);
  EXPECT_EQ(step(8U, 8U, 2U, ordinary).first.fp16_eligible_streak, 1U);
  EXPECT_EQ(step(9U, 8U, 2U, ordinary).first.fp16_eligible_streak, 2U);
  EXPECT_EQ(step(10U, 8U, 2U, brighter).first.fp16_eligible_streak, 1U);
  EXPECT_EQ(step(12U, 8U, 2U, brighter).first.fp16_eligible_streak, 2U);
  EXPECT_EQ(
    step(13U, 8U, 2U, brighter, 1024U, true).first.fp16_eligible_streak, 1U);
  const auto missing = step(14U, 8U, 2U, brighter, 1025U);
  EXPECT_EQ(missing.first.fp16_eligible_streak, 0U);
  EXPECT_EQ(missing.second.first_failure_product, 1U);
  EXPECT_NE(missing.second.first_failure_kind & 16U, 0U);
  EXPECT_EQ(
    step(0xffffffffULL, 8U, 2U, brighter).first.fp16_eligible_streak, 1U);
  EXPECT_EQ(
    step(0x100000000ULL, 8U, 2U, brighter).first.fp16_eligible_streak, 2U);
  settings.mode = engine::ExposureMode::kAuto;
  config = SharedConfig(settings);
  token = renderer_->QueueExposureTransition(
    ctx_.current_view.view_state_handle, ExposureTransitionPolicy::kRemeter);
  ASSERT_TRUE(token.has_value());
  const auto pending
    = step(0x100000001ULL, 8U, 3U, ordinary, 1024U, false, false);
  EXPECT_EQ(pending.first.fp16_eligible_streak, 0U);
  EXPECT_NE(
    pending.first.requested_generation, pending.first.applied_generation);
  EXPECT_EQ(
    step(0x100000002ULL, 8U, 3U, ordinary).first.fp16_eligible_streak, 1U);
  EXPECT_EQ(
    step(0x100000003ULL, 8U, 3U, ordinary).first.fp16_eligible_streak, 2U);
}

NOLINT_TEST_F(ExposureGpuTest, Fp16EligibilityBelongsToEachBorrowingImage)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 0.0F;
  const auto config = SharedConfig(settings);
  const auto root = CompositionView::ViewStateHandle { 10U };
  const auto borrower = CompositionView::ViewStateHandle { 20U };
  const auto ordinary = Uniform(1.0F, 4U, 4U);
  const std::array wide_pixels { Pixel { 0x1p30F, 0x1p30F, 0x1p30F, 1 },
    Pixel { 0x1p-16F, 0x1p-16F, 0x1p-16F, 1 } };
  const auto wide = MakeSignal(2U, 1U, wide_pixels);
  const auto source
    = postprocess::ExposurePass::Source { .handle = root, .config = config };
  const auto step
    = [&](std::uint64_t sequence, bool sharing, const Signal& signal) {
        ctx_.current_view.view_state_handle = sharing ? borrower : root;
        ctx_.current_view.view_id = ViewId { sharing ? 20U : 10U };
        return EligibilityStep(signal, config, sequence, 1U, 1024U, false,
          sharing ? &source : nullptr)
          .first;
      };
  EXPECT_EQ(step(1U, false, ordinary).fp16_eligible_streak, 1U);
  EXPECT_EQ(step(1U, true, ordinary).fp16_eligible_streak, 0U);
  EXPECT_EQ(step(2U, false, ordinary).fp16_eligible_streak, 2U);
  EXPECT_EQ(step(2U, true, ordinary).fp16_eligible_streak, 1U);
  EXPECT_EQ(step(3U, false, ordinary).fp16_eligible_streak, 2U);
  const auto rejected = step(3U, true, wide);
  EXPECT_EQ(rejected.fp16_eligible_streak, 0U);
  EXPECT_EQ(rejected.displayed_scale, 1.0F);
  EXPECT_EQ(step(4U, true, ordinary).fp16_eligible_streak, 1U);
  EXPECT_EQ(step(5U, true, ordinary).fp16_eligible_streak, 2U);
}

NOLINT_TEST_F(
  ExposureGpuTest, Fp16EligibilityExcludesStatelessAndDiagnosticFrames)
{
  auto settings = scene::ExposureSettings {};
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 0.0F;
  auto config = SharedConfig(settings);
  const auto signal = Uniform(1.0F, 4U, 4U);
  ctx_.current_view.view_state_handle
    = CompositionView::kInvalidViewStateHandle;
  EXPECT_EQ(EligibilityStep(signal, config, 1U).first.fp16_eligible_streak, 0U);
  EXPECT_EQ(EligibilityStep(signal, config, 2U).first.fp16_eligible_streak, 0U);
  ctx_.current_view.view_state_handle
    = CompositionView::ViewStateHandle { 30U };
  config = config.WithDiagnosticOverride(true);
  EXPECT_EQ(EligibilityStep(signal, config, 3U).first.fp16_eligible_streak, 0U);
  EXPECT_EQ(EligibilityStep(signal, config, 4U).first.fp16_eligible_streak, 0U);
  config = config.WithDiagnosticOverride(false);
  EXPECT_EQ(EligibilityStep(signal, config, 5U).first.fp16_eligible_streak, 1U);
  EXPECT_EQ(EligibilityStep(signal, config, 6U).first.fp16_eligible_streak, 2U);
  ctx_.frame_sequence = frame::SequenceNumber { 7U };
  ASSERT_TRUE(RecordShared(signal, config).executed);
  EXPECT_EQ(EligibilityStep(signal, config, 8U).first.fp16_eligible_streak, 1U);
  EXPECT_EQ(EligibilityStep(signal, config, 9U).first.fp16_eligible_streak, 2U);
  ctx_.frame_sequence = frame::SequenceNumber { 10U };
  const auto unsolved = pass_->ResolveFrame(ctx_, config, { .use_fp32 = true });
  ASSERT_NE(unsolved, nullptr);
  const std::array products { postprocess::ExposurePass::HdrProduct {
    .texture = signal.texture.get(), .srv = signal.srv, .id = 11U } };
  ASSERT_TRUE(
    pass_->EvaluateFp16Products(ctx_, unsolved, config, products, {}));
  ASSERT_TRUE(pass_->FinalizeFp16Suitability(ctx_, unsolved,
    { .product_layout_revision = 7U, .expected_products = 1024U }));
  const auto status = Read<ExposureCompletedStatus>(
    *unsolved->current_state->status_buffer, ResourceStates::kCopySource);
  EXPECT_EQ(status.flags & (1U | 4U), 0U);
  EXPECT_EQ(status.fp16_eligible_streak, 0U);
}

NOLINT_TEST_F(ExposureGpuTest,
  StaticSkyUploadKeepsHalfAndFloatStorageCoherentAcrossFacesAndMips)
{
  for (const bool wide : { false, true }) {
    data::pak::core::TextureResourceDesc desc {};
    desc.texture_type = static_cast<std::uint8_t>(TextureType::kTextureCube);
    desc.width = desc.height = 2U;
    desc.depth = 1U;
    desc.array_layers = 6U;
    desc.mip_levels = 1U;
    desc.format = static_cast<std::uint8_t>(Format::kRGBA32Float);
    desc.alignment = 256U;
    desc.content_hash = wide ? 2U : 1U;
    std::vector<std::uint8_t> data_region(6U * 4U * sizeof(Pixel));
    std::vector<data::pak::render::SubresourceLayout> layouts;
    std::array<Pixel, 6U> colors;
    for (unsigned face = 0U; face < 6U; ++face) {
      colors[face] = { wide ? 0x1p30F : .5F, wide ? 0x1p-24F : .25F,
        .25F + face * .125F, 1.0F };
      for (unsigned pixel = 0U; pixel < 4U; ++pixel)
        std::memcpy(data_region.data() + (face * 4U + pixel) * sizeof(Pixel),
          colors[face].data(), sizeof(Pixel));
      layouts.push_back({ .offset_bytes = face * 64U,
        .row_pitch_bytes = 32U,
        .size_bytes = 64U });
    }
    auto payload = vortex::testing::detail::BuildV4TexturePayload(
      desc, layouts, data_region);
    desc.size_bytes = static_cast<std::uint32_t>(payload.size());
    auto source = data::TextureResource(desc, std::move(payload));
    auto model = environment::SkyLightEnvironmentModel {};
    model.enabled = true;
    model.source = environment::kSkyLightSourceSpecifiedCubemap;
    model.cubemap_resource = content::ResourceKey { wide ? 502U : 501U };
    model.lower_hemisphere_is_solid_color = false;
    auto processor = environment::internal::IblProcessor(*renderer_);
    const auto first
      = processor.RefreshStaticSkyLightProducts({}, model, &source);
    auto texture
      = static_cast<ExposureFailureGraphics&>(Backend()).processed_sky.lock();
    ASSERT_NE(texture, nullptr);
    ASSERT_EQ(texture->GetDescriptor().format,
      wide ? Format::kRGBA32Float : Format::kRGBA16Float);
    ASSERT_EQ(texture->GetDescriptor().mip_levels, 2U);
    WaitForQueueIdle();
    renderer_->GetUploadCoordinator().OnFrameStart(
      vortex::internal::RendererTagFactory::Get(),
      frame::Slot { wide ? 1U : 0U });
    const auto ready = processor.RefreshStaticSkyLightProducts(
      first.probe_state, model, &source);
    ASSERT_TRUE(ready.probe_state.valid);
    const auto scale = ready.probe_state.static_sky_light.source_radiance_scale;
    for (unsigned face = 0U; face < 6U; ++face) {
      for (unsigned mip = 0U; mip < 2U; ++mip) {
        auto readback
          = GetReadbackManager()->CreateTextureReadback("Processed sky texel");
        {
          auto recorder = AcquireRecorder("Processed sky readback");
          ASSERT_TRUE(recorder->AdoptKnownResourceState(*texture));
          ASSERT_TRUE(readback
              ->EnqueueCopy(*recorder, *texture,
                { .src_slice = { .width = 1U,
                    .height = 1U,
                    .depth = 1U,
                    .mip_level = mip,
                    .array_slice = face } })
              .has_value());
        }
        const auto mapped = readback->MapNow();
        ASSERT_TRUE(mapped.has_value());
        Pixel pixel {};
        if (wide)
          std::memcpy(pixel.data(), mapped->Data(), sizeof(pixel));
        else {
          std::array<std::uint16_t, 4U> packed {};
          std::memcpy(packed.data(), mapped->Data(), sizeof(packed));
          for (unsigned channel = 0U; channel < 4U; ++channel)
            pixel[channel] = data::HalfFloat { packed[channel] }.ToFloat();
        }
        for (unsigned channel = 0U; channel < 3U; ++channel)
          EXPECT_NEAR(static_cast<double>(pixel[channel]) * scale,
            colors[face][channel],
            std::abs(static_cast<double>(colors[face][channel])) * 2e-5
              + 0x1p-120);
        EXPECT_EQ(pixel[3], 1.0F);
      }
    }
    FlushBackend();
  }
}

NOLINT_TEST_F(ExposureGpuTest,
  StaticSkyIntensityEditPromotesBeforePublishingAmplifiedHalfLoss)
{
  data::pak::core::TextureResourceDesc desc {};
  desc.texture_type = static_cast<std::uint8_t>(TextureType::kTextureCube);
  desc.width = desc.height = desc.depth = 1U;
  desc.array_layers = 6U;
  desc.mip_levels = 1U;
  desc.format = static_cast<std::uint8_t>(Format::kRGBA32Float);
  desc.alignment = 256U;
  desc.content_hash = 99U;
  std::vector<std::uint8_t> bytes(6U * sizeof(Pixel));
  std::vector<data::pak::render::SubresourceLayout> layouts;
  const Pixel color { 0x1p-50F, 0x1p-50F, 0x1p-50F, 1.0F };
  for (unsigned face = 0U; face < 6U; ++face) {
    std::memcpy(
      bytes.data() + face * sizeof(Pixel), color.data(), sizeof(Pixel));
    layouts.push_back({ .offset_bytes = face * 16U,
      .row_pitch_bytes = 16U,
      .size_bytes = 16U });
  }
  auto payload
    = vortex::testing::detail::BuildV4TexturePayload(desc, layouts, bytes);
  desc.size_bytes = static_cast<std::uint32_t>(payload.size());
  auto source = data::TextureResource(desc, std::move(payload));
  auto model = environment::SkyLightEnvironmentModel {};
  model.enabled = true;
  model.source = environment::kSkyLightSourceSpecifiedCubemap;
  model.cubemap_resource = content::ResourceKey { 511U };
  model.lower_hemisphere_is_solid_color = false;
  auto processor = environment::internal::IblProcessor(*renderer_);
  auto state
    = processor.RefreshStaticSkyLightProducts({}, model, &source).probe_state;
  auto half
    = static_cast<ExposureFailureGraphics&>(Backend()).processed_sky.lock();
  ASSERT_NE(half, nullptr);
  EXPECT_EQ(half->GetDescriptor().format, Format::kRGBA16Float);
  WaitForQueueIdle();
  renderer_->GetUploadCoordinator().OnFrameStart(
    vortex::internal::RendererTagFactory::Get(), frame::Slot { 0U });
  state = processor.RefreshStaticSkyLightProducts(state, model, &source)
            .probe_state;
  ASSERT_TRUE(state.valid);
  const auto original_key = state.static_sky_light.key;
  const auto original_revision = state.static_sky_light.product_revision;
  model.intensity_mul = 2.0F;
  const auto harmless
    = processor.RefreshStaticSkyLightProducts(state, model, &source);
  EXPECT_FALSE(harmless.refreshed);
  EXPECT_EQ(
    harmless.probe_state.static_sky_light.product_revision, original_revision);
  EXPECT_EQ(
    static_cast<ExposureFailureGraphics&>(Backend()).processed_sky.lock(),
    half);
  model.intensity_mul = 0x1p50F;
  const auto pending = processor.RefreshStaticSkyLightProducts(
    harmless.probe_state, model, &source);
  EXPECT_FALSE(pending.probe_state.valid);
  EXPECT_EQ(pending.probe_state.static_sky_light.processed_cubemap_srv,
    kInvalidShaderVisibleIndex);
  auto full
    = static_cast<ExposureFailureGraphics&>(Backend()).processed_sky.lock();
  ASSERT_NE(full, nullptr);
  EXPECT_NE(full, half);
  EXPECT_EQ(full->GetDescriptor().format, Format::kRGBA32Float);
  WaitForQueueIdle();
  renderer_->GetUploadCoordinator().OnFrameStart(
    vortex::internal::RendererTagFactory::Get(), frame::Slot { 1U });
  state = processor
            .RefreshStaticSkyLightProducts(pending.probe_state, model, &source)
            .probe_state;
  ASSERT_TRUE(state.valid);
  EXPECT_EQ(state.static_sky_light.key, original_key);
  EXPECT_GT(state.static_sky_light.product_revision, original_revision);
  auto service = EnvironmentLightingService(*renderer_);
  const auto published
    = vortex::testing::RendererPublicationProbe::BuildStaticSkyPublication(
      service, ctx_, state, model);
  EXPECT_EQ(published.sky_light.radiance_scale, 0x1p50F);
  auto readback
    = GetReadbackManager()->CreateTextureReadback("Promoted sky pixel");
  {
    auto recorder = AcquireRecorder("Promoted sky readback");
    ASSERT_TRUE(recorder->AdoptKnownResourceState(*full));
    ASSERT_TRUE(readback
        ->EnqueueCopy(*recorder, *full,
          { .src_slice = { .width = 1U, .height = 1U, .depth = 1U } })
        .has_value());
  }
  const auto mapped = readback->MapNow();
  ASSERT_TRUE(mapped.has_value());
  Pixel actual {};
  std::memcpy(actual.data(), mapped->Data(), sizeof(actual));
  for (unsigned channel = 0U; channel < 3U; ++channel)
    EXPECT_EQ(actual[channel] * published.sky_light.radiance_scale, 1.0F);
  model.intensity_mul = 1.0F;
  const auto dimmed
    = processor.RefreshStaticSkyLightProducts(state, model, &source);
  EXPECT_FALSE(dimmed.refreshed);
  EXPECT_EQ(dimmed.probe_state.static_sky_light.product_revision,
    state.static_sky_light.product_revision);
  EXPECT_EQ(
    static_cast<ExposureFailureGraphics&>(Backend()).processed_sky.lock(),
    full);
  FlushBackend();
}

NOLINT_TEST_F(ExposureGpuTest,
  CompletedPrecisionAdmissionTracksViewSettingsLayoutAndDiagnostics)
{
  auto service = PostProcessService(*renderer_);
  auto config = PostProcessConfig {};
  config.exposure.mode = engine::ExposureMode::kManual;
  config.exposure.manual_ev = 0.0F;
  config.exposure.key = 12.5F;
  service.SetConfig(config);
  const auto ordinary = Uniform(1.0F, 4U, 4U);
  const auto invalid = Uniform(std::numeric_limits<float>::infinity(), 4U, 4U);
  auto layout = std::uint64_t { 7U };
  const auto step = [&](bool expected_candidate, std::uint32_t expected_streak,
                      const Signal& signal, bool diagnostic = false) {
    WaitForQueueIdle();
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    ctx_.frame_slot = frame::Slot { static_cast<unsigned>(sequence_ % 3U) };
    ctx_.render_mode = diagnostic ? RenderMode::kWireframe : RenderMode::kSolid;
    service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    const auto requirements = postprocess::ExposurePass::EligibilityInputs {
      .product_layout_revision = layout, .expected_products = 1024U
    };
    const auto candidate = service.SelectPrecisionCandidate(ctx_, requirements);
    EXPECT_EQ(candidate != nullptr, expected_candidate) << sequence_;
    EXPECT_EQ(service.SelectPrecisionCandidate(ctx_, requirements), candidate);
    EXPECT_NE(service.PrepareFrameExposure(ctx_, true), nullptr);
    const auto prepared = service.PrepareSceneExposure(
      ctx_.current_view.view_id, ctx_,
      { .scene_signal = signal.texture.get(), .scene_signal_srv = signal.srv });
    CHECK_F(prepared.has_value());
    const std::array products { postprocess::ExposurePass::HdrProduct {
      .texture = signal.texture.get(),
      .srv = signal.srv,
      .id = 11U,
      .metering = true } };
    EXPECT_EQ((service.PrepareScenePrecision(ctx_, *prepared, products)
                && service.FinalizeScenePrecision(ctx_, *prepared)),
      !diagnostic);
    if (!diagnostic) {
      EXPECT_TRUE((service.PrepareScenePrecision(ctx_, *prepared, products)
        && service.FinalizeScenePrecision(ctx_, *prepared)));
      EXPECT_EQ(
        ReadState(prepared->exposure).fp16_eligible_streak, expected_streak);
    }
    const auto counts
      = vortex::testing::RendererPublicationProbe::ExposureStatusCounts(
        service, ctx_.current_view.view_state_handle);
    EXPECT_LE(counts.first, 1U);
    EXPECT_EQ(counts.second, 0U);
    return candidate;
  };
  EXPECT_EQ(step(false, 1U, ordinary), nullptr);
  EXPECT_EQ(step(false, 2U, ordinary), nullptr);
  const auto first = step(true, 2U, ordinary);
  ASSERT_NE(first, nullptr);
  EXPECT_EQ(
    Read<ExposureStateData>(*first->buffer, ResourceStates::kShaderResource)
      .frame_sequence[0],
    2U);
  layout = 8U;
  step(false, 1U, ordinary);
  step(false, 2U, ordinary);
  step(true, 2U, ordinary);
  config.exposure.manual_ev = 1.0F;
  service.SetConfig(config);
  step(false, 1U, ordinary);
  step(false, 2U, ordinary);
  step(true, 2U, ordinary);
  step(false, 0U, ordinary, true);
  step(false, 1U, ordinary);
  step(false, 2U, ordinary);
  step(true, 2U, ordinary);
  step(true, 0U, invalid);
  step(false, 1U, ordinary);
  step(false, 2U, ordinary);
  step(true, 2U, ordinary);
  service.RemoveViewState(
    ctx_.current_view.view_id, ctx_.current_view.view_state_handle);
  step(false, 1U, ordinary);
  WaitForQueueIdle();
}

NOLINT_TEST_F(
  ExposureGpuTest, PrecisionStatusRetriesTransportWithoutEarlyAdmission)
{
  auto service = PostProcessService(*renderer_);
  auto config = PostProcessConfig {};
  config.exposure.mode = engine::ExposureMode::kManual;
  config.exposure.manual_ev = 0.0F;
  config.exposure.key = 12.5F;
  service.SetConfig(config);
  auto& backend = static_cast<ExposureFailureGraphics&>(Backend());
  backend.fail_status_recorder = true;
  const auto signal = Uniform(1.0F, 4U, 4U);
  const auto requirements = postprocess::ExposurePass::EligibilityInputs {
    .product_layout_revision = 7U, .expected_products = 1024U
  };
  const auto begin = [&] {
    WaitForQueueIdle();
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    ctx_.frame_slot = frame::Slot { static_cast<unsigned>(sequence_ % 3U) };
    service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    return service.SelectPrecisionCandidate(ctx_, requirements);
  };
  for (unsigned i = 0U; i < 6U; ++i) {
    EXPECT_EQ(begin(), nullptr);
    ASSERT_NE(service.PrepareFrameExposure(ctx_, true), nullptr);
    const auto prepared = service.PrepareSceneExposure(
      ctx_.current_view.view_id, ctx_,
      { .scene_signal = signal.texture.get(), .scene_signal_srv = signal.srv });
    ASSERT_TRUE(prepared.has_value());
    const std::array products { postprocess::ExposurePass::HdrProduct {
      .texture = signal.texture.get(),
      .srv = signal.srv,
      .id = 11U,
      .metering = true } };
    ASSERT_TRUE((service.PrepareScenePrecision(ctx_, *prepared, products)
      && service.FinalizeScenePrecision(ctx_, *prepared)));
    const auto counts
      = vortex::testing::RendererPublicationProbe::ExposureStatusCounts(
        service, ctx_.current_view.view_state_handle);
    EXPECT_EQ(counts.first, 0U);
    EXPECT_EQ(counts.second, 1U);
  }
  backend.fail_status_recorder = false;
  EXPECT_EQ(
    begin(), nullptr); // Retry records a copy; it is not an acknowledgement.
  const auto candidate = begin();
  ASSERT_NE(candidate, nullptr);
  EXPECT_EQ(
    Read<ExposureStateData>(*candidate->buffer, ResourceStates::kShaderResource)
      .frame_sequence[0],
    6U);
  const auto token = renderer_->QueueExposureTransition(
    ctx_.current_view.view_state_handle, ExposureTransitionPolicy::kPreserve);
  ASSERT_TRUE(token.has_value());
  EXPECT_EQ(service.SelectPrecisionCandidate(ctx_, requirements), nullptr);
  WaitForQueueIdle();
}

NOLINT_TEST_F(ExposureGpuTest,
  FailedPrecisionRecordingCannotAdmitAnOlderDeferredCertificate)
{
  auto service = PostProcessService(*renderer_);
  auto config = PostProcessConfig {};
  config.exposure.mode = engine::ExposureMode::kManual;
  config.exposure.manual_ev = 0.0F;
  service.SetConfig(config);
  auto& backend = static_cast<ExposureFailureGraphics&>(Backend());
  backend.fail_status_recorder = true;
  const auto signal = Uniform(1.0F, 4U, 4U);
  const auto requirements = postprocess::ExposurePass::EligibilityInputs {
    .product_layout_revision = 7U, .expected_products = 1024U
  };
  const auto begin = [&] {
    WaitForQueueIdle();
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    ctx_.frame_slot = frame::Slot { static_cast<unsigned>(sequence_ % 3U) };
    service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    return service.SelectPrecisionCandidate(ctx_, requirements);
  };
  for (unsigned i = 0U; i < 3U; ++i) {
    EXPECT_EQ(begin(), nullptr);
    ASSERT_NE(service.PrepareFrameExposure(ctx_, true), nullptr);
    const auto prepared = service.PrepareSceneExposure(
      ctx_.current_view.view_id, ctx_,
      { .scene_signal = signal.texture.get(), .scene_signal_srv = signal.srv });
    ASSERT_TRUE(prepared.has_value());
    const std::array products { postprocess::ExposurePass::HdrProduct {
      .texture = signal.texture.get(),
      .srv = signal.srv,
      .id = 11U,
      .metering = true } };
    backend.fail_next_suitability_recorder = i == 2U;
    EXPECT_EQ((service.PrepareScenePrecision(ctx_, *prepared, products)
                && service.FinalizeScenePrecision(ctx_, *prepared)),
      i != 2U);
  }
  backend.fail_status_recorder = false;
  EXPECT_EQ(begin(), nullptr);
  EXPECT_EQ(begin(), nullptr);
  WaitForQueueIdle();
}

NOLINT_TEST_F(
  ExposureGpuTest, ProducerRangeFailureSurvivesSolveAndPreventsAutoAdaptation)
{
  pass_.reset();
  renderer_->OnShutdown();
  auto config = RendererConfig {};
  config.upload_queue_key = QueueKeyFor().get();
  renderer_ = std::make_unique<Renderer>(GetGraphicsShared(), config,
    RendererCapabilityFamily::kScenePreparation
      | RendererCapabilityFamily::kDeferredShading
      | RendererCapabilityFamily::kLightingData
      | RendererCapabilityFamily::kEnvironmentLighting
      | RendererCapabilityFamily::kFinalOutputComposition);
  console::Console console;
  renderer_->RegisterConsoleBindings(observer_ptr { &console });
  ASSERT_EQ(
    console.Execute("vtx.volumetric_fog.temporal_reprojection false").status,
    console::ExecutionStatus::kOk);
  ASSERT_EQ(console.Execute("vtx.volumetric_fog.jitter false").status,
    console::ExecutionStatus::kOk);
  auto scene = std::make_shared<scene::Scene>("Producer range", 4U);
  scene->SetEnvironment(std::make_unique<scene::SceneEnvironment>());
  auto& fog = scene->GetEnvironment()->AddSystem<scene::environment::Fog>();
  fog.SetEnabled(true);
  fog.SetEnableHeightFog(true);
  fog.SetEnableVolumetricFog(true);
  fog.SetExtinctionSigmaTPerMeter(.01F);
  fog.SetHeightFalloffPerMeter(0.0F);
  fog.SetVolumetricFogDistance(1000.0F);
  auto& sky
    = scene->GetEnvironment()->AddSystem<scene::environment::SkySphere>();
  sky.SetEnabled(true);
  sky.SetSource(scene::environment::SkySphereSource::kSolidColor);
  auto& post = scene->GetEnvironment()
                 ->AddSystem<scene::environment::PostProcessVolume>();
  auto settings = scene::ExposureSettings {};
  settings.key = 12.5F;
  post.SetExposureSettings(settings);
  auto camera = scene->CreateNode("Camera");
  auto lens = std::make_unique<scene::PerspectiveCamera>();
  auto view = View {};
  view.viewport = { .width = 4.0F, .height = 4.0F };
  lens->SetViewport(view.viewport);
  ASSERT_TRUE(camera.AttachCamera(std::move(lens)));
  auto color = CreateRegisteredTexture({ .width = 4U,
    .height = 4U,
    .format = Format::kRGBA32Float,
    .is_shader_resource = true,
    .is_render_target = true,
    .initial_state = ResourceStates::kCommon });
  auto target
    = Backend().CreateFramebuffer(FramebufferDesc {}.AddColorAttachment(color));
  struct Probe final : IViewExtension {
    EnvironmentLightingService half_producer;
    bool inject_half { false };
    postprocess::ExposurePass::FrameLease frame;
    std::shared_ptr<const Texture> half_texture;
    explicit Probe(Renderer& renderer)
      : half_producer(renderer)
    {
    }
    auto OnViewSetup(const ViewSetupContext& hook) -> void override
    {
      hook.render_context.current_view.with_height_fog = true;
    }
    auto OnPreRenderViewGpu(const ViewRenderGpuContext& hook) -> void override
    {
      EXPECT_FLOAT_EQ(hook.render_context.delta_time, 1.0F);
      if (!inject_half)
        return;
      auto& ctx = hook.render_context;
      const auto old_format = ctx.current_view.hdr_color_format;
      ctx.current_view.hdr_color_format = Format::kRGBA16Float;
      half_producer.OnFrameStart(ctx.frame_sequence, ctx.frame_slot);
      static_cast<void>(half_producer.PublishEnvironmentBindings(ctx));
      const auto* resources
        = half_producer.InspectViewRadianceResources(ctx.current_view.view_id);
      CHECK_NOTNULL_F(resources);
      half_texture = resources->volumetric_fog;
      ctx.current_view.hdr_color_format = old_format;
    }
    auto OnPostRenderViewGpu(const ViewRenderGpuContext& hook) -> void override
    {
      frame = hook.render_context.current_view.frame_exposure;
    }
  };
  auto probe = std::make_shared<Probe>(*renderer_);
  renderer_->RegisterViewExtension(probe);
  auto frame = engine::FrameContext {};
  frame.SetScene(observer_ptr { scene.get() });
  auto timing = engine::ModuleTimingData {};
  timing.game_delta_time
    = time::CanonicalDuration { std::chrono::nanoseconds { 1000000000 } };
  frame.SetModuleTimingData(timing, engine::internal::EngineTagFactory::Get());
  ExposureStateData previous {};
  for (unsigned step = 1U; step <= 7U; ++step) {
    SCOPED_TRACE(step);
    const float sky_value = step == 1U ? .25F : step == 6U ? 0x1p34F : 4.0F;
    sky.SetSolidColorRgb({ sky_value, sky_value, sky_value });
    const auto emissive = step == 1U ? .125F
      : step == 3U                   ? std::numeric_limits<float>::infinity()
      : step == 4U                   ? 0x1p36F
                                     : 0x1p26F;
    fog.SetVolumetricFogEmissive({ emissive, emissive, emissive });
    probe->inject_half = step == 2U;
    scene->Update();
    const auto slot = frame::Slot { (step - 1U) % 3U };
    frame.SetFrameSlot(slot, engine::internal::EngineTagFactory::Get());
    frame.SetFrameSequenceNumber(frame::SequenceNumber { step },
      engine::internal::EngineTagFactory::Get());
    renderer_->OnFrameStart(observer_ptr { &frame });
    auto input = Renderer::OffscreenSceneViewInput::FromCamera(
      "Producer range", ViewId { 941U }, view, camera);
    input.SetViewStateHandle(CompositionView::ViewStateHandle { 941U });
    auto facade = renderer_->ForOffscreenScene();
    facade.SetFrameSession({ .frame_slot = slot,
      .frame_sequence = frame::SequenceNumber { step },
      .delta_time_seconds = 1.0F });
    facade.SetSceneSource({ .scene = observer_ptr { scene.get() } });
    facade.SetViewIntent(input);
    facade.SetOutputTarget({ .framebuffer = observer_ptr { target.get() } });
    auto session = facade.Finalize();
    ASSERT_TRUE(session.has_value());
    const auto capture = step == 2U ? BeginOptionalCapture()
                                    : observer_ptr<FrameCaptureController> {};
    ASSERT_TRUE(session->ExecuteInsideFrame(frame));
    if (capture)
      EXPECT_TRUE(capture->EndCapture());
    ASSERT_NE(probe->frame, nullptr);
    const auto state = Read<ExposureStateData>(
      *probe->frame->current_state->buffer, ResourceStates::kShaderResource);
    const auto status = Read<ExposureCompletedStatus>(
      *probe->frame->current_state->status_buffer, ResourceStates::kCopySource);
    if (step == 2U || step == 3U || step == 4U || step == 6U) {
      EXPECT_EQ(status.flags & 18U, 18U);
      EXPECT_EQ(status.first_failure_product, step == 6U ? 7U : 10U);
      EXPECT_NE(status.first_failure_kind
          & (step == 2U    ? 2U
              : step == 3U ? 1U
                           : 32U),
        0U);
      EXPECT_EQ(status.fp16_eligible_streak, 0U);
      EXPECT_EQ(state.displayed_scale, previous.displayed_scale);
      EXPECT_EQ(state.latent_scale, previous.latent_scale);
      EXPECT_EQ(state.flags & 12U, 0U);
      EXPECT_NE(state.flags & 32U, 0U);
    } else {
      EXPECT_EQ(status.flags & 16U, 0U);
      EXPECT_NE(state.flags & 4U, 0U);
      if (step == 5U)
        EXPECT_LT(state.displayed_scale, previous.displayed_scale);
    }
    if (step == 2U) {
      ASSERT_NE(probe->half_texture, nullptr);
      EXPECT_EQ(
        probe->half_texture->GetDescriptor().format, Format::kRGBA16Float);
      auto readback
        = GetReadbackManager()->CreateTextureReadback("Overflowed half fog");
      {
        auto recorder = AcquireRecorder("Overflowed half fog readback");
        ASSERT_TRUE(recorder->AdoptKnownResourceState(*probe->half_texture));
        ASSERT_TRUE(readback
            ->EnqueueCopy(*recorder, *probe->half_texture,
              { .src_slice
                = { .z = 31U, .width = 1U, .height = 1U, .depth = 1U } })
            .has_value());
      }
      const auto mapped = readback->MapNow();
      ASSERT_TRUE(mapped.has_value());
      std::uint16_t red;
      std::memcpy(&red, mapped->Data(), sizeof(red));
      EXPECT_EQ(red, 0x7bffU); // The stored half has clipped to 65504.
    }
    previous = state;
    renderer_->OnFrameEnd(observer_ptr { &frame });
    WaitForQueueIdle();
  }
}

NOLINT_TEST_F(ExposureGpuTest,
  FailedSolveFallbackRestartsPrecisionWithoutRejectingSuccessfulReuse)
{
  auto service = PostProcessService(*renderer_);
  auto config = PostProcessConfig {};
  config.exposure.key = 12.5F;
  service.SetConfig(config);
  auto& backend = static_cast<ExposureFailureGraphics&>(Backend());
  const auto signal = Uniform(1.0F, 4U, 4U);
  const auto requirements = postprocess::ExposurePass::EligibilityInputs {
    .product_layout_revision = 7U, .expected_products = 1024U
  };
  const auto inputs
    = PostProcessService::Inputs { .scene_signal = signal.texture.get(),
        .scene_signal_srv = signal.srv };
  const std::array products { postprocess::ExposurePass::HdrProduct {
    .texture = signal.texture.get(),
    .srv = signal.srv,
    .id = 11U,
    .metering = true } };
  const auto begin = [&] {
    WaitForQueueIdle();
    ctx_.frame_sequence = frame::SequenceNumber { ++sequence_ };
    ctx_.frame_slot = frame::Slot { static_cast<unsigned>(sequence_ % 3U) };
    service.OnFrameStart(ctx_.frame_sequence, ctx_.frame_slot);
    return service.SelectPrecisionCandidate(ctx_, requirements);
  };
  const auto finish = [&](std::uint32_t streak, bool fail, bool reuse) {
    SCOPED_TRACE(sequence_);
    CHECK_NOTNULL_F(service.PrepareFrameExposure(ctx_, true).get());
    backend.fail_next_exposure_recorder = fail;
    const auto prepared
      = service.PrepareSceneExposure(ctx_.current_view.view_id, ctx_, inputs);
    CHECK_F(prepared.has_value());
    EXPECT_EQ(prepared->exposure.executed, !fail);
    CHECK_NOTNULL_F(prepared->exposure.state.get());
    if (reuse) {
      const auto reused
        = service.PrepareSceneExposure(ctx_.current_view.view_id, ctx_, inputs);
      CHECK_F(reused.has_value());
      EXPECT_FALSE(reused->exposure.executed);
      EXPECT_EQ(reused->exposure.state, prepared->exposure.state);
      EXPECT_TRUE((service.PrepareScenePrecision(ctx_, *reused, products)
        && service.FinalizeScenePrecision(ctx_, *reused)));
    }
    EXPECT_EQ((service.PrepareScenePrecision(ctx_, *prepared, products)
                && service.FinalizeScenePrecision(ctx_, *prepared)),
      !fail);
    const auto state = ReadState(prepared->exposure);
    if (!fail)
      EXPECT_EQ(state.fp16_eligible_streak, streak);
    return state;
  };
  EXPECT_EQ(begin(), nullptr);
  const auto initial = finish(1U, false, false);
  EXPECT_EQ(begin(), nullptr);
  finish(2U, false, false);
  ASSERT_NE(begin(), nullptr);
  backend.fail_status_recorder = true;
  finish(2U, false, false);
  ASSERT_NE(begin(), nullptr);
  const auto fallback = finish(0U, true, false);
  EXPECT_EQ(fallback.displayed_scale, initial.displayed_scale);
  EXPECT_EQ(service.SelectPrecisionCandidate(ctx_, requirements), nullptr);
  backend.fail_status_recorder = false;
  EXPECT_EQ(begin(), nullptr);
  finish(1U, false, true);
  EXPECT_EQ(begin(), nullptr);
  finish(2U, false, true);
  ASSERT_NE(begin(), nullptr);
  finish(2U, false, true);

  const auto seed
    = renderer_->QueueExposureTransition(ctx_.current_view.view_state_handle,
      ExposureTransitionPolicy::kSeedFromEv100, 4.0F);
  ASSERT_TRUE(seed.has_value());
  EXPECT_EQ(begin(), nullptr);
  const auto pending_fallback = finish(0U, true, false);
  EXPECT_EQ(pending_fallback.displayed_scale, initial.displayed_scale);
  const auto pending = renderer_->InspectExposureTransition(seed->target);
  ASSERT_TRUE(pending.has_value());
  EXPECT_EQ(pending->phase, ExposureTransitionPhase::kQueued);
  EXPECT_LT(pending->applied_generation, seed->generation);
  EXPECT_EQ(begin(), nullptr);
  const auto seeded = finish(1U, false, true);
  EXPECT_EQ(seeded.displayed_scale, 0x1p-4F);
  EXPECT_EQ(begin(), nullptr);
  const auto applied = renderer_->InspectExposureTransition(seed->target);
  ASSERT_TRUE(applied.has_value());
  EXPECT_EQ(applied->phase, ExposureTransitionPhase::kApplied);
  EXPECT_EQ(applied->applied_generation, seed->generation);
  WaitForQueueIdle();
}

NOLINT_TEST_F(ExposureGpuTest,
  FogErrorBoundsContainRepeatedHalfHistoryAndSurviveFloatRecovery)
{
  pass_.reset();
  renderer_->OnShutdown();
  auto config = RendererConfig {};
  config.upload_queue_key = QueueKeyFor().get();
  renderer_ = std::make_unique<Renderer>(GetGraphicsShared(), config,
    RendererCapabilityFamily::kScenePreparation
      | RendererCapabilityFamily::kDeferredShading
      | RendererCapabilityFamily::kLightingData
      | RendererCapabilityFamily::kEnvironmentLighting
      | RendererCapabilityFamily::kFinalOutputComposition);
  console::Console console;
  renderer_->RegisterConsoleBindings(observer_ptr { &console });
  ASSERT_EQ(
    console.Execute("vtx.volumetric_fog.temporal_reprojection true").status,
    console::ExecutionStatus::kOk);
  ASSERT_EQ(console.Execute("vtx.volumetric_fog.jitter false").status,
    console::ExecutionStatus::kOk);
  auto scene = std::make_shared<scene::Scene>("Fog error bounds", 4U);
  scene->SetEnvironment(std::make_unique<scene::SceneEnvironment>());
  auto& fog = scene->GetEnvironment()->AddSystem<scene::environment::Fog>();
  fog.SetEnabled(true);
  fog.SetEnableHeightFog(true);
  fog.SetEnableVolumetricFog(true);
  fog.SetExtinctionSigmaTPerMeter(.01F);
  fog.SetHeightFalloffPerMeter(0.0F);
  fog.SetVolumetricFogDistance(1000.0F);
  auto& sky
    = scene->GetEnvironment()->AddSystem<scene::environment::SkySphere>();
  sky.SetEnabled(true);
  sky.SetSource(scene::environment::SkySphereSource::kSolidColor);
  auto& post = scene->GetEnvironment()
                 ->AddSystem<scene::environment::PostProcessVolume>();
  auto settings = scene::ExposureSettings {};
  settings.key = 12.5F;
  settings.mode = engine::ExposureMode::kManual;
  settings.manual_ev = 0.0F;
  post.SetExposureSettings(settings);
  auto camera = scene->CreateNode("Camera");
  auto lens = std::make_unique<scene::PerspectiveCamera>();
  auto view = View {};
  view.viewport = { .width = 4.0F, .height = 4.0F };
  lens->SetViewport(view.viewport);
  ASSERT_TRUE(camera.AttachCamera(std::move(lens)));
  auto color = CreateRegisteredTexture({ .width = 4U,
    .height = 4U,
    .format = Format::kRGBA32Float,
    .is_shader_resource = true,
    .is_render_target = true,
    .initial_state = ResourceStates::kCommon });
  auto target
    = Backend().CreateFramebuffer(FramebufferDesc {}.AddColorAttachment(color));
  struct Probe final : IViewExtension {
    Renderer& renderer;
    EnvironmentLightingService producer;
    bool use_half { true };
    postprocess::ExposurePass::FrameLease frame;
    std::shared_ptr<const Texture> observed;
    std::shared_ptr<const Texture> reference;
    explicit Probe(Renderer& value)
      : renderer(value)
      , producer(value)
    {
    }
    auto OnViewSetup(const ViewSetupContext& hook) -> void override
    {
      hook.render_context.current_view.with_height_fog = true;
    }
    auto OnPreRenderViewGpu(const ViewRenderGpuContext& hook) -> void override
    {
      auto& ctx = hook.render_context;
      const auto old_format = ctx.current_view.hdr_color_format;
      ctx.current_view.hdr_color_format
        = use_half ? Format::kRGBA16Float : Format::kRGBA32Float;
      producer.OnFrameStart(ctx.frame_sequence, ctx.frame_slot);
      static_cast<void>(producer.PublishEnvironmentBindings(ctx));
      if (ctx.frame_sequence.get() == 1U) {
        static_cast<void>(producer.PublishEnvironmentBindings(ctx));
        EXPECT_FALSE(producer.GetLastViewProductGenerationState()
            .volumetric_fog_temporal_history_reprojection_executed);
      }
      const auto* resources
        = producer.InspectViewRadianceResources(ctx.current_view.view_id);
      CHECK_NOTNULL_F(resources);
      observed = resources->volumetric_fog;
      ctx.current_view.hdr_color_format = old_format;
    }
    auto OnPostRenderViewGpu(const ViewRenderGpuContext& hook) -> void override
    {
      auto* owner
        = vortex::testing::RendererPublicationProbe::GetSceneRenderer(renderer);
      reference = vortex::testing::RendererPublicationProbe::FogHistory(
        *owner, hook.render_context.current_view.view_id)
                    .first;
      frame = hook.render_context.current_view.frame_exposure;
    }
  };
  auto probe = std::make_shared<Probe>(*renderer_);
  renderer_->RegisterViewExtension(probe);
  const auto half_to_double = [](std::uint16_t bits) {
    const auto exponent = (bits >> 10U) & 31U;
    const auto mantissa = bits & 1023U;
    const double magnitude = exponent == 0U
      ? std::ldexp(double(mantissa), -24)
      : std::ldexp(double(1024U + mantissa), int(exponent) - 25);
    return bits & 0x8000U ? -magnitude : magnitude;
  };
  const auto read_volume = [&](const Texture& texture) {
    auto readback
      = GetReadbackManager()->CreateTextureReadback("Fog bound volume");
    {
      auto recorder = AcquireRecorder("Fog bound volume readback");
      CHECK_F(recorder->AdoptKnownResourceState(texture));
      CHECK_F(readback->EnqueueCopy(*recorder, texture, {}).has_value());
    }
    const auto mapped = readback->MapNow();
    CHECK_F(mapped.has_value());
    const auto& desc = texture.GetDescriptor();
    std::vector<std::array<double, 4>> values(
      desc.width * desc.height * desc.depth);
    const bool half = desc.format == Format::kRGBA16Float;
    for (unsigned z = 0U; z < desc.depth; ++z)
      for (unsigned y = 0U; y < desc.height; ++y)
        for (unsigned x = 0U; x < desc.width; ++x) {
          const auto* bytes = mapped->Data()
            + z * mapped->Layout().slice_pitch.get()
            + y * mapped->Layout().row_pitch.get() + x * (half ? 8U : 16U);
          auto& value = values[(z * desc.height + y) * desc.width + x];
          for (unsigned c = 0U; c < 4U; ++c) {
            if (half) {
              std::uint16_t bits;
              std::memcpy(&bits, bytes + c * 2U, 2U);
              value[c] = half_to_double(bits);
            } else {
              float component;
              std::memcpy(&component, bytes + c * 4U, 4U);
              value[c] = component;
            }
          }
        }
    return values;
  };
  auto frame = engine::FrameContext {};
  frame.SetScene(observer_ptr { scene.get() });
  double opacity = 1.0;
  double previous_observed = 1.0;
  double maximum_error = 0.0;
  double last_half_error = 0.0;
  HdrErrorBoundsData last_half_bounds;
  unsigned capture_step = 64U;
  char* capture_step_text = nullptr;
  std::size_t capture_step_size = 0U;
  if (_dupenv_s(&capture_step_text, &capture_step_size,
        "OXYGEN_EXPOSURE_FOG_CAPTURE_STEP")
      == 0
    && capture_step_text) {
    capture_step
      = static_cast<unsigned>(std::strtoul(capture_step_text, nullptr, 10));
    std::free(capture_step_text);
    ASSERT_GE(capture_step, 1U);
    ASSERT_LE(capture_step, 74U);
  }
  for (unsigned step = 1U; step <= 74U; ++step) {
    SCOPED_TRACE(step);
    const double desired = 1.0 + 1.0 / 2048.0 + 1.0 / 4194304.0;
    const double weight = double(.9F);
    const double fresh = step == 1U ? 1.0
      : step == 73U                 ? std::numeric_limits<double>::infinity()
      : step == 74U                 ? .25
                    : (desired - weight * previous_observed) / (1.0 - weight);
    const float emissive = float(std::max(fresh, 0.0) / opacity);
    fog.SetVolumetricFogEmissive({ emissive, emissive, emissive });
    probe->use_half = step <= 64U;
    scene->Update();
    const auto slot = frame::Slot { (step - 1U) % 3U };
    frame.SetFrameSlot(slot, engine::internal::EngineTagFactory::Get());
    frame.SetFrameSequenceNumber(frame::SequenceNumber { step },
      engine::internal::EngineTagFactory::Get());
    renderer_->OnFrameStart(observer_ptr { &frame });
    auto input = Renderer::OffscreenSceneViewInput::FromCamera(
      "Fog bounds", ViewId { 942U }, view, camera);
    input.SetViewStateHandle(CompositionView::ViewStateHandle { 942U });
    auto facade = renderer_->ForOffscreenScene();
    facade.SetFrameSession({ .frame_slot = slot,
      .frame_sequence = frame::SequenceNumber { step },
      .delta_time_seconds = 0.0F });
    facade.SetSceneSource({ .scene = observer_ptr { scene.get() } });
    facade.SetViewIntent(input);
    facade.SetOutputTarget({ .framebuffer = observer_ptr { target.get() } });
    auto session = facade.Finalize();
    ASSERT_TRUE(session.has_value());
    const auto capture = step == capture_step
      ? BeginOptionalCapture()
      : observer_ptr<FrameCaptureController> {};
    ASSERT_TRUE(session->ExecuteInsideFrame(frame));
    if (capture)
      EXPECT_TRUE(capture->EndCapture());
    ASSERT_NE(probe->observed, nullptr);
    ASSERT_NE(probe->reference, nullptr);
    const auto observed = read_volume(*probe->observed);
    const auto reference = read_volume(*probe->reference);
    ASSERT_EQ(observed.size(), reference.size());
    const auto storage = Read<ExposureStatusStorage>(
      *probe->frame->current_state->status_buffer, ResourceStates::kCopySource);
    const auto& bounds = storage.producer_errors[2];
    if (step == 73U) {
      EXPECT_TRUE(std::isinf(bounds.rgb_absolute));
      EXPECT_NE(storage.completed.flags & 16U, 0U);
      renderer_->OnFrameEnd(observer_ptr { &frame });
      WaitForQueueIdle();
      continue;
    }
    EXPECT_TRUE(std::isfinite(bounds.rgb_relative));
    EXPECT_TRUE(std::isfinite(bounds.rgb_absolute));
    for (std::size_t i = 0U; i < observed.size(); ++i)
      for (unsigned c = 0U; c < 4U; ++c) {
        const double error = std::abs(observed[i][c] - reference[i][c]);
        const double allowance = c == 3U
          ? double(bounds.transmittance_relative) * reference[i][c]
            + bounds.transmittance_absolute
          : double(bounds.rgb_relative) * reference[i][c] + bounds.rgb_absolute;
        EXPECT_LE(error, allowance) << "voxel=" << i << " channel=" << c;
      }
    const double error = std::abs(observed.back()[0] - reference.back()[0]);
    maximum_error = std::max(maximum_error, error);
    if (step == 1U)
      opacity = reference.back()[0];
    previous_observed = observed.back()[0];
    if (step == 64U) {
      last_half_error = error;
      last_half_bounds = bounds;
    }
    if (step == 65U) {
      EXPECT_GT(
        error, 0.0); // FP32 storage does not erase reused history error.
      EXPECT_GT(bounds.rgb_absolute + bounds.rgb_relative, 0.0F);
      const auto qualification = Read<HdrSuitabilityData>(
        *probe->frame->suitability_buffer, ResourceStates::kShaderResource);
      EXPECT_NE(qualification.failure_flags & 4U, 0U);
      EXPECT_EQ(qualification.first_failure_product, 10U);
      EXPECT_EQ(storage.completed.fp16_eligible_streak, 0U);
    }
    if (step == 72U) {
      EXPECT_LT(error, last_half_error);
      EXPECT_LT(bounds.rgb_absolute, last_half_bounds.rgb_absolute);
    }
    if (step == 74U) {
      EXPECT_EQ(error, 0.0);
      EXPECT_EQ(bounds.rgb_relative, 0.0F);
      EXPECT_EQ(bounds.rgb_absolute, 0.0F);
      EXPECT_EQ(bounds.transmittance_relative, 0.0F);
      EXPECT_EQ(bounds.transmittance_absolute, 0.0F);
      EXPECT_EQ(storage.completed.flags & 16U, 0U);
      const auto qualification = Read<HdrSuitabilityData>(
        *probe->frame->suitability_buffer, ResourceStates::kShaderResource);
      EXPECT_EQ(qualification.failure_flags, 0U);
      EXPECT_EQ(storage.completed.fp16_eligible_streak, 1U);
    }
    renderer_->OnFrameEnd(observer_ptr { &frame });
    WaitForQueueIdle();
  }
  EXPECT_GT(maximum_error, .001);
  RecordProperty("maximum_observed_error", std::to_string(maximum_error));
  RecordProperty("last_half_error", std::to_string(last_half_error));
  RecordProperty(
    "last_half_relative_bound", std::to_string(last_half_bounds.rgb_relative));
  RecordProperty(
    "last_half_absolute_bound", std::to_string(last_half_bounds.rgb_absolute));
}

} // namespace
