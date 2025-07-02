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
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/FileStream.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Reader.h>
#include <Oxygen/Content/api_export.h>
#include <Oxygen/Data/PakFormat.h>

namespace oxygen::content {

//! Reader for Oxygen .pak asset containers.
/*! Provides read-only access to asset directory and data in a .pak file.

  @see AssetDirectoryEntry, AssetKey
*/
class PakFile {
public:
  using Reader = oxygen::serio::Reader<oxygen::serio::FileStream<>>;

  //! 8-byte header magic: {'O','X','P','A','K',0,0,0}
  static constexpr std::array<uint8_t, 8> kHeaderMagic
    = { 'O', 'X', 'P', 'A', 'K', 0, 0, 0 };

  //! 8-byte footer magic: {'O','X','P','A','K','E','N','D'}
  static constexpr std::array<uint8_t, 8> kFooterMagic
    = { 'O', 'X', 'P', 'A', 'K', 'E', 'N', 'D' };

  //! Open a .pak file for reading.
  OXGN_CNTT_API explicit PakFile(const std::filesystem::path& path);

  ~PakFile() = default;

  OXYGEN_MAKE_NON_COPYABLE(PakFile)
  OXYGEN_DEFAULT_MOVABLE(PakFile)

  //! Find a directory entry by asset key.
  OXGN_CNTT_NDAPI auto FindEntry(const data::AssetKey& key) const noexcept
    -> std::optional<data::pak::AssetDirectoryEntry>;

  //! Get the full asset directory.
  OXGN_CNTT_NDAPI auto Directory() const noexcept
    -> std::span<const data::pak::AssetDirectoryEntry>;

  //! Create a Reader positioned at the asset's data.
  OXGN_CNTT_NDAPI auto CreateReader(
    const data::pak::AssetDirectoryEntry& entry) const -> Reader;

private:
  void ReadHeader(oxygen::serio::FileStream<>* stream);
  void ReadFooter(oxygen::serio::FileStream<>* stream);
  void ReadDirectory(oxygen::serio::FileStream<>* stream, uint32_t asset_count);
  auto ReadDirectoryEntry(Reader& reader) -> void;

  data::pak::PakHeader header_ {};
  data::pak::PakFooter footer_ {};
  std::unique_ptr<oxygen::serio::FileStream<>> stream_;
  std::vector<data::pak::AssetDirectoryEntry> directory_;
  mutable std::mutex mutex_;
  mutable std::unordered_map<data::AssetKey, size_t> key_to_index_;
};

} // namespace oxygen::content
