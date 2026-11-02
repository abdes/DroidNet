//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <chrono>
#include <cstddef>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/ByteUnits.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/ReadbackManager.h>
#include <Oxygen/Graphics/Common/ReadbackTracker.h>
#include <Oxygen/Graphics/Common/ReadbackTypes.h>
#include <Oxygen/Graphics/Common/Registration.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Graphics/Common/api_export.h>
#include <Oxygen/OxCo/Co.h>

namespace oxygen {
class Graphics;
} // namespace oxygen

namespace oxygen::graphics {

class CommandQueue;
class CommandRecorder;
class ReadbackStaging;
class StagedBufferReadback;
class StagedTextureReadback;

//! The copy a texture readback records.
struct TextureReadbackCopy {
  //! The texture it copies: the source, or the backend's resolve of it.
  observer_ptr<const Texture> texture;
  //! The descriptor of `texture`.
  TextureDesc desc {};
  //! The region the recorder copies.
  TextureBufferCopyRegion recorded_region {};
  //! The region as the copy places it in the staging buffer: its pitches.
  TextureBufferCopyRegion placed_region {};
  //! The bytes the copy writes to the staging buffer.
  SizeBytes bytes;
};

//! The texture a readback resolves a multisampled source into, kept from one
//! copy to the next and released with the readback.
struct TextureReadbackResolveTarget {
  std::shared_ptr<Texture> texture;
  RegistrationOwner registration;
  TextureDesc desc {};
};

//! The bytes of a region of a mapped readback surface.
struct ReadbackSurfaceBytes {
  const std::byte* data = nullptr;
  SizeBytes row_pitch;
  SizeBytes slice_pitch;
};

//! A readback manager that copies GPU resources into staging buffers and
//! tracks the copies by the fence of the one queue it reads back on.
/*!
 A backend supplies what differs between graphics APIs: the copy a texture
 readback records (a multisampled source may need a resolve) and the bytes of
 a mapped readback surface.
*/
class StagedReadbackManager : public ReadbackManager {
public:
  OXGN_GFX_API ~StagedReadbackManager() override;

  OXYGEN_MAKE_NON_COPYABLE(StagedReadbackManager)
  OXYGEN_MAKE_NON_MOVABLE(StagedReadbackManager)

  OXGN_GFX_NDAPI auto CreateBufferReadback(std::string_view debug_name)
    -> std::shared_ptr<GpuBufferReadback> final;
  OXGN_GFX_NDAPI auto CreateTextureReadback(std::string_view debug_name)
    -> std::shared_ptr<GpuTextureReadback> final;

  OXGN_GFX_API auto Await(ReadbackTicket ticket)
    -> std::expected<ReadbackResult, ReadbackError> final;
  OXGN_GFX_API auto AwaitAsync(ReadbackTicket ticket) -> co::Co<void> final;
  OXGN_GFX_API auto Cancel(ReadbackTicket ticket)
    -> std::expected<bool, ReadbackError> final;

  OXGN_GFX_API auto CreateReadbackTextureSurface(const TextureDesc& desc)
    -> std::expected<std::shared_ptr<Texture>, ReadbackError> final;
  OXGN_GFX_API auto MapReadbackTextureSurface(
    Texture& surface, TextureSlice slice)
    -> std::expected<ReadbackSurfaceMapping, ReadbackError> final;
  OXGN_GFX_API auto UnmapReadbackTextureSurface(Texture& surface) -> void final;

  OXGN_GFX_API auto OnFrameStart(frame::Slot slot) -> void final;
  OXGN_GFX_API auto Shutdown(std::chrono::milliseconds timeout)
    -> std::expected<void, ReadbackError> final;

protected:
  OXGN_GFX_API explicit StagedReadbackManager(oxygen::Graphics& graphics);

  [[nodiscard]] auto GetGraphics() const noexcept -> oxygen::Graphics&
  {
    return *graphics_;
  }

  //! Makes `target` a resolve texture for `source_desc`: single-sampled, and
  //! kept when it already is one.
  OXGN_GFX_API auto EnsureResolveTarget(TextureReadbackResolveTarget& target,
    const TextureDesc& source_desc) -> std::expected<void, ReadbackError>;

  OXGN_GFX_API auto DoReadBufferNow(const Buffer& source, BufferRange range)
    -> std::expected<std::vector<std::byte>, ReadbackError> final;
  OXGN_GFX_API auto DoReadTextureNow(const Texture& source,
    const TextureReadbackRequest& request, bool tightly_pack)
    -> std::expected<OwnedTextureReadbackData, ReadbackError> final;

  //! The copy a readback of `slice` of `source` records, after recording any
  //! resolve into `resolve`.
  virtual auto PrepareTextureReadbackCopy(CommandRecorder& recorder,
    const Texture& source, const TextureSlice& slice,
    TextureReadbackResolveTarget& resolve)
    -> std::expected<TextureReadbackCopy, ReadbackError> = 0;

  //! Maps the readback surface `surface` and returns the bytes of `slice`.
  virtual auto MapReadbackSurfaceBytes(
    Texture& surface, const TextureSlice& slice)
    -> std::expected<ReadbackSurfaceBytes, ReadbackError> = 0;

  //! Unmaps the readback surface `surface`.
  virtual auto UnmapReadbackSurfaceBytes(Texture& surface) -> void = 0;

private:
  friend class ReadbackStaging;
  friend class StagedBufferReadback;
  friend class StagedTextureReadback;

  auto EnsureTrackedQueue(observer_ptr<CommandQueue> queue)
    -> std::expected<void, ReadbackError>;
  auto PumpCompletions() -> void;
  [[nodiscard]] auto TryGetResult(ReadbackTicketId id) const
    -> std::optional<ReadbackResult>;
  auto TrackCancellationHandler(
    ReadbackTicketId id, std::function<void()> handler) -> void;
  auto UntrackCancellationHandler(ReadbackTicketId id) -> void;
  auto ForgetTicket(ReadbackTicketId id) -> void;
  [[nodiscard]] auto TrackedQueueRole() const -> QueueRole;

  observer_ptr<oxygen::Graphics> graphics_;
  mutable std::mutex mutex_;
  observer_ptr<CommandQueue> tracked_queue_;
  ReadbackTracker tracker_;
  bool shutdown_ { false };
  std::unordered_map<ReadbackTicketId, std::function<void()>>
    cancellation_handlers_;
};

} // namespace oxygen::graphics
