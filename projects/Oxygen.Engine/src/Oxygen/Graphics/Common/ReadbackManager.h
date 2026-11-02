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

class GpuBufferReadback
  : public std::enable_shared_from_this<GpuBufferReadback> {
public:
  GpuBufferReadback() = default;
  virtual ~GpuBufferReadback() = default;

  OXYGEN_MAKE_NON_COPYABLE(GpuBufferReadback)
  OXYGEN_MAKE_NON_MOVABLE(GpuBufferReadback)

  //! Records a copy of `range` of `source` into this readback's staging.
  auto EnqueueCopy(CommandRecorder& recorder, const Buffer& source,
    const BufferRange range = {})
    -> std::expected<ReadbackTicket, ReadbackError>
  {
    return DoEnqueueCopy(recorder, source, range);
  }

  [[nodiscard]] virtual auto GetState() const noexcept -> ReadbackState = 0;
  [[nodiscard]] virtual auto Ticket() const noexcept
    -> std::optional<ReadbackTicket> = 0;
  [[nodiscard]] virtual auto IsReady() const
    -> std::expected<bool, ReadbackError> = 0;

  virtual auto TryMap()
    -> std::expected<MappedBufferReadback, ReadbackError> = 0;
  virtual auto MapNow()
    -> std::expected<MappedBufferReadback, ReadbackError> = 0;

  virtual auto Cancel() -> std::expected<bool, ReadbackError> = 0;
  virtual auto Reset() -> void = 0;

  //! Rearm a completed, unmapped request while retaining staging capacity.
  /*!
    Does not wait or invalidate a live mapping. Success forgets the old ticket,
    clears the request range/error, and returns to Idle without releasing the
    staging allocation or its registration. A later EnqueueCopy may grow it.

    Pending requests return kAlreadyPending unless a nonblocking completion
    refresh proves them complete; mapped requests return kAlreadyMapped,
    cancelled requests kCancelled, failed requests their stored error (or
    kBackendFailure), and Idle requests kNotReady. Reset() retains its existing
    release behavior; owners opt into capacity reuse explicitly.
  */
  virtual auto ResetForReuse() -> std::expected<void, ReadbackError> = 0;

protected:
  virtual auto DoEnqueueCopy(CommandRecorder& recorder, const Buffer& source,
    BufferRange range) -> std::expected<ReadbackTicket, ReadbackError> = 0;
};

class GpuTextureReadback
  : public std::enable_shared_from_this<GpuTextureReadback> {
public:
  GpuTextureReadback() = default;
  virtual ~GpuTextureReadback() = default;

  OXYGEN_MAKE_NON_COPYABLE(GpuTextureReadback)
  OXYGEN_MAKE_NON_MOVABLE(GpuTextureReadback)

  //! Records a copy of the region `request` names of `source` into this
  //! readback's staging.
  auto EnqueueCopy(CommandRecorder& recorder, const Texture& source,
    const TextureReadbackRequest& request = {})
    -> std::expected<ReadbackTicket, ReadbackError>
  {
    return DoEnqueueCopy(recorder, source, request);
  }

  [[nodiscard]] virtual auto GetState() const noexcept -> ReadbackState = 0;
  [[nodiscard]] virtual auto Ticket() const noexcept
    -> std::optional<ReadbackTicket> = 0;
  [[nodiscard]] virtual auto IsReady() const
    -> std::expected<bool, ReadbackError> = 0;

  virtual auto TryMap()
    -> std::expected<MappedTextureReadback, ReadbackError> = 0;
  virtual auto MapNow()
    -> std::expected<MappedTextureReadback, ReadbackError> = 0;

  virtual auto Cancel() -> std::expected<bool, ReadbackError> = 0;
  virtual auto Reset() -> void = 0;

protected:
  virtual auto DoEnqueueCopy(CommandRecorder& recorder, const Texture& source,
    const TextureReadbackRequest& request)
    -> std::expected<ReadbackTicket, ReadbackError> = 0;
};

class ReadbackManager {
public:
  ReadbackManager() = default;
  OXGN_GFX_API virtual ~ReadbackManager();

  OXYGEN_MAKE_NON_COPYABLE(ReadbackManager)
  OXYGEN_MAKE_NON_MOVABLE(ReadbackManager)

  [[nodiscard]] virtual auto CreateBufferReadback(std::string_view debug_name)
    -> std::shared_ptr<GpuBufferReadback> = 0;
  [[nodiscard]] virtual auto CreateTextureReadback(std::string_view debug_name)
    -> std::shared_ptr<GpuTextureReadback> = 0;

  virtual auto Await(ReadbackTicket ticket)
    -> std::expected<ReadbackResult, ReadbackError> = 0;
  virtual auto AwaitAsync(ReadbackTicket ticket) -> co::Co<void> = 0;
  virtual auto Cancel(ReadbackTicket ticket)
    -> std::expected<bool, ReadbackError> = 0;

  //! Reads `range` of `source` back now: records, submits and waits.
  auto ReadBufferNow(const Buffer& source, const BufferRange range = {})
    -> std::expected<std::vector<std::byte>, ReadbackError>
  {
    return DoReadBufferNow(source, range);
  }

  //! Reads the region `request` names of `source` back now, its rows tightly
  //! packed unless `tightly_pack` is false.
  auto ReadTextureNow(const Texture& source,
    const TextureReadbackRequest& request = {}, const bool tightly_pack = true)
    -> std::expected<OwnedTextureReadbackData, ReadbackError>
  {
    return DoReadTextureNow(source, request, tightly_pack);
  }

  virtual auto CreateReadbackTextureSurface(const TextureDesc& desc)
    -> std::expected<std::shared_ptr<Texture>, ReadbackError> = 0;
  virtual auto MapReadbackTextureSurface(Texture& surface, TextureSlice slice)
    -> std::expected<ReadbackSurfaceMapping, ReadbackError> = 0;
  virtual auto UnmapReadbackTextureSurface(Texture& surface) -> void = 0;

  virtual auto OnFrameStart(frame::Slot slot) -> void = 0;
  virtual auto Shutdown(std::chrono::milliseconds timeout)
    -> std::expected<void, ReadbackError> = 0;

protected:
  virtual auto DoReadBufferNow(const Buffer& source, BufferRange range)
    -> std::expected<std::vector<std::byte>, ReadbackError> = 0;
  virtual auto DoReadTextureNow(const Texture& source,
    const TextureReadbackRequest& request, bool tightly_pack)
    -> std::expected<OwnedTextureReadbackData, ReadbackError> = 0;
};

} // namespace oxygen::graphics
