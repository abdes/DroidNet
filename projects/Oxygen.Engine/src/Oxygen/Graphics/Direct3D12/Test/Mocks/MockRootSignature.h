//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <d3d12.h>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Graphics/Direct3D12/Detail/Types.h>

namespace oxygen::graphics::d3d12::testing {

// ReSharper disable CppClangTidyModernizeUseTrailingReturnType
// ReSharper disable once CppClassCanBeFinal - Mock class should not be final
class MockRootSignature : public dx::IRootSignature {
public:
    MockRootSignature() = default;
    virtual ~MockRootSignature() = default;

    OXYGEN_DEFAULT_COPYABLE(MockRootSignature)
    OXYGEN_DEFAULT_MOVABLE(MockRootSignature)

    // clang-format off
    // IUnknown (stubbed)
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override { return E_NOINTERFACE; }

    // ID3D12Object (stubbed)
    HRESULT STDMETHODCALLTYPE GetPrivateData(const GUID&, UINT*, void*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetPrivateData(const GUID&, UINT, const void*) override { return S_OK; } // Allow debug name
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(const GUID&, const IUnknown*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetName(LPCWSTR) override { return S_OK; }

    // ID3D12DeviceChild (stubbed)
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID, void**) override { return E_NOTIMPL; }

    // ID3D12RootSignature has no additional methods beyond ID3D12DeviceChild
    // clang-format on
};

} // namespace oxygen::graphics::d3d12::testing
