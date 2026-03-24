//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Core/Types/ByteUnits.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Graphics/Common/ReadbackErrors.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ClearFlags.h>
#include <Oxygen/Graphics/Common/Types/FenceValue.h>

namespace oxygen::graphics {

using ReadbackTicketId = NamedType<uint64_t, struct ReadbackTicketIdTag,
  DefaultInitialized, Comparable, Printable, Hashable>;

inline auto to_string(ReadbackTicketId const& ticket_id)
{
  return std::to_string(ticket_id.get());
}

enum class ReadbackState : uint8_t {
  kIdle,
  kPending,
  kReady,
  kMapped,
  kFailed,
  kCancelled,
};

inline auto to_string(ReadbackState value) -> const char*
{
  // clang-format off
  switch (value) {
  case ReadbackState::kIdle: return "kIdle";
  case ReadbackState::kPending: return "kPending";
  case ReadbackState::kReady: return "kReady";
  case ReadbackState::kMapped: return "kMapped";
  case ReadbackState::kFailed: return "kFailed";
  case ReadbackState::kCancelled: return "kCancelled";
  }
  // clang-format on
  return "Unknown";
}

struct ReadbackTicket {
  ReadbackTicketId id {};
  FenceValue fence { fence::kInvalidValue };
};

struct ReadbackResult {
  ReadbackTicket ticket {};
  SizeBytes bytes_copied {};
  std::optional<ReadbackError> error {};
};

enum class MsaaReadbackMode : uint8_t {
  kDisallow,
  kResolveIfNeeded,
};

inline auto to_string(MsaaReadbackMode value) -> const char*
{
  // clang-format off
  switch (value) {
  case MsaaReadbackMode::kDisallow: return "kDisallow";
  case MsaaReadbackMode::kResolveIfNeeded: return "kResolveIfNeeded";
  }
  // clang-format on
  return "Unknown";
}

struct TextureReadbackRequest {
  TextureSlice src_slice {};
  ClearFlags aspects = ClearFlags::kColor;
  MsaaReadbackMode msaa_mode = MsaaReadbackMode::kResolveIfNeeded;
};

struct TextureReadbackLayout {
  Format format = Format::kUnknown;
  TextureType texture_type = TextureType::kUnknown;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t depth = 0;
  SizeBytes row_pitch {};
  SizeBytes slice_pitch {};
  MipLevel mip_level = 0;
  ArraySlice array_slice = 0;
  ClearFlags aspects = ClearFlags::kColor;
};

struct OwnedTextureReadbackData {
  std::vector<std::byte> bytes;
  TextureReadbackLayout layout {};
  bool tightly_packed = true;
};

struct ReadbackSurfaceMapping {
  const std::byte* data = nullptr;
  TextureReadbackLayout layout {};
};

class GpuBufferReadback;
class GpuTextureReadback;

class MappedBufferReadback {
public:
  MappedBufferReadback() = default;
  MappedBufferReadback(
    std::shared_ptr<void> guard, std::span<const std::byte> bytes)
    : guard_(std::move(guard))
    , bytes_(bytes)
  {
  }

  MappedBufferReadback(MappedBufferReadback&&) noexcept = default;
  auto operator=(MappedBufferReadback&&) noexcept
    -> MappedBufferReadback& = default;
  ~MappedBufferReadback() = default;

  [[nodiscard]] auto Bytes() const noexcept -> std::span<const std::byte>
  {
    return bytes_;
  }

private:
  friend class GpuBufferReadback;

  std::shared_ptr<void> guard_ {};
  std::span<const std::byte> bytes_ {};
};

class MappedTextureReadback {
public:
  MappedTextureReadback() = default;
  MappedTextureReadback(std::shared_ptr<void> guard, const std::byte* data,
    TextureReadbackLayout layout)
    : guard_(std::move(guard))
    , data_(data)
    , layout_(std::move(layout))
  {
  }

  MappedTextureReadback(MappedTextureReadback&&) noexcept = default;
  auto operator=(MappedTextureReadback&&) noexcept
    -> MappedTextureReadback& = default;
  ~MappedTextureReadback() = default;

  [[nodiscard]] auto Data() const noexcept -> const std::byte* { return data_; }

  [[nodiscard]] auto Layout() const noexcept -> const TextureReadbackLayout&
  {
    return layout_;
  }

private:
  friend class GpuTextureReadback;

  std::shared_ptr<void> guard_ {};
  const std::byte* data_ = nullptr;
  TextureReadbackLayout layout_ {};
};

} // namespace oxygen::graphics
