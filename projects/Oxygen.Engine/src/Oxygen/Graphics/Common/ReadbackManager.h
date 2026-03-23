//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>
#include <expected>
#include <memory>
#include <string_view>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/ReadbackTypes.h>
#include <Oxygen/Graphics/Common/api_export.h>
#include <Oxygen/OxCo/Co.h>

namespace oxygen::graphics {

class CommandRecorder;
class Texture;

class OXGN_GFX_API GpuBufferReadback {
public:
  GpuBufferReadback() = default;
  virtual ~GpuBufferReadback() = default;

  OXYGEN_MAKE_NON_COPYABLE(GpuBufferReadback)
  OXYGEN_MAKE_NON_MOVABLE(GpuBufferReadback)

  virtual auto EnqueueCopy(CommandRecorder& recorder, const Buffer& source,
    BufferRange range = {}) -> std::expected<ReadbackTicket, ReadbackError>
    = 0;

  [[nodiscard]] virtual auto GetState() const noexcept -> ReadbackState = 0;
  [[nodiscard]] virtual auto Ticket() const noexcept
    -> std::optional<ReadbackTicket>
    = 0;
  [[nodiscard]] virtual auto IsReady() const
    -> std::expected<bool, ReadbackError>
    = 0;

  virtual auto TryMap() -> std::expected<MappedBufferReadback, ReadbackError>
    = 0;
  virtual auto MapNow() -> std::expected<MappedBufferReadback, ReadbackError>
    = 0;

  virtual auto Cancel() -> std::expected<bool, ReadbackError> = 0;
  virtual auto Reset() -> void = 0;
};

class OXGN_GFX_API GpuTextureReadback {
public:
  GpuTextureReadback() = default;
  virtual ~GpuTextureReadback() = default;

  OXYGEN_MAKE_NON_COPYABLE(GpuTextureReadback)
  OXYGEN_MAKE_NON_MOVABLE(GpuTextureReadback)

  virtual auto EnqueueCopy(CommandRecorder& recorder, const Texture& source,
    TextureReadbackRequest request = {})
    -> std::expected<ReadbackTicket, ReadbackError>
    = 0;

  [[nodiscard]] virtual auto GetState() const noexcept -> ReadbackState = 0;
  [[nodiscard]] virtual auto Ticket() const noexcept
    -> std::optional<ReadbackTicket>
    = 0;
  [[nodiscard]] virtual auto IsReady() const
    -> std::expected<bool, ReadbackError>
    = 0;

  virtual auto TryMap() -> std::expected<MappedTextureReadback, ReadbackError>
    = 0;
  virtual auto MapNow() -> std::expected<MappedTextureReadback, ReadbackError>
    = 0;

  virtual auto Cancel() -> std::expected<bool, ReadbackError> = 0;
  virtual auto Reset() -> void = 0;
};

class OXGN_GFX_API ReadbackManager {
public:
  ReadbackManager() = default;
  virtual ~ReadbackManager();

  OXYGEN_MAKE_NON_COPYABLE(ReadbackManager)
  OXYGEN_MAKE_NON_MOVABLE(ReadbackManager)

  [[nodiscard]] virtual auto CreateBufferReadback(std::string_view debug_name)
    -> std::shared_ptr<GpuBufferReadback>
    = 0;
  [[nodiscard]] virtual auto CreateTextureReadback(std::string_view debug_name)
    -> std::shared_ptr<GpuTextureReadback>
    = 0;

  virtual auto Await(ReadbackTicket ticket)
    -> std::expected<ReadbackResult, ReadbackError>
    = 0;
  virtual auto AwaitAsync(ReadbackTicket ticket) -> co::Co<void> = 0;
  virtual auto Cancel(ReadbackTicket ticket)
    -> std::expected<bool, ReadbackError>
    = 0;

  virtual auto ReadBufferNow(const Buffer& source, BufferRange range = {})
    -> std::expected<std::vector<std::byte>, ReadbackError>
    = 0;
  virtual auto ReadTextureNow(const Texture& source,
    TextureReadbackRequest request = {}, bool tightly_pack = true)
    -> std::expected<OwnedTextureReadbackData, ReadbackError>
    = 0;

  virtual auto CreateReadbackTextureSurface(const TextureDesc& desc)
    -> std::expected<std::shared_ptr<Texture>, ReadbackError>
    = 0;
  virtual auto MapReadbackTextureSurface(Texture& surface, TextureSlice slice)
    -> std::expected<ReadbackSurfaceMapping, ReadbackError>
    = 0;
  virtual auto UnmapReadbackTextureSurface(Texture& surface) -> void = 0;

  virtual auto OnFrameStart(frame::Slot slot) -> void = 0;
  virtual auto Shutdown(std::chrono::milliseconds timeout)
    -> std::expected<void, ReadbackError>
    = 0;
};

} // namespace oxygen::graphics
