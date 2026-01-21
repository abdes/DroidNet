//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <span>

#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Types/ClearFlags.h>
#include <Oxygen/Graphics/Common/Types/Color.h>
#include <Oxygen/Graphics/Direct3D12/CommandList.h>
#include <Oxygen/Graphics/Direct3D12/Detail/Types.h>
#include <Oxygen/Graphics/Direct3D12/api_export.h>

namespace oxygen::graphics::d3d12 {

namespace detail {

  struct ShaderVisibleHeapInfo {
    D3D12_DESCRIPTOR_HEAP_TYPE heap_type;
    ID3D12DescriptorHeap* heap;
    D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle;

    ShaderVisibleHeapInfo(const D3D12_DESCRIPTOR_HEAP_TYPE type,
      ID3D12DescriptorHeap* heap, const D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle)
      : heap_type(type)
      , heap(heap)
      , gpu_handle(gpu_handle)
    {
    }
  };

} // namespace detail

class CommandRecorder final : public graphics::CommandRecorder {
  using Base = graphics::CommandRecorder;

public:
  CommandRecorder(std::weak_ptr<Graphics> graphics_weak,
    std::shared_ptr<graphics::CommandList> command_list,
    observer_ptr<graphics::CommandQueue> target_queue);

  ~CommandRecorder() = default;

  OXYGEN_MAKE_NON_COPYABLE(CommandRecorder)
  OXYGEN_MAKE_NON_MOVABLE(CommandRecorder)

  //! Returns the underlying native D3D12 command list for cases where backend
  //! implementations need direct access to the ID3D12GraphicsCommandList.
  //! Use sparingly; prefer the CommandRecorder abstraction where possible.
  auto GetD3D12CommandList() const -> ID3D12GraphicsCommandList*;

  auto BeginEvent(std::string_view name) -> void override;
  auto EndEvent() -> void override;
  auto SetMarker(std::string_view name) -> void override;

  auto SetPipelineState(GraphicsPipelineDesc desc) -> void override;
  auto SetPipelineState(ComputePipelineDesc desc) -> void override;
  auto SetGraphicsRootConstantBufferView(uint32_t root_parameter_index,
    uint64_t buffer_gpu_address) -> void override;

  auto SetComputeRootConstantBufferView(uint32_t root_parameter_index,
    uint64_t buffer_gpu_address) -> void override;

  auto SetGraphicsRoot32BitConstant(uint32_t root_parameter_index,
    uint32_t src_data, uint32_t dest_offset_in_32bit_values) -> void override;

  auto SetComputeRoot32BitConstant(uint32_t root_parameter_index,
    uint32_t src_data, uint32_t dest_offset_in_32bit_values) -> void override;

  OXGN_D3D12_API auto SetRenderTargets(
    std::span<NativeView> rtvs, std::optional<NativeView> dsv) -> void override;
  auto SetViewport(const ViewPort& viewport) -> void override;
  auto SetScissors(const Scissors& scissors) -> void override;

  auto SetVertexBuffers(uint32_t num,
    // ReSharper disable once CppRedundantQualifier
    const std::shared_ptr<graphics::Buffer>* vertex_buffers,
    const uint32_t* strides) const -> void override;

  auto Draw(uint32_t vertex_num, uint32_t instances_num, uint32_t vertex_offset,
    uint32_t instance_offset) -> void override;

  auto Dispatch(uint32_t thread_group_count_x, uint32_t thread_group_count_y,
    uint32_t thread_group_count_z) -> void override;

  // ReSharper disable once CppRedundantQualifier
  auto BindIndexBuffer(const graphics::Buffer& buffer, Format format)
    -> void override;

  auto BindFrameBuffer(const Framebuffer& framebuffer) -> void override;
  auto ClearFramebuffer(const Framebuffer& framebuffer,
    std::optional<std::vector<std::optional<Color>>> color_clear_values
    = std::nullopt,
    std::optional<float> depth_clear_value = std::nullopt,
    std::optional<uint8_t> stencil_clear_value = std::nullopt) -> void override;

  //! Clears a depth-stencil view.
  /*!
   \note The \p depth value will be clamped to the range [0.0, 1.0] if
         necessary. If the texture's descriptor has the `use_clear_value` flag
         set, the depth and stencil values will be ignored and the clear
         values, derived from the texture's format, will be used instead.
   */
  auto ClearDepthStencilView(const Texture& texture, const NativeView& dsv,
    ClearFlags clear_flags, float depth, uint8_t stencil) -> void override;

  auto CopyBuffer(
    // ReSharper disable once CppRedundantQualifier
    graphics::Buffer& dst, size_t dst_offset,
    // ReSharper disable once CppRedundantQualifier
    const graphics::Buffer& src, size_t src_offset, size_t size)
    -> void override;

  // Copies from a (staging) buffer into a texture region using GPU copy
  // commands. Currently, supports whole-subresource uploads (tight-packed
  // subresource layout produced by GetCopyableFootprints). Partial-region
  // uploads or mismatching row-pitches are not fully supported and will
  // produce a warning.
  auto CopyBufferToTexture(
    // ReSharper disable once CppRedundantQualifier
    const graphics::Buffer& src, const TextureUploadRegion& region,
    Texture& dst) -> void override;

  auto CopyBufferToTexture(
    // ReSharper disable once CppRedundantQualifier
    const graphics::Buffer& src, std::span<const TextureUploadRegion> regions,
    Texture& dst) -> void override;

  auto CopyTexture(const Texture& src, const TextureSlice& src_slice,
    const TextureSubResourceSet& src_subresources, Texture& dst,
    const TextureSlice& dst_slice,
    const TextureSubResourceSet& dst_subresources) -> void override;

  //! Bind shader-visible descriptor heaps to the underlying D3D12 command
  //! list. This sets up descriptor heaps only (no root-table binding).
  auto SetupDescriptorHeaps(
    std::span<const detail::ShaderVisibleHeapInfo> heaps) const -> void;

  //! Binds root descriptor tables (SetGraphicsRootDescriptorTable /
  //! SetComputeRootDescriptorTable) for the previously bound heaps. This
  //! assumes that descriptor heaps and a compatible root signature have
  //! already been bound to the command list.
  //! @param heaps The shader-visible heaps to bind.
  //! @param is_compute If true, uses SetComputeRootDescriptorTable; otherwise
  //!                   uses SetGraphicsRootDescriptorTable.
  auto SetupDescriptorTables(
    std::span<const detail::ShaderVisibleHeapInfo> heaps, bool is_compute) const
    -> void;

protected:
  auto ExecuteBarriers(std::span<const graphics::detail::Barrier> barriers)
    -> void override;

private:
  [[nodiscard]] auto GetConcreteCommandList() const -> CommandList&;

  std::weak_ptr<Graphics> graphics_weak_;

  size_t graphics_pipeline_hash_ = 0;
  size_t compute_pipeline_hash_ = 0;
};

} // namespace oxygen::graphics::d3d12
