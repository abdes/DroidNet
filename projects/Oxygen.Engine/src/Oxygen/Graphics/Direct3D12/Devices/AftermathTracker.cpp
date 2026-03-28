//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Direct3D12/Devices/AftermathTracker.h>

#include <chrono>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include <Oxygen/Base/Logging.h>

#if defined(USE_AFTERMATH)
// Try to include Aftermath headers - prefer new header layout
#  if __has_include(<GFSDK_Aftermath_Defines.h>)
#    include <GFSDK_Aftermath_Defines.h>
#  endif
#  if __has_include(<GFSDK_Aftermath_GpuCrashDump.h>)
#    include <GFSDK_Aftermath_GpuCrashDump.h>
#  endif
#  if __has_include(<GFSDK_Aftermath_DX12.h>)
#    include <GFSDK_Aftermath_DX12.h>
#  elif __has_include(<GFSDK_Aftermath.h>)
#    include <GFSDK_Aftermath.h>
#  endif

// Verify we have what we need
#  if defined(GFSDK_Aftermath_Version_API)                                     \
    && defined(GFSDK_Aftermath_GpuCrashDumpWatchedApiFlags_DX)
#    define OXYGEN_HAS_AFTERMATH 1
#  else
#    define OXYGEN_HAS_AFTERMATH 0
#  endif
#else
#  define OXYGEN_HAS_AFTERMATH 0
#endif

namespace oxygen::graphics::d3d12 {
namespace {

  constexpr uint32_t kNvidiaVendorId = 0x10DE;
  constexpr uint32_t kDeviceRemovedWaitTimeoutMs = 2000;
  constexpr uint32_t kMarkerSleepPollingMs = 50;
  constexpr size_t kMaxStoredMarkers = 8192;

  auto MakeTimestampedFilePath(const std::filesystem::path& base_dir,
    const char* extension) -> std::filesystem::path
  {
    using clock = std::chrono::system_clock;
    const auto now = clock::now();
    const auto tt = clock::to_time_t(now);

    std::tm tm = {};
#if defined(_WIN32)
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif

    char ts[32] = {};
    std::strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", &tm);

    auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now.time_since_epoch())
                    .count()
      % 1000;

    return base_dir
      / (std::string("aftermath_") + ts + "_" + std::to_string(millis)
        + extension);
  }

  auto WriteBlobToFile(const std::filesystem::path& path, const void* data,
    const uint32_t size) -> void
  {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
      LOG_F(
        ERROR, "Aftermath: failed to open file for writing: {}", path.string());
      return;
    }
    out.write(
      static_cast<const char*>(data), static_cast<std::streamsize>(size));
  }

#if OXYGEN_HAS_AFTERMATH
  struct AftermathState {
    std::mutex mutex;
    bool crash_dumps_enabled { false };
    bool dx12_initialized { false };

    std::filesystem::path output_dir { std::filesystem::path("logs")
      / "aftermath" };

    uint64_t next_marker_token { 1 };
    std::unordered_map<ID3D12GraphicsCommandList*,
      GFSDK_Aftermath_ContextHandle>
      context_handles;
    std::unordered_map<uint64_t, std::unique_ptr<std::string>> marker_payloads;
    std::unordered_map<ID3D12GraphicsCommandList*, std::deque<uint64_t>>
      marker_stack;
    std::deque<uint64_t> marker_order;

    std::string last_device_removal_context;
  };

  AftermathState g_aftermath;

  auto AftermathResultName(const GFSDK_Aftermath_Result result) -> const char*
  {
    switch (result) {
    case GFSDK_Aftermath_Result_Success:
      return "Success";
    case GFSDK_Aftermath_Result_Fail:
      return "Fail";
    case GFSDK_Aftermath_Result_FAIL_DriverVersionNotSupported:
      return "DriverVersionNotSupported";
    case GFSDK_Aftermath_Result_FAIL_D3DDebugLayerNotCompatible:
      return "D3DDebugLayerNotCompatible";
    case GFSDK_Aftermath_Result_FAIL_D3DDeviceNotSupported:
      return "D3DDeviceNotSupported";
    case GFSDK_Aftermath_Result_FAIL_NotInitialized:
      return "NotInitialized";
    default:
      return "Unknown";
    }
  }

  auto LogAftermathResult(
    const char* operation, const GFSDK_Aftermath_Result res) -> bool
  {
    if (res == GFSDK_Aftermath_Result_Success) {
      return true;
    }
    LOG_F(WARNING, "Aftermath: {} failed ({}, code={})", operation,
      AftermathResultName(res), static_cast<unsigned int>(res));
    return false;
  }

  void OnGpuCrashDump(
    const void* dump, const uint32_t dump_size, void* user_data) noexcept
  {
    auto* state = static_cast<AftermathState*>(user_data);
    if (state == nullptr || dump == nullptr || dump_size == 0) {
      return;
    }

    std::lock_guard<std::mutex> lock(state->mutex);
    std::filesystem::create_directories(state->output_dir);
    const auto path = MakeTimestampedFilePath(state->output_dir, ".nv-gpudmp");
    WriteBlobToFile(path, dump, dump_size);
    LOG_F(ERROR, "Aftermath: GPU crash dump written: {}", path.string());
  }

  void OnShaderDebugInfo(const void* shader_debug_info,
    const uint32_t shader_debug_info_size, void* user_data) noexcept
  {
    auto* state = static_cast<AftermathState*>(user_data);
    if (state == nullptr || shader_debug_info == nullptr
      || shader_debug_info_size == 0) {
      return;
    }

    std::lock_guard<std::mutex> lock(state->mutex);
    std::filesystem::create_directories(state->output_dir);
    const auto path = MakeTimestampedFilePath(state->output_dir, ".nvdbg");
    WriteBlobToFile(path, shader_debug_info, shader_debug_info_size);
  }

  void OnCrashDumpDescription(
    PFN_GFSDK_Aftermath_AddGpuCrashDumpDescription add_description,
    void* /*user_data*/) noexcept
  {
    if (add_description == nullptr) {
      return;
    }

    add_description(GFSDK_Aftermath_GpuCrashDumpDescriptionKey_ApplicationName,
      "Oxygen.Engine");
    add_description(
      GFSDK_Aftermath_GpuCrashDumpDescriptionKey_ApplicationVersion, "dev");
    add_description(GFSDK_Aftermath_GpuCrashDumpDescriptionKey_UserDefined,
      "D3D12 backend with integrated debug toolchain");
  }

  void OnResolveMarker(const void* marker_data, const uint32_t marker_data_size,
    void* user_data, void** resolved_marker_data,
    uint32_t* resolved_marker_data_size) noexcept
  {
    if (resolved_marker_data == nullptr
      || resolved_marker_data_size == nullptr) {
      return;
    }

    *resolved_marker_data = nullptr;
    *resolved_marker_data_size = 0;

    if (marker_data_size != 0 || marker_data == nullptr) {
      return;
    }

    auto* state = static_cast<AftermathState*>(user_data);
    if (state == nullptr) {
      return;
    }

    const auto token
      = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(marker_data));

    std::lock_guard<std::mutex> lock(state->mutex);
    const auto it = state->marker_payloads.find(token);
    if (it == state->marker_payloads.end() || it->second == nullptr) {
      return;
    }

    *resolved_marker_data = static_cast<void*>(it->second->data());
    *resolved_marker_data_size = static_cast<uint32_t>(it->second->size() + 1U);
  }
#endif

} // namespace

auto AftermathTracker::Instance() -> AftermathTracker&
{
  static AftermathTracker instance;
  return instance;
}

auto AftermathTracker::EnableCrashDumps() noexcept -> void
{
#if OXYGEN_HAS_AFTERMATH
  std::lock_guard<std::mutex> lock(g_aftermath.mutex);
  if (g_aftermath.crash_dumps_enabled) {
    return;
  }

  std::filesystem::create_directories(g_aftermath.output_dir);

  const auto feature_flags = GFSDK_Aftermath_GpuCrashDumpFeatureFlags_Default
    | GFSDK_Aftermath_GpuCrashDumpFeatureFlags_DeferDebugInfoCallbacks;

  const auto res = GFSDK_Aftermath_EnableGpuCrashDumps(
    GFSDK_Aftermath_Version_API, GFSDK_Aftermath_GpuCrashDumpWatchedApiFlags_DX,
    feature_flags, &OnGpuCrashDump, &OnShaderDebugInfo, &OnCrashDumpDescription,
    &OnResolveMarker, &g_aftermath);

  if (!LogAftermathResult("EnableGpuCrashDumps", res)) {
    return;
  }

  g_aftermath.crash_dumps_enabled = true;
  LOG_F(INFO, "Aftermath: crash dump collection enabled");
#else
  LOG_F(1, "Aftermath: SDK not available at build time");
#endif
}

auto AftermathTracker::DisableCrashDumps() noexcept -> void
{
#if OXYGEN_HAS_AFTERMATH
  std::lock_guard<std::mutex> lock(g_aftermath.mutex);
  if (!g_aftermath.crash_dumps_enabled) {
    return;
  }

  for (auto& [command_list, handle] : g_aftermath.context_handles) {
    (void)command_list;
    if (handle != nullptr) {
      (void)GFSDK_Aftermath_ReleaseContextHandle(handle);
    }
  }
  g_aftermath.context_handles.clear();
  g_aftermath.marker_payloads.clear();
  g_aftermath.marker_order.clear();
  g_aftermath.dx12_initialized = false;

  const auto res = GFSDK_Aftermath_DisableGpuCrashDumps();
  (void)LogAftermathResult("DisableGpuCrashDumps", res);

  g_aftermath.crash_dumps_enabled = false;
  LOG_F(INFO, "Aftermath: crash dump collection disabled");
#endif
}

auto AftermathTracker::InitializeDevice(
  ID3D12Device* device, const uint32_t vendor_id) noexcept -> void
{
#if OXYGEN_HAS_AFTERMATH
  if (device == nullptr) {
    return;
  }

  if (vendor_id != kNvidiaVendorId) {
    LOG_F(1,
      "Aftermath: skipped DX12 initialization for non-NVIDIA adapter "
      "(vendor=0x{:04X})",
      vendor_id);
    return;
  }

  std::lock_guard<std::mutex> lock(g_aftermath.mutex);
  if (!g_aftermath.crash_dumps_enabled || g_aftermath.dx12_initialized) {
    return;
  }

  const uint32_t aftermath_flags = GFSDK_Aftermath_FeatureFlags_EnableMarkers
    | GFSDK_Aftermath_FeatureFlags_EnableResourceTracking
    | GFSDK_Aftermath_FeatureFlags_EnableShaderErrorReporting;

  const auto res = GFSDK_Aftermath_DX12_Initialize(
    GFSDK_Aftermath_Version_API, aftermath_flags, device);
  if (!LogAftermathResult("DX12_Initialize", res)) {
    return;
  }

  g_aftermath.dx12_initialized = true;
  LOG_F(INFO,
    "Aftermath: DX12 initialized (markers, resource tracking, shader "
    "error reporting)");
#else
  (void)device;
  (void)vendor_id;
#endif
}

auto AftermathTracker::WaitForCrashDumpCompletion() noexcept -> void
{
#if OXYGEN_HAS_AFTERMATH
  std::lock_guard<std::mutex> lock(g_aftermath.mutex);
  if (!g_aftermath.crash_dumps_enabled) {
    return;
  }

  GFSDK_Aftermath_CrashDump_Status status
    = GFSDK_Aftermath_CrashDump_Status_Unknown;
  auto res = GFSDK_Aftermath_GetCrashDumpStatus(&status);
  if (!LogAftermathResult("GetCrashDumpStatus", res)) {
    return;
  }

  const auto start = std::chrono::steady_clock::now();
  while (status != GFSDK_Aftermath_CrashDump_Status_Finished
    && status != GFSDK_Aftermath_CrashDump_Status_CollectingDataFailed) {
    const auto elapsed_ms
      = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start)
          .count();
    if (elapsed_ms >= kDeviceRemovedWaitTimeoutMs) {
      LOG_F(WARNING,
        "Aftermath: crash dump status wait timeout after {}ms (status={})",
        kDeviceRemovedWaitTimeoutMs, static_cast<unsigned int>(status));
      break;
    }

    std::this_thread::sleep_for(
      std::chrono::milliseconds(kMarkerSleepPollingMs));
    res = GFSDK_Aftermath_GetCrashDumpStatus(&status);
    if (!LogAftermathResult("GetCrashDumpStatus(poll)", res)) {
      break;
    }
  }
#endif
}

auto AftermathTracker::RegisterResource(ID3D12Resource* resource) noexcept
  -> void
{
#if OXYGEN_HAS_AFTERMATH
  if (resource == nullptr) {
    return;
  }

  std::lock_guard<std::mutex> lock(g_aftermath.mutex);
  if (!g_aftermath.dx12_initialized) {
    return;
  }

  const auto res = GFSDK_Aftermath_DX12_RegisterResource(resource);
  (void)LogAftermathResult("DX12_RegisterResource", res);
#else
  (void)resource;
#endif
}

auto AftermathTracker::UnregisterResource(ID3D12Resource* resource) noexcept
  -> void
{
#if OXYGEN_HAS_AFTERMATH
  if (resource == nullptr) {
    return;
  }

  std::lock_guard<std::mutex> lock(g_aftermath.mutex);
  if (!g_aftermath.dx12_initialized) {
    return;
  }

  const auto res = GFSDK_Aftermath_DX12_UnregisterResource(resource);
  (void)LogAftermathResult("DX12_UnregisterResource", res);
#else
  (void)resource;
#endif
}

auto AftermathTracker::SetMarker(ID3D12GraphicsCommandList* command_list,
  const std::string_view marker_text) noexcept -> void
{
#if OXYGEN_HAS_AFTERMATH
  if (command_list == nullptr || marker_text.empty()) {
    return;
  }

  std::lock_guard<std::mutex> lock(g_aftermath.mutex);
  if (!g_aftermath.dx12_initialized) {
    return;
  }

  auto handle_it = g_aftermath.context_handles.find(command_list);
  if (handle_it == g_aftermath.context_handles.end()) {
    GFSDK_Aftermath_ContextHandle context_handle = nullptr;
    const auto create_res
      = GFSDK_Aftermath_DX12_CreateContextHandle(command_list, &context_handle);
    if (!LogAftermathResult("DX12_CreateContextHandle", create_res)
      || context_handle == nullptr) {
      return;
    }
    handle_it
      = g_aftermath.context_handles.emplace(command_list, context_handle).first;
  }

  const auto marker_token = g_aftermath.next_marker_token++;
  g_aftermath.marker_payloads.emplace(
    marker_token, std::make_unique<std::string>(marker_text));
  g_aftermath.marker_order.push_back(marker_token);
  if (g_aftermath.marker_order.size() > kMaxStoredMarkers) {
    const auto old_token = g_aftermath.marker_order.front();
    g_aftermath.marker_order.pop_front();
    g_aftermath.marker_payloads.erase(old_token);
  }

  const auto marker_ptr
    = reinterpret_cast<void*>(static_cast<uintptr_t>(marker_token));
  const auto marker_res = GFSDK_Aftermath_SetEventMarker(
    handle_it->second, marker_ptr, 0 /* token-only marker */);
  (void)LogAftermathResult("SetEventMarker", marker_res);
#else
  (void)command_list;
  (void)marker_text;
#endif
}

auto AftermathTracker::PushMarker(ID3D12GraphicsCommandList* command_list,
  const std::string_view marker_text) noexcept -> void
{
#if OXYGEN_HAS_AFTERMATH
  if (command_list == nullptr || marker_text.empty()) {
    return;
  }

  std::lock_guard<std::mutex> lock(g_aftermath.mutex);
  if (!g_aftermath.dx12_initialized) {
    return;
  }

  auto handle_it = g_aftermath.context_handles.find(command_list);
  if (handle_it == g_aftermath.context_handles.end()) {
    GFSDK_Aftermath_ContextHandle context_handle = nullptr;
    const auto create_res
      = GFSDK_Aftermath_DX12_CreateContextHandle(command_list, &context_handle);
    if (!LogAftermathResult("DX12_CreateContextHandle", create_res)
      || context_handle == nullptr) {
      return;
    }
    handle_it
      = g_aftermath.context_handles.emplace(command_list, context_handle).first;
  }

  const auto marker_token = g_aftermath.next_marker_token++;
  g_aftermath.marker_payloads.emplace(
    marker_token, std::make_unique<std::string>(marker_text));
  g_aftermath.marker_order.push_back(marker_token);
  if (g_aftermath.marker_order.size() > kMaxStoredMarkers) {
    const auto old_token = g_aftermath.marker_order.front();
    g_aftermath.marker_order.pop_front();
    g_aftermath.marker_payloads.erase(old_token);
  }

  g_aftermath.marker_stack[command_list].push_back(marker_token);

  const auto marker_ptr
    = reinterpret_cast<void*>(static_cast<uintptr_t>(marker_token));
  const auto marker_res = GFSDK_Aftermath_SetEventMarker(
    handle_it->second, marker_ptr, 0 /* token-only marker */);
  (void)LogAftermathResult("PushMarker", marker_res);
#else
  (void)command_list;
  (void)marker_text;
#endif
}

auto AftermathTracker::PopMarker(
  ID3D12GraphicsCommandList* command_list) noexcept -> void
{
#if OXYGEN_HAS_AFTERMATH
  if (command_list == nullptr) {
    return;
  }

  std::lock_guard<std::mutex> lock(g_aftermath.mutex);
  if (!g_aftermath.dx12_initialized) {
    return;
  }

  auto stack_it = g_aftermath.marker_stack.find(command_list);
  if (stack_it == g_aftermath.marker_stack.end() || stack_it->second.empty()) {
    LOG_F(WARNING, "Aftermath: PopMarker called with empty stack");
    return;
  }

  const auto popped_token = stack_it->second.back();
  stack_it->second.pop_back();

  // Generate a marker indicating the scope exit
  auto handle_it = g_aftermath.context_handles.find(command_list);
  if (handle_it != g_aftermath.context_handles.end()) {
    const auto pop_marker_token = g_aftermath.next_marker_token++;
    g_aftermath.marker_payloads.emplace(
      pop_marker_token, std::make_unique<std::string>("<pop>"));

    const auto marker_ptr
      = reinterpret_cast<void*>(static_cast<uintptr_t>(pop_marker_token));
    const auto marker_res
      = GFSDK_Aftermath_SetEventMarker(handle_it->second, marker_ptr, 0);
    (void)LogAftermathResult("PopMarker", marker_res);
  }
#else
  (void)command_list;
#endif
}

auto AftermathTracker::NotifyDeviceRemovalContext(
  const std::string_view context_info) noexcept -> void
{
#if OXYGEN_HAS_AFTERMATH
  std::lock_guard<std::mutex> lock(g_aftermath.mutex);
  g_aftermath.last_device_removal_context = std::string(context_info);
  LOG_F(
    WARNING, "Aftermath: Device removal context recorded: {}", context_info);
#else
  (void)context_info;
#endif
}

} // namespace oxygen::graphics::d3d12
