//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Graphics/Common/Types/FenceValue.h>
#include <Oxygen/Renderer/Upload/Errors.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen {
class Graphics; // forward owner context
} // namespace oxygen

namespace oxygen::graphics {
class Buffer;
class Texture;
} // namespace oxygen::graphics

namespace oxygen::engine::upload {

//=== Types & strong aliases -------------------------------------------------//

using SizeBytes = NamedType<uint64_t, struct BytesTag,
  // clang-format off
  DefaultInitialized,
  Arithmetic>; // clang-format on

inline auto to_string(SizeBytes const& b)
{
  return std::to_string(b.get()) + " bytes";
}

using OffsetBytes = NamedType<uint64_t, struct OffsetBytesTag,
  // clang-format off
  DefaultInitialized,
  Arithmetic>; // clang-format on

inline auto to_string(OffsetBytes const& b)
{
  return std::to_string(b.get()) + " bytes";
}

using Alignment = NamedType<uint32_t, struct AlignmentTag,
  // clang-format off
  DefaultInitialized,
  Comparable,
  Printable>; // clang-format on

using TicketId = NamedType<uint64_t, struct TicketIdTag,
  // clang-format off
  DefaultInitialized,
  Comparable,
  Printable,
  Hashable>; // clang-format on

using Priority = NamedType<int, struct PriorityTag,
  // clang-format off
  DefaultInitialized,
  Comparable,
  Printable>; // clang-format on

// FenceValue exists in graphics common; reuse to avoid duplication.
using FenceValue = graphics::FenceValue;

//=== POD contracts ----------------------------------------------------------//

enum class UploadKind : uint8_t {
  kBuffer,
  kTexture2D,
  kTexture3D,
  kTextureCube,
};

struct UploadBufferDesc {
  std::shared_ptr<graphics::Buffer> dst;
  uint64_t size_bytes { 0 };
  uint64_t dst_offset { 0 };
};

struct UploadTextureDesc {
  std::shared_ptr<graphics::Texture> dst;
  uint32_t width { 0 };
  uint32_t height { 0 };
  uint32_t depth { 1 };
  Format format { Format::kUnknown };
};

struct UploadSubresource {
  uint32_t mip { 0 };
  uint32_t array_slice { 0 };
  // Box in texels; width/height/depth of 0 means full subresource.
  uint32_t x { 0 }, y { 0 }, z { 0 };
  uint32_t width { 0 }, height { 0 }, depth { 0 };
  uint32_t row_pitch { 0 };
  uint32_t slice_pitch { 0 };
};

struct UploadDataView {
  std::span<const std::byte> bytes {};
};

struct UploadRequest {
  UploadKind kind { UploadKind::kBuffer };
  Priority priority { 0 };
  std::string debug_name;

  std::variant<UploadBufferDesc, UploadTextureDesc> desc;
  std::vector<UploadSubresource> subresources {};

  // Either a view or a producer; implementation will support both paths.
  std::variant<UploadDataView,
    std::move_only_function<bool(std::span<std::byte>)>>
    data;
};

//! Represents a valid GPU upload operation that can be tracked for completion.
/*!
 A ticket is issued for every successful upload submission and provides a way to
 query completion status and retrieve results. All tickets are guaranteed to be
 valid and represent actual upload operations.

 @see UploadCoordinator::Submit(), UploadTracker::IsComplete()
*/
struct UploadTicket {
  TicketId id;
  FenceValue fence;

  // Non-default constructible - all tickets must be explicitly created with
  // valid values
  UploadTicket() = delete;
  UploadTicket(TicketId ticket_id, FenceValue fence_value)
    : id(ticket_id)
    , fence(fence_value)
  {
  }

  // Rule of 5: explicit copy/move semantics
  OXYGEN_DEFAULT_COPYABLE(UploadTicket)
  OXYGEN_DEFAULT_MOVABLE(UploadTicket)

  ~UploadTicket() = default;
};

struct UploadResult {
  bool success { false };
  uint64_t bytes_uploaded { 0 };
  std::optional<UploadError> error;
};

} // namespace oxygen::engine::upload
