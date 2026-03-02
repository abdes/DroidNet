//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Cooker/Import/Internal/ResourceTableAggregator.h>
#include <Oxygen/Cooker/Loose/LooseCookedLayout.h>
#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/OxCo/Co.h>

namespace oxygen::content::import {

class IAsyncFileWriter;

class ResourceTableRegistry final {
public:
  OXGN_COOK_API explicit ResourceTableRegistry(IAsyncFileWriter& file_writer);

  OXYGEN_MAKE_NON_COPYABLE(ResourceTableRegistry)
  OXYGEN_MAKE_NON_MOVABLE(ResourceTableRegistry)

  OXGN_COOK_NDAPI auto TextureAggregator(
    const std::filesystem::path& cooked_root, const LooseCookedLayout& layout)
    -> TextureTableAggregator&;

  OXGN_COOK_NDAPI auto BufferAggregator(
    const std::filesystem::path& cooked_root, const LooseCookedLayout& layout)
    -> BufferTableAggregator&;

  OXGN_COOK_NDAPI auto PhysicsAggregator(
    const std::filesystem::path& cooked_root, const LooseCookedLayout& layout)
    -> PhysicsTableAggregator&;

  //! Register an active import session for a cooked root.
  OXGN_COOK_API auto BeginSession(const std::filesystem::path& cooked_root)
    -> void;

  //! Complete a session and finalize tables if it was the last one.
  OXGN_COOK_API auto EndSession(const std::filesystem::path& cooked_root)
    -> co::Co<bool>;

  OXGN_COOK_NDAPI auto FinalizeAll() -> co::Co<bool>;

  //! Register (or verify) the canonical .opres relpath for a physics resource.
  /*!
   For a given cooked root and physics resource index, all equivalent
   * emitted
   resources must use exactly one canonical descriptor relpath. The
   * first
   relpath seen for an index becomes canonical for the active
   * cooked-root
   session set.

   @param cooked_root Cooked-root key for this
   * mapping.
   @param resource_index Physics resource table index.
   @param
   * requested_relpath Candidate descriptor relpath.
   @param canonical_relpath
   * Receives the canonical relpath bound to the index.
   @return true if @p
   * requested_relpath matches the canonical relpath, false
           if it
   * conflicts with an already-registered canonical relpath.
  */
  OXGN_COOK_API auto TryRegisterPhysicsCanonicalDescriptorRelPath(
    const std::filesystem::path& cooked_root, uint32_t resource_index,
    std::string_view requested_relpath, std::string& canonical_relpath) -> bool;

private:
  [[nodiscard]] auto NormalizeKey(
    const std::filesystem::path& cooked_root) const -> std::string;

  IAsyncFileWriter& file_writer_;
  std::mutex mutex_;
  std::unordered_map<std::string, std::unique_ptr<TextureTableAggregator>>
    texture_tables_;
  std::unordered_map<std::string, std::unique_ptr<BufferTableAggregator>>
    buffer_tables_;
  std::unordered_map<std::string, std::unique_ptr<PhysicsTableAggregator>>
    physics_tables_;
  std::unordered_map<std::string, std::unordered_map<uint32_t, std::string>>
    physics_descriptor_relpath_by_index_;
  std::unordered_map<std::string, uint32_t> active_sessions_;
};

} // namespace oxygen::content::import
