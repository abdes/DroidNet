//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <exception>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <fmt/format.h>

#include "DeviceManager.h"
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/StringUtils.h>
#include <Oxygen/Base/Unreachable.h>
#include <Oxygen/Base/Windows/ComError.h> // needed for ThrowOnFailed
#include <Oxygen/Graphics/Direct3D12/Detail/Types.h>
#include <Oxygen/Graphics/Direct3D12/Devices/DebugLayer.h>
#include <Oxygen/Graphics/Direct3D12/Devices/DeviceManager.h>

using Microsoft::WRL::ComPtr;
using oxygen::graphics::d3d12::AdapterInfo;
using oxygen::graphics::d3d12::DeviceManager;
using oxygen::windows::ThrowOnFailed;

namespace {

//! A helper function to format memory size in human-readable format.
auto FormatMemorySize(const size_t memory_size) -> std::string
{
    constexpr size_t kBitsPerGigabyte = 30;
    constexpr size_t kBitsPerMegabyte = 20;

    if (memory_size >= (1ULL << kBitsPerGigabyte)) {
        return fmt::format("{:.2f} GB",
            static_cast<double>(memory_size) / (1ULL << kBitsPerGigabyte));
    }
    return fmt::format("{:.2f} MB",
        static_cast<double>(memory_size) / (1ULL << kBitsPerMegabyte));
}

auto GetAdapterName(const DXGI_ADAPTER_DESC1& desc)
{
    std::string description {};

    // Get array size from DXGI_ADAPTER_DESC1 structure at compile time
    constexpr size_t kDescriptionLength = std::extent_v<decltype(DXGI_ADAPTER_DESC1::Description)>;
    // Verify string is null-terminated within the array bounds
    DCHECK_NOTNULL_F(std::char_traits<wchar_t>::find(&desc.Description[0], kDescriptionLength, L'\0'),
        "Adapter description is not null-terminated");

    std::span<const wchar_t> desc_span(desc.Description);
    oxygen::string_utils::WideToUtf8(desc_span.data(), description);
    return description;
}

auto CheckConnectedDisplay(const ComPtr<IDXGIAdapter1>& adapter) -> bool
{
    ComPtr<IDXGIOutput> output;
    return SUCCEEDED(adapter->EnumOutputs(0, &output));
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
        oxygen::Unreachable();
    }
}

auto GetMaxFeatureLevel(const ComPtr<oxygen::graphics::d3d12::dx::IDevice>& device) -> D3D_FEATURE_LEVEL
{
    static constexpr std::array feature_levels {
        D3D_FEATURE_LEVEL_12_2,
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };

    D3D12_FEATURE_DATA_FEATURE_LEVELS feature_level_info = {};
    feature_level_info.NumFeatureLevels = static_cast<UINT>(feature_levels.size());
    feature_level_info.pFeatureLevelsRequested = feature_levels.data();

    if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &feature_level_info, sizeof(feature_level_info)))) {
        return feature_level_info.MaxSupportedFeatureLevel;
    }

    return D3D_FEATURE_LEVEL_11_0;
}

} // namespace

auto AdapterInfo::MemoryAsString() const -> std::string
{
    return FormatMemorySize(memory);
}

void DeviceManager::InitializeFactory()
{
    UINT dxgi_factory_flags { 0 };
    if (props_.enable_debug) {
        dxgi_factory_flags = DXGI_CREATE_FACTORY_DEBUG;
    }
    ThrowOnFailed(CreateDXGIFactory2(dxgi_factory_flags, IID_PPV_ARGS(&factory_)));
}

void DeviceManager::DiscoverAdapters()
{
    DCHECK_NOTNULL_F(factory_, "DXGI factory not initialized");
    LOG_SCOPE_F(INFO, "Discover adapters");

    contexts_.clear(); // Clear any previous entries

    int best_score = -1;
    size_t best_adapter_index { 0 };

    try {
        // Enumerate high-performance adapters only
        ComPtr<dxgi::IAdapter> adapter;
        UINT adapter_index = 0;
        while (SUCCEEDED(factory_->EnumAdapterByGpuPreference(
            adapter_index,
            DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
            IID_PPV_ARGS(&adapter)))) {

            DXGI_ADAPTER_DESC1 desc;
            ThrowOnFailed(adapter->GetDesc1(&desc));

            if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0U) {
                // Don't select the Basic Render Driver adapter.
                ++adapter_index;
                continue;
            }

            auto adapter_name = GetAdapterName(desc);

            // Check min and max feature levels
            auto meets_feature_level { false };
            D3D_FEATURE_LEVEL max_feature_level { D3D_FEATURE_LEVEL_11_0 };
            try {
                ComPtr<dx::IDevice> device;
                ThrowOnFailed(D3D12CreateDevice(adapter.Get(), props_.minFeatureLevel, IID_PPV_ARGS(&device)));
                meets_feature_level = true;
                max_feature_level = GetMaxFeatureLevel(device);
            } catch (std::exception& ex) {
                LOG_F(ERROR, "failed to check adapter `{}` feature level: {}", adapter_name, ex.what());
            }

            AdapterInfo adapter_info(
                std::move(adapter_name),
                desc.VendorId,
                desc.DeviceId,
                desc.DedicatedVideoMemory,
                CheckConnectedDisplay(adapter),
                meets_feature_level,
                max_feature_level,
                desc.AdapterLuid);

            // Score the new adapter
            auto score = GetAdapterScore(adapter_info);
            if (score > best_score) {
                best_score = score;
                best_adapter_index = contexts_.size();
            }

            contexts_.emplace_back(std::move(adapter_info), adapter);
            ++adapter_index;
        }
    } catch (std::exception ex) {
        LOG_F(ERROR, "discovery cancelled due to exception: {}", ex.what());
        contexts_.clear();
        throw;
    }

    DCHECK_F(best_adapter_index >= 0 && best_adapter_index < contexts_.size(),
        "Best adapter index out of bounds");

    if (!contexts_.empty()) {
        contexts_[best_adapter_index].info.is_best = true;
    }

    std::ranges::for_each(
        Adapters(),
        [](const AdapterInfo& a) {
            LOG_F(INFO, "[{}] {} {} ({}-{})", "+", a.Name(), a.MemoryAsString(), a.VendorId(), a.DeviceId());
            LOG_F(1, "  Meets Feature Level   : {}", a.MeetsFeatureLevel());
            LOG_F(1, "  Has Connected Display : {}", a.IsConnectedToDisplay());
            LOG_F(1, "  Max Feature Level     : {}", FeatureLevelToString(a.MaxFeatureLevel()));
            LOG_F(1, "  Is Best Adapter       : {}", a.IsBest());
        });
}

auto DeviceManager::GetAdapterScore(AdapterInfo& adapter) const -> int
{
    int score = 0;

    // Score based on feature level
    if (adapter.MeetsFeatureLevel()) {
        score += 1;
        score += static_cast<int>(adapter.MaxFeatureLevel()) - static_cast<int>(props_.minFeatureLevel);
    }

    // Score based on display connection
    if (props_.require_display && adapter.IsConnectedToDisplay()) {
        score += 1;
    }

    // Score based on dedicated memory
    constexpr int kMegaShift = 20; // 1 megabyte (MB) is equal to 1,048,576 bytes (2^20 bytes)
    score += static_cast<int>(adapter.Memory() / (static_cast<size_t>(1) << kMegaShift)); // Convert bytes to MB

    return score;
}

auto DeviceManager::SelectAdapter(const Context& adapter) -> bool
{
    LOG_SCOPE_FUNCTION(INFO);

    if (&adapter != current_context_) {
        // Find the context that matches the provided adapter
        auto it = std::ranges::find_if(contexts_, [&](const Context& context) {
            return &context == &adapter;
        });
        CHECK_F(it != contexts_.end());
        auto* new_context = &(*it);
        LOG_F(INFO, "requested: {} ({}healthy)", new_context->info.Name(), new_context->IsHealthy() ? "" : "not ");
        if (new_context->IsHealthy() || InitializeContext(*new_context)) {
            current_context_ = new_context;
            return true;
        }
        return false;
    }
    return current_context_->IsHealthy() && CheckForDeviceLoss();
}

auto DeviceManager::InitializeContext(Context& context) const -> bool
{
    LOG_SCOPE_F(INFO, "Setup Context");

    try {
        // Initialize the device
        LOG_F(INFO, "Device");
        ThrowOnFailed(D3D12CreateDevice(
            context.adapter.Get(),
            props_.minFeatureLevel,
            IID_PPV_ARGS(&context.device)));

        // Initialize the allocator
        LOG_F(INFO, "Memory Allocator");
        D3D12MA::ALLOCATOR_DESC allocatorDesc = {};
        allocatorDesc.pDevice = context.device.Get();
        allocatorDesc.pAdapter = context.adapter.Get();
        ThrowOnFailed(D3D12MA::CreateAllocator(&allocatorDesc, &context.allocator));

        // Initialize the command queues
        LOG_F(INFO, "Command Queues");
        context.commandQueues_.clear();
        for (D3D12_COMMAND_LIST_TYPE type : {
                 D3D12_COMMAND_LIST_TYPE_DIRECT,
                 D3D12_COMMAND_LIST_TYPE_COMPUTE,
                 D3D12_COMMAND_LIST_TYPE_COPY }) {
            ComPtr<ID3D12CommandQueue> commandQueue;
            D3D12_COMMAND_QUEUE_DESC queueDesc = {};
            queueDesc.Type = type;
            queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
            queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
            queueDesc.NodeMask = 0;
            ThrowOnFailed(context.device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue)));
            context.commandQueues_.emplace_back(std::move(commandQueue));
        }
        return true;
    } catch (std::exception& ex) {
        LOG_F(ERROR, "Context initialization failed: {}", ex.what());
        // Roolback the context
        context.device.Reset();
        context.allocator.Reset();
        context.commandQueues_.clear();
        return false;
    }
}

auto DeviceManager::SelectBestAdapter() -> bool
{
    // Find the best adapter
    auto it = std::ranges::find_if(contexts_, [](const Context& context) {
        return context.info.IsBest();
    });

    if (it != contexts_.end()) {
        return SelectAdapter(*it);
    }
    LOG_F(ERROR, "No suitable adapter found.");
    return false;
}

auto DeviceManager::Context::IsHealthy() const -> bool
{
    DCHECK_F(adapter != nullptr, "context is bad");
    if (device == nullptr) {
        DCHECK_F(allocator == nullptr, "context was partially cleaned-up");
        DCHECK_F(commandQueues_.empty(), "context was partially cleaned-up");
        return false;
    }
    CHECK_F(allocator != nullptr, "context is partially initialized");
    CHECK_F(!commandQueues_.empty(), "context is partially initialized");
    return true;
}

auto DeviceManager::CheckForDeviceLoss() -> bool
{
    DCHECK_F(current_context_->IsHealthy(), "context is not healthy");

    // Check for device removal
    HRESULT removedReason = current_context_->device->GetDeviceRemovedReason();
    if (SUCCEEDED(removedReason)) {
        return true;
    }

    LOG_F(ERROR, "Device removed (reason: 0x{:08X})", static_cast<uint32_t>(removedReason));
    if (debug_layer_) {
        debug_layer_->PrintDredReport();
    }
    return RecoverFromDeviceLoss();
}

auto DeviceManager::RecoverFromDeviceLoss() -> bool
{
    LOG_SCOPE_FUNCTION(INFO);

    // TODO(abdes) Implement device loss notification A good compromise we are
    // making is to only check for removal when the a context is selected. An
    // application can pretty much do that at every frame or more frequently if
    // needed. We can modify the SelectAdapter methods to accept an optional
    // callable indicating that the application wants to react to device
    // removal.

    // NotifyDeviceLost(current_context_->info.luid);

    // Reset the context
    current_context_->device.Reset();
    current_context_->allocator.Reset();
    current_context_->commandQueues_.clear();

    return InitializeContext(*current_context_);
}

DeviceManager::DeviceManager(DeviceManagerDesc desc)
    : props_(desc)
{
    LOG_SCOPE_F(INFO, "DeviceManager init");

    InitializeFactory();

#if defined(_DEBUG)
    if (props_.enable_debug) {
        // Initialize the Debug Layer and GPU-based validation
        debug_layer_ = std::make_unique<DebugLayer>(props_.enable_validation);
    }
#endif

    DiscoverAdapters();

    if (props_.auto_select_adapter) {
        SelectBestAdapter();
    }
}

DeviceManager::~DeviceManager()
{
    LOG_SCOPE_F(INFO, "DeviceManager cleanup");
    contexts_.clear();
    debug_layer_.reset();
}

auto DeviceManager::GetCommandQueue(D3D12_COMMAND_LIST_TYPE type) const -> dx::ICommandQueue*
{
    if (current_context_ == nullptr) {
        throw std::runtime_error("No adapter selected.");
    }

    auto it = std::ranges::find_if(current_context_->commandQueues_,
        [type](const ComPtr<ID3D12CommandQueue>& queue) {
            auto desc = queue->GetDesc();
            return desc.Type == type;
        });

    if (it != current_context_->commandQueues_.end()) {
        return it->Get();
    }
    throw std::runtime_error("Command queue not found.");
}

auto DeviceManager::Device() const -> dx::IDevice*
{
    if (current_context_ == nullptr) {
        throw std::runtime_error("No adapter selected.");
    }
    return current_context_->device.Get();
}

auto DeviceManager::Allocator() const -> D3D12MA::Allocator*
{
    if (current_context_ == nullptr) {
        throw std::runtime_error("No adapter selected.");
    }
    return current_context_->allocator.Get();
}
