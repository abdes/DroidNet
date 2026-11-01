//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/api_export.h>
#include <Oxygen/Serio/ByteBuffer.h>
#include <Oxygen/Serio/ByteReader.h>

namespace oxygen::data {

//! Physics resource as described in the PAK file resource table.
/*!
 Represents a physics resource referenced by assets in the PAK file.
 This mirrors the structure and purpose of BufferResource but for physics blobs.

 @see PhysicsResourceDesc for interpretation of fields.
 */
class PhysicsResource : public Object {
  OXYGEN_TYPED(PhysicsResource)

public:
  //! Type alias for the descriptor type used by this resource.
  using DescT = pak::physics::PhysicsResourceDesc;

  //! Constructs a PhysicsResource that owns `data`, the buffer its loader
  //! read the payload into.
  OXGN_DATA_API PhysicsResource(
    pak::physics::PhysicsResourceDesc desc, serio::ByteBuffer data);

  ~PhysicsResource() override = default;

  OXYGEN_MAKE_NON_COPYABLE(PhysicsResource)
  OXYGEN_DEFAULT_MOVABLE(PhysicsResource)

  [[nodiscard]] auto GetDataOffset() const noexcept
  {
    return desc_.data_offset;
  }
  [[nodiscard]] auto GetDataSize() const noexcept
  {
    return data_.Bytes().size();
  }
  [[nodiscard]] auto GetFormat() const noexcept { return desc_.format; }
  [[nodiscard]] auto GetContentHash() const noexcept -> uint64_t
  {
    const auto hash_prefix = serio::CopyAt<uint64_t>(
      std::as_bytes(std::span<const uint8_t> { desc_.content_hash }));
    if (!hash_prefix) {
      return 0;
    }
    return *hash_prefix;
  }
  [[nodiscard]] auto GetContentHashBytes() const noexcept
    -> std::span<const uint8_t, 32>
  {
    return std::span<const uint8_t, 32> { desc_.content_hash };
  }

  [[nodiscard]] auto GetData() const noexcept -> std::span<const uint8_t>
  {
    return data_.Unsigned();
  }

private:
  pak::physics::PhysicsResourceDesc desc_ {};
  serio::ByteBuffer data_;
};

} // namespace oxygen::data
