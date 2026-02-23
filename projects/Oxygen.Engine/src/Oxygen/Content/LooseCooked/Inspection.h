//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <memory>
#include <span>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Content/LooseCooked/Types.h>
#include <Oxygen/Content/api_export.h>
#include <Oxygen/Data/SourceKey.h>

namespace oxygen::content::lc {

//! Read-only inspection view over a loose cooked `container.index.bin`.
/*!
 This is a tooling-oriented API intended for diagnostics and inspection.

 It intentionally does not expose internal Content implementation types.
*/
class Inspection final {
public:
  using AssetEntry = lc::AssetEntry;
  using FileEntry = lc::FileEntry;

  OXGN_CNTT_API Inspection();
  OXGN_CNTT_API ~Inspection();

  OXYGEN_MAKE_NON_COPYABLE(Inspection)
  OXGN_CNTT_API Inspection(Inspection&&) noexcept;
  OXGN_CNTT_API auto operator=(Inspection&&) noexcept -> Inspection&;

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
  OXGN_CNTT_NDAPI auto Guid() const noexcept -> data::SourceKey;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace oxygen::content::lc
