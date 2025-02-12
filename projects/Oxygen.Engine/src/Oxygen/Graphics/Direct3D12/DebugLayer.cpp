//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Graphics/Direct3d12/DebugLayer.h"

#include "Oxygen/Base/Windows/ComError.h"
#include "Oxygen/Graphics/Common/ObjectRelease.h"
#include "Oxygen/Graphics/Direct3d12/Forward.h"

using namespace oxygen::graphics::d3d12;
using oxygen::windows::ThrowOnFailed;

void DebugLayer::OnInitialize(const bool enable, const bool enable_validation)
{
    if (!enable) {
        return;
    }

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
#ifdef _DEBUG
        if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgi_info_queue_)))) {
            ThrowOnFailed(dxgi_info_queue_->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, true));
            ThrowOnFailed(dxgi_info_queue_->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, true));
            ThrowOnFailed(dxgi_info_queue_->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_WARNING, true));
        }
#endif
    } else {
        LOG_F(WARNING, "Failed to enable the DXGI debug layer");
    }
}

void DebugLayer::OnShutdown() noexcept
{
#ifdef _DEBUG
    ThrowOnFailed(dxgi_info_queue_->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, false));
    ThrowOnFailed(dxgi_info_queue_->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, false));
    ThrowOnFailed(dxgi_info_queue_->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_WARNING, false));
#endif

    OutputDebugString("===-- LIVE OBJECTS REPORT -----------------------------------------------===\n");
    try {
        ThrowOnFailed(dxgi_debug_->ReportLiveObjects(
            DXGI_DEBUG_ALL,
            static_cast<DXGI_DEBUG_RLO_FLAGS>(
                DXGI_DEBUG_RLO_SUMMARY | DXGI_DEBUG_RLO_DETAIL | DXGI_DEBUG_RLO_IGNORE_INTERNAL)));
        OutputDebugString("===----------------------------------------------------------------------===\n");
    } catch (const windows::ComError& e) {
        OutputDebugStringA(e.what());
        OutputDebugString("===-- FAILED ------------------------------------------------------------===\n");
    }

    ObjectRelease(d3d12_debug_);
    ObjectRelease(dxgi_info_queue_);
    ObjectRelease(dxgi_debug_);
}
