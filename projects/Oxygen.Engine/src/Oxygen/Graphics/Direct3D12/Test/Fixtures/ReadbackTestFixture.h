//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <expected>
#include <memory>
#include <string>
#include <string_view>

#include <Oxygen/Core/Types/ByteUnits.h>
#include <Oxygen/Graphics/Common/ReadbackManager.h>
#include <Oxygen/Graphics/Common/ReadbackTypes.h>
#include <Oxygen/Graphics/Direct3D12/Buffer.h>
#include <Oxygen/Graphics/Direct3D12/Test/Fixtures/OffscreenTestFixture.h>

namespace oxygen::graphics::d3d12::testing {

class ReadbackTestFixture : public OffscreenTestFixture {
protected:
  [[nodiscard]] auto TryGetReadbackManager() const
    -> observer_ptr<graphics::ReadbackManager>
  {
    return Backend().GetReadbackManager();
  }

  [[nodiscard]] auto GetReadbackManager() const
    -> observer_ptr<graphics::ReadbackManager>
  {
    const auto manager = TryGetReadbackManager();
    CHECK_F(manager != nullptr, "Readback manager is not installed");
    return manager;
  }

  auto CreateReadbackBuffer(
    const SizeBytes size_bytes, std::string_view debug_name = "readback-buffer")
    -> std::shared_ptr<graphics::Buffer>
  {
    return CreateBuffer(graphics::BufferDesc {
      .size_bytes = size_bytes.get(),
      .usage = graphics::BufferUsage::kNone,
      .memory = graphics::BufferMemory::kReadBack,
      .debug_name = std::string(debug_name),
    });
  }

  auto CreateD3D12ReadbackBuffer(
    const SizeBytes size_bytes, std::string_view debug_name = "readback-buffer")
    -> std::shared_ptr<oxygen::graphics::d3d12::Buffer>
  {
    return CreateD3D12Buffer(graphics::BufferDesc {
      .size_bytes = size_bytes.get(),
      .usage = graphics::BufferUsage::kNone,
      .memory = graphics::BufferMemory::kReadBack,
      .debug_name = std::string(debug_name),
    });
  }

  auto AwaitReadback(const graphics::ReadbackTicket ticket) const
    -> std::expected<graphics::ReadbackResult, graphics::ReadbackError>
  {
    return GetReadbackManager()->Await(ticket);
  }

  auto CancelReadback(const graphics::ReadbackTicket ticket) const
    -> std::expected<bool, graphics::ReadbackError>
  {
    return GetReadbackManager()->Cancel(ticket);
  }
};

class TransferQueueReadbackTestFixture
  : public TransferQueueOffscreenTestFixture {
protected:
  [[nodiscard]] auto TryGetReadbackManager() const
    -> observer_ptr<graphics::ReadbackManager>
  {
    return Backend().GetReadbackManager();
  }

  [[nodiscard]] auto GetReadbackManager() const
    -> observer_ptr<graphics::ReadbackManager>
  {
    const auto manager = TryGetReadbackManager();
    CHECK_F(manager != nullptr, "Readback manager is not installed");
    return manager;
  }

  auto CreateReadbackBuffer(
    const SizeBytes size_bytes, std::string_view debug_name = "readback-buffer")
    -> std::shared_ptr<graphics::Buffer>
  {
    return CreateBuffer(graphics::BufferDesc {
      .size_bytes = size_bytes.get(),
      .usage = graphics::BufferUsage::kNone,
      .memory = graphics::BufferMemory::kReadBack,
      .debug_name = std::string(debug_name),
    });
  }

  auto CreateD3D12ReadbackBuffer(
    const SizeBytes size_bytes, std::string_view debug_name = "readback-buffer")
    -> std::shared_ptr<oxygen::graphics::d3d12::Buffer>
  {
    return CreateD3D12Buffer(graphics::BufferDesc {
      .size_bytes = size_bytes.get(),
      .usage = graphics::BufferUsage::kNone,
      .memory = graphics::BufferMemory::kReadBack,
      .debug_name = std::string(debug_name),
    });
  }

  auto AwaitReadback(const graphics::ReadbackTicket ticket) const
    -> std::expected<graphics::ReadbackResult, graphics::ReadbackError>
  {
    return GetReadbackManager()->Await(ticket);
  }

  auto CancelReadback(const graphics::ReadbackTicket ticket) const
    -> std::expected<bool, graphics::ReadbackError>
  {
    return GetReadbackManager()->Cancel(ticket);
  }
};

} // namespace oxygen::graphics::d3d12::testing
