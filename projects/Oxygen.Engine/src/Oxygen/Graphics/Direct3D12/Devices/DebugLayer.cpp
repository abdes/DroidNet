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
#include <wrl/client.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/StringUtils.h>
#include <Oxygen/Base/Windows/ComError.h>
#include <Oxygen/Graphics/Common/ObjectRelease.h>
#include <Oxygen/Graphics/Direct3D12/Detail/Types.h>
#include <Oxygen/Graphics/Direct3D12/Devices/DebugLayer.h>

using oxygen::graphics::d3d12::DebugLayer;
using oxygen::windows::ThrowOnFailed;

DebugLayer::DebugLayer(const bool enable_validation) noexcept
{
  InitializeDebugLayer(enable_validation);
  InitializeDred();
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
}

void DebugLayer::InitializeDebugLayer(const bool enable_validation) noexcept
{
  // Enable the Direct3D12 debug layer
  if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&d3d12_debug_)))) {
    d3d12_debug_->EnableDebugLayer();
    if (enable_validation) {
      d3d12_debug_->SetEnableGPUBasedValidation(TRUE);
    }
  } else {
    LOG_F(WARNING, "Failed to enable the debug layer");
  }

  // Enable the DXGI leak tracking debug layer
  if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgi_debug_)))) {
    dxgi_debug_->EnableLeakTrackingForThread();

    // Setup debugger breakpoints on errors and warnings
#if !defined(NDEBUG)
    if ((::IsDebuggerPresent() != 0)
      && SUCCEEDED(
        DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgi_info_queue_)))) {
      ThrowOnFailed(dxgi_info_queue_->SetBreakOnSeverity(
        DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, 1));
      ThrowOnFailed(dxgi_info_queue_->SetBreakOnSeverity(
        DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, 1));
    }
#endif
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
