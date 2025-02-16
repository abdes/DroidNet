//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Direct3D12/Graphics.h>

#include <iomanip>
#include <sstream>

#include <dxgi1_6.h>
#include <wrl/client.h>

#include <Oxygen/Graphics/Common/GraphicsModule.h>
#include <Oxygen/Graphics/Direct3D12/Renderer.h>
#include <Oxygen/Graphics/Direct3d12/DebugLayer.h>
#include <Oxygen/Graphics/Direct3d12/Detail/dx12_utils.h>
#include <Oxygen/Graphics/Direct3d12/Forward.h>
#include <Oxygen/Graphics/Direct3d12/ImGui/ImGuiModule.h>

//===----------------------------------------------------------------------===//
// Internal implementation of the graphics backend module API.

namespace {

std::shared_ptr<oxygen::graphics::d3d12::Graphics>& GetBackendInternal()
{
    static auto graphics = std::make_shared<oxygen::graphics::d3d12::Graphics>();
    return graphics;
}

void* CreateBackend()
{
    return GetBackendInternal().get();
}

void DestroyBackend()
{
    auto& renderer = GetBackendInternal();
    renderer.reset();
}

} // namespace

//===----------------------------------------------------------------------===//
// Implementation of the helper functions to access the important graphics
// backend objects.

namespace oxygen::graphics::d3d12::detail {

auto Graphics() -> d3d12::Graphics&
{
    CHECK_NOTNULL_F(GetBackendInternal());
    return *GetBackendInternal();
}

auto GetFactory() -> FactoryType*
{
    CHECK_NOTNULL_F(GetBackendInternal());
    return GetBackendInternal()->GetFactory();
}

auto GetMainDevice() -> DeviceType*
{
    CHECK_NOTNULL_F(GetBackendInternal());
    return GetBackendInternal()->GetMainDevice();
}

auto GetRenderer() -> Renderer&
{
    CHECK_NOTNULL_F(GetBackendInternal());
    const auto renderer = static_cast<Renderer*>(GetBackendInternal()->GetRenderer());
    CHECK_NOTNULL_F(renderer);
    return *renderer;
}

auto GetPerFrameResourceManager() -> graphics::PerFrameResourceManager&
{
    return GetRenderer().GetPerFrameResourceManager();
}

auto GetAllocator() -> D3D12MA::Allocator&
{
    auto* allocator = Graphics().GetAllocator();
    DCHECK_NOTNULL_F(allocator);
    return *allocator;
}

} // namespace oxygen::graphics::d3d12::detail

//===----------------------------------------------------------------------===//
// Public implementation of the graphics backend API.

extern "C" __declspec(dllexport) void* GetGraphicsModuleApi()
{
    static oxygen::graphics::GraphicsModuleApi render_module;
    render_module.CreateBackend = CreateBackend;
    render_module.DestroyBackend = DestroyBackend;
    return &render_module;
}

//===----------------------------------------------------------------------===//
// The Graphics class methods

using Microsoft::WRL::ComPtr;
using oxygen::graphics::d3d12::DeviceType;
using oxygen::graphics::d3d12::FactoryType;
using oxygen::graphics::d3d12::Graphics;
using oxygen::graphics::d3d12::NameObject;
using oxygen::windows::ThrowOnFailed;

namespace {
#if defined(_DEBUG)
auto GetDebugLayerInternal() -> oxygen::graphics::d3d12::DebugLayer&
{
    static oxygen::graphics::d3d12::DebugLayer debug_layer {};
    return debug_layer;
}
#endif
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

void InitializeFactory(ComPtr<FactoryType>& factory, const bool enable_debug)
{
    ThrowOnFailed(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)));
    UINT dxgi_factory_flags { 0 };
    if (enable_debug) {
        dxgi_factory_flags = DXGI_CREATE_FACTORY_DEBUG;
    }
    ThrowOnFailed(CreateDXGIFactory2(dxgi_factory_flags, IID_PPV_ARGS(&factory)));
}

auto DiscoverAdapters(
    const ComPtr<FactoryType>& factory,
    const std::function<bool(const AdapterDesc&)>& selector)
    -> std::tuple<ComPtr<IDXGIAdapter1>, size_t>
{
    LOG_SCOPE_FUNCTION(INFO);

    ComPtr<IDXGIAdapter1> selected_adapter;
    size_t selected_adapter_index = std::numeric_limits<size_t>::max();
    ComPtr<IDXGIAdapter1> adapter;

    // Enumerate high-performance adapters only
    for (UINT adapter_index = 0;
        DXGI_ERROR_NOT_FOUND != factory->EnumAdapterByGpuPreference(adapter_index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter));
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
        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)))) {
            adapter_info.meets_feature_level = true;
            adapter_info.max_feature_level = GetMaxFeatureLevel(device);
            if (selector(adapter_info)) {
                // Select the adapter with the most dedicated memory
                if (!selected_adapter
                    || adapter_info.dedicated_memory > adapters.at(selected_adapter_index).dedicated_memory) {
                    selected_adapter = adapter;
                    selected_adapter_index = adapters.size();
                }
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

auto Graphics::GetFactory() const -> FactoryType*
{
    CHECK_NOTNULL_F(factory_, "graphics backend not properly initialized");
    return factory_.Get();
}

auto Graphics::GetMainDevice() const -> DeviceType*
{
    CHECK_NOTNULL_F(main_device_, "graphics backend not properly initialized");
    return main_device_.Get();
}

auto Graphics::CreateImGuiModule(EngineWeakPtr engine, platform::WindowIdType window_id) const -> std::unique_ptr<imgui::ImguiModule>
{
    return std::make_unique<ImGuiModule>(std::move(engine), window_id);
}

void Graphics::InitializeGraphicsBackend(PlatformPtr platform, const GraphicsBackendProperties& props)
{
    LOG_SCOPE_FUNCTION(INFO);
    // Setup the DXGI factory
    InitializeFactory(factory_, props.enable_debug);

    // Discover adapters and select the most suitable one
    const auto [best_adapter, best_adapter_index] = DiscoverAdapters(factory_,
        [](const AdapterDesc& adapter) {
            return adapter.meets_feature_level && adapter.has_connected_display;
        });
    const auto& best_adapter_desc = adapters[best_adapter_index];
    LOG_F(INFO, "Selected adapter: {}", best_adapter_desc.name);

#if defined(_DEBUG)
    // Initialize the Debug Layer and GPU-based validation
    GetDebugLayerInternal().Initialize(props.enable_debug, props.enable_validation);
#endif

    // Create the device with the maximum feature level of the selected adapter
    ThrowOnFailed(
        D3D12CreateDevice(
            best_adapter.Get(),
            best_adapter_desc.max_feature_level,
            IID_PPV_ARGS(&main_device_)));
    NameObject(main_device_.Get(), L"MAIN DEVICE");

    D3D12MA::ALLOCATOR_DESC allocator_desc = {};
    allocator_desc.Flags = D3D12MA::ALLOCATOR_FLAG_NONE;
    allocator_desc.pDevice = main_device_.Get();
    allocator_desc.pAdapter = best_adapter.Get();
    ThrowOnFailed(CreateAllocator(&allocator_desc, &allocator_));
    LOG_F(INFO, "D3D12MA Memory Allocator initialized");
}

void Graphics::ShutdownGraphicsBackend()
{
    LOG_SCOPE_FUNCTION(INFO);

    ObjectRelease(allocator_);
    allocator_ = nullptr;
    LOG_F(INFO, "D3D12MA Memory Allocator released");

    factory_.Reset();
    LOG_F(INFO, "D3D12 DXGI Factory reset");

    CHECK_EQ_F(main_device_.Reset(), 0U);
    LOG_F(INFO, "D3D12 Main Device reset");

#if defined(_DEBUG)
    GetDebugLayerInternal().Shutdown();
#endif
}

auto Graphics::CreateRenderer() -> std::unique_ptr<graphics::Renderer>
{
    return std::make_unique<Renderer>();
}
