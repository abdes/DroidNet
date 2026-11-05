//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <span>
#include <stdexcept>
#include <string>

#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Serio/ByteReader.h>

//! Reading the packed records of cooked bytes.
/*!
 A test that reads a record of what a cook wrote fails when the bytes do not
 hold it, rather than go on with a default-constructed record that reads as
 what the cook should have written.
*/
namespace oxygen::cooker::test {

//! The `Record` at `offset` of `bytes`.
/*!
 @throw std::runtime_error When the bytes do not hold it.
*/
template <typename Record>
[[nodiscard]] auto ReadRecord(const std::span<const std::byte> bytes,
  const std::size_t offset = 0) -> Record
{
  const auto record
    = serio::CopyAt<Record>(bytes, serio::ByteOffset { offset });
  if (!record) {
    throw std::runtime_error("Cooked bytes of size "
      + std::to_string(bytes.size()) + " hold no record at offset "
      + std::to_string(offset));
  }
  return *record;
}

//! The material record a cooked material descriptor starts with.
[[nodiscard]] inline auto ReadMaterialDesc(
  const std::span<const std::byte> descriptor)
  -> data::pak::render::MaterialAssetDesc
{
  return ReadRecord<data::pak::render::MaterialAssetDesc>(descriptor);
}

} // namespace oxygen::cooker::test
