//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/api_export.h>

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
  using DescT = pak::PhysicsResourceDesc;

  //! Constructs a PhysicsResource with descriptor + exclusive data ownership.
  OXGN_DATA_API PhysicsResource(
    pak::PhysicsResourceDesc desc, std::vector<uint8_t> data);

  ~PhysicsResource() override = default;

  OXYGEN_MAKE_NON_COPYABLE(PhysicsResource)
  OXYGEN_DEFAULT_MOVABLE(PhysicsResource)

  [[nodiscard]] auto GetDataOffset() const noexcept
  {
    return desc_.data_offset;
  }
  [[nodiscard]] auto GetDataSize() const noexcept { return data_.size(); }
  [[nodiscard]] auto GetFormat() const noexcept { return desc_.format; }
  [[nodiscard]] auto GetContentHash() const noexcept -> uint64_t
  {
    return desc_.content_hash;
  }

  [[nodiscard]] auto GetData() const noexcept -> std::span<const uint8_t>
  {
    return std::span<const uint8_t>(data_.data(), data_.size());
  }

private:
  pak::PhysicsResourceDesc desc_ {};
  std::vector<uint8_t> data_;
};

} // namespace oxygen::data
