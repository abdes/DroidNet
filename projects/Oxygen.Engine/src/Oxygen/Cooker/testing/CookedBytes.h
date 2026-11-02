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

#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
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

  //! Sets a reserved byte of the pak header.
  static auto SetHeaderReserved(std::vector<std::byte>& pak) -> void;

  //! Sets a reserved byte of the pak footer.
  static auto SetFooterReserved(std::vector<std::byte>& pak) -> void;

  //! Sets a reserved byte of the first asset directory entry.
  static auto SetDirectoryEntryReserved(std::vector<std::byte>& pak) -> void;

  //! Records `offset` as the position of directory entry `entry`.
  static auto SetDirectoryEntryOffset(
    std::vector<std::byte>& pak, size_t entry, uint64_t offset) -> void;

  //! Makes the descriptor of the first asset start where the directory does.
  static auto MoveDescriptorOntoDirectory(std::vector<std::byte>& pak) -> void;

  //! Moves the reference inventory of the first asset one byte later; the
  //! asset must hold references.
  static auto DisplaceReferenceInventory(std::vector<std::byte>& pak) -> void;

  //! Makes the second asset's reference inventory name the first asset's;
  //! both must hold references.
  static auto ShareReferenceInventory(std::vector<std::byte>& pak) -> void;

  //! Gives the empty reference inventory of the first asset a location; the
  //! asset must hold no references.
  static auto LocateEmptyReferenceInventory(std::vector<std::byte>& pak)
    -> void;

  //! Makes the empty audio region name the texture region's span; the pak
  //! must hold textures.
  static auto OverlapAudioRegionWithTextures(std::vector<std::byte>& pak)
    -> void;

  //! Makes the texture table record a row size one byte too large; the pak
  //! must hold textures.
  static auto GrowTextureEntrySize(std::vector<std::byte>& pak) -> void;

  //! Gives the first resource binding of the first asset that has one a kind
  //! no format defines.
  static auto BreakReferenceKind(std::vector<std::byte>& pak) -> void;

  //! Clears the digest of the last texture row that holds a payload.
  static auto ClearTextureDigest(std::vector<std::byte>& pak) -> void;

  //! Moves the payload of the last texture row that holds one to just past
  //! the texture region.
  static auto MoveTexturePayloadOutsideRegion(std::vector<std::byte>& pak)
    -> void;

  //! Moves the payload of the last texture row that holds one off its
  //! payload boundary by one byte; the payload must not end its region.
  static auto MisalignTexturePayload(std::vector<std::byte>& pak) -> void;

  //! Makes the first browse path hold a byte that is not UTF-8.
  static auto BreakBrowsePathUtf8(std::vector<std::byte>& pak) -> void;

  //! Turns the start of the second segment of the first browse path into a
  //! `..` segment, keeping the path's length; that segment must hold at
  //! least three characters.
  static auto BreakBrowsePathDotSegment(std::vector<std::byte>& pak) -> void;

  //! Records `compression` for the last texture row that holds a payload.
  static auto SetTextureCompression(
    std::vector<std::byte>& pak, uint8_t compression) -> void;

  //! Renames the mount of the first browse path to another of its length.
  static auto ChangeBrowseMount(std::vector<std::byte>& pak) -> void;

  //! Makes the first browse entry name the asset `key`.
  static auto SetBrowseAssetKey(
    std::vector<std::byte>& pak, const data::AssetKey& key) -> void;

  //=== Browse indexes, encoded on their own (`EncodeBrowseIndex`) ===------//

  //! Makes the first texture entry's path start at `offset` of the strings.
  static auto SetBrowseTexturePathOffset(
    std::vector<std::byte>& browse, uint32_t offset) -> void;

  //! Gives the first texture entry the path of the first asset entry.
  static auto AliasBrowseTextureToFirstEntry(std::vector<std::byte>& browse)
    -> void;

  //=== Paks, continued ===-------------------------------------------------//

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

  //! Sets a reserved byte of the index header of `files`.
  static auto SetIndexReserved(LooseCookedFiles& files) -> void;

  //! Gives the first resource binding of the first indexed asset that has
  //! one a kind no format defines.
  static auto BreakIndexReferenceKind(LooseCookedFiles& files) -> void;

  //! Makes the index record one byte more for the resource table `table`
  //! than the whole rows it holds; the root must hold that table.
  static auto GivePartialRow(
    LooseCookedFiles& files, data::loose_cooked::FileKind table) -> void;

  //! The index of `files`.
  [[nodiscard]] static auto Index(LooseCookedFiles& files)
    -> std::vector<std::byte>&;
};

} // namespace oxygen::cooker::testing
