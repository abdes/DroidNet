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
  std::vector<TextureUploadRegion> regions {};
};

//! Lightweight CommandList used by the fake command recorder in tests.
class FakeCommandList final : public CommandList {
public:
  explicit FakeCommandList(std::string_view name, QueueRole role)
    : CommandList(name, role)
  {
  }
};

//! Simple CommandQueue that simulates signalling/completion for tests.
class FakeCommandQueue final : public CommandQueue {
public:
  explicit FakeCommandQueue(std::string_view name, QueueRole role)
    : CommandQueue(name)
    , role_(role)
  {
  }
  auto Signal(uint64_t value) const -> void override { current_ = value; }
  [[nodiscard]] auto Signal() const -> uint64_t override { return ++current_; }
  auto Wait(uint64_t, std::chrono::milliseconds) const -> void override { }
  auto Wait(uint64_t) const -> void override { }
  auto QueueSignalCommand(uint64_t value) -> void override
  {
    completed_ = value;
  }
  auto QueueWaitCommand(uint64_t) const -> void override { }
  [[nodiscard]] auto GetCompletedValue() const -> uint64_t override
  {
    return completed_;
  }
  [[nodiscard]] auto GetCurrentValue() const -> uint64_t override
  {
    return current_;
  }
  auto Submit(std::shared_ptr<CommandList>) -> void override { }
  auto Submit(std::span<std::shared_ptr<CommandList>>) -> void override { }
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
  FakeCommandRecorder(std::shared_ptr<CommandList> cl,
    observer_ptr<CommandQueue> q, BufferCommandLog* buffer_log,
    TextureCommandLog* texture_log)
    : CommandRecorder(std::move(cl), q)
    , buffer_log_(buffer_log)
    , texture_log_(texture_log)
  {
  }

  // No-op API
  auto SetPipelineState(graphics::GraphicsPipelineDesc) -> void override { }
  auto SetPipelineState(graphics::ComputePipelineDesc) -> void override { }
  auto SetGraphicsRootConstantBufferView(uint32_t, uint64_t) -> void override {
  }
  auto SetComputeRootConstantBufferView(uint32_t, uint64_t) -> void override { }
  auto SetGraphicsRoot32BitConstant(uint32_t, uint32_t, uint32_t)
    -> void override
  {
  }
  auto SetComputeRoot32BitConstant(uint32_t, uint32_t, uint32_t)
    -> void override
  {
  }
  auto SetRenderTargets(std::span<graphics::NativeView>,
    std::optional<graphics::NativeView>) -> void override
  {
  }
  auto SetViewport(const ViewPort&) -> void override { }
  auto SetScissors(const Scissors&) -> void override { }
  auto Draw(uint32_t, uint32_t, uint32_t, uint32_t) -> void override { }
  auto Dispatch(uint32_t, uint32_t, uint32_t) -> void override { }
  auto SetVertexBuffers(uint32_t, const std::shared_ptr<Buffer>*,
    const uint32_t*) const -> void override
  {
  }
  auto BindIndexBuffer(const Buffer&, Format) -> void override { }
  auto BindFrameBuffer(const graphics::Framebuffer&) -> void override { }
  auto ClearDepthStencilView(const Texture&, const graphics::NativeView&,
    graphics::ClearFlags, float, uint8_t) -> void override
  {
  }
  auto ClearFramebuffer(const graphics::Framebuffer&,
    std::optional<std::vector<std::optional<graphics::Color>>>,
    std::optional<float>, std::optional<uint8_t>) -> void override
  {
  }

  auto CopyBuffer(Buffer& dst, size_t dst_offset, const Buffer& src,
    size_t src_offset, size_t size) -> void override
  {
    if (!buffer_log_) {
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
  auto CopyBufferToTexture(const Buffer& src, const TextureUploadRegion& r,
    Texture& dst) -> void override
  {
    if (!texture_log_) {
      return;
    }
    texture_log_->copy_called = true;
    texture_log_->src = &src;
    texture_log_->dst = &dst;
    texture_log_->regions = { r };
  }
  auto CopyBufferToTexture(const Buffer& src,
    std::span<const TextureUploadRegion> regions, Texture& dst) -> void override
  {
    if (!texture_log_) {
      return;
    }
    texture_log_->copy_called = true;
    texture_log_->src = &src;
    texture_log_->dst = &dst;
    texture_log_->regions.assign(regions.begin(), regions.end());
  }

protected:
  auto ExecuteBarriers(std::span<const graphics::detail::Barrier>)
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

  auto Allocate(graphics::ResourceViewType view_type,
    graphics::DescriptorVisibility visibility)
    -> graphics::DescriptorHandle override
  {
    const auto key = Key(view_type, visibility);
    auto& state = domains_[key];
    const auto index = state.next_index++;
    return CreateDescriptorHandle(
      oxygen::bindless::HeapIndex { index }, view_type, visibility);
  }

  auto Release(graphics::DescriptorHandle& handle) -> void override
  {
    handle.Invalidate();
  }

  auto CopyDescriptor(const graphics::DescriptorHandle& /*source*/,
    const graphics::DescriptorHandle& /*destination*/) -> void override
  {
  }

  [[nodiscard]] auto GetRemainingDescriptorsCount(graphics::ResourceViewType,
    graphics::DescriptorVisibility) const -> oxygen::bindless::Count override
  {
    return oxygen::bindless::Count { 1'000'000 }; // ample room
  }

  [[nodiscard]] auto GetDomainBaseIndex(
    graphics::ResourceViewType, graphics::DescriptorVisibility) const
    -> oxygen::bindless::HeapIndex override
  {
    return oxygen::bindless::HeapIndex { 0 };
  }

  [[nodiscard]] auto Reserve(graphics::ResourceViewType view_type,
    graphics::DescriptorVisibility visibility, oxygen::bindless::Count count)
    -> std::optional<oxygen::bindless::HeapIndex> override
  {
    if (count.get() == 0) {
      return std::nullopt;
    }
    const auto key = Key(view_type, visibility);
    auto& state = domains_[key];
    const auto base = state.next_index;
    state.next_index += count.get();
    return oxygen::bindless::HeapIndex { base };
  }

  [[nodiscard]] auto Contains(const graphics::DescriptorHandle& handle) const
    -> bool override
  {
    return handle.IsValid();
  }

  [[nodiscard]] auto GetAllocatedDescriptorsCount(
    graphics::ResourceViewType view_type,
    graphics::DescriptorVisibility visibility) const
    -> oxygen::bindless::Count override
  {
    const auto key = Key(view_type, visibility);
    auto it = domains_.find(key);
    if (it == domains_.end()) {
      return oxygen::bindless::Count { 0 };
    }
    return oxygen::bindless::Count { static_cast<uint32_t>(
      it->second.next_index) };
  }

  [[nodiscard]] auto GetShaderVisibleIndex(
    const graphics::DescriptorHandle& handle) const noexcept
    -> oxygen::bindless::ShaderVisibleIndex override
  {
    return oxygen::bindless::ShaderVisibleIndex {
      handle.GetBindlessHandle().get()
    };
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
  std::unordered_map<DomainKey, DomainState> domains_ {};
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
  void SetFailMap(bool v) { fail_map_ = v; }
  void SetThrowOnCreateBuffer(bool v) { throw_on_create_buffer_ = v; }
  auto GetDescriptorAllocator() const
    -> const graphics::DescriptorAllocator& override
  {
    return descriptor_allocator_;
  }
  [[nodiscard]] auto CreateSurface(
    std::weak_ptr<platform::Window>, observer_ptr<CommandQueue>) const
    -> std::shared_ptr<graphics::Surface> override
  {
    return {};
  }
  [[nodiscard]] auto GetShader(std::string_view) const
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
      FakeTexture(std::string_view name, const TextureDesc& desc)
        : Texture(name)
        , desc_(desc)
      {
      }

      [[nodiscard]] auto GetDescriptor() const -> const TextureDesc& override
      {
        return desc_;
      }

      [[nodiscard]] auto GetNativeResource() const
        -> oxygen::graphics::NativeResource override
      {
        return oxygen::graphics::NativeResource(
          const_cast<FakeTexture*>(this), Texture::ClassTypeId());
      }

    protected:
      [[nodiscard]] auto CreateShaderResourceView(
        const oxygen::graphics::DescriptorHandle&, oxygen::Format,
        oxygen::TextureType, oxygen::graphics::TextureSubResourceSet) const
        -> oxygen::graphics::NativeView override
      {
        return {};
      }
      [[nodiscard]] auto CreateUnorderedAccessView(
        const oxygen::graphics::DescriptorHandle&, oxygen::Format,
        oxygen::TextureType, oxygen::graphics::TextureSubResourceSet) const
        -> oxygen::graphics::NativeView override
      {
        return {};
      }
      [[nodiscard]] auto CreateRenderTargetView(
        const oxygen::graphics::DescriptorHandle&, oxygen::Format,
        oxygen::graphics::TextureSubResourceSet) const
        -> oxygen::graphics::NativeView override
      {
        return {};
      }
      [[nodiscard]] auto CreateDepthStencilView(
        const oxygen::graphics::DescriptorHandle&, oxygen::Format,
        oxygen::graphics::TextureSubResourceSet, bool) const
        -> oxygen::graphics::NativeView override
      {
        return {};
      }

    private:
      TextureDesc desc_ {};
    };

    return std::make_shared<FakeTexture>("FakeTexture", desc);
  }
  [[nodiscard]] auto CreateTextureFromNativeObject(const TextureDesc&,
    const graphics::NativeResource&) const -> std::shared_ptr<Texture> override
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
      FakeBuffer(std::string_view name, uint64_t size, BufferUsage usage,
        BufferMemory memory, bool map_should_fail)
        : Buffer(name)
      {
        desc_.size_bytes = size;
        desc_.usage = usage;
        desc_.memory = memory;
        map_should_fail_ = map_should_fail;
      }
      [[nodiscard]] auto GetDescriptor() const noexcept -> BufferDesc override
      {
        return desc_;
      }
      [[nodiscard]] auto GetNativeResource() const
        -> graphics::NativeResource override
      {
        return graphics::NativeResource(
          const_cast<FakeBuffer*>(this), Buffer::ClassTypeId());
      }
      auto Update(const void* data, uint64_t size, uint64_t offset)
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
      auto DoMap(uint64_t, uint64_t) -> void* override
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
        const graphics::DescriptorHandle&, const graphics::BufferRange&) const
        -> graphics::NativeView override
      {
        return {};
      }
      [[nodiscard]] auto CreateShaderResourceView(
        const graphics::DescriptorHandle&, Format, graphics::BufferRange,
        uint32_t) const -> graphics::NativeView override
      {
        return {};
      }
      [[nodiscard]] auto CreateUnorderedAccessView(
        const graphics::DescriptorHandle&, Format, graphics::BufferRange,
        uint32_t) const -> graphics::NativeView override
      {
        return {};
      }

    private:
      BufferDesc desc_ {};
      bool mapped_ { false };
      bool map_should_fail_ { false };
      std::vector<std::byte> storage_ {};
    };
    return std::make_shared<FakeBuffer>(
      "Staging", desc.size_bytes, desc.usage, desc.memory, fail_map_);
  }
  auto CreateCommandQueues(const graphics::QueuesStrategy& strat)
    -> void override
  {
    const auto copy_key = strat.KeyFor(QueueRole::kTransfer);
    const auto gfx_key = strat.KeyFor(QueueRole::kGraphics);
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
  auto GetCommandQueue(QueueRole role) const
    -> observer_ptr<CommandQueue> override
  {
    for (auto& [k, v] : queues_) {
      if (v->GetQueueRole() == role) {
        return oxygen::observer_ptr<CommandQueue>(v.get());
      }
    }
    return {};
  }
  auto FlushCommandQueues() -> void override { }
  auto AcquireCommandRecorder(const QueueKey& key, std::string_view name, bool)
    -> std::unique_ptr<CommandRecorder,
      std::function<void(CommandRecorder*)>> override
  {
    auto q = GetCommandQueue(key);
    auto cl = std::make_shared<FakeCommandList>(
      name, q ? q->GetQueueRole() : QueueRole::kGraphics);
    auto raw = new FakeCommandRecorder(cl, q, &buffer_log_, &texture_log_);
    return { raw, [](CommandRecorder* p) { delete p; } };
  }

  BufferCommandLog buffer_log_ {};
  TextureCommandLog texture_log_ {};
  std::map<QueueKey, std::shared_ptr<CommandQueue>> queues_ {};
  mutable MiniDescriptorAllocator descriptor_allocator_ {};
  // Test injection flags (mutable to allow const CreateBuffer)
  mutable bool fail_map_ { false };
  mutable bool throw_on_create_buffer_ { false };

protected:
  [[nodiscard]] auto CreateCommandQueue(const QueueKey&, QueueRole)
    -> std::shared_ptr<CommandQueue> override
  {
    return {};
  }
  [[nodiscard]] auto CreateCommandListImpl(QueueRole, std::string_view)
    -> std::unique_ptr<CommandList> override
  {
    return {};
  }
  [[nodiscard]] auto CreateCommandRecorder(std::shared_ptr<CommandList>,
    observer_ptr<CommandQueue>) -> std::unique_ptr<CommandRecorder> override
  {
    return {};
  }
};

} // namespace oxygen::renderer::testing
