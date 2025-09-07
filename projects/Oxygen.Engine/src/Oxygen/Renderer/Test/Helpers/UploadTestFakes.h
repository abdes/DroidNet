//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Detail/Barriers.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/Texture.h>

#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace oxygen::tests::uploadhelpers {

using oxygen::graphics::Buffer;
using oxygen::graphics::BufferDesc;
using oxygen::graphics::BufferMemory;
using oxygen::graphics::BufferUsage;
using oxygen::graphics::CommandList;
using oxygen::graphics::CommandQueue;
using oxygen::graphics::CommandRecorder;
using oxygen::graphics::QueueKey;
using oxygen::graphics::QueueRole;
using oxygen::graphics::Texture;
using oxygen::graphics::TextureDesc;
using oxygen::graphics::TextureUploadRegion;

// --- Buffer copy logging ---------------------------------------------------
// //

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

class FakeCommandList final : public CommandList {
public:
  explicit FakeCommandList(std::string_view name, QueueRole role)
    : CommandList(name, role)
  {
  }
};

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

class FakeCommandRecorder_Buffer final : public CommandRecorder {
public:
  FakeCommandRecorder_Buffer(std::shared_ptr<CommandList> cl,
    oxygen::observer_ptr<CommandQueue> q, BufferCommandLog* log)
    : CommandRecorder(std::move(cl), q)
    , log_(log)
  {
  }

  // No-op API
  auto SetPipelineState(oxygen::graphics::GraphicsPipelineDesc) -> void override
  {
  }
  auto SetPipelineState(oxygen::graphics::ComputePipelineDesc) -> void override
  {
  }
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
  auto SetRenderTargets(std::span<oxygen::graphics::NativeObject>,
    std::optional<oxygen::graphics::NativeObject>) -> void override
  {
  }
  auto SetViewport(const oxygen::ViewPort&) -> void override { }
  auto SetScissors(const oxygen::Scissors&) -> void override { }
  auto Draw(uint32_t, uint32_t, uint32_t, uint32_t) -> void override { }
  auto Dispatch(uint32_t, uint32_t, uint32_t) -> void override { }
  auto SetVertexBuffers(uint32_t, const std::shared_ptr<Buffer>*,
    const uint32_t*) const -> void override
  {
  }
  auto BindIndexBuffer(const Buffer&, oxygen::Format) -> void override { }
  auto BindFrameBuffer(const oxygen::graphics::Framebuffer&) -> void override {
  }
  auto ClearDepthStencilView(const oxygen::graphics::Texture&,
    const oxygen::graphics::NativeObject&, oxygen::graphics::ClearFlags, float,
    uint8_t) -> void override
  {
  }
  auto ClearFramebuffer(const oxygen::graphics::Framebuffer&,
    std::optional<std::vector<std::optional<oxygen::graphics::Color>>>,
    std::optional<float>, std::optional<uint8_t>) -> void override
  {
  }

  auto CopyBuffer(Buffer& dst, size_t dst_offset, const Buffer& src,
    size_t src_offset, size_t size) -> void override
  {
    if (!log_)
      return;
    log_->copy_called = true;
    log_->copy_dst = &dst;
    log_->copy_dst_offset = dst_offset;
    log_->copy_src = &src;
    log_->copy_src_offset = src_offset;
    log_->copy_size = size;
    log_->copies.push_back(BufferCommandLog::CopyEvent { .dst = &dst,
      .dst_offset = dst_offset,
      .src = &src,
      .src_offset = src_offset,
      .size = size });
  }
  auto CopyBufferToTexture(const Buffer&,
    const oxygen::graphics::TextureUploadRegion&, oxygen::graphics::Texture&)
    -> void override
  {
  }
  auto CopyBufferToTexture(const Buffer&,
    std::span<const oxygen::graphics::TextureUploadRegion>,
    oxygen::graphics::Texture&) -> void override
  {
  }

protected:
  auto ExecuteBarriers(std::span<const oxygen::graphics::detail::Barrier>)
    -> void override
  {
  }

private:
  BufferCommandLog* log_ { nullptr };
};

class FakeGraphics_Buffer final : public oxygen::Graphics {
public:
  FakeGraphics_Buffer()
    : oxygen::Graphics("FakeGraphics")
  {
  }
  auto GetDescriptorAllocator() const
    -> const oxygen::graphics::DescriptorAllocator& override
  {
    static const oxygen::graphics::DescriptorAllocator* dummy = nullptr;
    return *dummy;
  }
  [[nodiscard]] auto CreateSurface(std::weak_ptr<oxygen::platform::Window>,
    oxygen::observer_ptr<oxygen::graphics::CommandQueue>) const
    -> std::shared_ptr<oxygen::graphics::Surface> override
  {
    return {};
  }
  [[nodiscard]] auto GetShader(std::string_view) const
    -> std::shared_ptr<oxygen::graphics::IShaderByteCode> override
  {
    return {};
  }
  [[nodiscard]] auto CreateTexture(const oxygen::graphics::TextureDesc&) const
    -> std::shared_ptr<oxygen::graphics::Texture> override
  {
    return {};
  }
  [[nodiscard]] auto CreateTextureFromNativeObject(
    const oxygen::graphics::TextureDesc&,
    const oxygen::graphics::NativeObject&) const
    -> std::shared_ptr<oxygen::graphics::Texture> override
  {
    return {};
  }
  [[nodiscard]] auto CreateBuffer(
    const oxygen::graphics::BufferDesc& desc) const
    -> std::shared_ptr<oxygen::graphics::Buffer> override
  {
    class FakeStagingBuffer final : public Buffer {
      OXYGEN_TYPED(FakeStagingBuffer)
    public:
      FakeStagingBuffer(std::string_view name, uint64_t size)
        : Buffer(name)
      {
        desc_.size_bytes = size;
        desc_.usage = BufferUsage::kNone;
        desc_.memory = BufferMemory::kUpload;
      }
      [[nodiscard]] auto GetDescriptor() const noexcept -> BufferDesc override
      {
        return desc_;
      }
      [[nodiscard]] auto GetNativeResource() const
        -> oxygen::graphics::NativeObject override
      {
        return oxygen::graphics::NativeObject(
          const_cast<FakeStagingBuffer*>(this),
          oxygen::graphics::Buffer::ClassTypeId());
      }
      auto Map(uint64_t, uint64_t) -> void* override
      {
        if (!mapped_) {
          storage_.resize(static_cast<size_t>(desc_.size_bytes));
          mapped_ = true;
        }
        return storage_.data();
      }
      auto UnMap() -> void override
      {
        mapped_ = false;
        storage_.clear();
      }
      auto Update(const void* data, uint64_t size, uint64_t offset)
        -> void override
      {
        if (offset + size <= storage_.size()) {
          std::memcpy(
            storage_.data() + offset, data, static_cast<size_t>(size));
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
      [[nodiscard]] auto CreateConstantBufferView(
        const oxygen::graphics::DescriptorHandle&,
        const oxygen::graphics::BufferRange&) const
        -> oxygen::graphics::NativeObject override
      {
        return {};
      }
      [[nodiscard]] auto CreateShaderResourceView(
        const oxygen::graphics::DescriptorHandle&, oxygen::Format,
        oxygen::graphics::BufferRange, uint32_t) const
        -> oxygen::graphics::NativeObject override
      {
        return {};
      }
      [[nodiscard]] auto CreateUnorderedAccessView(
        const oxygen::graphics::DescriptorHandle&, oxygen::Format,
        oxygen::graphics::BufferRange, uint32_t) const
        -> oxygen::graphics::NativeObject override
      {
        return {};
      }

    private:
      BufferDesc desc_ {};
      bool mapped_ { false };
      std::vector<std::byte> storage_ {};
    };
    return std::make_shared<FakeStagingBuffer>("Staging", desc.size_bytes);
  }
  auto CreateCommandQueues(const oxygen::graphics::QueuesStrategy& strat)
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
    -> oxygen::observer_ptr<CommandQueue> override
  {
    auto it = queues_.find(key);
    if (it == queues_.end())
      return {};
    return oxygen::observer_ptr<CommandQueue>(it->second.get());
  }
  auto GetCommandQueue(QueueRole role) const
    -> oxygen::observer_ptr<CommandQueue> override
  {
    for (auto& [k, v] : queues_) {
      if (v->GetQueueRole() == role)
        return oxygen::observer_ptr<CommandQueue>(v.get());
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
    auto raw = new FakeCommandRecorder_Buffer(cl, q, &buffer_log_);
    return { raw, [](CommandRecorder* p) { delete p; } };
  }

public:
  BufferCommandLog buffer_log_ {};
  std::map<QueueKey, std::shared_ptr<CommandQueue>> queues_ {};

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
  [[nodiscard]] auto CreateCommandRecorder(
    std::shared_ptr<CommandList>, oxygen::observer_ptr<CommandQueue>)
    -> std::unique_ptr<CommandRecorder> override
  {
    return {};
  }
};

// --- Texture copy logging --------------------------------------------------
// //

struct TextureCommandLog {
  bool copy_called { false };
  const Buffer* src { nullptr };
  Texture* dst { nullptr };
  std::vector<TextureUploadRegion> regions {};
};

class FakeCommandRecorder_Texture final : public CommandRecorder {
public:
  FakeCommandRecorder_Texture(std::shared_ptr<CommandList> cl,
    oxygen::observer_ptr<CommandQueue> q, TextureCommandLog* log)
    : CommandRecorder(std::move(cl), q)
    , log_(log)
  {
  }

  // No-op API
  auto SetPipelineState(oxygen::graphics::GraphicsPipelineDesc) -> void override
  {
  }
  auto SetPipelineState(oxygen::graphics::ComputePipelineDesc) -> void override
  {
  }
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
  auto SetRenderTargets(std::span<oxygen::graphics::NativeObject>,
    std::optional<oxygen::graphics::NativeObject>) -> void override
  {
  }
  auto SetViewport(const oxygen::ViewPort&) -> void override { }
  auto SetScissors(const oxygen::Scissors&) -> void override { }
  auto Draw(uint32_t, uint32_t, uint32_t, uint32_t) -> void override { }
  auto Dispatch(uint32_t, uint32_t, uint32_t) -> void override { }
  auto SetVertexBuffers(uint32_t, const std::shared_ptr<Buffer>*,
    const uint32_t*) const -> void override
  {
  }
  auto BindIndexBuffer(const Buffer&, oxygen::Format) -> void override { }
  auto BindFrameBuffer(const oxygen::graphics::Framebuffer&) -> void override {
  }
  auto ClearDepthStencilView(const oxygen::graphics::Texture&,
    const oxygen::graphics::NativeObject&, oxygen::graphics::ClearFlags, float,
    uint8_t) -> void override
  {
  }
  auto ClearFramebuffer(const oxygen::graphics::Framebuffer&,
    std::optional<std::vector<std::optional<oxygen::graphics::Color>>>,
    std::optional<float>, std::optional<uint8_t>) -> void override
  {
  }
  auto CopyBuffer(Buffer&, size_t, const Buffer&, size_t, size_t)
    -> void override
  {
  }

  auto CopyBufferToTexture(const Buffer& src,
    const oxygen::graphics::TextureUploadRegion& r,
    oxygen::graphics::Texture& dst) -> void override
  {
    if (!log_)
      return;
    log_->copy_called = true;
    log_->src = &src;
    log_->dst = &dst;
    log_->regions = { r };
  }
  auto CopyBufferToTexture(const Buffer& src,
    std::span<const oxygen::graphics::TextureUploadRegion> regions,
    oxygen::graphics::Texture& dst) -> void override
  {
    if (!log_)
      return;
    log_->copy_called = true;
    log_->src = &src;
    log_->dst = &dst;
    log_->regions.assign(regions.begin(), regions.end());
  }

protected:
  auto ExecuteBarriers(std::span<const oxygen::graphics::detail::Barrier>)
    -> void override
  {
  }

private:
  TextureCommandLog* log_ { nullptr };
};

class FakeGraphics_Texture final : public oxygen::Graphics {
public:
  FakeGraphics_Texture()
    : oxygen::Graphics("FakeGraphics")
  {
  }
  auto GetDescriptorAllocator() const
    -> const oxygen::graphics::DescriptorAllocator& override
  {
    static const oxygen::graphics::DescriptorAllocator* dummy = nullptr;
    return *dummy;
  }
  [[nodiscard]] auto CreateSurface(std::weak_ptr<oxygen::platform::Window>,
    oxygen::observer_ptr<oxygen::graphics::CommandQueue>) const
    -> std::shared_ptr<oxygen::graphics::Surface> override
  {
    return {};
  }
  [[nodiscard]] auto GetShader(std::string_view) const
    -> std::shared_ptr<oxygen::graphics::IShaderByteCode> override
  {
    return {};
  }
  [[nodiscard]] auto CreateTexture(const oxygen::graphics::TextureDesc&) const
    -> std::shared_ptr<oxygen::graphics::Texture> override
  {
    return {};
  }
  [[nodiscard]] auto CreateTextureFromNativeObject(
    const oxygen::graphics::TextureDesc&,
    const oxygen::graphics::NativeObject&) const
    -> std::shared_ptr<oxygen::graphics::Texture> override
  {
    return {};
  }
  [[nodiscard]] auto CreateBuffer(
    const oxygen::graphics::BufferDesc& desc) const
    -> std::shared_ptr<oxygen::graphics::Buffer> override
  {
    class FakeStagingBuffer final : public Buffer {
      OXYGEN_TYPED(FakeStagingBuffer)
    public:
      FakeStagingBuffer(std::string_view name, uint64_t size)
        : Buffer(name)
      {
        desc_.size_bytes = size;
        desc_.usage = BufferUsage::kNone;
        desc_.memory = BufferMemory::kUpload;
      }
      [[nodiscard]] auto GetDescriptor() const noexcept -> BufferDesc override
      {
        return desc_;
      }
      [[nodiscard]] auto GetNativeResource() const
        -> oxygen::graphics::NativeObject override
      {
        return oxygen::graphics::NativeObject(
          const_cast<FakeStagingBuffer*>(this),
          oxygen::graphics::Buffer::ClassTypeId());
      }
      auto Map(uint64_t, uint64_t) -> void* override
      {
        if (!mapped_) {
          storage_.resize(static_cast<size_t>(desc_.size_bytes));
          mapped_ = true;
        }
        return storage_.data();
      }
      auto UnMap() -> void override
      {
        mapped_ = false;
        storage_.clear();
      }
      auto Update(const void* data, uint64_t size, uint64_t offset)
        -> void override
      {
        if (offset + size <= storage_.size()) {
          std::memcpy(
            storage_.data() + offset, data, static_cast<size_t>(size));
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
      [[nodiscard]] auto CreateConstantBufferView(
        const oxygen::graphics::DescriptorHandle&,
        const oxygen::graphics::BufferRange&) const
        -> oxygen::graphics::NativeObject override
      {
        return {};
      }
      [[nodiscard]] auto CreateShaderResourceView(
        const oxygen::graphics::DescriptorHandle&, oxygen::Format,
        oxygen::graphics::BufferRange, uint32_t) const
        -> oxygen::graphics::NativeObject override
      {
        return {};
      }
      [[nodiscard]] auto CreateUnorderedAccessView(
        const oxygen::graphics::DescriptorHandle&, oxygen::Format,
        oxygen::graphics::BufferRange, uint32_t) const
        -> oxygen::graphics::NativeObject override
      {
        return {};
      }

    private:
      BufferDesc desc_ {};
      bool mapped_ { false };
      std::vector<std::byte> storage_ {};
    };
    return std::make_shared<FakeStagingBuffer>("Staging", desc.size_bytes);
  }
  auto CreateCommandQueues(const oxygen::graphics::QueuesStrategy& strat)
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
    -> oxygen::observer_ptr<CommandQueue> override
  {
    auto it = queues_.find(key);
    if (it == queues_.end())
      return {};
    return oxygen::observer_ptr<CommandQueue>(it->second.get());
  }
  auto GetCommandQueue(QueueRole role) const
    -> oxygen::observer_ptr<CommandQueue> override
  {
    for (auto& [k, v] : queues_) {
      if (v->GetQueueRole() == role)
        return oxygen::observer_ptr<CommandQueue>(v.get());
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
    auto raw = new FakeCommandRecorder_Texture(cl, q, &texture_log_);
    return { raw, [](CommandRecorder* p) { delete p; } };
  }

public:
  TextureCommandLog texture_log_ {};
  std::map<QueueKey, std::shared_ptr<CommandQueue>> queues_ {};

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
  [[nodiscard]] auto CreateCommandRecorder(
    std::shared_ptr<CommandList>, oxygen::observer_ptr<CommandQueue>)
    -> std::unique_ptr<CommandRecorder> override
  {
    return {};
  }
};

} // namespace oxygen::tests::uploadhelpers
