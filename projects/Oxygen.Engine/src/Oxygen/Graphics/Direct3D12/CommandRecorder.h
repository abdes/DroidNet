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

class RenderController;

class CommandRecorder final : public graphics::CommandRecorder {
  using Base = graphics::CommandRecorder;

public:
  CommandRecorder(RenderController* renderer,
    graphics::CommandList* command_list, graphics::CommandQueue* target_queue);

  OXYGEN_D3D12_API ~CommandRecorder() override;

  OXYGEN_MAKE_NON_COPYABLE(CommandRecorder)
  OXYGEN_MAKE_NON_MOVABLE(CommandRecorder)

  void Begin() override;
  auto End() -> graphics::CommandList* override;

  void SetPipelineState(GraphicsPipelineDesc desc) override;
  void SetPipelineState(ComputePipelineDesc desc) override;
  void SetGraphicsRootConstantBufferView(
    uint32_t root_parameter_index, uint64_t buffer_gpu_address) override;

  void SetComputeRootConstantBufferView(
    uint32_t root_parameter_index, uint64_t buffer_gpu_address) override;

  OXYGEN_D3D12_API void SetRenderTargets(
    std::span<NativeObject> rtvs, std::optional<NativeObject> dsv) override;
  void SetViewport(const ViewPort& viewport) override;
  void SetScissors(const Scissors& scissors) override;

  void SetVertexBuffers(uint32_t num,
    const std::shared_ptr<oxygen::graphics::Buffer>* vertex_buffers,
    const uint32_t* strides) const override;
  void Draw(uint32_t vertex_num, uint32_t instances_num, uint32_t vertex_offset,
    uint32_t instance_offset) override;
  void DrawIndexed(uint32_t index_count, uint32_t instance_count,
    uint32_t first_index, int32_t vertex_offset,
    uint32_t first_instance) override;
  void Dispatch(uint32_t thread_group_count_x, uint32_t thread_group_count_y,
    uint32_t thread_group_count_z) override;

  void BindIndexBuffer(const graphics::Buffer& buffer, Format format) override;

  void BindFrameBuffer(
    const oxygen::graphics::Framebuffer& framebuffer) override;
  void ClearFramebuffer(const oxygen::graphics::Framebuffer& framebuffer,
    std::optional<std::vector<std::optional<Color>>> color_clear_values
    = std::nullopt,
    std::optional<float> depth_clear_value = std::nullopt,
    std::optional<uint8_t> stencil_clear_value = std::nullopt) override;

  //! Clears a depth-stencil view.
  /*!
   \note The \p depth value will be clamped to the range [0.0, 1.0] if
         necessary. If the texture's descriptor has the `use_clear_value` flag
         set, the depth and stencil values will be ignored and the clear
         values, derived from the texture's format, will be used instead.
   */
  void ClearDepthStencilView(const oxygen::graphics::Texture& texture,
    const NativeObject& dsv, ClearFlags clear_flags, float depth,
    uint8_t stencil) override;

  void CopyBuffer(oxygen::graphics::Buffer& dst, size_t dst_offset,
    const oxygen::graphics::Buffer& src, size_t src_offset,
    size_t size) override;

  //! Binds the provided shader-visible descriptor heaps to the underlying
  //! D3D12 command list.
  void SetupDescriptorTables(
    std::span<const detail::ShaderVisibleHeapInfo> heaps) const;

protected:
  void ExecuteBarriers(
    std::span<const graphics::detail::Barrier> barriers) override;

private:
  [[nodiscard]] auto GetConcreteCommandList() const -> CommandList*;

  // Root signature creation helpers
  [[nodiscard]] auto CreateBindlessRootSignature(bool is_graphics) const
    -> dx::IRootSignature*;

  RenderController* renderer_;

  size_t graphics_pipeline_hash_ = 0;
  size_t compute_pipeline_hash_ = 0;
};

} // namespace oxygen::graphics::d3d12
