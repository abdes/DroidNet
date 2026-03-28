//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <exception>
#include <span>
#include <string>

#include <combaseapi.h>
#include <d3d12.h>
#include <debugapi.h>
#include <dxgi1_3.h>
#include <dxgidebug.h>
#include <fmt/format.h>
#include <libloaderapi.h>
#include <wrl/client.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/StringUtils.h>
#include <Oxygen/Base/Windows/ComError.h>
#include <Oxygen/Graphics/Common/ObjectRelease.h>
#include <Oxygen/Graphics/Direct3D12/Detail/Types.h>
#include <Oxygen/Graphics/Direct3D12/Devices/AftermathTracker.h>
#include <Oxygen/Graphics/Direct3D12/Devices/DebugLayer.h>

#if defined(USE_RENDERDOC) && __has_include(<renderdoc_app.h>)
#  include <renderdoc_app.h>
#endif

using oxygen::graphics::d3d12::DebugLayer;
using oxygen::windows::ThrowOnFailed;

namespace {

struct ToolingPolicy {
  bool requested_aftermath { true };
  bool requested_renderdoc { false };
  bool requested_pix { false };
  bool renderdoc_bootstrap_attempted { false };
  bool renderdoc_api_initialized { false };
  bool renderdoc_enabled { false };
  bool pix_enabled { false };
  bool aftermath_enabled { false };
};

ToolingPolicy g_tooling_policy;

constexpr auto IsRenderDocBuildAvailable() noexcept -> bool
{
#if defined(USE_RENDERDOC) && __has_include(<renderdoc_app.h>)
  return true;
#else
  return false;
#endif
}

constexpr auto IsPixBuildAvailable() noexcept -> bool
{
#if defined(USE_PIX) && __has_include(<pix3.h>)
  return true;
#else
  return false;
#endif
}

constexpr auto IsAftermathBuildAvailable() noexcept -> bool
{
#if defined(USE_AFTERMATH)
  return true;
#else
  return false;
#endif
}

auto RefreshToolingPolicy() noexcept -> void
{
  g_tooling_policy.renderdoc_enabled = g_tooling_policy.requested_renderdoc
    && g_tooling_policy.renderdoc_api_initialized;
  g_tooling_policy.pix_enabled
    = g_tooling_policy.requested_pix && IsPixBuildAvailable();
  g_tooling_policy.aftermath_enabled = g_tooling_policy.requested_aftermath
    && IsAftermathBuildAvailable() && !g_tooling_policy.renderdoc_enabled
    && !g_tooling_policy.pix_enabled;
}

auto IsRenderDocActive() noexcept -> bool
{
#if defined(USE_RENDERDOC) && __has_include(<renderdoc_app.h>)
  return ::GetModuleHandleA("renderdoc.dll") != nullptr;
#else
  return false;
#endif
}

} // namespace

DebugLayer::DebugLayer(
  const bool enable_debug, const bool enable_validation) noexcept
{
  BootstrapRenderDoc();
  InitializeDebugLayer(enable_debug, enable_validation);
  if (enable_debug) {
    InitializeDred();
  }
  InitializeAftermath();
}

DebugLayer::~DebugLayer() noexcept
{
  LOG_SCOPE_FUNCTION(INFO);

  if (::IsDebuggerPresent() != 0) {
    LOG_F(1, "report live objects (DebugOutput)");
    PrintLiveObjectsReport();
  }

  LOG_F(1, "release debug objects");
  ObjectRelease(d3d12_debug_);
  ObjectRelease(dxgi_info_queue_);
  ObjectRelease(dxgi_debug_);
  ObjectRelease(dred_settings_);

  AftermathTracker::Instance().DisableCrashDumps();
}

void DebugLayer::ConfigureTooling(const bool enable_aftermath,
  const bool enable_renderdoc, const bool enable_pix) noexcept
{
  g_tooling_policy = ToolingPolicy { .requested_aftermath = enable_aftermath,
    .requested_renderdoc = enable_renderdoc,
    .requested_pix = enable_pix };
  RefreshToolingPolicy();

  LOG_F(INFO,
    "D3D12 tooling policy requested: aftermath={} renderdoc={} pix={}",
    enable_aftermath, enable_renderdoc, enable_pix);

  if (enable_aftermath && !IsAftermathBuildAvailable()) {
    LOG_F(INFO,
      "Aftermath requested by GraphicsConfig, but the backend was built "
      "without Nsight Aftermath support");
  }
  if (enable_renderdoc && !IsRenderDocBuildAvailable()) {
    LOG_F(INFO,
      "RenderDoc requested by GraphicsConfig, but the backend was built "
      "without RenderDoc support");
  }
  if (enable_pix && !IsPixBuildAvailable()) {
    LOG_F(INFO,
      "PIX requested by GraphicsConfig, but the backend was built without "
      "PIX support");
  }
}

auto DebugLayer::IsPixEnabled() noexcept -> bool
{
  return g_tooling_policy.pix_enabled;
}

void DebugLayer::InitializeDebugLayer(
  const bool enable_debug, const bool enable_validation) noexcept
{
  if (!enable_debug) {
    return;
  }

  // Enable the Direct3D12 debug layer
  if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&d3d12_debug_)))) {
    d3d12_debug_->EnableDebugLayer();
    if (enable_validation) {
      d3d12_debug_->SetEnableGPUBasedValidation(TRUE);
    }
    LOG_F(INFO, "D3D12 debug layer enabled (gpu_validation={})",
      enable_validation ? "on" : "off");
  } else {
    LOG_F(WARNING, "Failed to enable the debug layer");
  }

  // Enable the DXGI leak tracking debug layer
  if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgi_debug_)))) {
    dxgi_debug_->EnableLeakTrackingForThread();

    // Setup debugger breakpoints on errors and warnings
    if ((::IsDebuggerPresent() != 0)
      && SUCCEEDED(
        DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgi_info_queue_)))) {
      ThrowOnFailed(dxgi_info_queue_->SetBreakOnSeverity(
        DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, 1));
      ThrowOnFailed(dxgi_info_queue_->SetBreakOnSeverity(
        DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, 1));
    }
  } else {
    LOG_F(WARNING, "Failed to enable the DXGI debug layer");
  }
}

void DebugLayer::InitializeDred() noexcept
{
  // Initialize DRED
  if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dred_settings_)))) {
    dred_settings_->SetAutoBreadcrumbsEnablement(
      D3D12_DRED_ENABLEMENT_FORCED_ON);
    dred_settings_->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
    dred_settings_->SetWatsonDumpEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
  } else {
    LOG_F(WARNING, "Failed to enable DRED settings");
  }
}

void DebugLayer::InitializeAftermath() noexcept
{
  if (!g_tooling_policy.requested_aftermath) {
    LOG_F(INFO, "Aftermath integration disabled by GraphicsConfig");
    return;
  }

  if (g_tooling_policy.renderdoc_enabled) {
    LOG_F(INFO,
      "Aftermath integration disabled because RenderDoc is explicitly "
      "enabled in GraphicsConfig");
    return;
  }

  if (g_tooling_policy.pix_enabled) {
    LOG_F(INFO,
      "Aftermath integration disabled because PIX is explicitly enabled in "
      "GraphicsConfig");
    return;
  }

  if (!g_tooling_policy.aftermath_enabled) {
    return;
  }

  AftermathTracker::Instance().EnableCrashDumps();
}

void DebugLayer::BootstrapRenderDoc() noexcept
{
  if (!g_tooling_policy.requested_renderdoc) {
    return;
  }

#if defined(USE_RENDERDOC) && __has_include(<renderdoc_app.h>)
  if (g_tooling_policy.renderdoc_bootstrap_attempted) {
    return;
  }
  g_tooling_policy.renderdoc_bootstrap_attempted = true;

  auto* renderdoc_module = ::GetModuleHandleA("renderdoc.dll");
  if (renderdoc_module == nullptr) {
    renderdoc_module = ::LoadLibraryA("renderdoc.dll");
  }

#  if defined(OXYGEN_RENDERDOC_DLL_PATH)
  if (renderdoc_module == nullptr) {
    renderdoc_module = ::LoadLibraryA(OXYGEN_RENDERDOC_DLL_PATH);
  }
#  endif

  if (renderdoc_module == nullptr) {
    LOG_F(INFO,
      "RenderDoc was requested by GraphicsConfig, but renderdoc.dll is not "
      "available in process path; continuing without RenderDoc API");
    return;
  }

  const auto get_api = reinterpret_cast<pRENDERDOC_GetAPI>(
    ::GetProcAddress(renderdoc_module, "RENDERDOC_GetAPI"));
  if (get_api == nullptr) {
    LOG_F(WARNING,
      "renderdoc.dll loaded, but RENDERDOC_GetAPI symbol was not found");
    return;
  }

  RENDERDOC_API_1_6_0* api = nullptr;
  if (get_api(eRENDERDOC_API_Version_1_6_0, reinterpret_cast<void**>(&api)) != 1
    || api == nullptr) {
    LOG_F(WARNING,
      "RenderDoc API negotiation failed for version 1.6.0; continuing "
      "without in-app RenderDoc controls");
    return;
  }

  g_tooling_policy.renderdoc_api_initialized = true;
  RefreshToolingPolicy();

  LOG_F(
    INFO, "RenderDoc API initialized successfully before DXGI/D3D12 startup");
#endif
}

void DebugLayer::ConfigureDeviceInfoQueue(dx::IDevice* device) noexcept
{
  if (device == nullptr) {
    return;
  }

  if (IsRenderDocActive()) {
    LOG_F(1,
      "Skipping D3D12 info queue configuration because RenderDoc is "
      "wrapping the device");
    return;
  }

  if (::IsDebuggerPresent() == 0) {
    return;
  }

  Microsoft::WRL::ComPtr<ID3D12InfoQueue> info_queue;
  if (!SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&info_queue)))) {
    LOG_F(WARNING, "Failed to query ID3D12InfoQueue from device");
    return;
  }

  if (FAILED(info_queue->SetBreakOnSeverity(
        D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE))) {
    LOG_F(WARNING, "Failed to set D3D12 break-on-corruption severity");
  }
  if (FAILED(
        info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE))) {
    LOG_F(WARNING, "Failed to set D3D12 break-on-error severity");
  }

  D3D12_MESSAGE_SEVERITY deny_severity[] = {
    D3D12_MESSAGE_SEVERITY_INFO,
  };
  D3D12_INFO_QUEUE_FILTER filter = {};
  filter.DenyList.NumSeverities = std::size(deny_severity);
  filter.DenyList.pSeverityList = deny_severity;
  if (FAILED(info_queue->PushStorageFilter(&filter))) {
    LOG_F(WARNING, "Failed to apply D3D12 info queue storage filter");
  }
}

void DebugLayer::ConfigureAftermathForDevice(
  dx::IDevice* device, const uint32_t vendor_id) noexcept
{
  if (!g_tooling_policy.aftermath_enabled) {
    return;
  }

  AftermathTracker::Instance().InitializeDevice(device, vendor_id);
}

void DebugLayer::NotifyDeviceRemoved() noexcept
{
  AftermathTracker::Instance().WaitForCrashDumpCompletion();
}

void DebugLayer::RegisterAftermathResource(ID3D12Resource* resource) noexcept
{
  AftermathTracker::Instance().RegisterResource(resource);
}

void DebugLayer::UnregisterAftermathResource(ID3D12Resource* resource) noexcept
{
  AftermathTracker::Instance().UnregisterResource(resource);
}

void DebugLayer::SetAftermathMarker(ID3D12GraphicsCommandList* command_list,
  const std::string_view marker) noexcept
{
  AftermathTracker::Instance().SetMarker(command_list, marker);
}

void DebugLayer::PushAftermathMarker(ID3D12GraphicsCommandList* command_list,
  const std::string_view marker) noexcept
{
  AftermathTracker::Instance().PushMarker(command_list, marker);
}

void DebugLayer::PopAftermathMarker(
  ID3D12GraphicsCommandList* command_list) noexcept
{
  AftermathTracker::Instance().PopMarker(command_list);
}

void DebugLayer::SetAftermathDeviceRemovalContext(
  const std::string_view context_info) noexcept
{
  AftermathTracker::Instance().NotifyDeviceRemovalContext(context_info);
}

void DebugLayer::PrintLiveObjectsReport() noexcept
{
  if (dxgi_debug_ == nullptr) {
    return;
  }

  OutputDebugString("===-- LIVE OBJECTS REPORT "
                    "-----------------------------------------------===\n");
  try {
    ThrowOnFailed(dxgi_debug_->ReportLiveObjects(DXGI_DEBUG_ALL,
      static_cast<DXGI_DEBUG_RLO_FLAGS>(DXGI_DEBUG_RLO_SUMMARY
        | DXGI_DEBUG_RLO_DETAIL | DXGI_DEBUG_RLO_IGNORE_INTERNAL)));
    OutputDebugString("===-----------------------------------------------------"
                      "-----------------===\n");
  } catch (const windows::ComError& e) {
    OutputDebugStringA(e.what());
    OutputDebugString(
      "===-- FAILED "
      "------------------------------------------------------------===\n");
  }
}

namespace nostd::adl_helper {

// Add before the existing as_string template
inline static auto as_string(D3D12_DRED_ALLOCATION_TYPE type) -> std::string
{
  switch (type) {
  case D3D12_DRED_ALLOCATION_TYPE_COMMAND_QUEUE:
    return "COMMAND_QUEUE";
  case D3D12_DRED_ALLOCATION_TYPE_COMMAND_ALLOCATOR:
    return "COMMAND_ALLOCATOR";
  case D3D12_DRED_ALLOCATION_TYPE_PIPELINE_STATE:
    return "PIPELINE_STATE";
  case D3D12_DRED_ALLOCATION_TYPE_COMMAND_LIST:
    return "COMMAND_LIST";
  case D3D12_DRED_ALLOCATION_TYPE_FENCE:
    return "FENCE";
  case D3D12_DRED_ALLOCATION_TYPE_DESCRIPTOR_HEAP:
    return "DESCRIPTOR_HEAP";
  case D3D12_DRED_ALLOCATION_TYPE_HEAP:
    return "HEAP";
  case D3D12_DRED_ALLOCATION_TYPE_QUERY_HEAP:
    return "QUERY_HEAP";
  case D3D12_DRED_ALLOCATION_TYPE_COMMAND_SIGNATURE:
    return "COMMAND_SIGNATURE";
  case D3D12_DRED_ALLOCATION_TYPE_PIPELINE_LIBRARY:
    return "PIPELINE_LIBRARY";
  case D3D12_DRED_ALLOCATION_TYPE_VIDEO_DECODER:
    return "VIDEO_DECODER";
  case D3D12_DRED_ALLOCATION_TYPE_VIDEO_PROCESSOR:
    return "VIDEO_PROCESSOR";
  case D3D12_DRED_ALLOCATION_TYPE_RESOURCE:
    return "RESOURCE";
  case D3D12_DRED_ALLOCATION_TYPE_PASS:
    return "PASS";
  case D3D12_DRED_ALLOCATION_TYPE_CRYPTOSESSION:
    return "CRYPTOSESSION";
  case D3D12_DRED_ALLOCATION_TYPE_CRYPTOSESSIONPOLICY:
    return "CRYPTOSESSIONPOLICY";
  case D3D12_DRED_ALLOCATION_TYPE_PROTECTEDRESOURCESESSION:
    return "PROTECTEDRESOURCESESSION";
  case D3D12_DRED_ALLOCATION_TYPE_VIDEO_DECODER_HEAP:
    return "VIDEO_DECODER_HEAP";
  case D3D12_DRED_ALLOCATION_TYPE_COMMAND_POOL:
    return "COMMAND_POOL";
  case D3D12_DRED_ALLOCATION_TYPE_COMMAND_RECORDER:
    return "COMMAND_RECORDER";
  case D3D12_DRED_ALLOCATION_TYPE_STATE_OBJECT:
    return "STATE_OBJECT";
  case D3D12_DRED_ALLOCATION_TYPE_METACOMMAND:
    return "METACOMMAND";
  case D3D12_DRED_ALLOCATION_TYPE_SCHEDULINGGROUP:
    return "SCHEDULINGGROUP";
  case D3D12_DRED_ALLOCATION_TYPE_VIDEO_MOTION_ESTIMATOR:
    return "VIDEO_MOTION_ESTIMATOR";
  case D3D12_DRED_ALLOCATION_TYPE_VIDEO_MOTION_VECTOR_HEAP:
    return "VIDEO_MOTION_VECTOR_HEAP";
  case D3D12_DRED_ALLOCATION_TYPE_VIDEO_EXTENSION_COMMAND:
    return "VIDEO_EXTENSION_COMMAND";
  case D3D12_DRED_ALLOCATION_TYPE_VIDEO_ENCODER:
    return "VIDEO_ENCODER";
  case D3D12_DRED_ALLOCATION_TYPE_VIDEO_ENCODER_HEAP:
    return "VIDEO_ENCODER_HEAP";
  case D3D12_DRED_ALLOCATION_TYPE_INVALID:
    return "INVALID";
  default:
    return "UNKNOWN";
  }
}

inline static auto as_string(D3D12_AUTO_BREADCRUMB_OP op) -> std::string
{
  switch (op) {
  case D3D12_AUTO_BREADCRUMB_OP_SETMARKER:
    return "SetMarker";
  case D3D12_AUTO_BREADCRUMB_OP_BEGINEVENT:
    return "BeginEvent";
  case D3D12_AUTO_BREADCRUMB_OP_ENDEVENT:
    return "EndEvent";
  case D3D12_AUTO_BREADCRUMB_OP_DRAWINSTANCED:
    return "DrawInstanced";
  case D3D12_AUTO_BREADCRUMB_OP_DRAWINDEXEDINSTANCED:
    return "DrawIndexedInstanced";
  case D3D12_AUTO_BREADCRUMB_OP_EXECUTEINDIRECT:
    return "ExecuteIndirect";
  case D3D12_AUTO_BREADCRUMB_OP_DISPATCH:
    return "Dispatch";
  case D3D12_AUTO_BREADCRUMB_OP_COPYBUFFERREGION:
    return "CopyBufferRegion";
  case D3D12_AUTO_BREADCRUMB_OP_COPYTEXTUREREGION:
    return "CopyTextureRegion";
  case D3D12_AUTO_BREADCRUMB_OP_COPYRESOURCE:
    return "CopyResource";
  case D3D12_AUTO_BREADCRUMB_OP_COPYTILES:
    return "CopyTiles";
  case D3D12_AUTO_BREADCRUMB_OP_RESOLVESUBRESOURCE:
    return "ResolveSubresource";
  case D3D12_AUTO_BREADCRUMB_OP_CLEARRENDERTARGETVIEW:
    return "ClearRenderTargetView";
  case D3D12_AUTO_BREADCRUMB_OP_CLEARUNORDEREDACCESSVIEW:
    return "ClearUnorderedAccessView";
  case D3D12_AUTO_BREADCRUMB_OP_CLEARDEPTHSTENCILVIEW:
    return "ClearDepthStencilView";
  case D3D12_AUTO_BREADCRUMB_OP_RESOURCEBARRIER:
    return "ResourceBarrier";
  case D3D12_AUTO_BREADCRUMB_OP_EXECUTEBUNDLE:
    return "ExecuteBundle";
  case D3D12_AUTO_BREADCRUMB_OP_PRESENT:
    return "Present";
  case D3D12_AUTO_BREADCRUMB_OP_RESOLVEQUERYDATA:
    return "ResolveQueryData";
  case D3D12_AUTO_BREADCRUMB_OP_BEGINSUBMISSION:
    return "BeginSubmission";
  case D3D12_AUTO_BREADCRUMB_OP_ENDSUBMISSION:
    return "EndSubmission";
  // ... add other cases as needed
  default:
    return fmt::format("Unknown({})", static_cast<int>(op));
  }
}

} // namespace nostd::adl_helper

namespace {
void PrintCommandListInfo(const D3D12_AUTO_BREADCRUMB_NODE1* node) noexcept
{
  LOG_F(INFO, "CommandList {} ({})", fmt::ptr(node->pCommandList),
    node->pCommandListDebugNameA ? node->pCommandListDebugNameA : "Unnamed");

  if (node->pCommandQueue != nullptr) {
    LOG_F(INFO, "  Queue {} ({})", fmt::ptr(node->pCommandQueue),
      node->pCommandQueueDebugNameA ? node->pCommandQueueDebugNameA
                                    : "Unnamed");
  }
}

void PrintBreadcrumbHistory(const D3D12_AUTO_BREADCRUMB_NODE1* node) noexcept
{
  if (node->pCommandHistory == nullptr || node->BreadcrumbCount == 0) {
    return;
  }

  auto commandHistory = std::span(node->pCommandHistory, node->BreadcrumbCount);
  for (const auto& command : commandHistory) {
    LOG_F(INFO, "  [{:3}] {}", &command - commandHistory.data(),
      nostd::to_string(command));
  }

  if (node->pLastBreadcrumbValue != nullptr) {
    LOG_F(INFO, "  Last Breadcrumb Value: {}", *node->pLastBreadcrumbValue);
  }
}

void PrintBreadcrumbContexts(const D3D12_AUTO_BREADCRUMB_NODE1* node) noexcept
{
  if (node->pBreadcrumbContexts == nullptr) {
    return;
  }

  auto contexts
    = std::span(node->pBreadcrumbContexts, node->BreadcrumbContextsCount);
  for (const auto& context : contexts) {
    if (context.pContextString != nullptr) {
      std::string utf8Context;
      try {
        oxygen::string_utils::WideToUtf8(context.pContextString, utf8Context);
      } catch (const std::exception& e) {
        LOG_F(ERROR, "Failed to convert context string: {}", e.what());
      }
      LOG_F(INFO, "  Context[{}]: {}", context.BreadcrumbIndex, utf8Context);
    }
  }
}

void PrintAllocationNode(
  const D3D12_DRED_ALLOCATION_NODE* node, const char* prefix) noexcept
{
  for (; node != nullptr; node = node->pNext) {
    LOG_F(INFO, "{}{} {}", prefix, nostd::to_string(node->AllocationType),
      node->ObjectNameA ? node->ObjectNameA : "Unnamed");
  }
}

void PrintBreadcrumbNode(const D3D12_AUTO_BREADCRUMB_NODE1* node) noexcept
{
  PrintCommandListInfo(node);
  PrintBreadcrumbHistory(node);
  PrintBreadcrumbContexts(node);
}

void PrintPageFaultInfo(const D3D12_DRED_PAGE_FAULT_OUTPUT& page_fault) noexcept
{
  LOG_SCOPE_F(INFO, "Memory Allocations at Fault");

  if (page_fault.pHeadExistingAllocationNode != nullptr) {
    LOG_SCOPE_F(INFO, "Active Allocations");
    PrintAllocationNode(page_fault.pHeadExistingAllocationNode, "  ");
  }

  if (page_fault.pHeadRecentFreedAllocationNode != nullptr) {
    LOG_SCOPE_F(INFO, "Recently Freed");
    PrintAllocationNode(page_fault.pHeadRecentFreedAllocationNode, "  ");
  }
}
} // anonymous namespace

void DebugLayer::PrintDredReport(dx::IDevice* device) noexcept
{
  Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedData1> dred;
  if (!SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&dred)))) {
    return;
  }

  LOG_SCOPE_F(INFO, "Device Removed Extended Data (DRED) Report");
  D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 breadcrumbs {};
  D3D12_DRED_PAGE_FAULT_OUTPUT page_fault {};
  bool has_data = false;

  if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput1(&breadcrumbs))
    && breadcrumbs.pHeadAutoBreadcrumbNode != nullptr) {
    LOG_SCOPE_F(INFO, "Command History");

    for (const auto* node = breadcrumbs.pHeadAutoBreadcrumbNode;
      node != nullptr; node = node->pNext) {
      PrintBreadcrumbNode(node);
    }
    has_data = true;
  }

  if (SUCCEEDED(dred->GetPageFaultAllocationOutput(&page_fault))) {
    PrintPageFaultInfo(page_fault);
    has_data = true;
  }

  if (!has_data) {
    LOG_F(WARNING, "No DRED data available");
  }
}
