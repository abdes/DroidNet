//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Config/GraphicsConfig.h>
#include <Oxygen/Core/Types/ByteUnits.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/FenceValue.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Direct3D12/Buffer.h>
#include <Oxygen/Graphics/Direct3D12/Graphics.h>
#include <Oxygen/Graphics/Direct3D12/Texture.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::graphics::d3d12::testing {

//! Offscreen fixture for backend-level D3D12 tests.
/*!
 Creates a concrete D3D12 backend, provisions command queues from an existing
 QueuesStrategy, exposes recorder/queue/resource helpers, and cleans up tracked
 resources automatically during teardown.
*/
class OffscreenTestFixture : public ::testing::Test {
protected:
  using RecorderPtr = std::unique_ptr<graphics::CommandRecorder,
    std::function<void(graphics::CommandRecorder*)>>;

  auto SetUp() -> void override
  {
    backend_config_json_ = BackendConfigJson();
    if (backend_config_json_.empty()) {
      backend_config_json_ = "{}";
    }

    path_finder_config_json_ = PathFinderConfigJson();
    if (path_finder_config_json_.empty()) {
      path_finder_config_json_ = "{}";
    }

    queue_strategy_ = CreateQueueStrategy();
    ASSERT_NE(queue_strategy_, nullptr);

    const SerializedBackendConfig backend_config {
      .json_data = backend_config_json_.c_str(),
      .size = backend_config_json_.size(),
    };
    const SerializedPathFinderConfig path_finder_config {
      .json_data = path_finder_config_json_.c_str(),
      .size = path_finder_config_json_.size(),
    };

    graphics_ = std::make_shared<oxygen::graphics::d3d12::Graphics>(
      backend_config, path_finder_config);
    ASSERT_NE(graphics_, nullptr);

    graphics_->CreateCommandQueues(*queue_strategy_);
    ASSERT_NE(TryGetQueue(graphics::QueueRole::kGraphics), nullptr);
  }

  auto TearDown() -> void override
  {
    CleanupTrackedResources();

    if (graphics_ != nullptr) {
      graphics_->Flush();
      graphics_->Stop();
    }

    textures_.clear();
    buffers_.clear();
    queue_strategy_.reset();
    graphics_.reset();
    backend_config_json_.clear();
    path_finder_config_json_.clear();
  }

  [[nodiscard]] virtual auto BackendConfigJson() const -> std::string
  {
    return "{}";
  }

  [[nodiscard]] virtual auto PathFinderConfigJson() const -> std::string
  {
    return "{}";
  }

  [[nodiscard]] virtual auto CreateQueueStrategy() const
    -> std::unique_ptr<oxygen::graphics::QueuesStrategy>
  {
    return std::make_unique<oxygen::graphics::SingleQueueStrategy>();
  }

  [[nodiscard]] auto Backend() const -> oxygen::graphics::d3d12::Graphics&
  {
    CHECK_NOTNULL_F(
      graphics_.get(), "D3D12 backend fixture is not initialized");
    return *graphics_;
  }

  [[nodiscard]] auto TryGetQueue(const graphics::QueueRole role) const
    -> observer_ptr<graphics::CommandQueue>
  {
    if (graphics_ == nullptr) {
      return {};
    }
    return graphics_->GetCommandQueue(role);
  }

  [[nodiscard]] auto GetQueue(
    const graphics::QueueRole role = graphics::QueueRole::kGraphics) const
    -> observer_ptr<graphics::CommandQueue>
  {
    const auto queue = TryGetQueue(role);
    CHECK_F(queue != nullptr, "Requested queue role is not available");
    return queue;
  }

  [[nodiscard]] auto QueueKeyFor(const graphics::QueueRole role
    = graphics::QueueRole::kGraphics) const -> graphics::QueueKey
  {
    return Backend().QueueKeyFor(role);
  }

  [[nodiscard]] auto HasQueue(const graphics::QueueRole role) const -> bool
  {
    return TryGetQueue(role) != nullptr;
  }

  auto AcquireRecorder(std::string_view command_list_name,
    const graphics::QueueRole role = graphics::QueueRole::kGraphics,
    const bool immediate_submission = true) -> RecorderPtr
  {
    return Backend().AcquireCommandRecorder(
      QueueKeyFor(role), command_list_name, immediate_submission);
  }

  auto AcquireDeferredRecorder(std::string_view command_list_name,
    const graphics::QueueRole role = graphics::QueueRole::kGraphics)
    -> RecorderPtr
  {
    return AcquireRecorder(command_list_name, role, false);
  }

  auto SubmitDeferredRecorders() -> void
  {
    Backend().SubmitDeferredCommandLists();
  }

  [[nodiscard]] auto SignalQueue(const graphics::QueueRole role
    = graphics::QueueRole::kGraphics) const -> graphics::FenceValue
  {
    return graphics::FenceValue { GetQueue(role)->Signal() };
  }

  auto WaitForQueue(const graphics::FenceValue value,
    const graphics::QueueRole role = graphics::QueueRole::kGraphics) const
    -> void
  {
    GetQueue(role)->Wait(value.get());
  }

  auto WaitForQueueIdle(const graphics::QueueRole role
    = graphics::QueueRole::kGraphics) const -> graphics::FenceValue
  {
    const auto value = SignalQueue(role);
    WaitForQueue(value, role);
    return value;
  }

  auto FlushBackend() -> void { Backend().Flush(); }

  auto CreateBuffer(const graphics::BufferDesc& desc)
    -> std::shared_ptr<graphics::Buffer>
  {
    auto buffer = Backend().CreateBuffer(desc);
    CHECK_NOTNULL_F(
      buffer.get(), "Failed to create buffer `{}`", desc.debug_name);
    buffers_.push_back(buffer);
    return buffer;
  }

  auto CreateD3D12Buffer(const graphics::BufferDesc& desc)
    -> std::shared_ptr<oxygen::graphics::d3d12::Buffer>
  {
    auto buffer_base = CreateBuffer(desc);
    auto buffer
      = std::static_pointer_cast<oxygen::graphics::d3d12::Buffer>(buffer_base);
    CHECK_NOTNULL_F(buffer.get(),
      "Backend returned a non-D3D12 buffer for `{}`", desc.debug_name);
    return buffer;
  }

  auto CreateDeviceBuffer(const SizeBytes size_bytes,
    const graphics::BufferUsage usage = graphics::BufferUsage::kNone,
    std::string_view debug_name = "device-buffer")
    -> std::shared_ptr<graphics::Buffer>
  {
    return CreateBuffer(graphics::BufferDesc {
      .size_bytes = size_bytes.get(),
      .usage = usage,
      .memory = graphics::BufferMemory::kDeviceLocal,
      .debug_name = std::string(debug_name),
    });
  }

  auto CreateUploadBuffer(const SizeBytes size_bytes,
    const graphics::BufferUsage usage = graphics::BufferUsage::kNone,
    std::string_view debug_name = "upload-buffer")
    -> std::shared_ptr<graphics::Buffer>
  {
    return CreateBuffer(graphics::BufferDesc {
      .size_bytes = size_bytes.get(),
      .usage = usage,
      .memory = graphics::BufferMemory::kUpload,
      .debug_name = std::string(debug_name),
    });
  }

  auto CreateTexture(const graphics::TextureDesc& desc)
    -> std::shared_ptr<graphics::Texture>
  {
    auto texture = Backend().CreateTexture(desc);
    CHECK_NOTNULL_F(
      texture.get(), "Failed to create texture `{}`", desc.debug_name);
    textures_.push_back(texture);
    return texture;
  }

  auto CreateD3D12Texture(const graphics::TextureDesc& desc)
    -> std::shared_ptr<oxygen::graphics::d3d12::Texture>
  {
    auto texture_base = CreateTexture(desc);
    auto texture = std::static_pointer_cast<oxygen::graphics::d3d12::Texture>(
      texture_base);
    CHECK_NOTNULL_F(texture.get(),
      "Backend returned a non-D3D12 texture for `{}`", desc.debug_name);
    return texture;
  }

  auto RegisterResource(const std::shared_ptr<graphics::Buffer>& buffer) const
    -> void
  {
    CHECK_NOTNULL_F(buffer.get(), "Cannot register a null buffer");
    auto& registry = Backend().GetResourceRegistry();
    if (!registry.Contains(*buffer)) {
      registry.Register(buffer);
    }
  }

  auto RegisterResource(const std::shared_ptr<graphics::Texture>& texture) const
    -> void
  {
    CHECK_NOTNULL_F(texture.get(), "Cannot register a null texture");
    auto& registry = Backend().GetResourceRegistry();
    if (!registry.Contains(*texture)) {
      registry.Register(texture);
    }
  }

  auto CreateRegisteredBuffer(const graphics::BufferDesc& desc)
    -> std::shared_ptr<graphics::Buffer>
  {
    auto buffer = CreateBuffer(desc);
    RegisterResource(buffer);
    return buffer;
  }

  auto CreateRegisteredTexture(const graphics::TextureDesc& desc)
    -> std::shared_ptr<graphics::Texture>
  {
    auto texture = CreateTexture(desc);
    RegisterResource(texture);
    return texture;
  }

  auto EnsureTracked(graphics::CommandRecorder& recorder,
    const std::shared_ptr<graphics::Buffer>& buffer,
    const graphics::ResourceStates initial_state
    = graphics::ResourceStates::kUnknown) const -> void
  {
    RegisterResource(buffer);
    recorder.BeginTrackingResourceState(*buffer, initial_state);
  }

  auto EnsureTracked(graphics::CommandRecorder& recorder,
    const std::shared_ptr<graphics::Texture>& texture,
    const graphics::ResourceStates initial_state
    = graphics::ResourceStates::kUnknown) const -> void
  {
    RegisterResource(texture);
    recorder.BeginTrackingResourceState(*texture, initial_state);
  }

private:
  auto CleanupTrackedResources() const noexcept -> void
  {
    if (graphics_ == nullptr) {
      return;
    }

    auto& registry = graphics_->GetResourceRegistry();

    for (const auto& texture : textures_) {
      if (texture == nullptr || !registry.Contains(*texture)) {
        continue;
      }
      try {
        registry.UnRegisterResource(*texture);
      } catch (const std::exception& ex) {
        LOG_F(WARNING, "Fixture cleanup failed for texture `{}`: {}",
          texture->GetName(), ex.what());
      }
    }

    for (const auto& buffer : buffers_) {
      if (buffer == nullptr || !registry.Contains(*buffer)) {
        continue;
      }
      try {
        registry.UnRegisterResource(*buffer);
      } catch (const std::exception& ex) {
        LOG_F(WARNING, "Fixture cleanup failed for buffer `{}`: {}",
          buffer->GetName(), ex.what());
      }
    }
  }

  std::shared_ptr<oxygen::graphics::d3d12::Graphics> graphics_ {};
  std::unique_ptr<oxygen::graphics::QueuesStrategy> queue_strategy_ {};
  std::vector<std::shared_ptr<graphics::Buffer>> buffers_ {};
  std::vector<std::shared_ptr<graphics::Texture>> textures_ {};
  std::string backend_config_json_ {};
  std::string path_finder_config_json_ {};
};

class TransferQueueOffscreenTestFixture : public OffscreenTestFixture {
protected:
  [[nodiscard]] auto CreateQueueStrategy() const
    -> std::unique_ptr<oxygen::graphics::QueuesStrategy> override
  {
    return std::make_unique<oxygen::graphics::SharedTransferQueueStrategy>();
  }
};

} // namespace oxygen::graphics::d3d12::testing
