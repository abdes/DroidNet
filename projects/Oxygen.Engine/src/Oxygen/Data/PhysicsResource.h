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
  //! Returns the SHA-256 of the physics data, from the descriptor.
  [[nodiscard]] auto GetContentHash() const noexcept
    -> const pak::core::ContentHashDigest&
  {
    return desc_.content_hash;
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
