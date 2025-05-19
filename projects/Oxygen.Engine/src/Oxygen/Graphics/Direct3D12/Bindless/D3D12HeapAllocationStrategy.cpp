//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Base/NoStd.h"

#include <array>
#include <format>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Direct3D12/Bindless/D3D12HeapAllocationStrategy.h>

namespace oxygen::graphics::d3d12 {

namespace {

    constexpr uint32_t kDefaultCbvSrvUavCapacity = 1000000;
    constexpr uint32_t kDefaultSamplerCapacity = 2048;
    constexpr uint32_t kDefaultRtvCapacity = 1024;
    constexpr uint32_t kDefaultDsvCapacity = 1024;

    struct HeapConfig {
        D3D12_DESCRIPTOR_HEAP_TYPE type;
        bool shader_visible;
        bool allow_shader_visible;
        uint32_t (*capacity_func)(dx::IDevice*);
    };

    auto GetCbvSrvUavCapacity(dx::IDevice* device) -> uint32_t
    {
        if (!device)
            return kDefaultCbvSrvUavCapacity;
        D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
        if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options)))) {
            // TODO: use tier-based capacity
            return kDefaultCbvSrvUavCapacity;
        }
        return kDefaultCbvSrvUavCapacity;
    }

    auto GetSamplerCapacity(dx::IDevice*) -> uint32_t { return kDefaultSamplerCapacity; }
    auto GetRtvCapacity(dx::IDevice*) -> uint32_t { return kDefaultRtvCapacity; }
    auto GetDsvCapacity(dx::IDevice*) -> uint32_t { return kDefaultDsvCapacity; }

    constexpr std::array<HeapConfig, 6> kHeapConfigs { {
        { .type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, .shader_visible = false, .allow_shader_visible = true, .capacity_func = GetCbvSrvUavCapacity },
        { .type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, .shader_visible = true, .allow_shader_visible = true, .capacity_func = GetCbvSrvUavCapacity },
        { .type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, .shader_visible = false, .allow_shader_visible = true, .capacity_func = GetSamplerCapacity },
        { .type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, .shader_visible = true, .allow_shader_visible = true, .capacity_func = GetSamplerCapacity },
        { .type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV, .shader_visible = false, .allow_shader_visible = false, .capacity_func = GetRtvCapacity },
        { .type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV, .shader_visible = false, .allow_shader_visible = false, .capacity_func = GetDsvCapacity },
    } };

} // namespace

D3D12HeapAllocationStrategy::D3D12HeapAllocationStrategy(dx::IDevice* device)
{
    DescriptorHandle::IndexT base_index = 0;
    for (const auto& cfg : kHeapConfigs) {
        // Only create shader-visible heaps if allowed by D3D12
        if (cfg.shader_visible && !cfg.allow_shader_visible)
            continue;

        auto key = BuildHeapKey(cfg.type, cfg.shader_visible);
        const auto capacity = cfg.capacity_func(device);

        HeapDescription desc {};
        if (cfg.shader_visible) {
            desc.shader_visible_capacity = static_cast<DescriptorHandle::IndexT>(capacity);
        } else {
            desc.cpu_visible_capacity = static_cast<DescriptorHandle::IndexT>(capacity);
        }
        desc.allow_growth = false;
        desc.growth_factor = 0.0f;
        desc.max_growth_iterations = 0;

        heap_descriptions_[key] = desc;
        heap_base_indices_[key] = base_index;
        base_index += capacity;
    }
    DLOG_F(INFO, "Initialized D3D12HeapAllocationStrategy with {} heap configurations", heap_descriptions_.size());
}

auto D3D12HeapAllocationStrategy::GetHeapKey(const ResourceViewType view_type, const DescriptorVisibility visibility) const
    -> std::string
{
    // Enforce only valid ResourceViewType and DescriptorVisibility combinations
    if (!IsValid(view_type) || !IsValid(visibility)) {
        throw std::runtime_error("Invalid ResourceViewType or DescriptorVisibility for D3D12HeapAllocationStrategy::GetHeapKey");
    }

    const auto heap_type = GetHeapType(view_type);
    const bool can_be_shader_visible = // Only CBV_SRV_UAV and SAMPLER can be shader-visible
        (heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
            || heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

    // RTV/DSV can only be CPU-only
    if ((heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV || heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_DSV) && visibility == DescriptorVisibility::kShaderVisible) {
        throw std::runtime_error("RTV/DSV cannot be shader-visible in D3D12HeapAllocationStrategy::GetHeapKey");
    }

    // Only CBV_SRV_UAV and SAMPLER can be shader-visible
    if (!can_be_shader_visible && visibility == DescriptorVisibility::kShaderVisible) {
        throw std::runtime_error("Only CBV_SRV_UAV and SAMPLER can be shader-visible in D3D12HeapAllocationStrategy::GetHeapKey");
    }

    const bool shader_visible = visibility == DescriptorVisibility::kShaderVisible;
    return BuildHeapKey(heap_type, shader_visible);
}

auto D3D12HeapAllocationStrategy::GetHeapDescription(const std::string& heap_key) const
    -> const HeapDescription&
{
    const auto it = heap_descriptions_.find(heap_key);
    if (it == heap_descriptions_.end()) {
        throw std::runtime_error(fmt::format("Invalid D3D12 heap key: {}", heap_key));
    }
    return it->second;
}

auto D3D12HeapAllocationStrategy::GetHeapBaseIndex(const ResourceViewType view_type, const DescriptorVisibility visibility) const
    -> DescriptorHandle::IndexT
{
    const auto key = GetHeapKey(view_type, visibility);
    const auto it = heap_base_indices_.find(key);
    if (it == heap_base_indices_.end()) {
        DLOG_F(WARNING, "No base index found for heap key: {}, using 0", key);
        return 0;
    }
    return it->second;
}

auto D3D12HeapAllocationStrategy::GetHeapType(const ResourceViewType view_type) noexcept
    -> D3D12_DESCRIPTOR_HEAP_TYPE
{
    switch (view_type) {
    case ResourceViewType::kTexture_SRV:
    case ResourceViewType::kTexture_UAV:
    case ResourceViewType::kTypedBuffer_SRV:
    case ResourceViewType::kTypedBuffer_UAV:
    case ResourceViewType::kStructuredBuffer_SRV:
    case ResourceViewType::kStructuredBuffer_UAV:
    case ResourceViewType::kRawBuffer_SRV:
    case ResourceViewType::kRawBuffer_UAV:
    case ResourceViewType::kConstantBuffer:
    case ResourceViewType::kRayTracingAccelStructure:
        return D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    case ResourceViewType::kSampler:
    case ResourceViewType::kSamplerFeedbackTexture_UAV:
        return D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    case ResourceViewType::kTexture_RTV:
        return D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    case ResourceViewType::kTexture_DSV:
        return D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    case ResourceViewType::kNone:
    case ResourceViewType::kMaxResourceViewType:
    default: // NOLINT(clang-diagnostic-covered-switch-default)
        ABORT_F("Illegal ResourceViewType `{}` used to GetHeapType()",
            nostd::to_string(view_type));
    }
}

auto D3D12HeapAllocationStrategy::BuildHeapKey(const D3D12_DESCRIPTOR_HEAP_TYPE type, const bool shader_visible) -> std::string
{
    std::string type_str;
    switch (type) {
    case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
        type_str = "CBV_SRV_UAV";
        break;
    case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:
        type_str = "SAMPLER";
        break;
    case D3D12_DESCRIPTOR_HEAP_TYPE_RTV:
        type_str = "RTV";
        break;
    case D3D12_DESCRIPTOR_HEAP_TYPE_DSV:
        type_str = "DSV";
        break;
    default:
        type_str = "UNKNOWN";
        break;
    }
    return std::format("{}:{}", type_str, shader_visible ? "gpu" : "cpu");
}

} // namespace oxygen::graphics::d3d12
