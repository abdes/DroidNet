//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Cooker/Loose/Types.h>
#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Data/TextureResourceDescriptor.h>

namespace oxygen::content::lc {

//! Read-only inspection view over a cooked source: a loose cooked root's
//! `container.index.bin`, or a `.pak` file.
/*!
 This is a tooling-oriented API intended for diagnostics, inspection and cook
 context lookups. A pak contributes its directory and, through its browse
 index, the virtual paths of its assets; it has no member files, so its asset
 entries carry no descriptor relative path.

 It intentionally does not expose internal Content implementation types.
*/
class Inspection final {
public:
  using AssetEntry = lc::AssetEntry;
  using FileEntry = lc::FileEntry;

  OXGN_COOK_API Inspection();
  OXGN_COOK_API ~Inspection();

  OXYGEN_MAKE_NON_COPYABLE(Inspection)
  OXGN_COOK_API Inspection(Inspection&&) noexcept;
  OXGN_COOK_API auto operator=(Inspection&&) noexcept -> Inspection&;

  //! Load a cooked source: a `.pak` file or a loose cooked root directory.
  /*!
   @param source Path to a pak file or to a directory containing
     `container.index.bin`.
   @throw std::runtime_error If the source cannot be loaded or fails
     validation.
  */
  OXGN_COOK_API auto Load(const std::filesystem::path& source) -> void;

  //! Whether a path is a cooked source `Load` accepts: a pak file or a
  //! directory containing `container.index.bin`.
  OXGN_COOK_NDAPI static auto IsCookedSource(
    const std::filesystem::path& source) noexcept -> bool;

  //! Load a pak file's directory and browse index.
  /*!
   @param pak_path Path to the `.pak` file.
   @throw std::runtime_error If the pak cannot be opened or fails validation.
  */
  OXGN_COOK_API auto LoadFromPak(const std::filesystem::path& pak_path) -> void;

  //! Load and validate `container.index.bin` from a cooked root.
  /*!
   @param cooked_root Root directory containing `container.index.bin`.
   @throw std::runtime_error If the index cannot be loaded or fails validation.
  */
  OXGN_COOK_API auto LoadFromRoot(const std::filesystem::path& cooked_root)
    -> void;

  //! Load and validate a specific index file.
  /*!
   @param index_path Path to `container.index.bin`.
   @throw std::runtime_error If the index cannot be loaded or fails validation.
  */
  OXGN_COOK_API auto LoadFromFile(const std::filesystem::path& index_path)
    -> void;

  OXGN_COOK_NDAPI auto Assets() const noexcept -> std::span<const AssetEntry>;
  OXGN_COOK_NDAPI auto Files() const noexcept -> std::span<const FileEntry>;
  OXGN_COOK_NDAPI auto Guid() const noexcept -> data::SourceKey;

  //! Whether the loaded source is a pak file rather than a loose root.
  OXGN_COOK_NDAPI auto IsPak() const noexcept -> bool;

  //! Read an asset's descriptor bytes from the loaded source.
  /*!
   @throw std::runtime_error If the source does not list the asset or its
     descriptor cannot be read.
  */
  OXGN_COOK_NDAPI auto ReadAssetDescriptor(const data::AssetKey& key) const
    -> std::vector<std::byte>;

  //! Read an indexed texture sidecar and verify agreement with its resource
  //! table.
  OXGN_COOK_NDAPI auto ReadTextureDescriptor(
    std::string_view relative_path) const -> data::TextureResourceDescriptor;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace oxygen::content::lc
