//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Detail/Barriers.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <optional>
#include <unordered_map>

#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace oxygen::renderer::testing {

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

//! Logs buffer copy commands captured by the fake command recorder.
struct BufferCommandLog {
  bool copy_called { false };
  Buffer* copy_dst { nullptr };
  size_t copy_dst_offset { 0 };
  const Buffer* copy_src { nullptr };
  size_t copy_src_offset { 0 };
  size_t copy_size { 0 };
  struct CopyEvent {
    Buffer* dst { nullptr };
    size_t dst_offset { 0 };
    const Buffer* src { nullptr };
    size_t src_offset { 0 };
    size_t size { 0 };
  };
  std::vector<CopyEvent> copies;
};
//! Logs buffer->texture copy regions captured by the fake command recorder.
struct TextureCommandLog {
  bool copy_called { false };
  const Buffer* src { nullptr };
  Texture* dst { nullptr };
  std::vector<TextureUploadRegion> regions;
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
    uint32_t index { 0 };
    const Texture* texture { nullptr };
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
  explicit FakeCommandQueue(const std::string_view name, const QueueRole role)
    : CommandQueue(name)
    , role_(role)
  {
  }
  auto Signal(const uint64_t value) const -> void override { current_ = value; }
  [[nodiscard]] auto Signal() const -> uint64_t override { return ++current_; }
  auto Wait(uint64_t /*value*/, std::chrono::milliseconds /*timeout*/) const
    -> void override
  {
  }
  auto Wait(uint64_t /*value*/) const -> void override { }
  auto QueueSignalCommand(const uint64_t value) -> void override
  {
    completed_ = value;
  }
  auto QueueWaitCommand(uint64_t /*value*/) const -> void override { }
  [[nodiscard]] auto GetCompletedValue() const -> uint64_t override
  {
    return completed_;
  }
  [[nodiscard]] auto GetCurrentValue() const -> uint64_t override
  {
    return current_;
  }
  auto Submit(std::shared_ptr<CommandList> /*command_list*/) -> void override {
  }
  auto Submit(std::span<std::shared_ptr<CommandList>> /*command_lists*/)
    -> void override
  {
  }
  [[nodiscard]] auto GetQueueRole() const -> QueueRole override
  {
    return role_;
  }

private:
  QueueRole role_ { QueueRole::kGraphics };
  mutable uint64_t current_ { 0 };
  mutable uint64_t completed_ { 0 };
};

//! CommandRecorder that records buffer and texture copy operations for
//! assertions.
class FakeCommandRecorder final : public CommandRecorder {
public:
  FakeCommandRecorder(std::shared_ptr<CommandList> command_list,
    const observer_ptr<CommandQueue> target_queue, BufferCommandLog* buffer_log,
    TextureCommandLog* texture_log)
    : CommandRecorder(std::move(command_list), target_queue)
    , buffer_log_(buffer_log)
    , texture_log_(texture_log)
  {
  }

  // No-op API
  auto BeginEvent(std::string_view /*name*/) -> void override { }
  auto EndEvent() -> void override { }
  auto SetMarker(std::string_view /*name*/) -> void override { }

  auto SetPipelineState(graphics::GraphicsPipelineDesc /*desc*/)
    -> void override
  {
  }
  auto SetPipelineState(graphics::ComputePipelineDesc /*desc*/) -> void override
  {
  }
  auto SetGraphicsRootConstantBufferView(uint32_t /*root_parameter_index*/,
    uint64_t /*buffer_gpu_address*/) -> void override
  {
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
  auto Draw(uint32_t /*vertex_num*/, uint32_t /*instances_num*/,
    uint32_t /*vertex_offset*/, uint32_t /*instance_offset*/) -> void override
  {
  }
  auto Dispatch(uint32_t /*thread_group_count_x*/,
    uint32_t /*thread_group_count_y*/, uint32_t /*thread_group_count_z*/)
    -> void override
  {
  }
  auto ExecuteIndirect(const Buffer& /*argument_buffer*/,
    uint64_t /*argument_buffer_offset*/) -> void override
  {
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
  auto ClearFramebuffer(const graphics::Framebuffer& /*framebuffer*/,
    std::optional<std::vector<std::optional<graphics::Color>>>
    /*color_clear_values*/,
    std::optional<float> /*depth_clear_value*/,
    std::optional<uint8_t> /*stencil_clear_value*/) -> void override
  {
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
    buffer_log_->copies.push_back(BufferCommandLog::CopyEvent { .dst = &dst,
      .dst_offset = dst_offset,
      .src = &src,
      .src_offset = src_offset,
      .size = size });
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
    texture_log_->regions = { region };
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

  // No-op copy texture for tests (satisfies abstract interface)
  auto CopyTexture(const Texture& /*src*/,
    const graphics::TextureSlice& /*src_slice*/,
    const graphics::TextureSubResourceSet& /*src_subresources*/,
    Texture& /*dst*/, const graphics::TextureSlice& /*dst_slice*/,
    const graphics::TextureSubResourceSet& /*dst_subresources*/)
    -> void override
  {
  }

protected:
  auto ExecuteBarriers(std::span<const graphics::detail::Barrier> /*barriers*/)
    -> void override
  {
  }

private:
  BufferCommandLog* buffer_log_ { nullptr };
  TextureCommandLog* texture_log_ { nullptr };
};

// Minimal in-memory descriptor allocator for tests
class MiniDescriptorAllocator final : public graphics::DescriptorAllocator {
public:
  MiniDescriptorAllocator() = default;
  ~MiniDescriptorAllocator() override = default;

  auto Allocate(const graphics::ResourceViewType view_type,
    const graphics::DescriptorVisibility visibility)
    -> graphics::DescriptorHandle override
  {
    const auto key = Key(view_type, visibility);
    auto& state = domains_[key];
    const auto index = state.next_index++;
    return CreateDescriptorHandle(
      bindless::HeapIndex { index }, view_type, visibility);
  }

  auto Release(graphics::DescriptorHandle& handle) -> void override
  {
    handle.Invalidate();
  }

  auto CopyDescriptor(const graphics::DescriptorHandle& /*source*/,
    const graphics::DescriptorHandle& /*destination*/) -> void override
  {
  }

  [[nodiscard]] auto GetRemainingDescriptorsCount(
    graphics::ResourceViewType /*view_type*/,
    graphics::DescriptorVisibility /*visibility*/) const
    -> bindless::Count override
  {
    return bindless::Count { 1'000'000 }; // ample room
  }

  [[nodiscard]] auto GetDomainBaseIndex(
    graphics::ResourceViewType /*view_type*/,
    graphics::DescriptorVisibility /*visibility*/) const
    -> bindless::HeapIndex override
  {
    return bindless::HeapIndex { 0 };
  }

  [[nodiscard]] auto Reserve(const graphics::ResourceViewType view_type,
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
    return bindless::HeapIndex { base };
  }

  [[nodiscard]] auto Contains(const graphics::DescriptorHandle& handle) const
    -> bool override
  {
    return handle.IsValid();
  }

  [[nodiscard]] auto GetAllocatedDescriptorsCount(
    const graphics::ResourceViewType view_type,
    const graphics::DescriptorVisibility visibility) const
    -> bindless::Count override
  {
    const auto key = Key(view_type, visibility);
    auto it = domains_.find(key);
    if (it == domains_.end()) {
      return bindless::Count { 0 };
    }
    return bindless::Count { static_cast<uint32_t>(it->second.next_index) };
  }

  [[nodiscard]] auto GetShaderVisibleIndex(
    const graphics::DescriptorHandle& handle) const noexcept
    -> bindless::ShaderVisibleIndex override
  {
    return bindless::ShaderVisibleIndex { handle.GetBindlessHandle().get() };
  }

private:
  struct DomainState {
    uint32_t next_index { 0 };
  };
  using DomainKey = uint64_t;
  static constexpr auto Key(graphics::ResourceViewType vt,
    graphics::DescriptorVisibility vis) -> DomainKey
  {
    return (static_cast<DomainKey>(static_cast<uint32_t>(vt)) << 32)
      | static_cast<DomainKey>(static_cast<uint32_t>(vis));
  }
  std::unordered_map<DomainKey, DomainState> domains_;
};

//! Fake Graphics implementation providing staging buffers, queues and recorders
//! for upload tests.
class FakeGraphics final : public Graphics {
public:
  FakeGraphics()
    : Graphics("FakeGraphics")
  {
  }
  // Test-only failure injection hooks
  void SetFailMap(const bool v) { fail_map_ = v; }
  void SetThrowOnCreateBuffer(const bool v) { throw_on_create_buffer_ = v; }
  auto GetDescriptorAllocator() const
    -> const graphics::DescriptorAllocator& override
  {
    return descriptor_allocator_;
  }
  auto GetDescriptorAllocator() -> graphics::DescriptorAllocator&
  {
    return descriptor_allocator_;
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
      FakeTexture(const std::string_view name, const TextureDesc& desc,
        SrvViewCreationLog* srv_view_log)
        : Texture(name)
        , desc_(desc)
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
        return { const_cast<FakeTexture*>(this), Texture::ClassTypeId() };
      }

    protected:
      [[nodiscard]] auto CreateShaderResourceView(
        const graphics::DescriptorHandle& view_handle, Format /*format*/,
        TextureType /*dimension*/,
        graphics::TextureSubResourceSet /*sub_resources*/) const
        -> graphics::NativeView override
      {
        if (srv_view_log_ != nullptr) {
          srv_view_log_->events.push_back(SrvViewCreationLog::Event {
            .index = view_handle.GetBindlessHandle().get(),
            .texture = this,
          });
        }
        // Use the texture object's address as a stable unique view handle.
        return { this, Texture::ClassTypeId() };
      }
      [[nodiscard]] auto CreateUnorderedAccessView(
        const graphics::DescriptorHandle& /*view_handle*/, Format /*format*/,
        TextureType /*dimension*/,
        graphics::TextureSubResourceSet /*sub_resources*/) const
        -> graphics::NativeView override
      {
        return { this, Texture::ClassTypeId() };
      }
      [[nodiscard]] auto CreateRenderTargetView(
        const graphics::DescriptorHandle& /*view_handle*/, Format /*format*/,
        graphics::TextureSubResourceSet /*sub_resources*/) const
        -> graphics::NativeView override
      {
        return { this, Texture::ClassTypeId() };
      }
      [[nodiscard]] auto CreateDepthStencilView(
        const graphics::DescriptorHandle& /*view_handle*/, Format /*format*/,
        graphics::TextureSubResourceSet /*sub_resources*/,
        bool /*is_read_only*/) const -> graphics::NativeView override
      {
        return { this, Texture::ClassTypeId() };
      }

    private:
      TextureDesc desc_ {};
      SrvViewCreationLog* srv_view_log_ { nullptr };
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
        const bool map_should_fail)
        : Buffer(name)
        , map_should_fail_(map_should_fail)
      {
        desc_.size_bytes = size;
        desc_.usage = usage;
        desc_.memory = memory;
      }
      [[nodiscard]] auto GetDescriptor() const noexcept -> BufferDesc override
      {
        return desc_;
      }
      [[nodiscard]] auto GetNativeResource() const
        -> graphics::NativeResource override
      {
        return { const_cast<FakeBuffer*>(this), Buffer::ClassTypeId() };
      }
      auto Update(const void* data, const uint64_t size, const uint64_t offset)
        -> void override
      {
        if (offset + size <= storage_.size()) {
          std::memcpy(storage_.data() + offset, data, size);
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
        return 0;
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
        const graphics::DescriptorHandle& /*view_handle*/,
        const graphics::BufferRange& /*range*/) const
        -> graphics::NativeView override
      {
        return {};
      }
      [[nodiscard]] auto CreateShaderResourceView(
        const graphics::DescriptorHandle& /*view_handle*/, Format /*format*/,
        graphics::BufferRange /*range*/, uint32_t /*stride*/) const
        -> graphics::NativeView override
      {
        return {};
      }
      [[nodiscard]] auto CreateUnorderedAccessView(
        const graphics::DescriptorHandle& /*view_handle*/, Format /*format*/,
        graphics::BufferRange /*range*/, uint32_t /*stride*/) const
        -> graphics::NativeView override
      {
        return {};
      }

    private:
      BufferDesc desc_ {};
      bool mapped_ { false };
      bool map_should_fail_ { false };
      std::vector<std::byte> storage_;
    };
    return std::make_shared<FakeBuffer>(
      "Staging", desc.size_bytes, desc.usage, desc.memory, fail_map_);
  }
  auto CreateCommandQueues(const graphics::QueuesStrategy& queue_strategy)
    -> void override
  {
    const auto copy_key = queue_strategy.KeyFor(QueueRole::kTransfer);
    const auto gfx_key = queue_strategy.KeyFor(QueueRole::kGraphics);
    queues_[copy_key]
      = std::make_shared<FakeCommandQueue>("CopyQ", QueueRole::kTransfer);
    queues_[gfx_key]
      = std::make_shared<FakeCommandQueue>("GfxQ", QueueRole::kGraphics);
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
    for (const auto& v : queues_ | std::views::values) {
      if (v->GetQueueRole() == role) {
        return oxygen::observer_ptr<CommandQueue>(v.get());
      }
    }
    return {};
  }
  auto FlushCommandQueues() -> void override { }
  auto AcquireCommandRecorder(const QueueKey& queue_key,
    std::string_view command_list_name, bool /*immediate_submission*/)
    -> std::unique_ptr<CommandRecorder,
      std::function<void(CommandRecorder*)>> override
  {
    auto q = GetCommandQueue(queue_key);
    auto cl = std::make_shared<FakeCommandList>(
      command_list_name, q ? q->GetQueueRole() : QueueRole::kGraphics);
    auto* raw = new FakeCommandRecorder(cl, q, &buffer_log_, &texture_log_);
    return { raw, [](CommandRecorder* p) -> void { delete p; } };
  }

  BufferCommandLog buffer_log_ {};
  TextureCommandLog texture_log_ {};
  mutable SrvViewCreationLog srv_view_log_ {};
  std::map<QueueKey, std::shared_ptr<CommandQueue>> queues_;
  mutable MiniDescriptorAllocator descriptor_allocator_;
  // Test injection flags (mutable to allow const CreateBuffer)
  mutable bool fail_map_ { false };
  mutable bool throw_on_create_buffer_ { false };

protected:
  [[nodiscard]] auto CreateCommandQueue(const QueueKey& /*queue_name*/,
    QueueRole /*role*/) -> std::shared_ptr<CommandQueue> override
  {
    return {};
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

} // namespace oxygen::renderer::testing
