//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <concepts>
#include <cstdint>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include <Windows.h>
#include <d3d12.h>
#include <d3dcommon.h>
#include <wrl/client.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/ComponentMacros.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Graphics/Direct3D12/Detail/Types.h>
#include <Oxygen/Graphics/Direct3D12/api_export.h>

auto operator==(const LUID& lhs, const LUID& rhs) -> bool
{
    return (lhs.LowPart == rhs.LowPart) && (lhs.HighPart == rhs.HighPart);
}

auto operator!=(const LUID& lhs, const LUID& rhs) -> bool
{
    return !(lhs == rhs);
}

namespace D3D12MA {
class Allocator; // NOLINT
} // namespace D3D12MA

namespace oxygen::graphics::d3d12 {

class DebugLayer;
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

template <typename Callable>
concept DeviceRemovalHandler = std::is_same_v<Callable, std::nullptr_t> || requires(Callable c) {
    { std::invoke(c) } -> std::same_as<void>;
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

class DeviceManager final : public Component {
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

    [[nodiscard]] auto Device() const -> dx::IDevice*
    {
        if (current_context_ == nullptr) {
            throw std::runtime_error("No adapter selected.");
        }
        return current_context_->device.Get();
    }

    [[nodiscard]] auto Allocator() const -> D3D12MA::Allocator*
    {
        if (current_context_ == nullptr) {
            throw std::runtime_error("No adapter selected.");
        }
        return current_context_->allocator.Get();
    }

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
    template <AdapterPredicate Pred, DeviceRemovalHandler Handler = std::nullptr_t>
    auto SelectAdapter(Pred&& criteria, Handler&& on_device_removed = nullptr) -> bool
    {
        // Find the context matching the criteria
        auto it = std::ranges::find_if(contexts_,
            [&](const Context& context) {
                return std::forward<Pred>(criteria)(context.info);
            });

        if (it == contexts_.end()) {
            return false;
        }

        return SelectAdapter(*it, std::forward<Handler>(on_device_removed));
    }

    template <DeviceRemovalHandler Handler = std::nullptr_t>
    auto SelectAdapter(LUID luid, Handler&& on_device_removed = nullptr) -> bool
    {
        // Optimize the case of re-selecting the current context
        if (current_context_ != nullptr && current_context_->info.luid == luid) {
            return SelectAdapter(*current_context_, std::forward<Handler>(on_device_removed));
        }

        // Otherwise, find and select the requested adapter
        return SelectAdapter(
            [&](const AdapterInfo& adapter) {
                return adapter.UniqueId() == luid;
            },
            std::forward<Handler>(on_device_removed));
    }

    template <DeviceRemovalHandler Handler = std::nullptr_t>
    auto SelectBestAdapter(Handler&& on_device_removed = nullptr) -> bool
    {
        // Optimize the case of re-selecting the current context
        if (current_context_ != nullptr && current_context_->info.IsBest()) {
            return SelectAdapter(*current_context_, std::forward<Handler>(on_device_removed));
        }

        // Otherwise, find and select the requested adapter
        return SelectAdapter(
            [&](const AdapterInfo& adapter) {
                return adapter.IsBest();
            },
            std::forward<Handler>(on_device_removed));
    }

    [[nodiscard]] auto GetCommandQueue(D3D12_COMMAND_LIST_TYPE type) const -> dx::ICommandQueue*;

    // TODO(abdes) Implement feature check support in device manager
    [[nodiscard]] auto CheckFeatureSupport(D3D12_FEATURE feature) const -> bool;

private:
    struct Context {
        AdapterInfo info;

        Microsoft::WRL::ComPtr<dxgi::IAdapter> adapter;
        Microsoft::WRL::ComPtr<dx::IDevice> device;
        Microsoft::WRL::ComPtr<D3D12MA::Allocator> allocator;

        std::vector<Microsoft::WRL::ComPtr<dx::ICommandQueue>> commandQueues_;

        Context(AdapterInfo info, Microsoft::WRL::ComPtr<dxgi::IAdapter> adapter);
        ~Context() noexcept;

        OXYGEN_MAKE_NON_COPYABLE(Context)
        OXYGEN_DEFAULT_MOVABLE(Context)

        [[nodiscard]] OXYGEN_D3D12_API auto IsActive() const -> bool;
    };

    void InitializeFactory();
    void DiscoverAdapters();
    auto GetAdapterScore(AdapterInfo& adapter) const -> int;

    template <DeviceRemovalHandler Handler = std::nullptr_t>
    auto SelectAdapter(Context& new_context, Handler&& on_device_removed = nullptr) -> bool
    {
        LOG_SCOPE_FUNCTION(INFO);

        LOG_F(INFO, "requested: {}", new_context.info.Name());

        // If the context we want to select is not active, initialize it
        auto context_is_active = new_context.IsActive();
        LOG_IF_F(INFO, !context_is_active, "(not active)");
        if (!context_is_active) {
            if (!InitializeContext(new_context)) {
                return false;
            }
        }
        DCHECK_F(new_context.IsActive());

        // If we had a device loss, attempt to recover before you select the
        // context
        if (CheckForDeviceLoss(new_context)) {
            // Invoke the handler if provided before we attempt the recovery
            if constexpr (!std::is_same_v<Handler, std::nullptr_t>) {
                std::invoke(std::forward<Handler>(on_device_removed));
            }
            if (!RecoverFromDeviceLoss(new_context)) {
                return false;
            }
        }
        DCHECK_F(new_context.IsActive());

        current_context_ = &new_context;
        return true;
    }

    [[nodiscard]] OXYGEN_D3D12_API auto InitializeContext(Context& context) const -> bool;
    [[nodiscard]] OXYGEN_D3D12_API auto CheckForDeviceLoss(const Context& context) -> bool;
    [[nodiscard]] OXYGEN_D3D12_API auto RecoverFromDeviceLoss(Context& context) -> bool;

    DeviceManagerDesc props_;

    Microsoft::WRL::ComPtr<dx::IFactory> factory_;

    Context* current_context_ { nullptr };
    std::vector<Context> contexts_;
    std::unique_ptr<DebugLayer> debug_layer_;
};

} // namespace oxygen::graphics::d3d12
