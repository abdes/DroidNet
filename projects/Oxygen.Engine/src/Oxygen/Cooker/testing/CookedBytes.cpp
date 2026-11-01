//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <Oxygen/Cooker/Import/Internal/LooseCookedLayout.h>
#include <Oxygen/Cooker/testing/CookedBytes.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Serio/ByteReader.h>

namespace oxygen::cooker::testing {

namespace {

  namespace core = data::pak::core;
  namespace lc = data::loose_cooked;

  //! Reads the record `T` at `offset` of `bytes`, lets `edit` change it and
  //! writes it back.
  template <class T, class Edit>
  auto EditRecord(std::vector<std::byte>& bytes, const size_t offset,
    const Edit& edit) -> void
  {
    const auto read = serio::CopyAt<T>(bytes, serio::ByteOffset { offset });
    if (!read) {
      throw std::invalid_argument("the artifact is too small for its record");
    }
    auto record = *read;
    edit(record);
    std::ranges::copy(std::bit_cast<std::array<std::byte, sizeof(T)>>(record),
      std::span { bytes }.subspan(offset).begin());
  }

  template <class Edit>
  auto EditHeader(std::vector<std::byte>& pak, const Edit& edit) -> void
  {
    EditRecord<core::PakHeader>(pak, 0U, edit);
  }

  auto FooterOffset(const std::vector<std::byte>& pak) -> size_t
  {
    if (pak.size() < sizeof(core::PakHeader) + sizeof(core::PakFooter)) {
      throw std::invalid_argument("the bytes are too small to be a pak");
    }
    return pak.size() - sizeof(core::PakFooter);
  }

  template <class Edit>
  auto EditFooter(std::vector<std::byte>& pak, const Edit& edit) -> void
  {
    EditRecord<core::PakFooter>(pak, FooterOffset(pak), edit);
  }

} // namespace

auto CookedBytes::SetPakVersion(
  std::vector<std::byte>& pak, const uint16_t version) -> void
{
  EditHeader(pak,
    [version](core::PakHeader& header) -> void { header.version = version; });
}

auto CookedBytes::BreakPakMagic(std::vector<std::byte>& pak) -> void
{
  EditHeader(
    pak, [](core::PakHeader& header) -> void { header.magic[0] = 'X'; });
}

auto CookedBytes::BreakFooterMagic(std::vector<std::byte>& pak) -> void
{
  EditFooter(
    pak, [](core::PakFooter& footer) -> void { footer.footer_magic[0] = 'X'; });
}

auto CookedBytes::SetHeaderIdentity(
  std::vector<std::byte>& pak, const data::SourceKey& key) -> void
{
  EditHeader(pak, [&key](core::PakHeader& header) -> void {
    const auto bytes = data::as_bytes(key);
    std::memcpy(header.source_identity.data(), bytes.data(), bytes.size());
  });
}

auto CookedBytes::DropBrowseIndex(std::vector<std::byte>& pak) -> void
{
  EditFooter(pak, [](core::PakFooter& footer) -> void {
    footer.browse_index_offset = 0U;
    footer.browse_index_size = 0U;
  });
}

auto CookedBytes::OverrunCatalog(std::vector<std::byte>& pak) -> void
{
  const auto footer_offset = FooterOffset(pak);
  EditFooter(pak, [footer_offset](core::PakFooter& footer) -> void {
    footer.catalog_size = footer_offset - footer.catalog_offset + 1U;
  });
}

auto CookedBytes::DropCatalog(std::vector<std::byte>& pak) -> void
{
  EditFooter(
    pak, [](core::PakFooter& footer) -> void { footer.catalog_size = 0U; });
}

auto CookedBytes::MoveDirectoryOutside(std::vector<std::byte>& pak) -> void
{
  const auto size = pak.size();
  EditFooter(pak, [size](core::PakFooter& footer) -> void {
    footer.directory_offset = size + 1U;
  });
}

auto CookedBytes::MisalignTextureRegion(std::vector<std::byte>& pak) -> void
{
  EditFooter(pak, [](core::PakFooter& footer) -> void {
    if (footer.texture_region.size == 0U) {
      throw std::invalid_argument("the pak holds no texture payloads");
    }
    ++footer.texture_region.offset;
  });
}

auto CookedBytes::SetCrc(std::vector<std::byte>& pak, const uint32_t crc)
  -> void
{
  EditFooter(
    pak, [crc](core::PakFooter& footer) -> void { footer.pak_crc32 = crc; });
}

auto CookedBytes::CorruptCrc(std::vector<std::byte>& pak) -> void
{
  EditFooter(pak, [](core::PakFooter& footer) -> void {
    footer.pak_crc32 = ~footer.pak_crc32;
  });
}

auto CookedBytes::CrcFieldOffset(const std::vector<std::byte>& pak) -> size_t
{
  return FooterOffset(pak) + offsetof(core::PakFooter, pak_crc32);
}

auto CookedBytes::SetIndexVersion(
  LooseCookedFiles& files, const uint16_t version) -> void
{
  EditRecord<lc::IndexHeader>(Index(files), 0U,
    [version](lc::IndexHeader& header) -> void { header.version = version; });
}

auto CookedBytes::BreakIndexMagic(LooseCookedFiles& files) -> void
{
  EditRecord<lc::IndexHeader>(Index(files), 0U,
    [](lc::IndexHeader& header) -> void { header.magic.front() = 'X'; });
}

auto CookedBytes::Index(LooseCookedFiles& files) -> std::vector<std::byte>&
{
  const auto it = files.find(
    std::string(content::import::internal::loose_layout::kIndexFileName));
  if (it == files.end()) {
    throw std::invalid_argument("the loose cooked root has no index");
  }
  return it->second;
}

} // namespace oxygen::cooker::testing
