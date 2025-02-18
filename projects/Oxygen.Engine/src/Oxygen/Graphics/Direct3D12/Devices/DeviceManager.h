//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <ranges>
#include <string>
#include <type_traits>
#include <vector>

#include <d3d12.h>
#include <d3dcommon.h>
#include <winnt.h>
#include <wrl/client.h>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/ComponentMacros.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Graphics/Direct3D12/D3D12MemAlloc.h>
#include <Oxygen/Graphics/Direct3D12/Detail/Types.h>
#include <Oxygen/Graphics/Direct3D12/Devices/DebugLayer.h>
#include <Oxygen/Graphics/Direct3D12/api_export.h>

auto operator==(const LUID& lhs, const LUID& rhs) -> bool
{
    return (lhs.LowPart == rhs.LowPart) && (lhs.HighPart == rhs.HighPart);
}

auto operator!=(const LUID& lhs, const LUID& rhs) -> bool
{
    return !(lhs == rhs);
}

namespace oxygen::graphics::d3d12 {

class DeviceManager;

class AdapterInfo {
public:
    friend class DeviceManager;

    [[nodiscard]] auto Name() const -> auto& { return name; }
    [[nodiscard]] auto UniqueId() const { return luid; }
    [[nodiscard]] auto VendorId() const { return vendor_id; }
    [[nodiscard]] auto DeviceId() const { return device_id; }
    [[nodiscard]] auto Memory() const { return memory; }
    [[nodiscard]] auto MemoryAsString() const -> std::string;
    [[nodiscard]] auto IsConnectedToDisplay() const { return has_display; }
    [[nodiscard]] auto MeetsFeatureLevel() const { return meets_feature_level; }
    [[nodiscard]] auto MaxFeatureLevel() const { return max_feature_level; }
    [[nodiscard]] auto IsBest() const { return is_best; }

private:
    // The language does not offer a lot of choice here. We want this to be a
    // POD type, that is read-only once created, and it can only be created by
    // the DeviceManager. It would have been nice if we could use designated
    // initializer, which would avoid the confusion of the order of the
    // parameters. But we cannot.
    AdapterInfo(
        std::string name,
        uint32_t vendor_id, // NOLINT(*-easily-swappable-parameters)
        uint32_t device_id,
        size_t memory,
        bool has_display,
        bool meets_feature_level,
        D3D_FEATURE_LEVEL max_feature_level,
        LUID luid)
        : name(std::move(name))
        , vendor_id(vendor_id)
        , device_id(device_id)
        , memory(memory)
        , has_display(has_display)
        , meets_feature_level(meets_feature_level)
        , max_feature_level(max_feature_level)
        , luid(luid)
    {
    }

    std::string name;
    uint32_t vendor_id;
    uint32_t device_id;
    size_t memory;
    bool has_display;
    bool meets_feature_level;
    D3D_FEATURE_LEVEL max_feature_level;
    LUID luid;
    bool is_best { false };
};

template <typename Callable>
concept AdapterPredicate = requires(Callable c, const AdapterInfo& a) {
    { c(a) } -> std::convertible_to<bool>;
};

struct DeviceManagerDesc {
    bool enable_debug {
#if defined(NDEBUG)
        false
#else
        true
#endif
    };
    bool enable_validation { false };
    bool require_display { true };
    bool auto_select_adapter { true };
    D3D_FEATURE_LEVEL minFeatureLevel;
};

class DeviceManager final
    : public std::enable_shared_from_this<DeviceManager>,
      public Component {
    OXYGEN_COMPONENT(DeviceManager)

public:
    OXYGEN_D3D12_API explicit DeviceManager(DeviceManagerDesc desc);
    OXYGEN_D3D12_API ~DeviceManager() override;

    OXYGEN_MAKE_NON_COPYABLE(DeviceManager)
    OXYGEN_MAKE_NON_MOVABLE(DeviceManager)

    // Special tag to indicate re-discovery of adapters
    struct ReDiscoverTag {
        constexpr ReDiscoverTag() = default;
    };

    static constexpr ReDiscoverTag re_discover {};

    [[nodiscard]] auto Factory() const -> dx::IFactory* { return factory_.Get(); }
    [[nodiscard]] auto Device() const -> dx::IDevice*;
    [[nodiscard]] auto Allocator() const -> D3D12MA::Allocator*;

    [[nodiscard]] auto Adapters() const
    {
        return contexts_
            | std::ranges::views::transform(
                [](const Context& context) -> const AdapterInfo& {
                    return context.info;
                });
    }

    [[nodiscard]] auto Adapters(ReDiscoverTag /*tag*/)
    {
        DiscoverAdapters();
        return Adapters();
    }

    auto SelectAdapter(AdapterPredicate auto&& criteria)
    {
        bool found = std::ranges::any_of(contexts_,
            [&](const Context& context) {
                if (criteria(context.info)) {
                    SelectAdapter(context);
                    return true;
                }
                return false;
            });

        return found && current_context_->IsHealthy();
    }

    auto SelectAdapter(const LUID& luid)
    {
        if (current_context_ != nullptr && current_context_->info.luid == luid) {
            return true;
        }
        return SelectAdapter([&](const AdapterInfo& adapter) -> bool {
            return adapter.luid == luid;
        });
    }

    OXYGEN_D3D12_API auto SelectBestAdapter() -> bool;

    [[nodiscard]] auto GetCommandQueue(D3D12_COMMAND_LIST_TYPE type) const -> dx::ICommandQueue*;

    // TODO(abdes) Implement feature check support in device manager
    [[nodiscard]] auto CheckFeatureSupport(D3D12_FEATURE feature) const -> bool;

private:
    void InitializeFactory();
    void DiscoverAdapters();

    struct Context {
        AdapterInfo info;

        Microsoft::WRL::ComPtr<dxgi::IAdapter> adapter;
        Microsoft::WRL::ComPtr<dx::IDevice> device;
        Microsoft::WRL::ComPtr<D3D12MA::Allocator> allocator;

        std::vector<Microsoft::WRL::ComPtr<dx::ICommandQueue>> commandQueues_;

        Context(AdapterInfo info, Microsoft::WRL::ComPtr<dxgi::IAdapter> adapter)
            : info(std::move(info))
            , adapter(std::move(adapter))
        {
        }

        [[nodiscard]] OXYGEN_D3D12_API auto IsHealthy() const -> bool;
    };

    OXYGEN_D3D12_API auto SelectAdapter(const Context& adapter) -> bool;
    auto GetAdapterScore(AdapterInfo& adapter) const -> int;
    auto InitializeContext(Context& context) const -> bool;
    [[nodiscard]] auto CheckForDeviceLoss() -> bool;
    auto RecoverFromDeviceLoss() -> bool;

    DeviceManagerDesc props_;

    Microsoft::WRL::ComPtr<dx::IFactory> factory_;

    Context* current_context_ { nullptr };
    std::vector<Context> contexts_;
    std::unique_ptr<DebugLayer> debug_layer_;
};

} // namespace oxygen::graphics::d3d12
