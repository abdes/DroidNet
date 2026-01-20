//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Content/api_export.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::content {

//! Read-only inspection view over a loose cooked `container.index.bin`.
/*!
 This is a tooling-oriented API intended for diagnostics and inspection.

 It intentionally does not expose internal Content implementation types.
*/
class LooseCookedInspection final {
public:
  struct AssetEntry {
    data::AssetKey key {};
    std::string virtual_path;
    std::string descriptor_relpath;
    uint64_t descriptor_size = 0;
    uint8_t asset_type = 0;
    std::optional<base::Sha256Digest> descriptor_sha256;
  };

  struct FileEntry {
    data::loose_cooked::v1::FileKind kind
      = data::loose_cooked::v1::FileKind::kUnknown;
    std::string relpath;
    uint64_t size = 0;
  };

  OXGN_CNTT_API LooseCookedInspection();
  OXGN_CNTT_API ~LooseCookedInspection();

  OXYGEN_MAKE_NON_COPYABLE(LooseCookedInspection)
  OXGN_CNTT_API LooseCookedInspection(LooseCookedInspection&&) noexcept;
  OXGN_CNTT_API auto operator=(LooseCookedInspection&&) noexcept
    -> LooseCookedInspection&;

  //! Load and validate `container.index.bin` from a cooked root.
  /*!
   @param cooked_root Root directory containing `container.index.bin`.
   @throw std::runtime_error If the index cannot be loaded or fails validation.
  */
  OXGN_CNTT_API auto LoadFromRoot(const std::filesystem::path& cooked_root)
    -> void;

  //! Load and validate a specific index file.
  /*!
   @param index_path Path to `container.index.bin`.
   @throw std::runtime_error If the index cannot be loaded or fails validation.
  */
  OXGN_CNTT_API auto LoadFromFile(const std::filesystem::path& index_path)
    -> void;

  OXGN_CNTT_NDAPI auto Assets() const noexcept -> std::span<const AssetEntry>;

  OXGN_CNTT_NDAPI auto Files() const noexcept -> std::span<const FileEntry>;

  //! Get the GUID from the index header.
  OXGN_CNTT_NDAPI auto Guid() const noexcept -> data::SourceKey;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace oxygen::content
