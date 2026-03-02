//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Loose/LooseCookedLayout.h>
#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/OxCo/Co.h>

namespace oxygen::content::import {

class IAsyncFileWriter;
struct FileErrorInfo;

//! Emits sidecar descriptors for resource-table entries (`.otex`, `.obuf`).
/*!
 Writes compact descriptor files that map a stable authored name to an emitted
 resource-table index and descriptor payload.

 Files are emitted asynchronously and finalized with `Finalize()`.
*/
class ResourceDescriptorEmitter final {
public:
  struct Record final {
    std::string relpath;
    uint64_t size_bytes = 0;
  };

  OXGN_COOK_API ResourceDescriptorEmitter(IAsyncFileWriter& file_writer,
    const LooseCookedLayout& layout, std::filesystem::path cooked_root);

  OXYGEN_MAKE_NON_COPYABLE(ResourceDescriptorEmitter)
  OXYGEN_MAKE_NON_MOVABLE(ResourceDescriptorEmitter)

  //! Emit a texture resource descriptor file and return its relative path.
  OXGN_COOK_NDAPI auto EmitTexture(std::string_view name_hint,
    std::string_view stable_id, data::pak::core::ResourceIndexT resource_index,
    const data::pak::render::TextureResourceDesc& descriptor) -> std::string;

  //! Emit a buffer resource descriptor file and return its relative path.
  OXGN_COOK_NDAPI auto EmitBuffer(std::string_view name_hint,
    std::string_view stable_id, data::pak::core::ResourceIndexT resource_index,
    const data::pak::core::BufferResourceDesc& descriptor) -> std::string;

  //! Emit a buffer descriptor at an explicit relative path and return it.
  OXGN_COOK_NDAPI auto EmitBufferAtRelPath(std::string_view relpath,
    data::pak::core::ResourceIndexT resource_index,
    const data::pak::core::BufferResourceDesc& descriptor) -> std::string;

  //! Snapshot of emitted descriptor records.
  OXGN_COOK_NDAPI auto Records() const -> std::vector<Record>;

  //! Wait for pending descriptor writes and report success.
  OXGN_COOK_NDAPI auto Finalize() -> co::Co<bool>;

private:
  auto QueueWrite(
    std::string relpath, std::shared_ptr<std::vector<std::byte>> bytes) -> void;
  auto OnWriteComplete(std::string_view relpath, const FileErrorInfo& error)
    -> void;

  IAsyncFileWriter& file_writer_;
  LooseCookedLayout layout_;
  std::filesystem::path cooked_root_;
  std::unordered_map<std::string, uint64_t> record_sizes_;
  std::atomic<size_t> pending_count_ { 0 };
  std::atomic<size_t> error_count_ { 0 };
};

} // namespace oxygen::content::import
