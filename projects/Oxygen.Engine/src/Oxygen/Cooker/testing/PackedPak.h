//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>

#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Content/ResourceTypeList.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Core/Meta/Data/ResourceIndex.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>

namespace oxygen::cooker::testing {

//! A built pak, read back the way a loader reads it.
/*!
 Opening it checks the pak's CRC. Descriptors and payloads are views of the
 pak, valid while this object lives; every accessor throws when what it reads
 is absent or out of bounds, so a test body holds no read plumbing.
*/
class PackedPak final {
public:
  //! Opens the pak at `path` of `storage`.
  PackedPak(const content::cooked::CookerStorageReader& storage,
    const std::filesystem::path& path);

  //! Opens the pak `bytes` hold.
  explicit PackedPak(std::span<const std::byte> bytes);

  //! The pak, for the queries `PakFile` answers itself.
  [[nodiscard]] auto Pak() const noexcept -> const content::PakFile&
  {
    return *pak_;
  }

  //! The descriptor bytes of the asset `key`.
  [[nodiscard]] auto Descriptor(const data::AssetKey& key) const
    -> std::span<const std::byte>;

  //! The record `T` at `offset` of the descriptor of `key`.
  template <class T>
  [[nodiscard]] auto DescriptorRecord(
    const data::AssetKey& key, const size_t offset = 0) const -> T
  {
    return RecordAt<T>(Descriptor(key), offset);
  }

  //! The table row `reference` of the asset `key` resolves to, if any.
  [[nodiscard]] auto Resolve(const data::AssetKey& key,
    data::ResourceReferenceIndex reference, data::ResourceKind kind) const
    -> std::optional<ResourceIndexT>;

  //! The resource descriptor at `index` of the table of `T`.
  template <content::PakResource T>
  [[nodiscard]] auto ResourceDesc(const ResourceIndexT index) const -> T::DescT
  {
    const auto* table = pak_->GetResourceTable<T>();
    const auto offset
      = table != nullptr ? table->GetResourceOffset(index) : std::nullopt;
    if (!offset) {
      throw std::out_of_range("the pak's table has no such resource");
    }
    return RecordAt<typename T::DescT>(Bytes(), static_cast<size_t>(*offset));
  }

  //! The payload of the resource at `index` of the table of `T`.
  template <content::PakResource T>
  [[nodiscard]] auto Payload(const ResourceIndexT index) const
    -> std::span<const std::byte>
  {
    const auto desc = ResourceDesc<T>(index);
    const auto bytes = Bytes();
    if (desc.data_offset > bytes.size()
      || desc.size_bytes > bytes.size() - desc.data_offset) {
      throw std::out_of_range("the payload lies outside the pak");
    }
    return bytes.subspan(
      static_cast<size_t>(desc.data_offset), desc.size_bytes);
  }

  //! All the pak's bytes.
  [[nodiscard]] auto Bytes() const -> std::span<const std::byte>;

  //! The record `T` at `offset` of `bytes`.
  template <class T>
  [[nodiscard]] static auto RecordAt(
    const std::span<const std::byte> bytes, const size_t offset) -> T
  {
    if (offset > bytes.size() || sizeof(T) > bytes.size() - offset) {
      throw std::out_of_range("the record lies outside its bytes");
    }
    auto record = T {};
    std::memcpy(&record, bytes.subspan(offset).data(), sizeof(T));
    return record;
  }

private:
  std::unique_ptr<content::PakFile> pak_;
};

} // namespace oxygen::cooker::testing
