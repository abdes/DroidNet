//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/WriteGroup.h>
#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/ResourceDescriptor.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/Serio/IoDriver.h>

namespace oxygen::content::import {

//! Emits sidecar descriptors for resource-table entries (`.otex`, `.obuf`).
/*!
 Writes compact descriptor files that map a stable authored name to an emitted
 resource-table index and descriptor payload.

 Each descriptor is published through `ImportStorage::PublishAsync` by a task
 of the import session's nursery, and `Finalize()` joins the writes.
*/
class ResourceDescriptorEmitter final {
public:
  struct Record final {
    std::string relpath;
    uint64_t size_bytes = 0;
    //! SHA-256 of the descriptor bytes, computed before they are written.
    base::Sha256Digest sha256 {};
    //! Virtual path of a texture descriptor; empty for buffer descriptors.
    std::string virtual_path;
  };

  //! `writes` is the nursery the writes run in; it outlives them.
  OXGN_COOK_API ResourceDescriptorEmitter(cooked::ImportStorage& storage,
    serio::IoDriver& driver, co::Nursery& writes,
    std::filesystem::path cooked_root);

  ~ResourceDescriptorEmitter() = default;

  OXYGEN_MAKE_NON_COPYABLE(ResourceDescriptorEmitter)
  OXYGEN_MAKE_NON_MOVABLE(ResourceDescriptorEmitter)

  //! Emit a texture descriptor at its identity and return its root-relative
  //! path. An identity in no mount throws std::invalid_argument; a path the
  //! loose cooked index cannot hold throws std::runtime_error, before any
  //! write.
  OXGN_COOK_NDAPI auto EmitTexture(std::string_view virtual_path,
    data::pak::core::ResourceIndexT resource_index,
    const data::pak::core::TextureResourceDesc& descriptor) -> std::string;

  //! Emit a buffer descriptor at an explicit relative path and return it. A
  //! path the loose cooked index cannot hold throws std::runtime_error, before
  //! any write.
  OXGN_COOK_NDAPI auto EmitBufferAtRelPath(std::string_view relpath,
    data::pak::core::ResourceIndexT resource_index,
    const data::pak::core::BufferResourceDesc& descriptor,
    std::span<const data::BufferResourceView> views = {}) -> std::string;

  //! Snapshot of emitted descriptor records.
  OXGN_COOK_NDAPI auto Records() const -> std::vector<Record>;

  //! Wait for pending descriptor writes and report success.
  OXGN_COOK_NDAPI auto Finalize() -> co::Co<Result<void, WriteFailure>>;

private:
  //! Records the descriptor `bytes` at `relpath`; false when an equal
  //! descriptor is already recorded there, so nothing is to be written.
  /*!
   @throw std::runtime_error When `relpath` already holds other bytes.
  */
  auto RecordOnce(const std::string& relpath, std::span<const std::byte> bytes,
    std::string virtual_path) -> bool;
  auto QueueWrite(std::string relpath, std::vector<std::byte> bytes) -> void;
  auto WriteDescriptor(std::string relpath, std::vector<std::byte> bytes)
    -> co::Co<Result<void>>;

  observer_ptr<cooked::ImportStorage> storage_;
  observer_ptr<serio::IoDriver> driver_;
  WriteGroup writes_;
  std::filesystem::path cooked_root_;
  std::unordered_map<std::string, Record> records_;
};

} // namespace oxygen::content::import
