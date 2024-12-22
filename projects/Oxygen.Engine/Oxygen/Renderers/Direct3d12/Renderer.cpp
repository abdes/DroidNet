//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Renderers/Direct3d12/Renderer.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <mutex>

#include <dxgi1_6.h>
#include <wrl/client.h>

#include "D3DPtr.h"
#include "oxygen/base/compilers.h"
#include "oxygen/base/macros.h"
#include "oxygen/Renderers/Common/Types.h"
#include "Oxygen/Renderers/Direct3d12/commander.h"
#include "Oxygen/Renderers/Direct3d12/Detail/dx12_utils.h"
#include "Oxygen/Renderers/Direct3d12/detail/resources.h"
#include "Oxygen/Renderers/Direct3d12/IDeferredReleaseController.h"
#include "Oxygen/Renderers/Direct3d12/Surface.h"
#include "Oxygen/Renderers/Direct3d12/Types.h"

using Microsoft::WRL::ComPtr;
using oxygen::CheckResult;
using oxygen::renderer::d3d12::ToNarrow;

namespace {
  auto GetMainDeviceInternal() -> ComPtr<ID3D12Device9>&
  {
    static ComPtr<ID3D12Device9> main_device;
    return main_device;
  }
}  // namesape
namespace oxygen::renderer::d3d12 {
  auto GetMainDevice() -> ID3D12Device9*
  {
    return GetMainDeviceInternal().Get();
  }
}  // namespace oxygen::renderer::d3d12

// Anonymous namespace for adapter discovery helper functions
namespace {

  struct AdapterDesc
  {
    std::string name;
    uint32_t vendor_id;
    uint32_t device_id;
    size_t dedicated_memory;
    bool meets_feature_level{ false };
    bool has_connected_display{ false };
    D3D_FEATURE_LEVEL max_feature_level{ D3D_FEATURE_LEVEL_11_0 };
  };

  std::vector<AdapterDesc> adapters;
  ComPtr<IDXGIFactory7> dxgi_factory;

  bool CheckConnectedDisplay(const ComPtr<IDXGIAdapter1>& adapter)
  {
    ComPtr<IDXGIOutput> output;
    return SUCCEEDED(adapter->EnumOutputs(0, &output));
  }

  AdapterDesc CreateAdapterDesc(const DXGI_ADAPTER_DESC1& desc, const ComPtr<IDXGIAdapter1>& adapter)
  {
    AdapterDesc adapter_info{
      .name = ToNarrow(desc.Description),
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
    }
    else {
      oss << std::fixed << std::setprecision(2) << (static_cast<double>(memory_size) / (1ull << 20)) << " MB";
    }
    return oss.str();
  }

  auto FeatureLevelToString(const D3D_FEATURE_LEVEL feature_level) -> std::string
  {
    switch (feature_level) {  // NOLINT(clang-diagnostic-switch-enum)
    case D3D_FEATURE_LEVEL_12_2: return "12_2";
    case D3D_FEATURE_LEVEL_12_1: return "12_1";
    case D3D_FEATURE_LEVEL_12_0: return "12_0";
    case D3D_FEATURE_LEVEL_11_1: return "11_1";
    case D3D_FEATURE_LEVEL_11_0: return "11_0";
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

  auto GetMaxFeatureLevel(const ComPtr<ID3D12Device>& device) -> D3D_FEATURE_LEVEL
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
    CheckResult(CreateDXGIFactory2(0, IID_PPV_ARGS(&dxgi_factory)));
    UINT dxgi_factory_flags{ 0 };
    if (enable_debug) {
      dxgi_factory_flags = DXGI_CREATE_FACTORY_DEBUG;
    }
    CheckResult(CreateDXGIFactory2(dxgi_factory_flags, IID_PPV_ARGS(&dxgi_factory)));
  }

  std::tuple<ComPtr<IDXGIAdapter1>, size_t> DiscoverAdapters(const std::function<bool(const AdapterDesc&)>& selector)
  {
    LOG_SCOPE_FUNCTION(INFO);

    ComPtr<IDXGIAdapter1> selected_adapter;
    size_t selected_adapter_index = std::numeric_limits<size_t>::max();
    ComPtr<IDXGIAdapter1> adapter;

    // Enumerate high-performance adapters only
    for (UINT adapter_index = 0;
         DXGI_ERROR_NOT_FOUND != dxgi_factory->EnumAdapterByGpuPreference(
           adapter_index,
           DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
           IID_PPV_ARGS(&adapter));
         adapter_index++) {
      DXGI_ADAPTER_DESC1 desc;
      CheckResult(adapter->GetDesc1(&desc));

      if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
        // Don't select the Basic Render Driver adapter.
        continue;
      }

      AdapterDesc adapter_info = CreateAdapterDesc(desc, adapter);

      // Check if the adapter supports the minimum required feature level
      ComPtr<ID3D12Device> device;
      if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) {
        adapter_info.meets_feature_level = true;
        adapter_info.max_feature_level = GetMaxFeatureLevel(device);
        if (selector(adapter_info))
        {
          selected_adapter = adapter;
          selected_adapter_index = adapters.size();
        }
        else
        {
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
  class RendererImpl final
    : public std::enable_shared_from_this<RendererImpl>
    , public IDeferredReleaseController
  {
  public:
    RendererImpl();
    ~RendererImpl() override = default;
    OXYGEN_MAKE_NON_COPYABLE(RendererImpl);
    OXYGEN_MAKE_NON_MOVEABLE(RendererImpl);

    void Init(PlatformPtr platform, const RendererProperties& props);
    void Shutdown();

    void Render(const resources::SurfaceId& surface_id) const;

    void RegisterDeferredReleases(std::function<void(size_t)> handler) const override
    {
      auto& deferred_release = deferred_releases_[CurrentFrameIndex()];
      deferred_release.AddHandler(std::move(handler));
    }

    [[nodiscard]] auto RtvHeap() const->DescriptorHeap& { return rtv_heap_; }
    [[nodiscard]] auto DsvHeap() const->DescriptorHeap& { return dsv_heap_; }
    [[nodiscard]] auto SrvHeap() const->DescriptorHeap& { return srv_heap_; }
    [[nodiscard]] auto UavHeap() const->DescriptorHeap& { return uav_heap_; }

    void CreateSwapChain(const resources::SurfaceId& surface_id, DXGI_FORMAT format) const;

    [[nodiscard]] D3D12MA::Allocator* GetAllocator() const { return allocator_; }

  private:
    void ProcessDeferredRelease(size_t frame_index) const
    {
      auto& deferred_release = deferred_releases_[frame_index];
      deferred_release.InvokeHandlers(frame_index);
    }

    D3D12MA::Allocator* allocator_{ nullptr };
    std::unique_ptr<Commander> commander_{};

    DeferredReleaseControllerPtr GetWeakPtr() { return shared_from_this(); }

    mutable DescriptorHeap rtv_heap_{ D3D12_DESCRIPTOR_HEAP_TYPE_RTV };
    mutable DescriptorHeap dsv_heap_{ D3D12_DESCRIPTOR_HEAP_TYPE_DSV };
    mutable DescriptorHeap srv_heap_{ D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV };
    mutable DescriptorHeap uav_heap_{ D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV };

    class DeferredRelease
    {
    public:
      DeferredRelease() = default;
      ~DeferredRelease() = default;

      OXYGEN_MAKE_NON_COPYABLE(DeferredRelease);
      OXYGEN_MAKE_NON_MOVEABLE(DeferredRelease);

      void InvokeHandlers(const size_t frame_index)
      {
        std::lock_guard lock{ mutex_ };

        if (handlers_.empty()) return;

        for (const auto& handler : handlers_)
        {
          handler(frame_index);
        }
        handlers_.clear();
      }

      void AddHandler(std::function<void(size_t)> handler)
      {
        std::lock_guard lock{ mutex_ };

        // Check if the handler already exists
        const auto it = std::ranges::find_if(
          handlers_,
          [&handler](const std::function<void(size_t)>& existing_handler) {
            return handler.target_type() == existing_handler.target_type();
          });

        if (it == handlers_.end())
        {
          handlers_.push_back(std::move(handler));
        }
      }

    private:
      mutable std::mutex mutex_{};
      std::vector<std::function<void(size_t)>> handlers_{};
    };

    mutable DeferredRelease deferred_releases_[kFrameBufferCount]{ };
  };
  RendererImpl::RendererImpl() : IDeferredReleaseController()
  {
  }

  void RendererImpl::Init(PlatformPtr platform, const RendererProperties& props)
  {
    if (GetMainDevice()) Shutdown();

    LOG_SCOPE_FUNCTION(INFO);
    try {
      // Setup the DXGI factory
      InitializeFactory(props.enable_debug);


      // Optionally enable debugging layer and GPU-based validation
      if (props.enable_debug)
      {
        ComPtr<ID3D12Debug1> debug_controller;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller)))) {
          debug_controller->EnableDebugLayer();
          if (props.enable_validation) {
            debug_controller->SetEnableGPUBasedValidation(TRUE);
          }
        }
        else
        {
          LOG_F(WARNING, "Failed to enable the debug layer");
        }
      }

      // Discover adapters and select the most suitable one
      const auto [best_adapter, best_adapter_index] = DiscoverAdapters(
        [](const AdapterDesc& adapter) {
          return adapter.meets_feature_level && adapter.has_connected_display;
        });
      const auto& best_adapter_desc = adapters[best_adapter_index];
      LOG_F(INFO, "Selected adapter: {}", best_adapter_desc.name);

      // Create the device with the maximum feature level of the selected adapter
      CheckResult(
        D3D12CreateDevice(
          best_adapter.Get(),
          best_adapter_desc.max_feature_level,
          IID_PPV_ARGS(&GetMainDeviceInternal())));
      NameObject(GetMainDevice(), L"MAIN DEVICE");

#ifdef _DEBUG
      ComPtr<ID3D12InfoQueue> info_queue;
      if (SUCCEEDED(GetMainDevice()->QueryInterface(IID_PPV_ARGS(&info_queue)))) {
        CheckResult(info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true));
        CheckResult(info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true));
        CheckResult(info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true));
      }
#endif

      {
        D3D12MA::ALLOCATOR_DESC allocator_desc = {};
        allocator_desc.pDevice = GetMainDevice();
        allocator_desc.pAdapter = best_adapter.Get();

        if (FAILED(D3D12MA::CreateAllocator(&allocator_desc, &allocator_)))
        {
          LOG_F(ERROR, "Failed to initialize D3D12MemoryAllocator");
          throw std::runtime_error("Failed to initialize D3D12MemoryAllocator");
        }
      }

      // Initialize deferred release manager
      DeferredResourceReleaseTracker::Instance().Initialize(GetWeakPtr());

      // Initialize the commander
      commander_.reset(new Commander(GetMainDevice(), D3D12_COMMAND_LIST_TYPE_DIRECT));

      // Initialize heaps
      rtv_heap_.Initialize(512, false, GetMainDevice(), GetWeakPtr());
      NameObject(rtv_heap_.Heap(), L"RTV Descriptor Heap");
      dsv_heap_.Initialize(512, false, GetMainDevice(), GetWeakPtr());
      NameObject(dsv_heap_.Heap(), L"DSV Descriptor Heap");
      srv_heap_.Initialize(4096, true, GetMainDevice(), GetWeakPtr());
      NameObject(srv_heap_.Heap(), L"SRV Descriptor Heap");
      uav_heap_.Initialize(512, false, GetMainDevice(), GetWeakPtr());
      NameObject(uav_heap_.Heap(), L"UAV Descriptor Heap");
    }
    catch (const std::runtime_error& e) {
      LOG_F(ERROR, "Initialization failed: {}", e.what());
      // TODO: cleanup
      throw;
    }
  }

  void RendererImpl::Shutdown()
  {
    LOG_SCOPE_FUNCTION(INFO);

    // Flush any pending commands and release any defeerred resources for all
    // our frame indices
    commander_->Flush();
    for (uint32_t index = 0; index < kFrameBufferCount; ++index) {
      ProcessDeferredRelease(index);
    }

    srv_heap_.Release();
    uav_heap_.Release();
    dsv_heap_.Release();
    rtv_heap_.Release();

    commander_->Release();
    commander_.reset();

    // Call deferred release for the last time in case some deferred releases
    // have been made while we are rleasing things here
    ProcessDeferredRelease(CurrentFrameIndex());

    SafeRelease(&allocator_);
    dxgi_factory.Reset();

#ifdef _DEBUG
    ComPtr<ID3D12InfoQueue> info_queue;
    if (SUCCEEDED(GetMainDevice()->QueryInterface(IID_PPV_ARGS(&info_queue)))) {
      CheckResult(info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, false));
      CheckResult(info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, false));
      CheckResult(info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, false));
      info_queue.Reset();

      // Check for leftover live objects
      ComPtr<ID3D12DebugDevice2> debug_device;
      if (SUCCEEDED(GetMainDevice()->QueryInterface(IID_PPV_ARGS(&debug_device)))) {
        GetMainDeviceInternal().Reset();
        CheckResult(debug_device->ReportLiveDeviceObjects(
          D3D12_RLDO_SUMMARY | D3D12_RLDO_DETAIL | D3D12_RLDO_IGNORE_INTERNAL));
        debug_device.Reset();
      }
    }
#endif

    GetMainDeviceInternal().Reset();
  }

  void RendererImpl::Render(const resources::SurfaceId& surface_id) const
  {
    DCHECK_NOTNULL_F(commander_);

    // Wait for the GPU to finish executing the previous frame, reset the
    // allocator once the GPU is done with it to free the memory we allocated to
    // store the commands.
    commander_->BeginFrame();

    // Process deferred releases
    ProcessDeferredRelease(CurrentFrameIndex());

    // Presenting
    auto const surface = GetSurface(surface_id);
    surface.Present();

    ID3D12GraphicsCommandList7* command_list{ commander_->CommandList() };
    // Record commands
    //...

    // Done with recording -> execute the commands, signal and increment the fence
    // value for the next frame.
    commander_->EndFrame();

  }

  void RendererImpl::CreateSwapChain(const resources::SurfaceId& surface_id, const DXGI_FORMAT format) const
  {
    auto surface = GetSurface(surface_id);
    if (!surface.IsValid()) {
      LOG_F(ERROR, "Invalid surface ID: {}", surface_id.ToString());
      return;
    }
    surface.CreateSwapChain(dxgi_factory.Get(), commander_->CommandQueue(), format);
  }

}  // namespace oxygen::renderer::d3d12::detail

using oxygen::renderer::d3d12::Renderer;


void Renderer::OnInitialize()
{
  pimpl_ = std::make_shared<detail::RendererImpl>();
  pimpl_->Init(GetPlatform(), GetPInitProperties());
  LOG_F(INFO, "Renderer `{}` initialized", Name());
}

void Renderer::OnShutdown()
{
  pimpl_->Shutdown();
  LOG_F(INFO, "Renderer `{}` shut down", Name());
}

void Renderer::Render(const resources::SurfaceId& surface_id)
{
  pimpl_->Render(surface_id);
}

auto Renderer::CurrentFrameIndex() const -> size_t
{
  return d3d12::CurrentFrameIndex();
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

auto Renderer::GetAllocator() const -> D3D12MA::Allocator*
{
  return pimpl_->GetAllocator();
}

void Renderer::CreateSwapChain(const resources::SurfaceId& surface_id) const
{
  pimpl_->CreateSwapChain(surface_id, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
}
