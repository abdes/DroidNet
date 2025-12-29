//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include <Oxygen/Config/GraphicsConfig.h>
#include <Oxygen/Graphics/Common/BackendModule.h>
#include <Oxygen/Graphics/Common/DescriptorHandle.h>
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
#include <Oxygen/Graphics/Direct3D12/Shaders/EngineShaders.h>
#include <Oxygen/Graphics/Direct3D12/Texture.h>

//===----------------------------------------------------------------------===//
// Internal implementation of the graphics backend module API.
//===----------------------------------------------------------------------===//

namespace {

auto GetBackendInternal() -> std::shared_ptr<oxygen::graphics::d3d12::Graphics>&
{
  static std::shared_ptr<oxygen::graphics::d3d12::Graphics> graphics;
  return graphics;
}

auto CreateBackend(const oxygen::SerializedBackendConfig& config) -> void*
{
  auto& backend = GetBackendInternal();
  if (!backend) {
    backend = std::make_shared<oxygen::graphics::d3d12::Graphics>(config);
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
      allocator_->Reserve(ResourceViewType::kStructuredBuffer_SRV,
        DescriptorVisibility::kShaderVisible, oxygen::bindless::Count { 1 });
      allocator_->Reserve(ResourceViewType::kSampler,
        DescriptorVisibility::kShaderVisible, oxygen::bindless::Count { 1 });

      // Ensure a default sampler exists for bindless texture sampling.
      // The current shaders use SamplerDescriptorHeap[0].
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
    } catch (const std::exception& ex) {
      LOG_F(WARNING, "Failed to eagerly create shader-visible heaps: {}",
        ex.what());
    }
  }

private:
  std::unique_ptr<oxygen::graphics::d3d12::DescriptorAllocator> allocator_ {};
  oxygen::graphics::DescriptorHandle default_sampler_ {};
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

Graphics::Graphics(const SerializedBackendConfig& config)
  : Base("D3D12 Backend")
{
  LOG_SCOPE_FUNCTION(INFO);

  // Parse JSON configuration
  nlohmann::json jsonConfig
    = nlohmann::json::parse(config.json_data, config.json_data + config.size);

  DeviceManagerDesc desc {};
  if (jsonConfig.contains("enable_debug")) {
    desc.enable_debug = jsonConfig["enable_debug"].get<bool>();
  }
  if (jsonConfig.contains("enable_vsync")) {
    enable_vsync_ = jsonConfig["enable_vsync"].get<bool>();
  }
  AddComponent<DeviceManager>(desc);
  AddComponent<EngineShaders>();
  AddComponent<DescriptorAllocatorComponent>();
  AddComponent<detail::PipelineStateCache>(this);
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
  // Implicit upcast: unique_ptr<WindowSurface> → unique_ptr<Surface>
  return std::unique_ptr<Surface>(std::move(surface));
}

auto Graphics::CreateSurfaceFromNative(void* native_handle,
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
  CHECK_NOTNULL_F(surface, "Failed to create composition surface");
  return std::static_pointer_cast<Surface>(surface);
}

auto Graphics::GetShader(const std::string_view unique_id) const
  -> std::shared_ptr<IShaderByteCode>
{
  return GetComponent<EngineShaders>().GetShader(unique_id);
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
