//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <span>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/api_export.h>
#include <Oxygen/Serio/ByteBuffer.h>

namespace oxygen::data {

//! Script resource as described in the PAK file resource table.
/*!
 Represents a script payload (bytecode or source) referenced by a
 ScriptAsset.
 This is a low-level container for the script's binary payload
 bytes.

 @see ScriptResourceDesc for interpretation of fields in PakFormat.h
*/
class ScriptResource : public Object {
  OXYGEN_TYPED(ScriptResource)

public:
  //! Type alias for the descriptor type used by this resource.
  using DescT = pak::scripting::ScriptResourceDesc;

  //! Constructs a ScriptResource that owns `data`, the buffer its loader read
  //! the payload into.
  OXGN_DATA_API ScriptResource(
    pak::scripting::ScriptResourceDesc desc, serio::ByteBuffer data);

  ~ScriptResource() override = default;

  OXYGEN_MAKE_NON_COPYABLE(ScriptResource)
  OXYGEN_DEFAULT_MOVABLE(ScriptResource)

  //! Returns the absolute offset to the payload in the PAK file.
  [[nodiscard]] auto GetDataOffset() const noexcept
  {
    return desc_.data_offset;
  }

  //! Returns the payload size in bytes.
  [[nodiscard]] auto GetDataSize() const noexcept
  {
    return data_.Bytes().size();
  }

  //! Returns the script language (Luau, etc.).
  [[nodiscard]] auto GetLanguage() const noexcept { return desc_.language; }

  //! Returns the script encoding (Bytecode, etc.).
  [[nodiscard]] auto GetEncoding() const noexcept { return desc_.encoding; }

  //! Returns the compression type used for the payload.
  [[nodiscard]] auto GetCompression() const noexcept
  {
    return desc_.compression;
  }

  //! Returns the SHA-256 of the script payload, from the descriptor.
  [[nodiscard]] auto GetContentHash() const noexcept
    -> const pak::core::ContentHashDigest&
  {
    return desc_.content_hash;
  }

  //! Returns an immutable span of the script payload bytes.
  [[nodiscard]] auto GetData() const noexcept -> std::span<const uint8_t>
  {
    return data_.Unsigned();
  }

private:
  pak::scripting::ScriptResourceDesc desc_ {};
  serio::ByteBuffer data_;
};

} // namespace oxygen::data
