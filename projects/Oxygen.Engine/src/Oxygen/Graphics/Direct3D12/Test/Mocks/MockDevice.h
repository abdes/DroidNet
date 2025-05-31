//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <d3d12.h>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Graphics/Direct3D12/Detail/Types.h>

namespace oxygen::graphics::d3d12::testing {

// ReSharper disable CppClangTidyModernizeUseTrailingReturnType
// ReSharper disable once CppClassCanBeFinal - Mock class should not be final
class MockDevice : public dx::IDevice {
public:
    virtual ~MockDevice() = default;

    // clang-format off
    // Mocked methods
    MOCK_METHOD(HRESULT, CreateDescriptorHeap, (const D3D12_DESCRIPTOR_HEAP_DESC*, REFIID, void**), (override));
    MOCK_METHOD(UINT, GetDescriptorHandleIncrementSize, (D3D12_DESCRIPTOR_HEAP_TYPE), (override));
    MOCK_METHOD(HRESULT, CreateRootSignature, (UINT, const void*, SIZE_T, REFIID, void**), (override));
    MOCK_METHOD(HRESULT, CreateGraphicsPipelineState, (const D3D12_GRAPHICS_PIPELINE_STATE_DESC*, REFIID, void**), (override));
    MOCK_METHOD(HRESULT, CreateComputePipelineState, (const D3D12_COMPUTE_PIPELINE_STATE_DESC*, REFIID, void**), (override));

    // IUnknown (stubbed)
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override { return E_NOINTERFACE; }

    // ID3D12Object (stubbed)
    HRESULT STDMETHODCALLTYPE GetPrivateData(const GUID&, UINT*, void*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetPrivateData(const GUID&, UINT, const void*) override { return S_OK; } // Allow debug name
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(const GUID&, const IUnknown*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetName(LPCWSTR) override { return E_NOTIMPL; } // ID3D12Device (stubbed pure virtual)
    HRESULT STDMETHODCALLTYPE CreateCommandQueue(const D3D12_COMMAND_QUEUE_DESC*, REFIID, void**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE, REFIID, void**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CreateCommandList(UINT, D3D12_COMMAND_LIST_TYPE, ID3D12CommandAllocator*, ID3D12PipelineState*, REFIID, void**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CheckFeatureSupport(D3D12_FEATURE, void*, UINT) override { return E_NOTIMPL; }
    void STDMETHODCALLTYPE CreateConstantBufferView(const D3D12_CONSTANT_BUFFER_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE) override { }
    void STDMETHODCALLTYPE CreateShaderResourceView(ID3D12Resource*, const D3D12_SHADER_RESOURCE_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE) override { }
    void STDMETHODCALLTYPE CreateUnorderedAccessView(ID3D12Resource*, ID3D12Resource*, const D3D12_UNORDERED_ACCESS_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE) override { }
    void STDMETHODCALLTYPE CreateRenderTargetView(ID3D12Resource*, const D3D12_RENDER_TARGET_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE) override { }
    void STDMETHODCALLTYPE CreateDepthStencilView(ID3D12Resource*, const D3D12_DEPTH_STENCIL_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE) override { }
    void STDMETHODCALLTYPE CreateSampler(const D3D12_SAMPLER_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE) override { }
    void STDMETHODCALLTYPE CopyDescriptors(UINT, const D3D12_CPU_DESCRIPTOR_HANDLE*, const UINT*, UINT, const D3D12_CPU_DESCRIPTOR_HANDLE*, const UINT*, D3D12_DESCRIPTOR_HEAP_TYPE) override { }
    void STDMETHODCALLTYPE CopyDescriptorsSimple(UINT, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_DESCRIPTOR_HEAP_TYPE) override { }
    D3D12_RESOURCE_ALLOCATION_INFO STDMETHODCALLTYPE GetResourceAllocationInfo(UINT, UINT, const D3D12_RESOURCE_DESC*) override { return {}; }
    D3D12_HEAP_PROPERTIES STDMETHODCALLTYPE GetCustomHeapProperties(UINT, D3D12_HEAP_TYPE) override { return {}; }
    HRESULT STDMETHODCALLTYPE CreateCommittedResource(const D3D12_HEAP_PROPERTIES*, D3D12_HEAP_FLAGS, const D3D12_RESOURCE_DESC*, D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE*, REFIID, void**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CreateHeap(const D3D12_HEAP_DESC*, REFIID, void**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CreatePlacedResource(ID3D12Heap*, UINT64, const D3D12_RESOURCE_DESC*, D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE*, REFIID, void**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CreateReservedResource(const D3D12_RESOURCE_DESC*, D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE*, REFIID, void**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CreateSharedHandle(ID3D12DeviceChild*, const SECURITY_ATTRIBUTES*, DWORD, LPCWSTR, HANDLE*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE OpenSharedHandle(HANDLE, REFIID, void**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE OpenSharedHandleByName(LPCWSTR, DWORD, HANDLE*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE MakeResident(UINT, ID3D12Pageable* const*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Evict(UINT, ID3D12Pageable* const*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CreateFence(UINT64, D3D12_FENCE_FLAGS, REFIID, void**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetDeviceRemovedReason() override { return E_NOTIMPL; }
    void STDMETHODCALLTYPE GetCopyableFootprints(const D3D12_RESOURCE_DESC*, UINT, UINT, UINT64, D3D12_PLACED_SUBRESOURCE_FOOTPRINT*, UINT*, UINT64*, UINT64*) override { }
    HRESULT STDMETHODCALLTYPE CreateQueryHeap(const D3D12_QUERY_HEAP_DESC*, REFIID, void**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetStablePowerState(BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CreateCommandSignature(const D3D12_COMMAND_SIGNATURE_DESC*, ID3D12RootSignature*, REFIID, void**) override { return E_NOTIMPL; }
    void STDMETHODCALLTYPE GetResourceTiling(ID3D12Resource*, UINT*, D3D12_PACKED_MIP_INFO*, D3D12_TILE_SHAPE*, UINT*, UINT, D3D12_SUBRESOURCE_TILING*) override { }
    LUID STDMETHODCALLTYPE GetAdapterLuid() override { return {}; }
    UINT STDMETHODCALLTYPE GetNodeCount(void) override { return 0; }
    // ID3D12Device1 (stubbed pure virtual)
    HRESULT STDMETHODCALLTYPE CreatePipelineLibrary(const void*, SIZE_T, REFIID, void**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetEventOnMultipleFenceCompletion(ID3D12Fence* const*, const UINT64*, UINT, D3D12_MULTIPLE_FENCE_WAIT_FLAGS, HANDLE) override { return E_NOTIMPL; }
    // ID3D12Device2 (stubbed pure virtual)
    HRESULT STDMETHODCALLTYPE CreatePipelineState(const D3D12_PIPELINE_STATE_STREAM_DESC*, REFIID, void**) override { return E_NOTIMPL; }
    // ID3D12Device3 (stubbed pure virtual)
    HRESULT STDMETHODCALLTYPE OpenExistingHeapFromAddress(const void*, REFIID, void**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE OpenExistingHeapFromFileMapping(HANDLE, REFIID, void**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetResidencyPriority(UINT, ID3D12Pageable* const*, const D3D12_RESIDENCY_PRIORITY*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnqueueMakeResident(D3D12_RESIDENCY_FLAGS, UINT, ID3D12Pageable* const*, ID3D12Fence*, UINT64) override { return E_NOTIMPL; }
    // ID3D12Device4 (stubbed pure virtual)
    HRESULT STDMETHODCALLTYPE CreateCommandList1(UINT, D3D12_COMMAND_LIST_TYPE, D3D12_COMMAND_LIST_FLAGS, REFIID, void**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CreateProtectedResourceSession(const D3D12_PROTECTED_RESOURCE_SESSION_DESC*, REFIID, void**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CreateCommittedResource1(const D3D12_HEAP_PROPERTIES*, D3D12_HEAP_FLAGS, const D3D12_RESOURCE_DESC*, D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE*, ID3D12ProtectedResourceSession*, REFIID, void**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CreateHeap1(const D3D12_HEAP_DESC*, ID3D12ProtectedResourceSession*, REFIID, void**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CreateReservedResource1(const D3D12_RESOURCE_DESC*, D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE*, ID3D12ProtectedResourceSession*, REFIID, void**) override { return E_NOTIMPL; }
    D3D12_RESOURCE_ALLOCATION_INFO STDMETHODCALLTYPE GetResourceAllocationInfo1(UINT, UINT, const D3D12_RESOURCE_DESC*, D3D12_RESOURCE_ALLOCATION_INFO1*) override { return {}; }
    // ID3D12Device5 (stubbed pure virtual)
    HRESULT STDMETHODCALLTYPE CreateLifetimeTracker(ID3D12LifetimeOwner*, REFIID, void**) override { return E_NOTIMPL; }
    void STDMETHODCALLTYPE RemoveDevice() override { }
    HRESULT STDMETHODCALLTYPE EnumerateMetaCommands(UINT*, D3D12_META_COMMAND_DESC*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumerateMetaCommandParameters(const GUID&, D3D12_META_COMMAND_PARAMETER_STAGE, UINT*, UINT*, D3D12_META_COMMAND_PARAMETER_DESC*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CreateMetaCommand(const GUID&, UINT, const void*, SIZE_T, REFIID, void**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CreateStateObject(const D3D12_STATE_OBJECT_DESC*, REFIID, void**) override { return E_NOTIMPL; }
    void STDMETHODCALLTYPE GetRaytracingAccelerationStructurePrebuildInfo(const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS*, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO*) override { }
    D3D12_DRIVER_MATCHING_IDENTIFIER_STATUS STDMETHODCALLTYPE CheckDriverMatchingIdentifier(D3D12_SERIALIZED_DATA_TYPE, const D3D12_SERIALIZED_DATA_DRIVER_MATCHING_IDENTIFIER*) override { return D3D12_DRIVER_MATCHING_IDENTIFIER_STATUS::D3D12_DRIVER_MATCHING_IDENTIFIER_COMPATIBLE_WITH_DEVICE; }
    // ID3D12Device6 (stubbed pure virtual)
    HRESULT STDMETHODCALLTYPE SetBackgroundProcessingMode(D3D12_BACKGROUND_PROCESSING_MODE, D3D12_MEASUREMENTS_ACTION, HANDLE, BOOL*) override { return E_NOTIMPL; }
    // ID3D12Device7 (stubbed pure virtual)
    HRESULT STDMETHODCALLTYPE AddToStateObject(const D3D12_STATE_OBJECT_DESC*, ID3D12StateObject*, REFIID, void**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CreateProtectedResourceSession1(const D3D12_PROTECTED_RESOURCE_SESSION_DESC1*, REFIID, void**) override { return E_NOTIMPL; }
    // ID3D12Device8 (stubbed pure virtual)
    D3D12_RESOURCE_ALLOCATION_INFO STDMETHODCALLTYPE GetResourceAllocationInfo2(UINT, UINT, const D3D12_RESOURCE_DESC1*, D3D12_RESOURCE_ALLOCATION_INFO1*) override { return {}; }
    HRESULT STDMETHODCALLTYPE CreateCommittedResource2(const D3D12_HEAP_PROPERTIES*, D3D12_HEAP_FLAGS, const D3D12_RESOURCE_DESC1*, D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE*, ID3D12ProtectedResourceSession*, REFIID, void**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CreatePlacedResource1(ID3D12Heap*, UINT64, const D3D12_RESOURCE_DESC1*, D3D12_RESOURCE_STATES, const D3D12_CLEAR_VALUE*, REFIID, void**) override { return E_NOTIMPL; }
    void STDMETHODCALLTYPE CreateSamplerFeedbackUnorderedAccessView(ID3D12Resource*, ID3D12Resource*, D3D12_CPU_DESCRIPTOR_HANDLE) override { }
    void STDMETHODCALLTYPE GetCopyableFootprints1(const D3D12_RESOURCE_DESC1*, UINT, UINT, UINT64, D3D12_PLACED_SUBRESOURCE_FOOTPRINT*, UINT*, UINT64*, UINT64*) override { }
    // ID3D12Device9 (stubbed pure virtual)
    HRESULT STDMETHODCALLTYPE CreateShaderCacheSession(const D3D12_SHADER_CACHE_SESSION_DESC*, REFIID, void**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE ShaderCacheControl(D3D12_SHADER_CACHE_KIND_FLAGS, D3D12_SHADER_CACHE_CONTROL_FLAGS) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CreateCommandQueue1(const D3D12_COMMAND_QUEUE_DESC*, REFIID, REFIID, void**) override { return E_NOTIMPL; }
    // clang-format on
};

} // namespace oxygen::graphics::d3d12::testing
