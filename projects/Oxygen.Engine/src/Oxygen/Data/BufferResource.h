//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/api_export.h>

namespace oxygen::data {

//! Buffer resource as described in the PAK file resource table.
/*!
 Represents a buffer resource referenced by assets in the PAK file. This is not
 a first-class asset: it is not named or globally identified, but is referenced
 by index in the buffers resource table from geometry or other assets.

 ```text
 offset size name             description
 ------ ---- ---------------- ---------------------------------------------
 0x00   8    offset_bytes     Absolute offset to buffer data in PAK file
 0x08   4    size_bytes       Size of buffer data in bytes
 0x0C   4    usage_flags      Buffer usage and access flags (bitfield)
 0x10   4    element_stride   Stride of each element in bytes (1 = raw)
 0x14   1    element_format   Buffer element format enum value
 0x15   11   reserved         Reserved for future use
 ```

 @see BufferResourceDesc for interpretation of fields.
*/
class BufferResource : public oxygen::Object {
  OXYGEN_TYPED(BufferResource)

public:
  //! Buffer usage and access flags for BufferResource.
  enum class UsageFlags : uint32_t {
    kNone = 0, //!< No flags set

    // --- Buffer Role Flags (can be combined) ---
    kVertexBuffer = 0x01, //!< Vertex input source
    kIndexBuffer = 0x02, //!< Index input source
    kConstantBuffer = 0x04, //!< Shader constants/uniforms
    kStorageBuffer = 0x08, //!< Read/write in shaders
    kIndirectBuffer = 0x10, //!< Indirect draw/dispatch arguments

    // --- CPU Access Flags (can be combined) ---
    kCPUWritable = 0x20, //!< CPU can write to buffer
    kCPUReadable = 0x40, //!< CPU can read from buffer

    // --- Update Frequency Flags (mutually exclusive) ---
    kDynamic = 0x80, //!< Frequently updated
    kStatic = 0x100, //!< Rarely updated
    kImmutable = 0x200, //!< Never updated after creation
  };

  explicit BufferResource(pak::BufferResourceDesc desc)
    : desc_(std::move(desc))
  {
  }

  ~BufferResource() override = default;

  OXYGEN_MAKE_NON_COPYABLE(BufferResource)
  OXYGEN_DEFAULT_MOVABLE(BufferResource)

  [[nodiscard]] auto GetDataOffset() const noexcept
  {
    return desc_.offset_bytes;
  }

  [[nodiscard]] auto GetDataSize() const noexcept { return desc_.size_bytes; }

  //! Returns the buffer usage and access flags (bitfield).
  [[nodiscard]] auto GetUsageFlags() const noexcept
  {
    return static_cast<UsageFlags>(desc_.usage_flags);
  }

  //! Returns the stride of each element in bytes (ignored for raw buffers, i.e.
  //! format is Format::kUnknown).
  [[nodiscard]] auto GetElementStride() const noexcept
  {
    return desc_.element_stride;
  }

  //! Returns the element format. If 0, buffer is raw bytes (Format::kUnknown).
  OXGN_DATA_NDAPI auto GetElementFormat() const noexcept -> Format;

  //! Returns true if the buffer is a formatted buffer (typed, not
  //! raw/structured).
  [[nodiscard]] auto IsFormatted() const noexcept
  {
    // Formatted: element_format != 0, element_stride should be 0 and is ignored
    return GetElementFormat() != Format::kUnknown;
  }

  //! Returns true if the buffer is structured (element_format == 0, stride >
  //! 1).
  [[nodiscard]] auto IsStructured() const noexcept
  {
    return GetElementFormat() == Format::kUnknown && GetElementStride() > 1;
  }

  //! Returns true if the buffer is raw (element_format == 0, stride == 1).
  [[nodiscard]] auto IsRaw() const noexcept
  {
    return GetElementFormat() == Format::kUnknown && GetElementStride() == 1;
  }

private:
  pak::BufferResourceDesc desc_ {};
};

// Define bitwise operators for UsageFlags enum.
OXYGEN_DEFINE_FLAGS_OPERATORS(BufferResource::UsageFlags)

// Returns a string representation of UsageFlags bitmask.
auto to_string(BufferResource::UsageFlags value) -> std::string;

} // namespace oxygen::data
