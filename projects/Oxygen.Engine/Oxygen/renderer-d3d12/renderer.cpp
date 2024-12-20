//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "oxygen/renderer-d3d12/renderer.h"

#include <functional>
#include <memory>

#include <dxgi1_6.h>
#include <wrl/client.h>

#include "detail/dx12_utils.h"
#include "oxygen/base/compilers.h"
#include "oxygen/renderer-d3d12/commander.h"

using Microsoft::WRL::ComPtr;
using oxygen::CheckResult;
using oxygen::renderer::direct3d12::ToNarrow;

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
namespace oxygen::renderer::direct3d12::detail {
  class RendererImpl
  {
  public:
    RendererImpl(PlatformPtr platform, const RendererProperties& props);
    ~RendererImpl() = default;
    OXYGEN_MAKE_NON_COPYABLE(RendererImpl);
    OXYGEN_MAKE_NON_MOVEABLE(RendererImpl);

    void Init();
    void Shutdown();
    void Render() const;

  private:
    PlatformPtr platform_;
    RendererProperties props_;
    ComPtr<ID3D12Device9> device_;
    std::unique_ptr<Commander> command_{ nullptr };
  };
  RendererImpl::RendererImpl(PlatformPtr platform, const RendererProperties& props)
    : platform_(std::move(platform)), props_(props)
  {
  }

  void RendererImpl::Init()
  {
    if (device_) Shutdown();

    try {
      // Setup the DXGI factory
      InitializeFactory(props_.enable_debug);


      // Optionally enable debugging layer and GPU-based validation
      if (props_.enable_debug)
      {
        ComPtr<ID3D12Debug1> debug_controller;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_controller)))) {
          debug_controller->EnableDebugLayer();
          if (props_.enable_validation) {
            debug_controller->SetEnableGPUBasedValidation(TRUE);
          }
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
          IID_PPV_ARGS(&device_)));

      NameObject(device_.Get(), L"MAIN DEVICE");

      command_.reset(new Commander(device_.Get(), D3D12_COMMAND_LIST_TYPE_DIRECT));

#ifdef _DEBUG
      ComPtr<ID3D12InfoQueue> info_queue;
      if (SUCCEEDED(device_->QueryInterface(IID_PPV_ARGS(&info_queue)))) {
        CheckResult(info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true));
        CheckResult(info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true));
        CheckResult(info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true));
      }
#endif
    }
    catch (const std::runtime_error& e) {
      LOG_F(ERROR, "Initialization failed: {}", e.what());
      throw;
    }
  }

  void RendererImpl::Shutdown()
  {
    command_->Release();
    dxgi_factory.Reset();

#ifdef _DEBUG
    ComPtr<ID3D12InfoQueue> info_queue;
    if (SUCCEEDED(device_->QueryInterface(IID_PPV_ARGS(&info_queue)))) {
      CheckResult(info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, false));
      CheckResult(info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, false));
      CheckResult(info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, false));
      info_queue.Reset();

      // Check for leftover live objects
      ComPtr<ID3D12DebugDevice2> debug_device;
      if (SUCCEEDED(device_->QueryInterface(IID_PPV_ARGS(&debug_device)))) {
        device_.Reset();
        CheckResult(debug_device->ReportLiveDeviceObjects(
          D3D12_RLDO_SUMMARY | D3D12_RLDO_DETAIL | D3D12_RLDO_IGNORE_INTERNAL));
        debug_device.Reset();
      }
    }
#endif

    device_.Reset();
  }

  void RendererImpl::Render() const
  {
    DCHECK_NOTNULL_F(command_);

    LOG_SCOPE_FUNCTION(INFO);
    // Wait for the GPU to finish executing the previous frame, reset the
    // allocator once the GPU is done with it to free the memory we allocated to
    // store the commands.
    command_->BeginFrame();

    ID3D12GraphicsCommandList7* command_list{ command_->CommandList() };
    // Record commands
    //...

    // Done with recording -> execute the commands, signal and increment the fence
    // value for the next frame.
    command_->EndFrame();

  }

}  // namespace oxygen::renderer::direct3d12::detail

using oxygen::renderer::direct3d12::Renderer;

Renderer::Renderer() = default;
Renderer::~Renderer() = default;

void Renderer::Init(PlatformPtr platform, const RendererProperties& props)
{
  pimpl_ = std::make_unique<detail::RendererImpl>(platform, props);
  pimpl_->Init();
  LOG_F(INFO, "Renderer `{}` initialized", Name());
}

void Renderer::DoShutdown()
{
  pimpl_->Shutdown();
  LOG_F(INFO, "Renderer `{}` shut down", Name());
}

void Renderer::Render()
{
  pimpl_->Render();
}
