//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <wrl/client.h>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Config/GraphicsConfig.h>
#include <Oxygen/Core/Types/Frame.h>
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
class Allocator;
} // namespace D3D12MA

namespace oxygen::graphics::d3d12 {

class CommandRecorder;

namespace detail {
  struct InlineRootConstantsDesc {
    std::uint32_t root_parameter_index { 0U };
    std::uint32_t dest_offset_in_32bit_values { 0U };
    std::uint32_t value_count { 0U };

    auto operator==(const InlineRootConstantsDesc&) const -> bool = default;
  };

  struct IndirectCommandSignatureKey {
    graphics::CommandRecorder::IndirectCommandKind kind {
      graphics::CommandRecorder::IndirectCommandKind::kDraw
    };
    std::optional<InlineRootConstantsDesc> inline_root_constants {};
    ID3D12RootSignature* root_signature { nullptr };

    auto operator==(const IndirectCommandSignatureKey&) const -> bool = default;
  };

  struct IndirectCommandSignatureKeyHash {
    auto operator()(const IndirectCommandSignatureKey& key) const noexcept
      -> std::size_t
    {
      auto combine = [](std::size_t& seed, const std::size_t value) {
        seed ^= value + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
      };

      std::size_t seed = std::hash<int> {}(static_cast<int>(key.kind));
      combine(seed,
        std::hash<std::uintptr_t> {}(
          reinterpret_cast<std::uintptr_t>(key.root_signature)));
      combine(seed, std::hash<bool> {}(key.inline_root_constants.has_value()));

      if (key.inline_root_constants.has_value()) {
        const auto& constants = *key.inline_root_constants;
        combine(
          seed, std::hash<std::uint32_t> {}(constants.root_parameter_index));
        combine(seed,
          std::hash<std::uint32_t> {}(constants.dest_offset_in_32bit_values));
        combine(seed, std::hash<std::uint32_t> {}(constants.value_count));
      }

      return seed;
    }
  };
} // namespace detail

class Graphics : public oxygen::Graphics {
  using Base = oxygen::Graphics;

public:
  OXGN_D3D12_API explicit Graphics(const SerializedBackendConfig& config,
    const SerializedPathFinderConfig& path_finder_config);

  OXGN_D3D12_API ~Graphics() override;

  OXYGEN_MAKE_NON_COPYABLE(Graphics)
  OXYGEN_MAKE_NON_MOVABLE(Graphics)

  OXGN_D3D12_NDAPI auto GetDescriptorAllocator() const
    -> const graphics::DescriptorAllocator& override;

  [[nodiscard]] OXGN_D3D12_NDAPI auto GetTimestampQueryProvider() const
    -> observer_ptr<graphics::TimestampQueryProvider> override;

  OXGN_D3D12_NDAPI auto GetReadbackManager() const
    -> observer_ptr<graphics::ReadbackManager> override;

  [[nodiscard]] OXGN_D3D12_NDAPI auto GetFrameCaptureController() const
    -> observer_ptr<graphics::FrameCaptureController> override;

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

  OXGN_D3D12_NDAPI auto CreateSurfaceFromNative(void* native_handle,
    observer_ptr<graphics::CommandQueue> command_queue) const
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
  Graphics()
    : Base("Dummy Graphics Backend")
  {
  }

  OXGN_D3D12_NDAPI auto CreateCommandQueue(const QueueKey& queue_key,
    QueueRole role) -> std::shared_ptr<graphics::CommandQueue> override;

  OXGN_D3D12_NDAPI auto CreateCommandListImpl(
    QueueRole role, std::string_view command_list_name)
    -> std::unique_ptr<graphics::CommandList> override;

private:
  friend class CommandRecorder;

  OXGN_D3D12_NDAPI auto GetOrCreateIndirectCommandSignature(
    const graphics::CommandRecorder::IndirectCommandDesc& command_desc,
    ID3D12RootSignature* current_root_signature, size_t pipeline_hash) const
    -> ID3D12CommandSignature*;

  mutable std::unordered_map<DXGI_FORMAT, uint8_t>
    dxgi_format_plane_count_cache_ {};
  bool enable_vsync_ { true };
  mutable std::unordered_map<detail::IndirectCommandSignatureKey,
    Microsoft::WRL::ComPtr<ID3D12CommandSignature>,
    detail::IndirectCommandSignatureKeyHash>
    indirect_command_signatures_ {};
  std::unique_ptr<graphics::FrameCaptureController>
    frame_capture_controller_ {};
  std::unique_ptr<TimestampQueryBackend> timestamp_query_backend_ {};
  std::unique_ptr<D3D12ReadbackManager> readback_manager_ {};
};

}
