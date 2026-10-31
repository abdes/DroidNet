//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <Oxygen/Data/SourceKey.h>

namespace oxygen::cooker::testing {

//! The files of a loose cooked root, by path relative to it.
using LooseCookedFiles = std::map<std::string, std::vector<std::byte>>;

//! Named defects applied to a valid built artifact.
/*!
 Each defect breaks exactly one rule of its format, expressed against the
 format structs, so a test of that rule keeps testing it after a format
 change. Defects of a pak leave its CRC stale unless they say otherwise; a
 reader checks the CRC only when asked.

 ```cpp
 auto bytes = CookedPak(game).Encode();
 CookedBytes::SetPakVersion(bytes, kCurrentPakFormatVersion + 1);
 ```
*/
struct CookedBytes final {
  //=== Paks ===------------------------------------------------------------//

  //! Sets the format version of the pak header.
  static auto SetPakVersion(std::vector<std::byte>& pak, uint16_t version)
    -> void;

  //! Gives the pak header a magic of another format.
  static auto BreakPakMagic(std::vector<std::byte>& pak) -> void;

  //! Gives the pak footer a magic of another format.
  static auto BreakFooterMagic(std::vector<std::byte>& pak) -> void;

  //! Sets the source identity of the pak header only, so it disagrees with
  //! the catalog's.
  static auto SetHeaderIdentity(
    std::vector<std::byte>& pak, const data::SourceKey& key) -> void;

  //! Makes the footer name no browse index.
  static auto DropBrowseIndex(std::vector<std::byte>& pak) -> void;

  //! Makes the footer's catalog reach past the footer.
  static auto OverrunCatalog(std::vector<std::byte>& pak) -> void;

  //! Makes the footer name no embedded catalog.
  static auto DropCatalog(std::vector<std::byte>& pak) -> void;

  //! Makes the footer's asset directory start past the end of the pak.
  static auto MoveDirectoryOutside(std::vector<std::byte>& pak) -> void;

  //! Moves the footer's texture payload region off its payload boundary, by
  //! one byte; the pak must hold textures.
  static auto MisalignTextureRegion(std::vector<std::byte>& pak) -> void;

  //! Changes the CRC the footer records.
  static auto CorruptCrc(std::vector<std::byte>& pak) -> void;

  //! Records `crc` as the CRC of the pak; zero records none.
  static auto SetCrc(std::vector<std::byte>& pak, uint32_t crc) -> void;

  //! The offset of the CRC field of the footer of `pak`.
  [[nodiscard]] static auto CrcFieldOffset(const std::vector<std::byte>& pak)
    -> size_t;

  //=== Loose cooked roots ===----------------------------------------------//

  //! Sets the format version of the index of `files`.
  static auto SetIndexVersion(LooseCookedFiles& files, uint16_t version)
    -> void;

  //! Gives the index of `files` a magic of another format.
  static auto BreakIndexMagic(LooseCookedFiles& files) -> void;

  //! The index of `files`.
  [[nodiscard]] static auto Index(LooseCookedFiles& files)
    -> std::vector<std::byte>&;
};

} // namespace oxygen::cooker::testing
