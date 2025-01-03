//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Renderers/Direct3d12/Renderer.h"

#include <algorithm>
#include <functional>
#include <memory>

#include <dxgi1_6.h>
#include <wrl/client.h>

#include "Oxygen/Base/Compilers.h"
#include "Oxygen/Base/ResourceTable.h"
#include "Oxygen/ImGui/ImGuiPlatformBackend.h"
#include "Oxygen/Renderers/Common/CommandRecorder.h"
#include "Oxygen/Renderers/Common/ShaderManager.h"
#include "Oxygen/Renderers/Common/Types.h"
#include "Oxygen/Renderers/Direct3d12/CommandQueue.h"
#include "Oxygen/Renderers/Direct3d12/CommandRecorder.h"
#include "Oxygen/Renderers/Direct3d12/DebugLayer.h"
#include "Oxygen/Renderers/Direct3d12/Detail/DescriptorHeap.h"
#include "Oxygen/Renderers/Direct3d12/Detail/dx12_utils.h"
#include "Oxygen/Renderers/Direct3d12/Detail/WindowSurfaceImpl.h"
#include "Oxygen/Renderers/Direct3d12/ImGui/ImGuiModule.h"
#include "Oxygen/Renderers/Direct3d12/ShaderCompiler.h"
#include "Oxygen/Renderers/Direct3d12/Types.h"
#include "Oxygen/Renderers/Direct3d12/WindowSurface.h"

using Microsoft::WRL::ComPtr;
using oxygen::renderer::d3d12::DeviceType;
using oxygen::renderer::d3d12::FactoryType;
using oxygen::windows::ThrowOnFailed;

namespace {
auto GetFactoryInternal() -> ComPtr<FactoryType>&
{
  static ComPtr<FactoryType> factory;
  return factory;
}
auto GetMainDeviceInternal() -> ComPtr<DeviceType>&
{
  static ComPtr<DeviceType> main_device;
#if defined(_DEBUG)
  if (main_device) {
    main_device.Get()->AddRef();
    main_device.Get()->Release();
    // DLOG_F(2, "Main Device reference count: {}", ref_count);
  }
#endif
  return main_device;
}
} // namespace
namespace oxygen::renderer::d3d12 {
auto GetFactory() -> FactoryType*
{
  return GetFactoryInternal().Get();
}
auto GetMainDevice() -> DeviceType*
{
  return GetMainDeviceInternal().Get();
}
} // namespace oxygen::renderer::d3d12

namespace {
using oxygen::renderer::resources::kSurface;
oxygen::ResourceTable<oxygen::renderer::d3d12::detail::WindowSurfaceImpl> surfaces(kSurface, 256);
} // namespace

namespace {
// Specification of engine shaders. Each entry is a ShaderProfile
// corresponding to one of the shaders we want to automatically compile,
// package and load.
const oxygen::renderer::ShaderProfile kEngineShaders[] = {
  { .type = oxygen::ShaderType::kPixel, .path = "FullScreenTriangle.hlsl", .entry_point = "PS" },
  { .type = oxygen::ShaderType::kVertex, .path = "FullScreenTriangle.hlsl", .entry_point = "VS" },
};
} // namespace

// Anonymous namespace for adapter discovery helper functions
namespace {

struct AdapterDesc {
  std::string name;
  uint32_t vendor_id;
  uint32_t device_id;
  size_t dedicated_memory;
  bool meets_feature_level { false };
  bool has_connected_display { false };
  D3D_FEATURE_LEVEL max_feature_level { D3D_FEATURE_LEVEL_11_0 };
};

std::vector<AdapterDesc> adapters;

bool CheckConnectedDisplay(const ComPtr<IDXGIAdapter1>& adapter)
{
  ComPtr<IDXGIOutput> output;
  return SUCCEEDED(adapter->EnumOutputs(0, &output));
}

AdapterDesc CreateAdapterDesc(const DXGI_ADAPTER_DESC1& desc, const ComPtr<IDXGIAdapter1>& adapter)
{
  std::string description {};
  oxygen::string_utils::WideToUtf8(desc.Description, description);
  AdapterDesc adapter_info {
    .name = description,
    .vendor_id = desc.VendorId,
    .device_id = desc.DeviceId,
    .dedicated_memory = desc.DedicatedVideoMemory,
    .has_connected_display = CheckConnectedDisplay(adapter),
  };
  return adapter_info;
}

std::string FormatMemorySize(const size_t memory_size)
{
  std::ostringstream oss;
  if (memory_size >= (1ull << 30)) {
    oss << std::fixed << std::setprecision(2) << (static_cast<double>(memory_size) / (1ull << 30)) << " GB";
  } else {
    oss << std::fixed << std::setprecision(2) << (static_cast<double>(memory_size) / (1ull << 20)) << " MB";
  }
  return oss.str();
}

auto FeatureLevelToString(const D3D_FEATURE_LEVEL feature_level) -> std::string
{
  switch (feature_level) { // NOLINT(clang-diagnostic-switch-enum)
  case D3D_FEATURE_LEVEL_12_2:
    return "12_2";
  case D3D_FEATURE_LEVEL_12_1:
    return "12_1";
  case D3D_FEATURE_LEVEL_12_0:
    return "12_0";
  case D3D_FEATURE_LEVEL_11_1:
    return "11_1";
  case D3D_FEATURE_LEVEL_11_0:
    return "11_0";
  default:
    OXYGEN_UNREACHABLE_RETURN("_UNEXPECTED_");
  }
}

void LogAdapters()
{
  std::ranges::for_each(
    adapters,
    [](const AdapterDesc& a) {
      LOG_F(INFO, "[{}] {} {} ({}-{})", "+", a.name, FormatMemorySize(a.dedicated_memory), a.vendor_id, a.device_id);
      LOG_F(INFO, "  Meets Feature Level: {}", a.meets_feature_level);
      LOG_F(INFO, "  Has Connected Display: {}", a.has_connected_display);
      LOG_F(INFO, "  Max Feature Level: {}", FeatureLevelToString(a.max_feature_level));
    });
}

auto GetMaxFeatureLevel(const ComPtr<DeviceType>& device) -> D3D_FEATURE_LEVEL
{
  static constexpr D3D_FEATURE_LEVEL feature_levels[] = {
    D3D_FEATURE_LEVEL_12_2,
    D3D_FEATURE_LEVEL_12_1,
    D3D_FEATURE_LEVEL_12_0,
    D3D_FEATURE_LEVEL_11_1,
    D3D_FEATURE_LEVEL_11_0,
  };

  D3D12_FEATURE_DATA_FEATURE_LEVELS feature_level_info = {};
  feature_level_info.NumFeatureLevels = _countof(feature_levels);
  feature_level_info.pFeatureLevelsRequested = feature_levels;

  if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &feature_level_info, sizeof(feature_level_info)))) {
    return feature_level_info.MaxSupportedFeatureLevel;
  }

  return D3D_FEATURE_LEVEL_11_0;
}

void InitializeFactory(const bool enable_debug)
{
  ThrowOnFailed(CreateDXGIFactory2(0, IID_PPV_ARGS(&GetFactoryInternal())));
  UINT dxgi_factory_flags { 0 };
  if (enable_debug) {
    dxgi_factory_flags = DXGI_CREATE_FACTORY_DEBUG;
  }
  ThrowOnFailed(CreateDXGIFactory2(dxgi_factory_flags, IID_PPV_ARGS(&GetFactoryInternal())));
}

std::tuple<ComPtr<IDXGIAdapter1>, size_t> DiscoverAdapters(const std::function<bool(const AdapterDesc&)>& selector)
{
  LOG_SCOPE_FUNCTION(INFO);

  ComPtr<IDXGIAdapter1> selected_adapter;
  size_t selected_adapter_index = std::numeric_limits<size_t>::max();
  ComPtr<IDXGIAdapter1> adapter;

  // Enumerate high-performance adapters only
  for (UINT adapter_index = 0;
    DXGI_ERROR_NOT_FOUND != GetFactoryInternal()->EnumAdapterByGpuPreference(adapter_index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter));
    adapter_index++) {
    DXGI_ADAPTER_DESC1 desc;
    ThrowOnFailed(adapter->GetDesc1(&desc));

    if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
      // Don't select the Basic Render Driver adapter.
      continue;
    }

    AdapterDesc adapter_info = CreateAdapterDesc(desc, adapter);

    // Check if the adapter supports the minimum required feature level
    ComPtr<DeviceType> device;
    if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) {
      adapter_info.meets_feature_level = true;
      adapter_info.max_feature_level = GetMaxFeatureLevel(device);
      if (selector(adapter_info)) {
        selected_adapter = adapter;
        selected_adapter_index = adapters.size();
      } else {
        device.Reset();
      }
    }

    adapters.push_back(adapter_info);
  }

  LogAdapters();

  if (selected_adapter_index == std::numeric_limits<size_t>::max()) {
    throw std::runtime_error("No suitable adapter found.");
  }

  return std::make_tuple(selected_adapter, selected_adapter_index);
}

} // namespace

// Implementation details of the Renderer class
namespace oxygen::renderer::d3d12::detail {
// Anonymous namespace for command frame management
namespace {

  struct CommandFrame {
    uint64_t fence_value { 0 };
  };

} // namespace

class RendererImpl final
{
 public:
  RendererImpl() = default;
  ~RendererImpl() = default;

  OXYGEN_MAKE_NON_COPYABLE(RendererImpl);
  OXYGEN_MAKE_NON_MOVEABLE(RendererImpl);

  auto CurrentFrameIndex() const -> size_t { return current_frame_index_; }

  void Init(const RendererProperties& props);
  void ShutdownRenderer();
  void ShutdownDevice();

  auto BeginFrame(const resources::SurfaceId& surface_id) const
    -> const renderer::RenderTarget&;
  void EndFrame(CommandLists& command_lists, const resources::SurfaceId& surface_id) const;
  auto CreateWindowSurfaceImpl(platform::WindowPtr window) const
    -> std::pair<resources::SurfaceId, std::shared_ptr<WindowSurfaceImpl>>;

  [[nodiscard]] auto RtvHeap() const -> DescriptorHeap& { return rtv_heap_; }
  [[nodiscard]] auto DsvHeap() const -> DescriptorHeap& { return dsv_heap_; }
  [[nodiscard]] auto SrvHeap() const -> DescriptorHeap& { return srv_heap_; }
  [[nodiscard]] auto UavHeap() const -> DescriptorHeap& { return uav_heap_; }

  [[nodiscard]] D3D12MA::Allocator* GetAllocator() const { return allocator_; }
  CommandRecorderPtr GetCommandRecorder() { return command_recorder_; }
  ShaderCompilerPtr GetShaderCompiler() const { return std::dynamic_pointer_cast<renderer::ShaderCompiler>(shader_compiler_); }

 private:
  D3D12MA::Allocator* allocator_ { nullptr };
  std::shared_ptr<ShaderCompiler> shader_compiler_ {};
  std::unique_ptr<ShaderManager> engine_shaders_ {};

  std::unique_ptr<CommandQueue> command_queue_ {};
  std::shared_ptr<CommandRecorder> command_recorder_ {};
  mutable size_t current_frame_index_ { 0 };
  mutable CommandFrame frames_[kFrameBufferCount] {};

  // DeferredReleaseControllerPtr GetWeakPtr() { return shared_from_this(); }

  mutable DescriptorHeap rtv_heap_ { D3D12_DESCRIPTOR_HEAP_TYPE_RTV, "RTV Descriptor Heap" };
  mutable DescriptorHeap dsv_heap_ { D3D12_DESCRIPTOR_HEAP_TYPE_DSV, "DSV Descriptor Heap" };
  mutable DescriptorHeap srv_heap_ { D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, "SRV Descriptor Heap" };
  mutable DescriptorHeap uav_heap_ { D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, "UAV Descriptor Heap" };

#if defined(_DEBUG)
  DebugLayer debug_layer_ {};
#endif
};

void RendererImpl::Init(const RendererProperties& props)
{
  DCHECK_F(GetMainDevice() == nullptr);

  LOG_SCOPE_FUNCTION(INFO);

  // Setup the DXGI factory
  InitializeFactory(props.enable_debug);

  // Discover adapters and select the most suitable one
  const auto [best_adapter, best_adapter_index] = DiscoverAdapters(
    [](const AdapterDesc& adapter) {
      return adapter.meets_feature_level && adapter.has_connected_display;
    });
  const auto& best_adapter_desc = adapters[best_adapter_index];
  LOG_F(INFO, "Selected adapter: {}", best_adapter_desc.name);

#if defined(_DEBUG)
  // Initialize the Debug Layer and GPU-based validation
  debug_layer_.Initialize(props.enable_debug, props.enable_validation);
#endif

  // Create the device with the maximum feature level of the selected adapter
  ThrowOnFailed(
    D3D12CreateDevice(
      best_adapter.Get(),
      best_adapter_desc.max_feature_level,
      IID_PPV_ARGS(&GetMainDeviceInternal())));
  NameObject(GetMainDevice(), L"MAIN DEVICE");

  {
    D3D12MA::ALLOCATOR_DESC allocator_desc = {};
    allocator_desc.pDevice = GetMainDevice();
    allocator_desc.pAdapter = best_adapter.Get();

    if (FAILED(D3D12MA::CreateAllocator(&allocator_desc, &allocator_))) {
      LOG_F(ERROR, "Failed to initialize D3D12MemoryAllocator");
      throw std::runtime_error("Failed to initialize D3D12MemoryAllocator");
    }
  }

  // Initialize the command recorder
  command_queue_.reset(new CommandQueue(CommandListType::kGraphics));
  command_queue_->Initialize();
  command_recorder_.reset(new CommandRecorder(CommandListType::kGraphics));
  command_recorder_->Initialize();

  // Initialize heaps
  rtv_heap_.Initialize(512, false, GetMainDevice());
  dsv_heap_.Initialize(512, false, GetMainDevice());
  srv_heap_.Initialize(4096, true, GetMainDevice());
  uav_heap_.Initialize(512, false, GetMainDevice());

  // Load engine shaders
  shader_compiler_ = std::make_shared<ShaderCompiler>(ShaderCompilerConfig {});
  shader_compiler_->Initialize();
  // TODO: Make this better by not hard-coding the path
  ShaderManagerConfig shader_manager_config {
    .renderer_name = "D3D12 Renderer",
    .archive_dir = R"(F:\projects\DroidNet\projects\Oxygen.Engine\bin\Oxygen)",
    .source_dir = R"(F:\projects\DroidNet\projects\Oxygen.Engine\Oxygen\Renderers\Direct3D12\Shaders)",
    .shaders = std::span(kEngineShaders, std::size(kEngineShaders)),
    .compiler = shader_compiler_,
  };
  engine_shaders_ = std::make_unique<ShaderManager>(std::move(shader_manager_config));
  engine_shaders_->Initialize();
}

void RendererImpl::ShutdownRenderer()
{
  LOG_SCOPE_FUNCTION(INFO);

  // Cleanup engine shaders
  shader_compiler_->IsInitialized(false);
  ObjectRelease(shader_compiler_);
  engine_shaders_->Shutdown();
  engine_shaders_.reset();

  // Flush any pending commands and release any deferred resources for all
  // our frame indices
  command_queue_->Flush();

  srv_heap_.Release();
  uav_heap_.Release();
  dsv_heap_.Release();
  rtv_heap_.Release();

  command_queue_->Release();
  command_queue_.reset();
  command_recorder_->Release();
  command_recorder_.reset();

  ObjectRelease(allocator_);
  // TODO: SafeRelease for objects that need to be released after a full flush
  // otherwise, we should use ObjectRelease() or DeferredObjectRelease()
  LOG_F(INFO, "D3D12MA Memory Allocator released");
}

void RendererImpl::ShutdownDevice()
{
  LOG_SCOPE_FUNCTION(INFO);

  GetFactoryInternal().Reset();
  LOG_F(INFO, "D3D12 DXGI Factory reset");

  CHECK_EQ_F(GetMainDeviceInternal().Reset(), 0);
  LOG_F(INFO, "D3D12 Main Device reset");

#if defined(_DEBUG)
  debug_layer_.Shutdown();
#endif
}

auto RendererImpl::BeginFrame(const resources::SurfaceId& surface_id) const
  -> const renderer::RenderTarget&
{
  DCHECK_NOTNULL_F(command_recorder_);

  // Wait for the GPU to finish executing the previous frame, reset the
  // allocator once the GPU is done with it to free the memory we allocated to
  // store the commands.
  const auto& fence_value = frames_[CurrentFrameIndex()].fence_value;
  command_queue_->Wait(fence_value);

  DCHECK_F(surface_id.IsValid());

  auto& surface = surfaces.ItemAt(surface_id);
  if (surface.ShouldResize()) {
    command_queue_->Flush();
    surface.Resize();
  }
  return surface;
}

void RendererImpl::EndFrame(CommandLists& command_lists, const resources::SurfaceId& surface_id) const
{
  try {
    const auto& surface = surfaces.ItemAt(surface_id);

    command_queue_->Submit(command_lists);
    for (auto& command_list : command_lists) {
      command_list->Release();
      command_list.reset();
    }
    command_lists.clear();

    // Presenting
    surface.Present();
  } catch (const std::exception& e) {
    LOG_F(WARNING, "No surface for id=`{}`; frame discarded: {}", surface_id.ToString(), e.what());
  }

  // Signal and increment the fence value for the next frame.
  frames_[CurrentFrameIndex()].fence_value = command_queue_->Signal();
  current_frame_index_ = (current_frame_index_ + 1) % kFrameBufferCount;
}

auto RendererImpl::CreateWindowSurfaceImpl(platform::WindowPtr window) const -> std::pair<resources::SurfaceId, std::shared_ptr<WindowSurfaceImpl>>
{
  DCHECK_NOTNULL_F(window.lock());
  DCHECK_F(window.lock()->IsValid());

  const auto surface_id = surfaces.Emplace(std::move(window), command_queue_->GetCommandQueue());
  if (!surface_id.IsValid()) {
    return {};
  }
  LOG_F(INFO, "Window Surface created: {}", surface_id.ToString());

  // Use a custom deleter to call Erase when the shared_ptr<WindowSurfaceImpl> is destroyed
  auto deleter = [surface_id](WindowSurfaceImpl* ptr) {
    surfaces.Erase(surface_id);
  };
  auto& surface_impl = surfaces.ItemAt(surface_id);
  return { surface_id, { &surface_impl, deleter } };
}

} // namespace oxygen::renderer::d3d12::detail

using oxygen::renderer::d3d12::Renderer;

Renderer::Renderer()
  : oxygen::Renderer("D3D12 Renderer")
{
}

void Renderer::OnInitialize(PlatformPtr platform, const RendererProperties& props)
{
  if (GetMainDevice())
    OnShutdown();

  oxygen::Renderer::OnInitialize(std::move(platform), props);
  pimpl_ = std::make_shared<detail::RendererImpl>();
  try {
    pimpl_->Init(GetInitProperties());
  } catch (const std::runtime_error&) {
    // Request a shutdown to cleanup resources
    IsInitialized(true);
    throw;
  }
}

void Renderer::OnShutdown()
{
  pimpl_->ShutdownRenderer();
  oxygen::Renderer::OnShutdown();
  pimpl_->ShutdownDevice();
}

auto Renderer::BeginFrame(const resources::SurfaceId& surface_id)
  -> const renderer::RenderTarget&
{
  current_render_target_ = &pimpl_->BeginFrame(surface_id);
  return *current_render_target_;
}

void Renderer::EndFrame(CommandLists& command_lists, const resources::SurfaceId& surface_id) const
{
  pimpl_->EndFrame(command_lists, surface_id);
}

auto Renderer::CreateImGuiModule(EngineWeakPtr engine, platform::WindowIdType window_id) const -> std::unique_ptr<imgui::ImguiModule>
{
  return std::make_unique<ImGuiModule>(std::move(engine), window_id);
}

auto Renderer::GetCommandRecorder() const -> CommandRecorderPtr
{
  return pimpl_->GetCommandRecorder();
}

auto Renderer::GetShaderCompiler() const -> ShaderCompilerPtr
{
  return pimpl_->GetShaderCompiler();
}

auto Renderer::RtvHeap() const -> detail::DescriptorHeap&
{
  return pimpl_->RtvHeap();
}

auto Renderer::DsvHeap() const -> detail::DescriptorHeap&
{
  return pimpl_->DsvHeap();
}

auto Renderer::SrvHeap() const -> detail::DescriptorHeap&
{
  return pimpl_->SrvHeap();
}

auto Renderer::UavHeap() const -> detail::DescriptorHeap&
{
  return pimpl_->UavHeap();
}

auto Renderer::CreateWindowSurface(platform::WindowPtr window) const -> SurfacePtr
{
  DCHECK_NOTNULL_F(window.lock());
  DCHECK_F(window.lock()->IsValid());

  const auto [surface_id, surface_impl] { pimpl_->CreateWindowSurfaceImpl(window) };
  if (!surface_impl) {
    return {};
  }
  return SurfacePtr(new WindowSurface(surface_id, std::move(window), surface_impl));
}

auto Renderer::GetAllocator() const -> D3D12MA::Allocator*
{
  return pimpl_->GetAllocator();
}
