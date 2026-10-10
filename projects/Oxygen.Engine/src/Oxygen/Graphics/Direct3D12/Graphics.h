//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string_view>
#include <unordered_map>

#include <wrl/client.h>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Config/GraphicsConfig.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/AllocationBudget.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/FrameCaptureController.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Direct3D12/Detail/PipelineStateCache.h>
#include <Oxygen/Graphics/Direct3D12/Detail/Types.h>
#include <Oxygen/Graphics/Direct3D12/ReadbackManager.h>
#include <Oxygen/Graphics/Direct3D12/TimestampQueryBackend.h>

// Forward declarations for pipeline management
namespace oxygen::graphics {
class GraphicsPipelineDesc;
class ComputePipelineDesc;
} // namespace oxygen::graphics

namespace oxygen::graphics::d3d12::detail {
class PipelineStateCache;
} // namespace oxygen::graphics::d3d12::detail

// ReSharper disable once CppInconsistentNaming
namespace D3D12MA {
// D3D12MA objects are destroyed by Release(), not public deletion.
class Allocator; // NOLINT(cppcoreguidelines-virtual-class-destructor)
class Allocation; // NOLINT(cppcoreguidelines-virtual-class-destructor)
struct ALLOCATION_DESC;
} // namespace D3D12MA

namespace oxygen::graphics::d3d12 {

class CommandRecorder;
struct NativeLifetime;
struct MemoryStatistics;

class Graphics : public oxygen::Graphics {
  using Base = oxygen::Graphics;

public:
  [[nodiscard]] auto GetNativeLifetime() const
    -> const std::shared_ptr<NativeLifetime>&
  {
    return native_lifetime_;
  }
  OXGN_D3D12_API explicit Graphics(const SerializedBackendConfig& config,
    const SerializedPathFinderConfig& path_finder_config);

  OXGN_D3D12_API ~Graphics() override;

  OXYGEN_MAKE_NON_COPYABLE(Graphics)
  OXYGEN_MAKE_NON_MOVABLE(Graphics)

  OXGN_D3D12_NDAPI auto GetDescriptorAllocator() const
    -> const graphics::DescriptorAllocator& override;

  OXGN_D3D12_NDAPI auto GetTimestampQueryProvider() const
    -> observer_ptr<graphics::TimestampQueryProvider> override;

  OXGN_D3D12_NDAPI auto GetReadbackManager() const
    -> observer_ptr<graphics::ReadbackManager> override;

  OXGN_D3D12_NDAPI auto GetFrameCaptureController() const
    -> observer_ptr<graphics::FrameCaptureController> override;

  OXGN_D3D12_NDAPI auto CreateImGuiGraphicsBackend() const
    -> std::unique_ptr<graphics::imgui::ImGuiGraphicsBackend> override;

  //! Get the V-Sync setting.
  [[nodiscard]] auto IsVSyncEnabled() const noexcept -> bool override
  {
    return enable_vsync_;
  }

  OXGN_D3D12_API auto SetVSyncEnabled(bool enabled) -> void override;

  //=== D3D12 specific factories ===----------------------------------------//

  OXGN_D3D12_NDAPI auto CreateSurface(
    std::weak_ptr<platform::Window> window_weak,
    observer_ptr<graphics::CommandQueue> command_queue) const
    -> std::unique_ptr<Surface> override;

  OXGN_D3D12_NDAPI auto CreateSurfaceFromNative(
    void* native_handle, observer_ptr<graphics::CommandQueue> command_queue)
    -> std::shared_ptr<Surface> override;

  OXGN_D3D12_NDAPI auto CreateTexture(const TextureDesc& desc) const
    -> std::shared_ptr<graphics::Texture> override;

  OXGN_D3D12_NDAPI auto CreateTextureFromNativeObject(
    const TextureDesc& desc, const NativeResource& native) const
    -> std::shared_ptr<graphics::Texture> override;

  OXGN_D3D12_NDAPI auto CreateBuffer(const BufferDesc& desc) const
    -> std::shared_ptr<graphics::Buffer> override;

  OXGN_D3D12_NDAPI auto GetShader(const ShaderRequest& request) const
    -> std::shared_ptr<IShaderByteCode> override;

  //=== Device Manager Internal API ===-------------------------------------//

  //! @{
  //! Device Manager API (module internal)
  OXGN_D3D12_NDAPI virtual auto GetFactory() const -> dx::IFactory*;
  OXGN_D3D12_NDAPI virtual auto GetCurrentDevice() const -> dx::IDevice*;
  OXGN_D3D12_NDAPI virtual auto GetAllocator() const -> D3D12MA::Allocator*;
  //! Query cached allocator counters and current budget estimates on demand.
  //! Does not traverse resources or add collection to the render loop.
  OXGN_D3D12_NDAPI auto GetMemoryStatistics() const -> MemoryStatistics;
  //! Serialize parent-allocator admission with resource creation. The returned
  //! domain charge follows the native allocation through deferred retirement.
  OXGN_D3D12_NDAPI auto AllocateResource(const AllocationBudgetTag& budget,
    const D3D12MA::ALLOCATION_DESC& allocation_desc,
    const D3D12_RESOURCE_DESC& resource_desc,
    D3D12_RESOURCE_STATES initial_state, const D3D12_CLEAR_VALUE* clear_value,
    D3D12MA::Allocation** allocation, ID3D12Resource** resource) const
    -> AllocationReservation;
  //! @}

  //=== D3D12 Helpers ===---------------------------------------------------//

  OXGN_D3D12_NDAPI auto GetFormatPlaneCount(DXGI_FORMAT format) const
    -> uint8_t;

  //=== Pipeline State Management ===--------------------------------------//

  OXGN_D3D12_NDAPI auto GetOrCreateGraphicsPipeline(GraphicsPipelineDesc desc,
    size_t hash) -> detail::PipelineStateCache::Entry;

  OXGN_D3D12_NDAPI auto GetOrCreateComputePipeline(
    ComputePipelineDesc desc, size_t hash) -> detail::PipelineStateCache::Entry;

protected:
  OXGN_D3D12_NDAPI auto CreateCommandRecorder(
    std::shared_ptr<graphics::CommandList> command_list,
    observer_ptr<graphics::CommandQueue> target_queue)
    -> std::unique_ptr<graphics::CommandRecorder> override;

  // Default constructor that does not initialize the backend. Used for testing
  // purposes.
  OXGN_D3D12_API Graphics();

  OXGN_D3D12_NDAPI auto CreateCommandQueue(const QueueKey& queue_key,
    QueueRole role) -> std::shared_ptr<graphics::CommandQueue> override;

  OXGN_D3D12_NDAPI auto CreateCommandListImpl(
    QueueRole role, std::string_view command_list_name)
    -> std::unique_ptr<graphics::CommandList> override;

private:
  std::shared_ptr<NativeLifetime> native_lifetime_;
  friend class CommandRecorder;
  mutable std::mutex resource_allocation_mutex_;

  //! The command signature for one indirect draw or dispatch per command.
  [[nodiscard]] auto GetIndirectCommandSignature(
    graphics::CommandRecorder::IndirectCommandKind kind) const
    -> ID3D12CommandSignature*;
  auto CreateIndirectCommandSignatures() -> void;

  mutable std::unordered_map<DXGI_FORMAT, uint8_t>
    dxgi_format_plane_count_cache_;
  bool enable_vsync_ { true };
  // Indexed by IndirectCommandKind; created with the device.
  std::array<Microsoft::WRL::ComPtr<ID3D12CommandSignature>, 2>
    indirect_command_signatures_;
  std::unique_ptr<graphics::FrameCaptureController> frame_capture_controller_;
  std::unique_ptr<TimestampQueryBackend> timestamp_query_backend_;
  std::unique_ptr<D3D12ReadbackManager> readback_manager_;
};

} // namespace oxygen::graphics::d3d12
