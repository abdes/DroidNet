//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include <Oxygen/Config/GraphicsConfig.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Graphics/Common/BackendModule.h>
#include <Oxygen/Graphics/Common/DescriptorHandle.h>
#include <Oxygen/Graphics/Common/FrameCaptureController.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Graphics/Direct3D12/Bindless/D3D12HeapAllocationStrategy.h>
#include <Oxygen/Graphics/Direct3D12/Bindless/DescriptorAllocator.h>
#include <Oxygen/Graphics/Direct3D12/Buffer.h>
#include <Oxygen/Graphics/Direct3D12/CommandList.h>
#include <Oxygen/Graphics/Direct3D12/CommandQueue.h>
#include <Oxygen/Graphics/Direct3D12/Detail/CompositionSurface.h>
#include <Oxygen/Graphics/Direct3D12/Detail/PipelineStateCache.h>
#include <Oxygen/Graphics/Direct3D12/Detail/WindowSurface.h>
#include <Oxygen/Graphics/Direct3D12/Devices/DeviceManager.h>
#include <Oxygen/Graphics/Direct3D12/Graphics.h>
#include <Oxygen/Graphics/Direct3D12/PixFrameCaptureController.h>
#include <Oxygen/Graphics/Direct3D12/ReadbackManager.h>
#include <Oxygen/Graphics/Direct3D12/RenderDocFrameCaptureController.h>
#include <Oxygen/Graphics/Direct3D12/Shaders/EngineShaders.h>
#include <Oxygen/Graphics/Direct3D12/Texture.h>
#include <Oxygen/Graphics/Direct3D12/TimestampQueryBackend.h>

//===----------------------------------------------------------------------===//
// Internal implementation of the graphics backend module API.
//===----------------------------------------------------------------------===//

namespace {
namespace bindless_d3d12 = oxygen::bindless::generated::d3d12;

auto ParseFrameCaptureProvider(const std::string& value)
  -> oxygen::FrameCaptureProvider
{
  if (value == "none" || value == "off") {
    return oxygen::FrameCaptureProvider::kNone;
  }
  if (value == "renderdoc") {
    return oxygen::FrameCaptureProvider::kRenderDoc;
  }
  if (value == "pix") {
    return oxygen::FrameCaptureProvider::kPix;
  }

  throw std::runtime_error("unsupported frame_capture.provider: " + value);
}

auto ParseFrameCaptureInitMode(const std::string& value)
  -> oxygen::FrameCaptureInitMode
{
  if (value == "disabled") {
    return oxygen::FrameCaptureInitMode::kDisabled;
  }
  if (value == "attached") {
    return oxygen::FrameCaptureInitMode::kAttachedOnly;
  }
  if (value == "search") {
    return oxygen::FrameCaptureInitMode::kSearchPath;
  }
  if (value == "path") {
    return oxygen::FrameCaptureInitMode::kExplicitPath;
  }

  throw std::runtime_error("unsupported frame_capture.init_mode: " + value);
}

auto ParseFrameCaptureConfig(const nlohmann::json& json_config)
  -> oxygen::FrameCaptureConfig
{
  oxygen::FrameCaptureConfig config {};
  if (!json_config.contains("frame_capture")) {
    return config;
  }

  const auto& frame_capture = json_config["frame_capture"];
  if (frame_capture.contains("provider")) {
    config.provider
      = ParseFrameCaptureProvider(frame_capture["provider"].get<std::string>());
  }
  if (frame_capture.contains("init_mode")) {
    config.init_mode = ParseFrameCaptureInitMode(
      frame_capture["init_mode"].get<std::string>());
  }
  if (frame_capture.contains("from_frame")) {
    config.from_frame = frame_capture["from_frame"].get<uint64_t>();
  }
  if (frame_capture.contains("frame_count")) {
    config.frame_count = frame_capture["frame_count"].get<uint32_t>();
  }
  if (frame_capture.contains("module_path")) {
    config.module_path = frame_capture["module_path"].get<std::string>();
  }
  if (frame_capture.contains("capture_file_template")) {
    config.capture_file_template
      = frame_capture["capture_file_template"].get<std::string>();
  }
  return config;
}

auto GetBackendInternal() -> std::shared_ptr<oxygen::graphics::d3d12::Graphics>&
{
  static std::shared_ptr<oxygen::graphics::d3d12::Graphics> graphics;
  return graphics;
}

auto CreateBackend(const oxygen::SerializedBackendConfig& config,
  const oxygen::SerializedPathFinderConfig& path_finder_config) -> void*
{
  auto& backend = GetBackendInternal();
  if (!backend) {
    try {
      backend = std::make_shared<oxygen::graphics::d3d12::Graphics>(
        config, path_finder_config);
    } catch (const std::exception& ex) {
      LOG_F(ERROR, "Failed to create D3D12 backend: {}", ex.what());
      backend.reset();
      return nullptr;
    } catch (...) {
      LOG_F(ERROR, "Failed to create D3D12 backend: unknown error");
      backend.reset();
      return nullptr;
    }
  }
  return backend.get();
}

auto DestroyBackend() -> void
{
  LOG_SCOPE_F(INFO, "DestroyBackend");
  auto& backend = GetBackendInternal();
  if (backend) {
    // Ensure async tasks are stopped and resources are released in a safe
    // order before resetting the backend instance.
    backend->Stop();
    backend->Flush();
  }
  backend.reset();
}

} // namespace

//===----------------------------------------------------------------------===//
// Public implementation of the graphics backend API.
//===----------------------------------------------------------------------===//

extern "C" __declspec(dllexport) auto GetGraphicsModuleApi() -> void*
{
  static oxygen::graphics::GraphicsModuleApi render_module;
  render_module.CreateBackend = CreateBackend;
  render_module.DestroyBackend = DestroyBackend;
  return &render_module;
}

//===----------------------------------------------------------------------===//
// DescriptorAllocator Component
//===----------------------------------------------------------------------===//

namespace {

class DescriptorAllocatorComponent : public oxygen::Component {
  OXYGEN_COMPONENT(DescriptorAllocatorComponent)
  OXYGEN_COMPONENT_REQUIRES(oxygen::graphics::d3d12::DeviceManager)

public:
  explicit DescriptorAllocatorComponent() = default;

  OXYGEN_MAKE_NON_COPYABLE(DescriptorAllocatorComponent)
  OXYGEN_DEFAULT_MOVABLE(DescriptorAllocatorComponent)

  ~DescriptorAllocatorComponent() override = default;

  [[nodiscard]] auto GetAllocator() const -> const auto& { return *allocator_; }

protected:
  auto UpdateDependencies(
    const std::function<Component&(oxygen::TypeId)>& get_component) noexcept
    -> void override
  {
    using oxygen::graphics::DescriptorVisibility;
    using oxygen::graphics::ResourceViewType;
    using oxygen::graphics::d3d12::D3D12HeapAllocationStrategy;
    using oxygen::graphics::d3d12::DescriptorAllocator;
    using oxygen::graphics::d3d12::DeviceManager;

    const auto& dm = static_cast<DeviceManager&>(
      get_component(DeviceManager::ClassTypeId()));
    auto* device = dm.Device();
    DCHECK_NOTNULL_F(device, "DeviceManager not properly initialized");
    allocator_ = std::make_unique<DescriptorAllocator>(
      std::make_shared<D3D12HeapAllocationStrategy>(device), device);

    // Ensure shader-visible heaps (CBV_SRV_UAV and SAMPLER) exist up-front.
    // Some pipelines expect directly-indexed sampler/srv heaps at pipeline
    // signature time. Create initial segments so command recording can bind
    // descriptor heaps even if no descriptors have been allocated yet.
    try {
      // Reserve will create initial segments when none exist according to
      // BaseDescriptorAllocator::Reserve.
      auto _ = allocator_->Reserve(ResourceViewType::kStructuredBuffer_SRV,
        DescriptorVisibility::kShaderVisible, oxygen::bindless::Count { 1 });
      _ = allocator_->Reserve(ResourceViewType::kSampler,
        DescriptorVisibility::kShaderVisible, oxygen::bindless::Count { 1 });

      // Ensure stable default samplers exist for bindless sampling.
      // Current contracts:
      // - SamplerDescriptorHeap[0] = default linear wrap sampler
      // - SamplerDescriptorHeap[1] = shadow comparison sampler
      if (!default_sampler_.IsValid()) {
        default_sampler_ = allocator_->Allocate(
          ResourceViewType::kSampler, DescriptorVisibility::kShaderVisible);
        if (default_sampler_.IsValid()) {
          D3D12_SAMPLER_DESC sampler_desc {};
          sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
          sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
          sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
          sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
          sampler_desc.MipLODBias = 0.0f;
          sampler_desc.MaxAnisotropy = 1;
          // ComparisonFunc is only used with D3D12_FILTER_COMPARISON_*.
          // Keep it as NEVER for non-comparison filters to avoid the DX12
          // debug-layer warning CREATE_SAMPLER_COMPARISON_FUNC_IGNORED.
          sampler_desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
          sampler_desc.BorderColor[0] = 0.0f;
          sampler_desc.BorderColor[1] = 0.0f;
          sampler_desc.BorderColor[2] = 0.0f;
          sampler_desc.BorderColor[3] = 0.0f;
          sampler_desc.MinLOD = 0.0f;
          sampler_desc.MaxLOD = D3D12_FLOAT32_MAX;

          device->CreateSampler(
            &sampler_desc, allocator_->GetCpuHandle(default_sampler_));
          DLOG_F(2, "Default sampler created at bindless index {}",
            default_sampler_.GetBindlessHandle().get());
        }
      }

      if (!shadow_comparison_sampler_.IsValid()) {
        shadow_comparison_sampler_ = allocator_->Allocate(
          ResourceViewType::kSampler, DescriptorVisibility::kShaderVisible);
        if (shadow_comparison_sampler_.IsValid()) {
          D3D12_SAMPLER_DESC sampler_desc {};
          sampler_desc.Filter
            = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
          sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
          sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
          sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
          sampler_desc.MipLODBias = 0.0f;
          sampler_desc.MaxAnisotropy = 1;
          sampler_desc.ComparisonFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
          sampler_desc.BorderColor[0] = 0.0f;
          sampler_desc.BorderColor[1] = 0.0f;
          sampler_desc.BorderColor[2] = 0.0f;
          sampler_desc.BorderColor[3] = 0.0f;
          sampler_desc.MinLOD = 0.0f;
          sampler_desc.MaxLOD = D3D12_FLOAT32_MAX;

          device->CreateSampler(&sampler_desc,
            allocator_->GetCpuHandle(shadow_comparison_sampler_));
          DLOG_F(2, "Shadow comparison sampler created at bindless index {}",
            shadow_comparison_sampler_.GetBindlessHandle().get());
        }
      }
    } catch (const std::exception& ex) {
      LOG_F(WARNING, "Failed to eagerly create shader-visible heaps: {}",
        ex.what());
    }
  }

private:
  std::unique_ptr<oxygen::graphics::d3d12::DescriptorAllocator> allocator_ {};
  oxygen::graphics::DescriptorHandle default_sampler_ {};
  oxygen::graphics::DescriptorHandle shadow_comparison_sampler_ {};
};

} // namespace

//===----------------------------------------------------------------------===//
// The Graphics class methods
//===----------------------------------------------------------------------===//

using oxygen::graphics::ComputePipelineDesc;
using oxygen::graphics::GraphicsPipelineDesc;
using oxygen::graphics::d3d12::Graphics;

auto Graphics::GetFactory() const -> dx::IFactory*
{
  auto* factory = GetComponent<DeviceManager>().Factory();
  CHECK_NOTNULL_F(factory, "graphics backend not properly initialized");
  return factory;
}

auto Graphics::GetDescriptorAllocator() const
  -> const graphics::DescriptorAllocator&
{
  return GetComponent<DescriptorAllocatorComponent>().GetAllocator();
}

auto Graphics::GetTimestampQueryProvider() const
  -> observer_ptr<graphics::TimestampQueryProvider>
{
  return observer_ptr<graphics::TimestampQueryProvider>(
    timestamp_query_backend_.get());
}

auto Graphics::GetReadbackManager() const
  -> observer_ptr<graphics::ReadbackManager>
{
  return observer_ptr<graphics::ReadbackManager>(readback_manager_.get());
}

auto Graphics::GetFrameCaptureController() const
  -> observer_ptr<graphics::FrameCaptureController>
{
  return observer_ptr<graphics::FrameCaptureController>(
    frame_capture_controller_.get());
}

auto Graphics::GetCurrentDevice() const -> dx::IDevice*
{
  auto* device = GetComponent<DeviceManager>().Device();
  CHECK_NOTNULL_F(device, "graphics backend not properly initialized");
  return device;
}

auto Graphics::GetAllocator() const -> D3D12MA::Allocator*
{
  auto* allocator = GetComponent<DeviceManager>().Allocator();
  CHECK_NOTNULL_F(allocator, "graphics backend not properly initialized");
  return allocator;
}

Graphics::~Graphics() = default;

Graphics::Graphics(const SerializedBackendConfig& config,
  const SerializedPathFinderConfig& path_finder_config)
  : Base("D3D12 Backend")
{
  LOG_SCOPE_FUNCTION(INFO);

  // Parse JSON configuration
  nlohmann::json jsonConfig
    = nlohmann::json::parse(config.json_data, config.json_data + config.size);

  oxygen::PathFinderConfig parsed_path_finder_config {};
  if (path_finder_config.json_data != nullptr && path_finder_config.size > 0U) {
    const auto path_finder_json
      = nlohmann::json::parse(path_finder_config.json_data,
        path_finder_config.json_data + path_finder_config.size);
    std::filesystem::path workspace_root;
    std::filesystem::path shader_library;
    std::filesystem::path cvars_archive;

    if (path_finder_json.contains("workspace_root_path")) {
      workspace_root
        = path_finder_json["workspace_root_path"].get<std::string>();
    }
    if (path_finder_json.contains("shader_library_path")) {
      shader_library
        = path_finder_json["shader_library_path"].get<std::string>();
    }

    if (!workspace_root.empty() || !shader_library.empty()
      || !cvars_archive.empty()) {
      auto builder = oxygen::PathFinderConfig::Create();
      if (!workspace_root.empty()) {
        builder
          = std::move(builder).WithWorkspaceRoot(std::move(workspace_root));
      }
      if (!shader_library.empty()) {
        builder
          = std::move(builder).WithShaderLibraryPath(std::move(shader_library));
      }
      parsed_path_finder_config = std::move(builder).Build();
    }
  }

  DeviceManagerDesc desc {};
  const auto frame_capture_config = ParseFrameCaptureConfig(jsonConfig);
  if (jsonConfig.contains("enable_debug_layer")) {
    desc.enable_debug_layer = jsonConfig["enable_debug_layer"].get<bool>();
  } else if (jsonConfig.contains("enable_debug")) {
    desc.enable_debug_layer = jsonConfig["enable_debug"].get<bool>();
    LOG_F(WARNING,
      "D3D12 Graphics: legacy serialized key 'enable_debug' detected; "
      "treating it as 'enable_debug_layer'");
  }
  if (jsonConfig.contains("enable_validation")) {
    desc.enable_validation = jsonConfig["enable_validation"].get<bool>();
  }
  if (jsonConfig.contains("enable_aftermath")) {
    desc.enable_aftermath = jsonConfig["enable_aftermath"].get<bool>();
  }
  if (!oxygen::AreGraphicsToolingOptionsMutuallyExclusive(
        desc.enable_debug_layer, desc.enable_aftermath)) {
    LOG_F(ERROR,
      "D3D12 Graphics rejected serialized tooling config: debug_layer={} "
      "aftermath={} (mutually exclusive)",
      desc.enable_debug_layer, desc.enable_aftermath);
    throw std::invalid_argument(
      "Serialized backend config cannot enable both the D3D12 debug layer "
      "and Nsight Aftermath");
  }
  LOG_F(INFO,
    "D3D12 Graphics resolved tooling config: debug_layer={} validation={} "
    "aftermath={} capture_provider={}",
    desc.enable_debug_layer, desc.enable_validation, desc.enable_aftermath,
    static_cast<int>(frame_capture_config.provider));
  desc.frame_capture = frame_capture_config;
  if (jsonConfig.contains("enable_vsync")) {
    enable_vsync_ = jsonConfig["enable_vsync"].get<bool>();
  }
  AddComponent<DeviceManager>(desc);
  if (frame_capture_config.provider
    == oxygen::FrameCaptureProvider::kRenderDoc) {
    frame_capture_controller_
      = CreateRenderDocFrameCaptureController(*this, frame_capture_config);
  } else if (frame_capture_config.provider
    == oxygen::FrameCaptureProvider::kPix) {
    frame_capture_controller_
      = CreatePixFrameCaptureController(*this, frame_capture_config);
  }
  AddComponent<EngineShaders>(std::move(parsed_path_finder_config));
  AddComponent<DescriptorAllocatorComponent>();
  AddComponent<detail::PipelineStateCache>(this);
  timestamp_query_backend_ = std::make_unique<TimestampQueryBackend>(*this);
  readback_manager_ = std::make_unique<D3D12ReadbackManager>(*this);
}

auto Graphics::SetVSyncEnabled(const bool enabled) -> void
{
  if (enable_vsync_ == enabled) {
    return;
  }
  enable_vsync_ = enabled;
  LOG_F(INFO, "D3D12 Graphics: enable_vsync={}", enable_vsync_);
}

auto Graphics::CreateCommandQueue(const QueueKey& queue_key, QueueRole role)
  -> std::shared_ptr<graphics::CommandQueue>
{
  return std::make_shared<CommandQueue>(queue_key.get(), role, this);
}

auto Graphics::CreateCommandListImpl(QueueRole role,
  std::string_view command_list_name) -> std::unique_ptr<graphics::CommandList>
{
  return std::make_unique<CommandList>(command_list_name, role, this);
}

auto Graphics::CreateCommandRecorder(
  std::shared_ptr<graphics::CommandList> command_list,
  observer_ptr<graphics::CommandQueue> target_queue)
  -> std::unique_ptr<graphics::CommandRecorder>
{
  auto this_shared = shared_from_this();
  auto d3d12_graphics = std::static_pointer_cast<Graphics>(this_shared);
  return std::make_unique<CommandRecorder>(
    d3d12_graphics, std::move(command_list), target_queue);
}

auto Graphics::GetFormatPlaneCount(DXGI_FORMAT format) const -> uint8_t
{
  uint8_t& plane_count = dxgi_format_plane_count_cache_[format];
  if (plane_count == 0) {
    D3D12_FEATURE_DATA_FORMAT_INFO format_info = { format, 1 };
    if (FAILED(GetCurrentDevice()->CheckFeatureSupport(
          D3D12_FEATURE_FORMAT_INFO, &format_info, sizeof(format_info)))) {
      // Format is not supported - store a special value in the cache to avoid
      // querying later
      plane_count = 255;
    } else {
      // Format supported - store the plane count in the cache
      plane_count = format_info.PlaneCount;
    }
  }

  if (plane_count == 255) {
    return 0;
  }

  return plane_count;
}

auto Graphics::GetDrawCommandSignature() const -> ID3D12CommandSignature*
{
  if (!draw_command_signature_) {
    D3D12_INDIRECT_ARGUMENT_DESC args[1];
    args[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;

    D3D12_COMMAND_SIGNATURE_DESC desc;
    desc.ByteStride = sizeof(D3D12_DRAW_ARGUMENTS);
    desc.NumArgumentDescs = 1;
    desc.pArgumentDescs = args;
    desc.NodeMask = 0;

    if (FAILED(GetCurrentDevice()->CreateCommandSignature(
          &desc, nullptr, IID_PPV_ARGS(&draw_command_signature_)))) {
      throw std::runtime_error("Failed to create draw command signature");
    }
  }
  return draw_command_signature_.Get();
}

auto Graphics::GetDrawRootConstantCommandSignature(
  ID3D12RootSignature* root_signature) const -> ID3D12CommandSignature*
{
  DCHECK_NOTNULL_F(root_signature);

  if (const auto it
    = draw_root_constant_command_signatures_.find(root_signature);
    it != draw_root_constant_command_signatures_.end()) {
    return it->second.Get();
  }

  D3D12_INDIRECT_ARGUMENT_DESC args[2] {};
  args[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
  args[0].Constant.RootParameterIndex
    = static_cast<UINT>(bindless_d3d12::RootParam::kRootConstants);
  args[0].Constant.DestOffsetIn32BitValues = 0U;
  args[0].Constant.Num32BitValuesToSet = 1U;
  args[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;

  D3D12_COMMAND_SIGNATURE_DESC desc {};
  desc.ByteStride = sizeof(std::uint32_t) + sizeof(D3D12_DRAW_ARGUMENTS);
  desc.NumArgumentDescs = 2U;
  desc.pArgumentDescs = args;
  desc.NodeMask = 0;

  Microsoft::WRL::ComPtr<ID3D12CommandSignature> signature;
  if (FAILED(GetCurrentDevice()->CreateCommandSignature(
        &desc, root_signature, IID_PPV_ARGS(&signature)))) {
    throw std::runtime_error(
      "Failed to create draw+root-constant command signature");
  }

  auto* const raw_signature = signature.Get();
  draw_root_constant_command_signatures_.emplace(
    root_signature, std::move(signature));
  return raw_signature;
}

auto Graphics::GetDispatchCommandSignature() const -> ID3D12CommandSignature*
{
  if (!dispatch_command_signature_) {
    D3D12_INDIRECT_ARGUMENT_DESC args[1] {};
    args[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;

    D3D12_COMMAND_SIGNATURE_DESC desc {};
    desc.ByteStride = sizeof(D3D12_DISPATCH_ARGUMENTS);
    desc.NumArgumentDescs = 1U;
    desc.pArgumentDescs = args;
    desc.NodeMask = 0U;

    if (FAILED(GetCurrentDevice()->CreateCommandSignature(
          &desc, nullptr, IID_PPV_ARGS(&dispatch_command_signature_)))) {
      throw std::runtime_error("Failed to create dispatch command signature");
    }
  }
  return dispatch_command_signature_.Get();
}

auto Graphics::CreateSurface(std::weak_ptr<platform::Window> window_weak,
  const observer_ptr<graphics::CommandQueue> command_queue) const
  -> std::unique_ptr<Surface>
{
  DCHECK_F(!window_weak.expired());
  DCHECK_NOTNULL_F(command_queue);
  DCHECK_EQ_F(command_queue->GetTypeId(),
    graphics::d3d12::CommandQueue::ClassTypeId(),
    "Invalid command queue class");

  // NOLINTNEXTLINE(*-pro-type-static-cast-downcast)
  const auto* queue = static_cast<CommandQueue*>(command_queue.get());
  auto surface = std::make_unique<detail::WindowSurface>(
    window_weak, queue->GetCommandQueue(), this);
  if (!surface->GetComponent<detail::SwapChain>().IsValid()) {
    throw std::runtime_error(
      "Failed to create D3D12 window surface swap chain");
  }
  // Implicit upcast: unique_ptr<WindowSurface> → unique_ptr<Surface>
  return std::unique_ptr<Surface>(std::move(surface));
}

auto Graphics::CreateSurfaceFromNative(void* /*native_handle*/,
  const observer_ptr<graphics::CommandQueue> command_queue) const
  -> std::shared_ptr<Surface>
{
  // native_handle is unused for CompositionSurface as we create the SwapChain
  // internally and expose it via GetSwapChain() for the interop layer to
  // connect.
  DCHECK_NOTNULL_F(command_queue);
  DCHECK_EQ_F(command_queue->GetTypeId(),
    graphics::d3d12::CommandQueue::ClassTypeId(),
    "Invalid command queue class");

  // NOLINTNEXTLINE(*-pro-type-static-cast-downcast)
  const auto* queue = static_cast<CommandQueue*>(command_queue.get());
  const auto surface = std::make_shared<detail::CompositionSurface>(
    queue->GetCommandQueue(), const_cast<Graphics*>(this));
  if (!surface->GetComponent<detail::CompositionSwapChain>().IsValid()) {
    throw std::runtime_error(
      "Failed to create D3D12 composition surface swap chain");
  }
  CHECK_NOTNULL_F(surface, "Failed to create composition surface");
  return std::static_pointer_cast<Surface>(surface);
}

auto Graphics::GetShader(const ShaderRequest& request) const
  -> std::shared_ptr<IShaderByteCode>
{
  return GetComponent<EngineShaders>().GetShader(request);
}

auto Graphics::CreateTexture(const TextureDesc& desc) const
  -> std::shared_ptr<graphics::Texture>
{
  return std::make_shared<Texture>(desc, this);
}

auto Graphics::CreateTextureFromNativeObject(const TextureDesc& desc,
  const NativeResource& native) const -> std::shared_ptr<graphics::Texture>
{
  return std::make_shared<Texture>(desc, native, this);
}

auto Graphics::CreateBuffer(const BufferDesc& desc) const
  -> std::shared_ptr<graphics::Buffer>
{
  return std::make_shared<Buffer>(desc, this);
}

auto Graphics::GetOrCreateGraphicsPipeline(GraphicsPipelineDesc desc,
  const size_t hash) -> detail::PipelineStateCache::Entry
{
  auto& cache = GetComponent<detail::PipelineStateCache>();
  return cache.GetOrCreatePipeline<GraphicsPipelineDesc>(std::move(desc), hash);
}

auto Graphics::GetOrCreateComputePipeline(ComputePipelineDesc desc,
  const size_t hash) -> detail::PipelineStateCache::Entry
{
  auto& cache = GetComponent<detail::PipelineStateCache>();
  return cache.GetOrCreatePipeline<ComputePipelineDesc>(std::move(desc), hash);
}
