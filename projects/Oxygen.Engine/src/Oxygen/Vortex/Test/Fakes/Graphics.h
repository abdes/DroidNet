//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Detail/Barriers.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Internal/Commander.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/TimestampQueryProvider.h>

namespace oxygen::vortex::testing {

using graphics::Buffer;
using graphics::BufferDesc;
using graphics::BufferMemory;
using graphics::BufferUsage;
using graphics::CommandList;
using graphics::CommandQueue;
using graphics::CommandRecorder;
using graphics::QueueKey;
using graphics::QueueRole;
using graphics::Texture;
using graphics::TextureDesc;
using graphics::TextureUploadRegion;
using graphics::TimestampQueryProvider;

//! Logs buffer copy commands captured by the fake command recorder.
struct BufferCommandLog {
  bool copy_called {
    false,
  };
  Buffer* copy_dst {
    nullptr,
  };
  size_t copy_dst_offset {
    0,
  };
  const Buffer* copy_src {
    nullptr,
  };
  size_t copy_src_offset {
    0,
  };
  size_t copy_size {
    0,
  };
  struct CopyEvent {
    Buffer* dst {
      nullptr,
    };
    size_t dst_offset {
      0,
    };
    const Buffer* src {
      nullptr,
    };
    size_t src_offset {
      0,
    };
    size_t size {
      0,
    };
  };
  std::vector<CopyEvent> copies;
};
struct BufferViewCreationLog {
  struct Event {
    ShaderVisibleIndex slot;
    const std::byte* data;
    std::size_t size;
    std::uint32_t stride;
  };
  std::vector<Event> events;
};
//! Logs buffer->texture copy regions captured by the fake command recorder.
struct TextureCommandLog {
  bool copy_called {
    false,
  };
  const Buffer* src {
    nullptr,
  };
  Texture* dst {
    nullptr,
  };
  std::vector<TextureUploadRegion> regions;
};

struct TextureToTextureCopyLog {
  struct Event {
    const Texture* src {
      nullptr,
    };
    Texture* dst {
      nullptr,
    };
  };
  std::vector<Event> copies;
};

//! Logs graphics PSO descriptor binds captured by the fake command recorder.
struct GraphicsPipelineCommandLog {
  struct Event {
    graphics::GraphicsPipelineDesc desc;
  };
  std::vector<Event> binds;
};

struct ComputePipelineCommandLog {
  struct Event {
    graphics::ComputePipelineDesc desc;
  };
  std::vector<Event> binds;
};

//! Logs graphics root CBV bindings captured by the fake command recorder.
struct RootConstantBufferViewLog {
  struct Event {
    uint32_t root_parameter_index {
      0U,
    };
    uint64_t buffer_gpu_address {
      0U,
    };
  };
  std::vector<Event> binds;
};

struct DrawCommandLog {
  struct Event {
    uint32_t vertex_num {
      0U,
    };
    uint32_t instances_num {
      0U,
    };
    uint32_t vertex_offset {
      0U,
    };
    uint32_t instance_offset {
      0U,
    };
    std::optional<graphics::RasterizerStateDesc> rasterizer;
    std::string pipeline_name;
  };
  std::vector<Event> draws;
};

struct DispatchCommandLog {
  struct Event {
    uint32_t thread_group_count_x {
      0U,
    };
    uint32_t thread_group_count_y {
      0U,
    };
    uint32_t thread_group_count_z {
      0U,
    };
  };
  std::vector<Event> dispatches;
};

struct ClearFramebufferLog {
  struct Event {
    std::size_t color_attachment_count {
      0U,
    };
    bool has_depth_attachment {
      false,
    };
    bool depth_clear_requested {
      false,
    };
    bool stencil_clear_requested {
      false,
    };
  };
  std::vector<Event> clears;
};

struct IndirectCommandLog {
  struct Event {
    const Buffer* argument_buffer {
      nullptr,
    };
    CommandRecorder::IndirectCommandDesc command_desc {};
    CommandRecorder::IndirectExecutionDesc execution_desc {};
  };
  std::vector<Event> draws;
};

//! Logs SRV view creations for bindless indices.
/*! This is used by higher-level tests (e.g., TextureBinder) to observe when a

   descriptor slot is first registered and when it is repointed via

   ResourceRegistry::UpdateView, without adding any test-only API surface to
    production code.

    The log records the bindless heap index (which maps 1:1 to shader-visible
    index in this fake backend) and the Texture instance that produced the
    native view.
*/
struct SrvViewCreationLog {
  struct Event {
    uint32_t index {
      0,
    };
    const Texture* texture {
      nullptr,
    };
    Format view_format {
      Format::kUnknown,
    };
  };
  std::vector<Event> events;
};

//! Lightweight CommandList used by the fake command recorder in tests.
class FakeCommandList final : public CommandList {
public:
  explicit FakeCommandList(const std::string_view name, const QueueRole type)
    : CommandList(name, type)
  {
  }
};

//! Simple CommandQueue that simulates signalling/completion for tests.
class FakeCommandQueue final : public CommandQueue {
public:
  explicit FakeCommandQueue(const std::string_view name, const QueueRole role,
    const observer_ptr<const bool> submission_failure = {})
    : CommandQueue(name)
    , submission_failure_(submission_failure)
    , role_(role)
  {
  }
  auto Signal(const uint64_t value) const -> void override
  {
    current_ = value;
    if (auto_complete_) {
      completed_ = value;
    }
  }
  [[nodiscard]] auto Signal() const -> uint64_t override
  {
    const auto value = ++current_;
    if (auto_complete_) {
      completed_ = value;
    }
    return value;
  }
  auto Wait(uint64_t /*value*/, std::chrono::milliseconds /*timeout*/) const
    -> void override
  {
  }
  auto Wait(uint64_t /*value*/) const -> void override { }
  [[nodiscard]] auto GetCompletedValue() const -> uint64_t override
  {
    return completed_;
  }
  [[nodiscard]] auto GetCurrentValue() const -> uint64_t override
  {
    return current_;
  }
  auto SetAutoComplete(const bool enabled) -> void { auto_complete_ = enabled; }
  auto CompleteThrough(const uint64_t value) -> void { completed_ = value; }
  auto TryGetTimestampFrequency(uint64_t& out_hz) const -> bool override
  {
    out_hz = timestamp_frequency_hz_;
    return true;
  }
  auto Submit(std::shared_ptr<CommandList> command_list) -> void override
  {
    if (submission_failure_ && *submission_failure_) {
      throw std::runtime_error("Injected queue submission failure");
    }
    auto known_states = std::vector<KnownResourceState> {};
    for (const auto& state : command_list->TakeRecordedResourceStates()) {
      known_states.push_back({
        .resource = state.resource,
        .state = state.state,
      });
    }
    AdoptKnownResourceStates(known_states);
  }
  auto Submit(std::span<std::shared_ptr<CommandList>> command_lists)
    -> void override
  {
    for (const auto& command_list : command_lists) {
      Submit(command_list);
    }
  }
  [[nodiscard]] auto GetQueueRole() const -> QueueRole override
  {
    return role_;
  }

private:
  auto SignalImmediate(const uint64_t value) const -> void override
  {
    current_ = value;
    if (auto_complete_) {
      completed_ = value;
    }
  }

  observer_ptr<const bool> submission_failure_;
  QueueRole role_ {
    QueueRole::kGraphics,
  };
  mutable std::atomic<uint64_t> current_ {
    0,
  };
  mutable std::atomic<uint64_t> completed_ {
    0,
  };
  bool auto_complete_ {
    true,
  };
  uint64_t timestamp_frequency_hz_ {
    1'000'000U,
  };
};

class FakeTimestampQueryProvider final : public TimestampQueryProvider {
public:
  auto SetNextTick(const uint64_t next_tick) -> void { next_tick_ = next_tick; }
  auto SetResolveSucceeds(const bool succeeds) -> void
  {
    resolve_succeeds_ = succeeds;
  }

  auto EnsureCapacity(const uint32_t required_query_count) -> bool override
  {
    ticks_.resize(required_query_count, 0U);
    return true;
  }

  [[nodiscard]] auto GetCapacity() const noexcept -> uint32_t override
  {
    return static_cast<uint32_t>(ticks_.size());
  }

  auto WriteTimestamp(CommandRecorder& /*recorder*/, const uint32_t query_slot)
    -> bool override
  {
    if (query_slot >= ticks_.size()) {
      return false;
    }
    ++write_count_;
    ticks_.at(query_slot) = next_tick_;
    next_tick_ += 100U;
    return true;
  }

  auto RecordResolve(CommandRecorder& /*recorder*/,
    const uint32_t used_query_slots, const uint32_t first_query_slot)
    -> bool override
  {
    if (first_query_slot > ticks_.size()
      || used_query_slots > ticks_.size() - first_query_slot) {
      return false;
    }
    ++resolve_count_;
    last_resolved_query_count_ = used_query_slots;
    return resolve_succeeds_;
  }

  [[nodiscard]] auto GetResolvedTicks() const
    -> std::span<const uint64_t> override
  {
    return ticks_;
  }

  [[nodiscard]] auto WriteCount() const noexcept -> uint32_t
  {
    return write_count_;
  }

  [[nodiscard]] auto ResolveCount() const noexcept -> uint32_t
  {
    return resolve_count_;
  }

  [[nodiscard]] auto LastResolvedQueryCount() const noexcept -> uint32_t
  {
    return last_resolved_query_count_;
  }

private:
  std::vector<uint64_t> ticks_;
  uint64_t next_tick_ {
    1000U,
  };
  uint32_t write_count_ {
    0U,
  };
  uint32_t resolve_count_ {
    0U,
  };
  uint32_t last_resolved_query_count_ {
    0U,
  };
  bool resolve_succeeds_ {
    true,
  };
};

//! CommandRecorder that records buffer and texture copy operations for
//! assertions.
class FakeCommandRecorder final : public CommandRecorder {
public:
  FakeCommandRecorder(std::shared_ptr<CommandList> command_list,
    const observer_ptr<CommandQueue> target_queue, BufferCommandLog* buffer_log,
    TextureCommandLog* texture_log, GraphicsPipelineCommandLog* pipeline_log,
    ComputePipelineCommandLog* compute_pipeline_log,
    TextureToTextureCopyLog* texture_copy_log,
    RootConstantBufferViewLog* root_cbv_log, DrawCommandLog* draw_log,
    DispatchCommandLog* dispatch_log,
    ClearFramebufferLog* clear_framebuffer_log,
    IndirectCommandLog* indirect_log)
    : CommandRecorder(std::move(command_list), target_queue)
    , buffer_log_(buffer_log)
    , texture_log_(texture_log)
    , pipeline_log_(pipeline_log)
    , compute_pipeline_log_(compute_pipeline_log)
    , texture_copy_log_(texture_copy_log)
    , root_cbv_log_(root_cbv_log)
    , draw_log_(draw_log)
    , dispatch_log_(dispatch_log)
    , clear_framebuffer_log_(clear_framebuffer_log)
    , indirect_log_(indirect_log)
  {
  }

  void SetRecordingFailureFlag(observer_ptr<const bool> flag) noexcept
  {
    recording_failure_ = flag;
  }

  auto End() noexcept -> std::shared_ptr<CommandList> override
  {
    auto completed = CommandRecorder::End();
    if (recording_failure_ && *recording_failure_) {
      if (completed) {
        completed->OnFailed();
      }
      return {};
    }
    return completed;
  }

  // No-op API
  auto BeginEvent(std::string_view /*name*/) -> void override { }
  auto EndEvent() -> void override { }
  auto SetMarker(std::string_view /*name*/) -> void override { }

  auto SetPipelineState(graphics::GraphicsPipelineDesc desc) -> void override
  {
    current_rasterizer_ = desc.RasterizerState();
    current_pipeline_name_ = desc.GetName();
    if (pipeline_log_ != nullptr) {
      pipeline_log_->binds.push_back(GraphicsPipelineCommandLog::Event {
        .desc = std::move(desc),
      });
    }
  }
  auto SetPipelineState(graphics::ComputePipelineDesc desc) -> void override
  {
    if (compute_pipeline_log_ != nullptr) {
      compute_pipeline_log_->binds.push_back(ComputePipelineCommandLog::Event {
        .desc = std::move(desc),
      });
    }
  }
  auto SetGraphicsRootConstantBufferView(const uint32_t root_parameter_index,
    const uint64_t buffer_gpu_address) -> void override
  {
    if (root_cbv_log_ != nullptr) {
      root_cbv_log_->binds.push_back(RootConstantBufferViewLog::Event {
        .root_parameter_index = root_parameter_index,
        .buffer_gpu_address = buffer_gpu_address,
      });
    }
  }
  auto SetComputeRootConstantBufferView(uint32_t /*root_parameter_index*/,
    uint64_t /*buffer_gpu_address*/) -> void override
  {
  }
  auto SetGraphicsRoot32BitConstant(uint32_t /*root_parameter_index*/,
    uint32_t /*src_data*/, uint32_t /*dest_offset_in_32bit_values*/)
    -> void override
  {
  }
  auto SetComputeRoot32BitConstant(uint32_t /*root_parameter_index*/,
    uint32_t /*src_data*/, uint32_t /*dest_offset_in_32bit_values*/)
    -> void override
  {
  }
  auto SetRenderTargets(std::span<graphics::NativeView> /*rtvs*/,
    std::optional<graphics::NativeView> /*dsv*/) -> void override
  {
  }
  auto SetViewport(const ViewPort& /*viewport*/) -> void override { }
  auto SetScissors(const Scissors& /*scissors*/) -> void override { }
  auto Draw(uint32_t vertex_num, uint32_t instances_num, uint32_t vertex_offset,
    uint32_t instance_offset) -> void override
  {
    if (draw_log_ != nullptr) {
      draw_log_->draws.push_back(DrawCommandLog::Event {
        .vertex_num = vertex_num,
        .instances_num = instances_num,
        .vertex_offset = vertex_offset,
        .instance_offset = instance_offset,
        .rasterizer = current_rasterizer_,
        .pipeline_name = current_pipeline_name_,
      });
    }
  }
  auto Dispatch(uint32_t thread_group_count_x, uint32_t thread_group_count_y,
    uint32_t thread_group_count_z) -> void override
  {
    if (dispatch_log_ != nullptr) {
      dispatch_log_->dispatches.push_back(DispatchCommandLog::Event {
        .thread_group_count_x = thread_group_count_x,
        .thread_group_count_y = thread_group_count_y,
        .thread_group_count_z = thread_group_count_z,
      });
    }
  }
  auto ExecuteIndirect(const Buffer& argument_buffer,
    const IndirectCommandDesc& command_desc,
    const IndirectExecutionDesc& execution_desc) -> void override
  {
    if (indirect_log_ != nullptr) {
      indirect_log_->draws.push_back(IndirectCommandLog::Event {
        .argument_buffer = &argument_buffer,
        .command_desc = command_desc,
        .execution_desc = execution_desc,
      });
    }
  }
  auto SetVertexBuffers(uint32_t /*num*/,
    const std::shared_ptr<Buffer>* /*vertex_buffers*/,
    const uint32_t* /*strides*/) const -> void override
  {
  }
  auto BindIndexBuffer(const Buffer& /*buffer*/, Format /*format*/)
    -> void override
  {
  }
  auto BindFrameBuffer(const graphics::Framebuffer& /*framebuffer*/)
    -> void override
  {
  }
  auto ClearDepthStencilView(const Texture& /*texture*/,
    const graphics::NativeView& /*dsv*/, graphics::ClearFlags /*clear_flags*/,
    float /*depth*/, uint8_t /*stencil*/) -> void override
  {
  }
  auto ClearDepthStencilView(const Texture& /*texture*/,
    const graphics::NativeView& /*dsv*/, graphics::ClearFlags /*clear_flags*/,
    float /*depth*/, uint8_t /*stencil*/,
    std::span<const oxygen::Scissors> /*rects*/) -> void override
  {
  }
  auto ClearFramebuffer(const graphics::Framebuffer& framebuffer,
    std::optional<std::vector<std::optional<graphics::Color>>>
    /*color_clear_values*/,
    std::optional<float> depth_clear_value,
    std::optional<uint8_t> stencil_clear_value) -> void override
  {
    if (clear_framebuffer_log_ != nullptr) {
      const auto& desc = framebuffer.GetDescriptor();
      clear_framebuffer_log_->clears.push_back(ClearFramebufferLog::Event {
        .color_attachment_count = desc.color_attachments.size(),
        .has_depth_attachment = desc.depth_attachment.IsValid(),
        .depth_clear_requested = depth_clear_value.has_value(),
        .stencil_clear_requested = stencil_clear_value.has_value(),
      });
    }
  }

  auto CopyBuffer(Buffer& dst, const size_t dst_offset, const Buffer& src,
    const size_t src_offset, const size_t size) -> void override
  {
    if (buffer_log_ == nullptr) {
      return;
    }
    buffer_log_->copy_called = true;
    buffer_log_->copy_dst = &dst;
    buffer_log_->copy_dst_offset = dst_offset;
    buffer_log_->copy_src = &src;
    buffer_log_->copy_src_offset = src_offset;
    buffer_log_->copy_size = size;
    buffer_log_->copies.push_back(BufferCommandLog::CopyEvent {
      .dst = &dst,
      .dst_offset = dst_offset,
      .src = &src,
      .src_offset = src_offset,
      .size = size,
    });
  }
  auto CopyBufferToTexture(const Buffer& src, const TextureUploadRegion& region,
    Texture& dst) -> void override
  {
    if (texture_log_ == nullptr) {
      return;
    }
    texture_log_->copy_called = true;
    texture_log_->src = &src;
    texture_log_->dst = &dst;
    texture_log_->regions = {
      region,
    };
  }
  auto CopyBufferToTexture(const Buffer& src,
    std::span<const TextureUploadRegion> regions, Texture& dst) -> void override
  {
    if (texture_log_ == nullptr) {
      return;
    }
    texture_log_->copy_called = true;
    texture_log_->src = &src;
    texture_log_->dst = &dst;
    texture_log_->regions.assign(regions.begin(), regions.end());
  }
  auto CopyTextureToBuffer(Buffer& /*dst*/, const Texture& /*src*/,
    const graphics::TextureBufferCopyRegion& /*region*/) -> void override
  {
  }

  // No-op copy texture for tests (satisfies abstract interface)
  auto CopyTexture(const Texture& src,
    const graphics::TextureSlice& /*src_slice*/,
    const graphics::TextureSubResourceSet& /*src_subresources*/, Texture& dst,
    const graphics::TextureSlice& /*dst_slice*/,
    const graphics::TextureSubResourceSet& /*dst_subresources*/)
    -> void override
  {
    if (texture_copy_log_ != nullptr) {
      texture_copy_log_->copies.push_back(TextureToTextureCopyLog::Event {
        .src = &src,
        .dst = &dst,
      });
    }
  }

protected:
  auto ExecuteBarriers(std::span<const graphics::detail::Barrier> /*barriers*/)
    -> void override
  {
  }

private:
  observer_ptr<const bool> recording_failure_;
  std::optional<graphics::RasterizerStateDesc> current_rasterizer_;
  std::string current_pipeline_name_;
  BufferCommandLog* buffer_log_ {
    nullptr,
  };
  TextureCommandLog* texture_log_ {
    nullptr,
  };
  GraphicsPipelineCommandLog* pipeline_log_ {
    nullptr,
  };
  ComputePipelineCommandLog* compute_pipeline_log_ {
    nullptr,
  };
  TextureToTextureCopyLog* texture_copy_log_ {
    nullptr,
  };
  RootConstantBufferViewLog* root_cbv_log_ {
    nullptr,
  };
  DrawCommandLog* draw_log_ {
    nullptr,
  };
  DispatchCommandLog* dispatch_log_ {
    nullptr,
  };
  ClearFramebufferLog* clear_framebuffer_log_ {
    nullptr,
  };
  IndirectCommandLog* indirect_log_ {
    nullptr,
  };
};

// Minimal in-memory descriptor allocator for tests
class MiniDescriptorAllocator final : public graphics::DescriptorAllocator {
public:
  MiniDescriptorAllocator() = default;
  ~MiniDescriptorAllocator() override = default;
  OXYGEN_MAKE_NON_COPYABLE(MiniDescriptorAllocator)
  OXYGEN_MAKE_NON_MOVABLE(MiniDescriptorAllocator)

  auto AllocateRaw(const graphics::ResourceViewType view_type,
    const graphics::DescriptorVisibility visibility)
    -> graphics::DescriptorAllocationHandle override
  {
    const auto key = Key(view_type, visibility);
    auto& state = domains_[key];
    const auto index = state.next_index++;
    return CreateRawDescriptorHandle(
      bindless::HeapIndex {
        index,
      },
      view_type, visibility);
  }

  auto AllocateBindless(const bindless::DomainToken domain,
    const graphics::ResourceViewType view_type)
    -> graphics::DescriptorAllocationHandle override
  {
    auto& state = bindless_domains_[domain.get()];
    const auto local_slot = state.next_index++;
    state.active_counts[view_type] += 1U;
    return CreateBindlessHandle(
      graphics::DescriptorAllocationHandle::PackBindlessSlot(
        domain, local_slot),
      domain, view_type);
  }

  auto Release(graphics::DescriptorAllocationHandle& handle) -> void override
  {
    if (handle.IsBindless()) {
      auto it = bindless_domains_.find(handle.GetDomain().get());
      if (it != bindless_domains_.end()) {
        auto count_it = it->second.active_counts.find(handle.GetViewType());
        if (count_it != it->second.active_counts.end()
          && count_it->second > 0U) {
          count_it->second -= 1U;
        }
      }
    }
    handle.Invalidate();
  }

  auto CopyDescriptor(const graphics::DescriptorAllocationHandle& /*source*/,
    const graphics::DescriptorAllocationHandle& /*destination*/)
    -> void override
  {
  }

  [[nodiscard]] auto GetRemainingDescriptorsCount(
    graphics::ResourceViewType /*view_type*/,
    graphics::DescriptorVisibility /*visibility*/) const
    -> bindless::Count override
  {
    return bindless::Count {
      1'000'000,
    }; // ample room
  }

  [[nodiscard]] auto GetDomainBaseIndex(bindless::DomainToken domain) const
    -> bindless::ShaderVisibleIndex override
  {
    const auto* const desc = bindless::generated::TryGetDomainDesc(domain);
    return desc != nullptr
      ? bindless::ShaderVisibleIndex { desc->shader_index_base, }
      : bindless::ShaderVisibleIndex { 0U, };
  }

  [[nodiscard]] auto ReserveRaw(const graphics::ResourceViewType view_type,
    const graphics::DescriptorVisibility visibility, bindless::Count count)
    -> std::optional<bindless::HeapIndex> override
  {
    if (count.get() == 0) {
      return std::nullopt;
    }
    const auto key = Key(view_type, visibility);
    auto& state = domains_[key];
    const auto base = state.next_index;
    state.next_index += count.get();
    return bindless::HeapIndex {
      base,
    };
  }

  [[nodiscard]] auto Contains(
    const graphics::DescriptorAllocationHandle& handle) const -> bool override
  {
    return handle.IsValid();
  }

  [[nodiscard]] auto GetAllocatedDescriptorsCount(
    const graphics::ResourceViewType view_type,
    const graphics::DescriptorVisibility visibility) const
    -> bindless::Count override
  {
    uint32_t total = 0U;
    const auto key = Key(view_type, visibility);
    auto it = domains_.find(key);
    if (it != domains_.end()) {
      total += it->second.next_index;
    }
    if (visibility == graphics::DescriptorVisibility::kShaderVisible) {
      for (const auto& [_, state] : bindless_domains_) {
        auto active_it = state.active_counts.find(view_type);
        if (active_it != state.active_counts.end()) {
          total += active_it->second;
        }
      }
    }
    return bindless::Count {
      total,
    };
  }

  [[nodiscard]] auto GetShaderVisibleIndex(
    const graphics::DescriptorAllocationHandle& handle) const noexcept
    -> bindless::ShaderVisibleIndex override
  {
    if (handle.IsBindless()) {
      const auto* const desc
        = bindless::generated::TryGetDomainDesc(handle.GetDomain());
      return desc != nullptr
        ? bindless::ShaderVisibleIndex { desc->shader_index_base
            + handle.GetLocalSlot(), }
        : oxygen::kInvalidShaderVisibleIndex;
    }
    constexpr uint32_t kRawHeapStride = 4096U;
    const auto raw_base
      = handle.GetViewType() == graphics::ResourceViewType::kSampler
      ? bindless::generated::kSamplersCapacity
      : (bindless::generated::kTexturesShaderIndexBase
          + bindless::generated::kTexturesCapacity
          + (static_cast<uint32_t>(handle.GetViewType()) * kRawHeapStride));
    return bindless::ShaderVisibleIndex {
      raw_base + handle.GetBindlessHandle().get(),
    };
  }

private:
  struct DomainState {
    uint32_t next_index {
      0,
    };
    std::unordered_map<graphics::ResourceViewType, uint32_t> active_counts;
  };
  using DomainKey = uint64_t;
  static constexpr auto Key(graphics::ResourceViewType vt,
    graphics::DescriptorVisibility vis) -> DomainKey
  {
    return (static_cast<DomainKey>(static_cast<uint32_t>(vt)) << 32)
      | static_cast<DomainKey>(static_cast<uint32_t>(vis));
  }
  std::unordered_map<DomainKey, DomainState> domains_;
  std::unordered_map<uint16_t, DomainState> bindless_domains_;
};

//! Fake Graphics implementation providing staging buffers, queues and recorders
//! for upload tests.
namespace detail {
  // Base classes are destroyed in reverse declaration order. Keep the allocator
  // alive until Graphics has released its registry-owned descriptor handles.
  class FakeGraphicsAllocatorOwner {
  protected:
    mutable std::unique_ptr<MiniDescriptorAllocator> descriptor_allocator_ {
      std::make_unique<MiniDescriptorAllocator>(),
    };
  };
} // namespace detail

class FakeGraphics final : private detail::FakeGraphicsAllocatorOwner,
                           public Graphics {
public:
  FakeGraphics()
    : Graphics("FakeGraphics")
  {
  }
  OXYGEN_MAKE_NON_COPYABLE(FakeGraphics)
  OXYGEN_MAKE_NON_MOVABLE(FakeGraphics)

  ~FakeGraphics() override
  {
    GetDeferredReclaimer().ProcessAllDeferredReleases();
  }
  // Test-only failure injection hooks
  void SetFailMap(const bool v) { fail_map_ = v; }
  void SetFailConstantBufferViews(const bool value)
  {
    fail_constant_buffer_views_ = value;
  }
  void SetFailSubmission(bool value) { fail_submission_ = value; }
  void SetFailRecording(bool value) { fail_recording_ = value; }
  void SetThrowOnCreateBuffer(const bool v) { throw_on_create_buffer_ = v; }
  auto GetDescriptorAllocator() const
    -> const graphics::DescriptorAllocator& override
  {
    return *descriptor_allocator_;
  }
  using Graphics::GetDescriptorAllocator;
  [[nodiscard]] auto GetTimestampQueryProvider() const
    -> observer_ptr<TimestampQueryProvider> override
  {
    return observer_ptr<TimestampQueryProvider>((&timestamp_query_provider_));
  }
  [[nodiscard]] auto GetTimestampQueryProvider() -> FakeTimestampQueryProvider&
  {
    return timestamp_query_provider_;
  }
  [[nodiscard]] auto CreateSurface(
    std::weak_ptr<platform::Window> /*window_weak*/,
    observer_ptr<CommandQueue> /*command_queue*/) const
    -> std::unique_ptr<graphics::Surface> override
  {
    return {};
  }
  [[nodiscard]] auto CreateSurfaceFromNative(
    void* /*native_handle*/, observer_ptr<CommandQueue> /*command_queue*/) const
    -> std::shared_ptr<graphics::Surface> override
  {
    return {};
  }
  [[nodiscard]] auto GetShader(const graphics::ShaderRequest& /*request*/) const
    -> std::shared_ptr<graphics::IShaderByteCode> override
  {
    return {};
  }
  [[nodiscard]] auto CreateTexture(const TextureDesc& desc) const
    -> std::shared_ptr<Texture> override
  {
    //! In-memory texture used by the fake graphics for uploads.
    class FakeTexture final : public Texture {
      OXYGEN_TYPED(FakeTexture)
    public:
      FakeTexture(const std::string_view name, TextureDesc desc,
        SrvViewCreationLog* srv_view_log)
        : Texture(name)
        , desc_(std::move(desc))
        , srv_view_log_(srv_view_log)
      {
      }

      [[nodiscard]] auto GetDescriptor() const -> const TextureDesc& override
      {
        return desc_;
      }

      [[nodiscard]] auto GetNativeResource() const
        -> graphics::NativeResource override
      {

        return {
          this,
          Texture::ClassTypeId(),
        };
      }

    protected:
      [[nodiscard]] auto CreateShaderResourceView(
        const graphics::DescriptorAllocationHandle& view_handle, Format format,
        TextureType /*dimension*/,
        graphics::TextureSubResourceSet /*sub_resources*/) const
        -> graphics::NativeView override
      {
        if (srv_view_log_ != nullptr) {
          const auto index = view_handle.GetAllocator() != nullptr
            ? view_handle.GetAllocator()
                ->GetShaderVisibleIndex(view_handle)
                .get()
            : view_handle.GetBindlessHandle().get();
          srv_view_log_->events.push_back(SrvViewCreationLog::Event {
            .index = index,
            .texture = this,
            .view_format = format,
          });
        }
        const auto raw_view_id = (static_cast<uint64_t>(static_cast<uint32_t>(
                                    view_handle.GetViewType()))
                                   << 32U)
          | static_cast<uint64_t>(view_handle.GetBindlessHandle().get() + 1U);
        return {
          raw_view_id,
          Texture::ClassTypeId(),
        };
      }
      [[nodiscard]] auto CreateUnorderedAccessView(
        const graphics::DescriptorAllocationHandle& view_handle,
        Format /*format*/, TextureType /*dimension*/,
        graphics::TextureSubResourceSet /*sub_resources*/) const
        -> graphics::NativeView override
      {
        const auto raw_view_id = (static_cast<uint64_t>(static_cast<uint32_t>(
                                    view_handle.GetViewType()))
                                   << 32U)
          | static_cast<uint64_t>(view_handle.GetBindlessHandle().get() + 1U);
        return {
          raw_view_id,
          Texture::ClassTypeId(),
        };
      }
      [[nodiscard]] auto CreateRenderTargetView(
        const graphics::DescriptorAllocationHandle& view_handle,
        Format /*format*/,
        graphics::TextureSubResourceSet /*sub_resources*/) const
        -> graphics::NativeView override
      {
        const auto raw_view_id = (static_cast<uint64_t>(static_cast<uint32_t>(
                                    view_handle.GetViewType()))
                                   << 32U)
          | static_cast<uint64_t>(view_handle.GetBindlessHandle().get() + 1U);
        return {
          raw_view_id,
          Texture::ClassTypeId(),
        };
      }
      [[nodiscard]] auto CreateDepthStencilView(
        const graphics::DescriptorAllocationHandle& view_handle,
        Format /*format*/, graphics::TextureSubResourceSet /*sub_resources*/,
        bool /*is_read_only*/) const -> graphics::NativeView override
      {
        const auto raw_view_id = (static_cast<uint64_t>(static_cast<uint32_t>(
                                    view_handle.GetViewType()))
                                   << 32U)
          | static_cast<uint64_t>(view_handle.GetBindlessHandle().get() + 1U);
        return {
          raw_view_id,
          Texture::ClassTypeId(),
        };
      }

    private:
      TextureDesc desc_ {};
      SrvViewCreationLog* srv_view_log_ {
        nullptr,
      };
    };

    return std::make_shared<FakeTexture>("FakeTexture", desc, &srv_view_log_);
  }
  [[nodiscard]] auto CreateTextureFromNativeObject(const TextureDesc& /*desc*/,
    const graphics::NativeResource& /*native*/) const
    -> std::shared_ptr<Texture> override
  {
    return {};
  }
  [[nodiscard]] auto CreateBuffer(const BufferDesc& desc) const
    -> std::shared_ptr<Buffer> override
  {
    if (throw_on_create_buffer_) {
      throw std::runtime_error("FakeGraphics: CreateBuffer forced failure");
    }
    //! In-memory staging buffer used by the fake graphics for uploads.
    class FakeBuffer final : public Buffer {
      OXYGEN_TYPED(FakeBuffer)
    public:
      FakeBuffer(const std::string_view name, const uint64_t size,
        const BufferUsage usage, const BufferMemory memory,
        const bool map_should_fail, BufferViewCreationLog* srv_log,
        observer_ptr<const bool> constant_view_failure)
        : Buffer(name)
        , map_should_fail_(map_should_fail)
        , srv_log_(srv_log)
        , constant_view_failure_(constant_view_failure)
      {
        desc_.size_bytes = size;
        desc_.usage = usage;
        desc_.memory = memory;
      }
      // Buffer requires a string-bearing descriptor returned by value under
      // noexcept. NOLINTNEXTLINE(bugprone-exception-escape)
      [[nodiscard]] auto GetDescriptor() const noexcept -> BufferDesc override
      {
        return desc_;
      }
      [[nodiscard]] auto GetNativeResource() const
        -> graphics::NativeResource override
      {

        return {
          this,
          Buffer::ClassTypeId(),
        };
      }
      auto Update(const void* data, const uint64_t size, const uint64_t offset)
        -> void override
      {
        if (offset <= storage_.size() && size <= storage_.size() - offset) {
          std::memcpy(
            std::span {
              storage_,
            }
              .subspan(offset)
              .data(),
            data, size);
        }
      }
      [[nodiscard]] auto GetSize() const noexcept -> uint64_t override
      {
        return desc_.size_bytes;
      }
      [[nodiscard]] auto GetUsage() const noexcept -> BufferUsage override
      {
        return desc_.usage;
      }
      [[nodiscard]] auto GetMemoryType() const noexcept -> BufferMemory override
      {
        return desc_.memory;
      }
      [[nodiscard]] auto IsMapped() const noexcept -> bool override
      {
        return mapped_;
      }
      [[nodiscard]] auto GetGPUVirtualAddress() const -> uint64_t override
      {
        // Use object identity as an opaque fake GPU address, never
        // dereferenced.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        return static_cast<uint64_t>(reinterpret_cast<std::uintptr_t>(this));
      }

    protected:
      auto DoMap(uint64_t /*offset*/, uint64_t /*size*/) -> void* override
      {
        if (map_should_fail_) {
          return nullptr;
        }
        if (!mapped_) {
          storage_.resize(desc_.size_bytes);
          mapped_ = true;
        }
        return storage_.data();
      }

      auto DoUnMap() noexcept -> void override
      {
        mapped_ = false;
        // Don't clear storage_ to maintain data integrity for testing
      }

      [[nodiscard]] auto CreateConstantBufferView(
        const graphics::DescriptorAllocationHandle& view_handle,
        const graphics::BufferRange& range) const
        -> graphics::NativeView override
      {
        if (constant_view_failure_ && *constant_view_failure_) {
          return {};
        }
        if ((srv_log_ != nullptr) && mapped_
          && range.offset_bytes <= storage_.size()
          && range.size_bytes <= storage_.size() - range.offset_bytes) {
          srv_log_->events.push_back({ .slot
            = view_handle.GetAllocator()->GetShaderVisibleIndex(view_handle),
            .data = std::span { storage_, }.subspan(range.offset_bytes).data(),
            .size = static_cast<std::size_t>(range.size_bytes),
            .stride = 0U, });
        }
        return {
          this,
          Buffer::ClassTypeId(),
        };
      }
      [[nodiscard]] auto CreateShaderResourceView(
        const graphics::DescriptorAllocationHandle& view_handle,
        Format /*format*/, graphics::BufferRange range, uint32_t stride) const
        -> graphics::NativeView override
      {
        if ((srv_log_ != nullptr) && mapped_
          && range.offset_bytes <= storage_.size()
          && range.size_bytes <= storage_.size() - range.offset_bytes) {
          srv_log_->events.push_back({
            .slot
            = view_handle.GetAllocator()->GetShaderVisibleIndex(view_handle),
            .data = std::span { storage_, }.subspan(range.offset_bytes).data(),
            .size = static_cast<std::size_t>(range.size_bytes),
            .stride = stride,
          });
        }
        return {
          this,
          Buffer::ClassTypeId(),
        };
      }
      [[nodiscard]] auto CreateUnorderedAccessView(
        const graphics::DescriptorAllocationHandle& /*view_handle*/,
        Format /*format*/, graphics::BufferRange /*range*/,
        uint32_t /*stride*/) const -> graphics::NativeView override
      {
        return {
          this,
          Buffer::ClassTypeId(),
        };
      }

    private:
      BufferDesc desc_ {};
      bool mapped_ {
        false,
      };
      bool map_should_fail_ {
        false,
      };
      BufferViewCreationLog* srv_log_ {
        nullptr,
      };
      observer_ptr<const bool> constant_view_failure_;
      std::vector<std::byte> storage_;
    };
    return std::make_shared<FakeBuffer>("Staging", desc.size_bytes, desc.usage,
      desc.memory, fail_map_, &buffer_view_log_,
      make_observer(&fail_constant_buffer_views_));
  }
  auto CreateCommandQueues(const graphics::QueuesStrategy& queue_strategy)
    -> void override
  {
    queue_strategy_ = queue_strategy.Clone();
    queues_.clear();

    for (const auto& spec : queue_strategy.Specifications()) {
      if (queues_.contains(spec.key)) {
        continue;
      }

      queues_[spec.key] = CreateCommandQueue(spec.key, spec.role);
    }
    Graphics::CreateCommandQueues(queue_strategy);
  }
  auto QueueKeyFor(const graphics::QueueRole role) const
    -> graphics::QueueKey override
  {
    return queue_strategy_ ? queue_strategy_->KeyFor(role) : QueueKey {};
  }
  auto GetCommandQueue(const QueueKey& key) const
    -> observer_ptr<CommandQueue> override
  {
    auto it = queues_.find(key);
    if (it == queues_.end()) {
      return {};
    }
    return oxygen::observer_ptr<CommandQueue>(it->second.get());
  }
  auto GetCommandQueue(const QueueRole role) const
    -> observer_ptr<CommandQueue> override
  {
    if (queue_strategy_) {
      if (const auto it = queues_.find(queue_strategy_->KeyFor(role));
        it != queues_.end()) {
        return oxygen::observer_ptr<CommandQueue>(it->second.get());
      }
    }

    for (const auto& v : queues_ | std::views::values) {
      if (v->GetQueueRole() == role) {
        return oxygen::observer_ptr<CommandQueue>(v.get());
      }
    }
    return {};
  }
  auto FlushCommandQueues() -> void override { }
  auto AcquireCommandRecorder(const QueueKey& queue_key,
    std::string_view command_list_name,
    graphics::SubmissionPolicy policy
    = graphics::SubmissionPolicy::kOnScopeExit)
    -> graphics::CommandRecording override
  {
    auto queue = GetCommandQueue(queue_key);
    auto list = std::make_shared<FakeCommandList>(
      command_list_name, queue ? queue->GetQueueRole() : QueueRole::kGraphics);
    auto recorder = std::make_unique<FakeCommandRecorder>(list, queue,
      &buffer_log_, &texture_log_, &graphics_pipeline_log_,
      &compute_pipeline_log_, &texture_copy_log_, &root_cbv_log_, &draw_log_,
      &dispatch_log_, &clear_framebuffer_log_, &indirect_log_);
    recorder->SetRecordingFailureFlag(make_observer(&fail_recording_));
    return GetComponent<graphics::internal::Commander>().PrepareCommandRecorder(
      std::move(recorder), policy);
  }

  BufferCommandLog buffer_log_ {};
  mutable BufferViewCreationLog buffer_view_log_ {};
  TextureCommandLog texture_log_ {};
  GraphicsPipelineCommandLog graphics_pipeline_log_ {};
  ComputePipelineCommandLog compute_pipeline_log_ {};
  TextureToTextureCopyLog texture_copy_log_ {};
  RootConstantBufferViewLog root_cbv_log_ {};
  DrawCommandLog draw_log_ {};
  DispatchCommandLog dispatch_log_ {};
  ClearFramebufferLog clear_framebuffer_log_ {};
  IndirectCommandLog indirect_log_ {};
  mutable SrvViewCreationLog srv_view_log_ {};
  std::map<QueueKey, std::shared_ptr<CommandQueue>> queues_;
  std::unique_ptr<graphics::QueuesStrategy> queue_strategy_;
  mutable FakeTimestampQueryProvider timestamp_query_provider_ {};
  // Test injection flags (mutable to allow const CreateBuffer)
  bool fail_submission_ {
    false,
  };
  bool fail_recording_ {
    false,
  };
  bool fail_constant_buffer_views_ { false };
  mutable bool fail_map_ {
    false,
  };
  mutable bool throw_on_create_buffer_ {
    false,
  };

protected:
  [[nodiscard]] auto CreateCommandQueue(const QueueKey& queue_name,
    const QueueRole role) -> std::shared_ptr<CommandQueue> override
  {
    if (const auto found = queues_.find(queue_name); found != queues_.end()) {
      return found->second;
    }
    const auto* const name = role == QueueRole::kTransfer ? "CopyQ" : "GfxQ";
    return std::make_shared<FakeCommandQueue>(
      name, role, make_observer(&fail_submission_));
  }
  [[nodiscard]] auto CreateCommandListImpl(
    QueueRole /*role*/, std::string_view /*command_list_name*/)
    -> std::unique_ptr<CommandList> override
  {
    return {};
  }
  [[nodiscard]] auto CreateCommandRecorder(
    std::shared_ptr<CommandList> /*command_list*/,
    observer_ptr<CommandQueue> /*target_queue*/)
    -> std::unique_ptr<CommandRecorder> override
  {
    return {};
  }
};

} // namespace oxygen::vortex::testing
