//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <d3d12.h>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Graphics/Direct3D12/Detail/Types.h>

namespace oxygen::graphics::d3d12::testing {

// ReSharper disable CppClangTidyModernizeUseTrailingReturnType
// ReSharper disable once CppClassCanBeFinal - Mock class should not be final
class MockDescriptorHeap : public dx::IDescriptorHeap {
public:
    MockDescriptorHeap() = default;
    virtual ~MockDescriptorHeap() = default;

    OXYGEN_DEFAULT_COPYABLE(MockDescriptorHeap)
    OXYGEN_DEFAULT_MOVABLE(MockDescriptorHeap)

    // clang-format off
    // Mocked methods
    MOCK_METHOD(D3D12_CPU_DESCRIPTOR_HANDLE, GetCPUDescriptorHandleForHeapStart, (), (override));
    MOCK_METHOD(D3D12_GPU_DESCRIPTOR_HANDLE, GetGPUDescriptorHandleForHeapStart, (), (override));
    MOCK_METHOD(D3D12_DESCRIPTOR_HEAP_DESC, GetDesc, (), (override));
    MOCK_METHOD(HRESULT, SetPrivateData, (const GUID&, UINT, const void*), (override));

    // IUnknown (stubbed)
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override { return E_NOINTERFACE; }

    // ID3D12Object (stubbed)
    HRESULT STDMETHODCALLTYPE GetPrivateData(const GUID&, UINT*, void*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(const GUID&, const IUnknown*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetName(LPCWSTR) override { return S_OK; }

    // ID3D12DescriptorHeap (stubbed)
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID, void**) override { return E_NOTIMPL; }
    // clang-format on
};

} // namespace oxygen::graphics::d3d12::testing
