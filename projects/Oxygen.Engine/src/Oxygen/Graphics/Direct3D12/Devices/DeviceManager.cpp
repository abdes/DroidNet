//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Windows.h> // IWYU pragma: keep

#include <algorithm>
#include <array>
#include <cstddef>
#include <exception>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <combaseapi.h>
#include <d3d12.h>
#include <d3dcommon.h>
#include <dxgi.h>
#include <dxgi1_3.h>
#include <dxgi1_6.h>
#include <fmt/format.h>
#include <minwindef.h>
#include <winerror.h>
#include <wrl/client.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/StringUtils.h>
#include <Oxygen/Base/Unreachable.h>
#include <Oxygen/Base/Windows/ComError.h>
#include <Oxygen/Config/GraphicsConfig.h>
#include <Oxygen/Graphics/Direct3D12/Allocator/D3D12MemAlloc.h>
#include <Oxygen/Graphics/Direct3D12/Detail/Types.h>
#include <Oxygen/Graphics/Direct3D12/Devices/AdapterSelection.h>
#include <Oxygen/Graphics/Direct3D12/Devices/DebugLayer.h>
#include <Oxygen/Graphics/Direct3D12/Devices/DeviceManager.h>

using Microsoft::WRL::ComPtr;
using oxygen::graphics::d3d12::AdapterInfo;
using oxygen::graphics::d3d12::AdapterSelectionPolicy;
using oxygen::graphics::d3d12::AdapterTraits;
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
  return fmt::format(
    "{:.2f} MB", static_cast<double>(memory_size) / (1ULL << kBitsPerMegabyte));
}

auto GetAdapterName(const DXGI_ADAPTER_DESC1& desc)
{
  std::string description {};

  // Get array size from DXGI_ADAPTER_DESC1 structure at compile time
  constexpr size_t kDescriptionLength
    = std::extent_v<decltype(DXGI_ADAPTER_DESC1::Description)>;
  // Verify string is null-terminated within the array bounds
  CHECK_NOTNULL_F(std::char_traits<wchar_t>::find(
                    &desc.Description[0], kDescriptionLength, L'\0'),
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

//! The highest feature level, at least `minimum`, the adapter supports. A null
//! output device asks the runtime whether creation would succeed, without
//! creating a device on an adapter that may never be selected.
auto ProbeMaxFeatureLevel(
  const ComPtr<oxygen::graphics::d3d12::dxgi::IAdapter>& adapter,
  const D3D_FEATURE_LEVEL minimum) -> std::optional<D3D_FEATURE_LEVEL>
{
  static constexpr std::array feature_levels {
    D3D_FEATURE_LEVEL_12_2,
    D3D_FEATURE_LEVEL_12_1,
    D3D_FEATURE_LEVEL_12_0,
    D3D_FEATURE_LEVEL_11_1,
    D3D_FEATURE_LEVEL_11_0,
  };

  for (const auto level : feature_levels) {
    if (level < minimum) {
      break;
    }
    if (SUCCEEDED(D3D12CreateDevice(
          adapter.Get(), level, __uuidof(ID3D12Device), nullptr))) {
      return level;
    }
  }
  return std::nullopt;
}

auto SupportsShaderModel(
  const ComPtr<oxygen::graphics::d3d12::dx::IDevice>& device,
  const D3D_SHADER_MODEL required) -> bool
{
  auto shader_model = D3D12_FEATURE_DATA_SHADER_MODEL {
    .HighestShaderModel = required,
  };
  return SUCCEEDED(device->CheckFeatureSupport(
           D3D12_FEATURE_SHADER_MODEL, &shader_model, sizeof(shader_model)))
    && shader_model.HighestShaderModel >= required;
}

} // namespace

auto AdapterInfo::MemoryAsString() const -> std::string
{
  return FormatMemorySize(memory);
}

auto AdapterInfo::MaxFeatureLevelAsString() const -> std::string
{
  return FeatureLevelToString(max_feature_level);
}

void DeviceManager::InitializeFactory()
{
  if (props_.enable_debug_layer) {
    const auto hr
      = CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&factory_));
    if (SUCCEEDED(hr)) {
      return;
    }

    // If debug factory creation fails (e.g. debug runtime not installed),
    // gracefully continue without DXGI debug factory.
    LOG_F(WARNING,
      "Failed to create DXGI debug factory (hr=0x{:08X}), falling back to "
      "non-debug factory",
      static_cast<unsigned int>(hr));
  }

  ThrowOnFailed(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory_)));
}

void DeviceManager::DiscoverAdapters()
{
  DCHECK_NOTNULL_F(factory_, "DXGI factory not initialized");
  LOG_SCOPE_F(INFO, "Discover adapters");

  contexts_ = DiscoverContexts(*factory_.Get(), props_.min_feature_level);
  ranking_ = RankContexts(contexts_, props_);
}

auto DeviceManager::DiscoverContexts(dx::IFactory& factory,
  const D3D_FEATURE_LEVEL minimum) -> std::vector<Context>
{
  auto contexts = std::vector<Context> {};
  // Enumerate high-performance adapters only
  ComPtr<dxgi::IAdapter> adapter;
  UINT adapter_index = 0;
  while (SUCCEEDED(factory.EnumAdapterByGpuPreference(adapter_index,
    DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)))) {

    DXGI_ADAPTER_DESC1 desc;
    ThrowOnFailed(adapter->GetDesc1(&desc));

    if ((desc.Flags & static_cast<UINT>(DXGI_ADAPTER_FLAG_SOFTWARE)) != 0U) {
      // Don't select the Basic Render Driver adapter.
      ++adapter_index;
      continue;
    }

    auto adapter_name = GetAdapterName(desc);
    const auto max_feature_level = ProbeMaxFeatureLevel(adapter, minimum);
    LOG_IF_F(WARNING, !max_feature_level.has_value(),
      "adapter `{}` does not support feature level 0x{:X}", adapter_name,
      static_cast<unsigned>(minimum));

    LOG_F(INFO, "[+] {} {} (vendor 0x{:04X}, device 0x{:04X})", adapter_name,
      FormatMemorySize(desc.DedicatedVideoMemory), desc.VendorId,
      desc.DeviceId);
    const auto has_display = CheckConnectedDisplay(adapter);
    LOG_F(1, "  Has Connected Display : {}", has_display);
    LOG_F(1, "  Max Feature Level     : {}",
      max_feature_level.has_value() ? FeatureLevelToString(*max_feature_level)
                                    : std::string { "unsupported" });

    AdapterInfo adapter_info(std::move(adapter_name), desc.VendorId,
      desc.DeviceId, desc.DedicatedVideoMemory, has_display,
      max_feature_level.has_value(),
      max_feature_level.value_or(D3D_FEATURE_LEVEL_11_0));

    contexts.emplace_back(std::move(adapter_info), adapter);
    ++adapter_index;
  }
  return contexts;
}

auto DeviceManager::RankContexts(const std::span<Context> contexts,
  const DeviceManagerDesc& desc) -> std::vector<std::size_t>
{
  auto traits = std::vector<AdapterTraits> {};
  traits.reserve(contexts.size());
  for (const auto& context : contexts) {
    const auto& info = context.info;
    traits.push_back({
      .name = info.Name(),
      .device_id = info.DeviceId(),
      .dedicated_memory = info.Memory(),
      .has_display = info.IsConnectedToDisplay(),
      .max_feature_level = info.MeetsFeatureLevel()
        ? std::optional { info.MaxFeatureLevel() }
        : std::nullopt,
    });
  }

  const auto ranking = RankAdapters(traits,
    AdapterSelectionPolicy {
      .require_display = desc.require_display,
      .preferred_card_name = desc.preferred_card_name,
      .preferred_card_device_id = desc.preferred_card_device_id,
    });
  if (ranking.order.empty()) {
    LOG_F(WARNING, "No adapter supports feature level 0x{:X}{}",
      static_cast<unsigned>(desc.min_feature_level),
      desc.require_display ? " and drives a display" : "");
    return {};
  }

  const auto& best = contexts[ranking.order.front()];
  MarkBest(contexts, best);
  if (ranking.reason == AdapterChoiceReason::kNoPreferredMatch) {
    LOG_F(WARNING,
      "No usable adapter matches the preferred card (name `{}`, device id "
      "{})",
      desc.preferred_card_name.value_or(""),
      desc.preferred_card_device_id.value_or(0));
  }
  LOG_IF_F(WARNING,
    ranking.reason == AdapterChoiceReason::kPreferredCard
      && desc.require_display && !best.info.IsConnectedToDisplay(),
    "Preferred adapter `{}` drives no display", best.info.Name());
  LOG_F(INFO, "Best adapter: `{}` ({})", best.info.Name(),
    to_string(ranking.reason));
  return ranking.order;
}

auto DeviceManager::MarkBest(
  const std::span<Context> contexts, const Context& best) -> void
{
  for (auto& context : contexts) {
    context.info.is_best = &context == &best;
  }
}

auto DeviceManager::ListAdapters(const DeviceManagerDesc& desc)
  -> std::vector<AdapterInfo>
{
  ComPtr<dx::IFactory> factory;
  ThrowOnFailed(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)));
  auto contexts = DiscoverContexts(*factory.Get(), desc.min_feature_level);
  std::ignore = RankContexts(contexts, desc);

  auto adapters = std::vector<AdapterInfo> {};
  adapters.reserve(contexts.size());
  for (const auto& context : contexts) {
    adapters.push_back(context.info);
  }
  return adapters;
}

auto DeviceManager::InitializeContext(Context& context) const -> bool
{
  LOG_SCOPE_F(INFO, "Setup Context");

  try {
    // Initialize the device
    LOG_F(INFO, "Device");
    ThrowOnFailed(D3D12CreateDevice(context.adapter.Get(),
      props_.min_feature_level, IID_PPV_ARGS(&context.device)));
    if (!SupportsShaderModel(context.device, props_.min_shader_model)) {
      throw std::runtime_error(
        fmt::format("adapter `{}` does not support shader model 0x{:X}",
          context.info.Name(), static_cast<unsigned>(props_.min_shader_model)));
    }

    if (props_.enable_debug_layer) {
      DebugLayer::ConfigureDeviceInfoQueue(context.device.Get());
    }
    DebugLayer::ConfigureAftermathForDevice(
      context.device.Get(), context.info.VendorId());

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
           D3D12_COMMAND_LIST_TYPE_COPY,
         }) {
      ComPtr<ID3D12CommandQueue> commandQueue;
      D3D12_COMMAND_QUEUE_DESC queueDesc = {};
      queueDesc.Type = type;
      queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
      queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
      queueDesc.NodeMask = 0;
      ThrowOnFailed(context.device->CreateCommandQueue(
        &queueDesc, IID_PPV_ARGS(&commandQueue)));
      context.commandQueues_.emplace_back(std::move(commandQueue));
    }
    return true;
  } catch (std::exception& ex) {
    LOG_F(ERROR, "Context initialization failed: {}", ex.what());
    // Rollback the context
    context.device.Reset();
    context.allocator.Reset();
    context.commandQueues_.clear();
    return false;
  }
}

DeviceManager::Context::Context(
  AdapterInfo info, Microsoft::WRL::ComPtr<dxgi::IAdapter> adapter)
  : info(std::move(info))
  , adapter(std::move(adapter))
{
}

DeviceManager::Context::~Context() noexcept
{
  // The destructor may be called when the vector is constructing a new
  // Context in place. In that case, the placeholder object in the vector was
  // not initialized.
  if (adapter != nullptr && IsActive()) {
    LOG_F(INFO, "Releasing context for: {}", info.Name());
  }
}

auto DeviceManager::Context::IsActive() const -> bool
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

auto DeviceManager::CheckForDeviceLoss(const Context& context) -> bool
{
  DCHECK_F(context.IsActive(), "context is not healthy");

  try {
    // Check for device removal
    ThrowOnFailed(context.device->GetDeviceRemovedReason());
    return false;
  } catch (const oxygen::windows::ComError& ex) {
    LOG_F(ERROR, "Device removed: {}", ex.what());
    if (debug_layer_) {
      DebugLayer::NotifyDeviceRemoved();
      DebugLayer::PrintDredReport(context.device.Get());
    }
    return true;
  }
}

auto DeviceManager::RecoverFromDeviceLoss(Context& context) -> bool
{
  LOG_SCOPE_FUNCTION(INFO);

  // Reset the context
  context.device.Reset();
  context.allocator.Reset();
  context.commandQueues_.clear();

  return InitializeContext(context);
}

DeviceManager::DeviceManager(DeviceManagerDesc desc)
  : props_(std::move(desc))
{
  LOG_SCOPE_F(INFO, "DeviceManager init");

  if (!oxygen::AreGraphicsToolingOptionsMutuallyExclusive(
        props_.enable_debug_layer, props_.enable_aftermath)) {
    LOG_F(ERROR,
      "Rejected D3D12 device manager config: debug_layer={} aftermath={} "
      "(mutually exclusive)",
      props_.enable_debug_layer, props_.enable_aftermath);
    throw std::invalid_argument(
      "D3D12 debug layer and Nsight Aftermath are mutually exclusive");
  }

  LOG_F(INFO,
    "D3D12 device manager config accepted: debug_layer={} validation={} "
    "aftermath={} capture_provider={}",
    props_.enable_debug_layer, props_.enable_validation,
    props_.enable_aftermath, static_cast<int>(props_.frame_capture.provider));

  DebugLayer::ConfigureTooling(
    props_.enable_debug_layer, props_.enable_aftermath, props_.frame_capture);

  // RenderDoc must be bootstrapped before any DXGI factory creation or
  // D3D12 debug/device entry-point usage so its hooks can wrap the objects
  // later passed into CreateSwapChainForHwnd. Moving this later can surface
  // E_NOINTERFACE during swap-chain creation under RenderDoc.
  DebugLayer::BootstrapRenderDoc();
  DebugLayer::BootstrapPix();
  InitializeFactory();

  if (props_.enable_debug_layer || props_.enable_aftermath
    || props_.frame_capture.provider != oxygen::FrameCaptureProvider::kNone) {
    // The DebugLayer object also owns runtime debug-tool integration state.
    debug_layer_ = std::make_shared<DebugLayer>(
      props_.enable_debug_layer, props_.enable_validation, props_.enable_dred);
  }

  DiscoverAdapters();

  if (props_.auto_select_adapter) {
    LOG_F(INFO, "Auto-selecting best adapter");
    if (!SelectBestAdapter()) {
      throw std::runtime_error(fmt::format(
        "No adapter supports feature level 0x{:X} and shader model 0x{:X}{}",
        static_cast<unsigned>(props_.min_feature_level),
        static_cast<unsigned>(props_.min_shader_model),
        props_.require_display ? " and drives a display" : ""));
    }
  }
}

DeviceManager::~DeviceManager()
{
  LOG_SCOPE_F(INFO, "DeviceManager cleanup");
  contexts_.clear();
  debug_layer_.reset();
}

auto DeviceManager::GetCommandQueue(D3D12_COMMAND_LIST_TYPE type) const
  -> dx::ICommandQueue*
{
  if (current_context_ == nullptr) {
    throw std::runtime_error("No adapter selected.");
  }

  auto it = std::ranges::find_if(current_context_->commandQueues_,
    [type](const ComPtr<ID3D12CommandQueue>& queue) -> bool {
      auto desc = queue->GetDesc();
      return desc.Type == type;
    });

  if (it != current_context_->commandQueues_.end()) {
    return it->Get();
  }
  throw std::runtime_error("Command queue not found.");
}
