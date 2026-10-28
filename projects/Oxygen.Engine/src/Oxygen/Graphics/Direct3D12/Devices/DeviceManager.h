//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Windows.h> // IWYU pragma: keep

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include <d3d12.h>
#include <d3dcommon.h>
#include <wrl/client.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/Component.h>
#include <Oxygen/Config/GraphicsConfig.h>
#include <Oxygen/Graphics/Direct3D12/Detail/Types.h>
#include <Oxygen/Graphics/Direct3D12/Devices/DeviceFeatures.h>
#include <Oxygen/Graphics/Direct3D12/api_export.h>

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
  AdapterInfo(std::string name,
    uint32_t vendor_id, // NOLINT(*-easily-swappable-parameters)
    uint32_t device_id, size_t memory, bool has_display,
    bool meets_feature_level, D3D_FEATURE_LEVEL max_feature_level)
    : name(std::move(name))
    , vendor_id(vendor_id)
    , device_id(device_id)
    , memory(memory)
    , has_display(has_display)
    , meets_feature_level(meets_feature_level)
    , max_feature_level(max_feature_level)
  {
  }

  std::string name;
  uint32_t vendor_id;
  uint32_t device_id;
  size_t memory;
  bool has_display;
  bool meets_feature_level;
  D3D_FEATURE_LEVEL max_feature_level;
  bool is_best { false };
};

//! One hardware adapter as `DeviceManager::ListAdapters` reports it.
struct AdapterReport {
  AdapterInfo info;
  //! Queried with a short-lived device; none when no device could be created.
  std::optional<DeviceFeatures> features;
  //! Why the adapter cannot be selected; empty when it qualifies.
  std::vector<std::string> shortfalls;
};

template <typename Callable>
concept DeviceRemovalHandler
  = std::is_same_v<std::remove_cvref_t<Callable>, std::nullptr_t>
  || requires(Callable c) {
       { std::invoke(c) } -> std::same_as<void>;
     };

struct DeviceManagerDesc {
  bool enable_debug_layer { DefaultGraphicsDebugLayerEnabled() };
  bool enable_validation { false };
  //! Device Removed Extended Data with the debug layer: breadcrumbs, contexts
  //! and page faults for removal reports, at a cost on every command list.
  bool enable_dred { true };
  bool enable_aftermath { DefaultGraphicsAftermathEnabled() };
  FrameCaptureConfig frame_capture {};
  //! Adapter selection, ranked as `AdapterSelectionPolicy` states: a usable
  //! adapter matching a hint first, then, among adapters that drive a display
  //! when `require_display` is set, the best score.
  bool require_display { false };
  bool auto_select_adapter { true };
  std::optional<std::string> preferred_card_name;
  std::optional<DeviceId> preferred_card_device_id;
  DeviceRequirements requirements {};
};

class DeviceManager final : public Component {
  OXYGEN_COMPONENT(DeviceManager)

public:
  OXGN_D3D12_API explicit DeviceManager(DeviceManagerDesc desc);
  OXGN_D3D12_API ~DeviceManager() override;

  OXYGEN_MAKE_NON_COPYABLE(DeviceManager)
  OXYGEN_MAKE_NON_MOVABLE(DeviceManager)

  //! The hardware adapters with their features and shortfalls.
  /*!
   Each adapter at the minimum feature level is probed with a short-lived
   device. The adapter marked best is the one `SelectBestAdapter` would pick:
   the first ranked adapter that meets every requirement. The tooling policy is
   left alone.

   @param desc The selection policy and requirements to report against.
   @return The adapters in enumeration order.
   @throw oxygen::windows::ComError When the DXGI factory cannot be created.
  */
  OXGN_D3D12_NDAPI static auto ListAdapters(const DeviceManagerDesc& desc)
    -> std::vector<AdapterReport>;

  [[nodiscard]] auto Factory() const -> dx::IFactory* { return factory_.Get(); }

  //! Keep diagnostics active until the last retained native allocation retires.
  [[nodiscard]] auto ShareDebugLayer() const noexcept
    -> std::shared_ptr<DebugLayer>
  {
    return debug_layer_;
  }

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

  //! The features of the selected device.
  [[nodiscard]] auto Features() const -> const DeviceFeatures&
  {
    if (current_context_ == nullptr) {
      throw std::runtime_error("No adapter selected.");
    }
    return current_context_->features;
  }

  [[nodiscard]] auto Adapters() const
  {
    return contexts_
      | std::ranges::views::transform(
        [](const Context& context) -> const AdapterInfo& {
          return context.info;
        });
  }

  //! Select the best ranked adapter whose device initializes and meets the
  //! requirements; an adapter that falls short gives way to the next one.
  template <DeviceRemovalHandler Handler = std::nullptr_t>
  auto SelectBestAdapter(Handler on_device_removed = nullptr) -> bool
  {
    // Optimize the case of re-selecting the current context
    if (current_context_ != nullptr && current_context_->info.IsBest()) {
      return SelectAdapter(*current_context_, on_device_removed);
    }

    for (const auto index : ranking_) {
      auto& context = contexts_.at(index);
      if (SelectAdapter(context, on_device_removed)) {
        MarkBest(contexts_, context);
        return true;
      }
      LOG_F(WARNING, "Adapter `{}` is unusable; trying the next ranked one",
        context.info.Name());
    }
    return false;
  }

  OXGN_D3D12_NDAPI auto GetCommandQueue(D3D12_COMMAND_LIST_TYPE type) const
    -> dx::ICommandQueue*;

private:
  struct Context {
    AdapterInfo info;

    Microsoft::WRL::ComPtr<dxgi::IAdapter> adapter;
    Microsoft::WRL::ComPtr<dx::IDevice> device;
    Microsoft::WRL::ComPtr<D3D12MA::Allocator> allocator;
    DeviceFeatures features;

    std::vector<Microsoft::WRL::ComPtr<dx::ICommandQueue>> commandQueues_;

    Context(AdapterInfo info, Microsoft::WRL::ComPtr<dxgi::IAdapter> adapter);
    ~Context() noexcept;

    OXYGEN_MAKE_NON_COPYABLE(Context)
    OXYGEN_DEFAULT_MOVABLE(Context)

    OXGN_D3D12_NDAPI auto IsActive() const -> bool;
  };

  auto InitializeFactory() -> void;
  auto DiscoverAdapters() -> void;
  static auto DiscoverContexts(dx::IFactory& factory, D3D_FEATURE_LEVEL minimum)
    -> std::vector<Context>;
  static auto RankContexts(std::span<Context> contexts,
    const DeviceManagerDesc& desc) -> std::vector<std::size_t>;
  OXGN_D3D12_API static auto MarkBest(
    std::span<Context> contexts, const Context& best) -> void;
  static auto ProbeAdapter(const Context& context,
    const DeviceRequirements& requirements) -> AdapterReport;

  template <DeviceRemovalHandler Handler = std::nullptr_t>
  auto SelectAdapter(
    Context& new_context, Handler&& on_device_removed = nullptr) -> bool
  {
    LOG_SCOPE_FUNCTION(INFO);

    LOG_F(INFO, "requested: {}", new_context.info.Name());

    // If the context we want to select is not active, initialize it
    auto context_is_active = new_context.IsActive();
    LOG_IF_F(INFO, !context_is_active, "(not active)");
    if ((!context_is_active) && (!InitializeContext(new_context))) {
      return false;
    }

    DCHECK_F(new_context.IsActive());

    // If we had a device loss, attempt to recover before you select the
    // context
    if (CheckForDeviceLoss(new_context)) {
      // Invoke the handler if provided before we attempt the recovery
      if constexpr (!std::is_same_v<std::remove_cvref_t<Handler>,
                      std::nullptr_t>) {
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

  OXGN_D3D12_NDAPI auto InitializeContext(Context& context) const -> bool;
  OXGN_D3D12_NDAPI auto CheckForDeviceLoss(const Context& context) -> bool;
  OXGN_D3D12_NDAPI auto RecoverFromDeviceLoss(Context& context) -> bool;

  DeviceManagerDesc props_;

  Microsoft::WRL::ComPtr<dx::IFactory> factory_;

  Context* current_context_ { nullptr };
  std::vector<Context> contexts_;
  //! Indices into `contexts_` of the selectable adapters, best first.
  std::vector<std::size_t> ranking_;
  std::shared_ptr<DebugLayer> debug_layer_;
};

} // namespace oxygen::graphics::d3d12
