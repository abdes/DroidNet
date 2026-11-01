//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/LooseCookedIndexEncoder.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/LooseCookedPaths.h>
#include <Oxygen/Data/LooseCookedResources.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Serio/Writer.h>

namespace oxygen::data::loose_cooked {

namespace {

  using AssetRef = std::reference_wrapper<const IndexedAsset>;
  using FileRef = std::reference_wrapper<const IndexedFile>;

  //! Records keyed by path rather than by kind: a root holds many of them.
  [[nodiscard]] auto IsPathKeyed(const FileKind kind) noexcept -> bool
  {
    return kind == FileKind::kAuxiliary || kind == FileKind::kTextureDescriptor;
  }

  //! The string table, laid out before it is written: each distinct string
  //! once, in the order it is first added, after the leading NUL that the
  //! empty string names. It holds views of the content's strings.
  class StringTable final {
  public:
    [[nodiscard]] auto Add(const std::string_view value) -> uint32_t
    {
      if (value.empty()) {
        return 0;
      }
      if (const auto found = offsets_.find(value); found != offsets_.end()) {
        return found->second;
      }
      if (size_ > std::numeric_limits<uint32_t>::max()) {
        throw std::invalid_argument(
          "Loose cooked index string table exceeds 4 GiB");
      }
      const auto offset = static_cast<uint32_t>(size_);
      offsets_.emplace(value, offset);
      strings_.push_back(value);
      size_ += value.size() + 1;
      return offset;
    }

    [[nodiscard]] auto Size() const noexcept -> uint64_t { return size_; }

    [[nodiscard]] auto Write(serio::AnyWriter& writer) const -> Result<void>
    {
      constexpr auto kNul = std::array { std::byte { 0 } };
      CHECK_RESULT(writer.WriteBlob(kNul));
      for (const auto value : strings_) {
        CHECK_RESULT(writer.WriteBlob(std::as_bytes(std::span(value))));
        CHECK_RESULT(writer.WriteBlob(kNul));
      }
      return {};
    }

  private:
    std::unordered_map<std::string_view, uint32_t> offsets_;
    std::vector<std::string_view> strings_;
    uint64_t size_ = 1;
  };

  auto RequireUnique(std::unordered_set<std::string_view>& seen,
    const std::string_view value, const std::string_view what) -> void
  {
    if (!seen.insert(value).second) {
      throw std::invalid_argument("Loose cooked index lists the "
        + std::string(what) + " '" + std::string(value) + "' twice");
    }
  }

  auto RequireDigest(
    const base::Sha256Digest& digest, const std::string_view relpath) -> void
  {
    if (base::IsAllZero(digest)) {
      throw std::invalid_argument(
        "Loose cooked index has no SHA-256 for '" + std::string(relpath) + "'");
    }
  }

  //! Refuses a digest of a resource data file, whose resources are verified
  //! one by one from their table rows.
  auto RequireNoDigest(
    const base::Sha256Digest& digest, const std::string_view relpath) -> void
  {
    if (!base::IsAllZero(digest)) {
      throw std::invalid_argument("Loose cooked index has a SHA-256 for the "
                                  "resource data file '"
        + std::string(relpath) + "'");
    }
  }

  //! Refuses a path the index may not list (`RequireIndexRelPath`).
  auto RequireRelPath(const std::string_view relpath) -> void
  {
    try {
      RequireIndexRelPath(relpath);
    } catch (const std::runtime_error& error) {
      throw std::invalid_argument("Loose cooked index path '"
        + std::string(relpath) + "': " + error.what());
    }
  }

  //! A name an index record must give.
  enum class RecordName : uint8_t {
    kVirtualPath,
    kProducingSource,
  };

  //! Refuses a record whose `name` is empty.
  auto RequireNamed(const std::string_view value, const RecordName name,
    const std::string_view relpath) -> void
  {
    if (value.empty()) {
      throw std::invalid_argument("Loose cooked index record '"
        + std::string(relpath) + "' has no "
        + (name == RecordName::kVirtualPath ? "virtual path"
                                            : "producing source"));
    }
  }

  //! Refuses a file record that names what only another kind of record may:
  //! only a texture descriptor has a virtual path, and only it and an
  //! auxiliary file a producing source.
  auto RequireFileNames(const IndexedFile& file) -> void
  {
    if (file.kind == FileKind::kTextureDescriptor) {
      RequireNamed(file.virtual_path, RecordName::kVirtualPath, file.relpath);
      RequireNamed(
        file.source_path, RecordName::kProducingSource, file.relpath);
      return;
    }
    if (!file.virtual_path.empty()) {
      throw std::invalid_argument("Loose cooked index file '" + file.relpath
        + "' has a virtual path, which only a texture descriptor may have");
    }
    if (!file.source_path.empty() && file.kind != FileKind::kAuxiliary) {
      throw std::invalid_argument("Loose cooked index file '" + file.relpath
        + "' has a producing source, which only a texture descriptor or an "
          "auxiliary file may have");
    }
  }

  //! Refuses content the loader would refuse for its identities, names and
  //! digests, so a producer, or a test, fails where it builds the content.
  auto ValidateContent(const std::span<const AssetRef> assets,
    const std::span<const FileRef> files) -> void
  {
    auto virtual_paths = std::unordered_set<std::string_view> {};
    auto content_paths = std::unordered_set<std::string_view> {};
    for (size_t i = 0; i < assets.size(); ++i) {
      const auto& asset = assets[i].get();
      if (i != 0 && assets[i - 1].get().key == asset.key) {
        throw std::invalid_argument("Loose cooked index lists the asset key "
          + to_string(asset.key) + " twice");
      }
      RequireRelPath(asset.descriptor_relpath);
      RequireNamed(
        asset.virtual_path, RecordName::kVirtualPath, asset.descriptor_relpath);
      RequireNamed(asset.source_path, RecordName::kProducingSource,
        asset.descriptor_relpath);
      RequireUnique(virtual_paths, asset.virtual_path, "virtual path");
      RequireUnique(content_paths, asset.descriptor_relpath, "content path");
      RequireDigest(asset.descriptor_sha256, asset.descriptor_relpath);
    }
    auto kinds = std::unordered_set<FileKind> {};
    for (const auto& ref : files) {
      const auto& file = ref.get();
      if (file.kind == FileKind::kUnknown) {
        throw std::invalid_argument(
          "Loose cooked index file '" + file.relpath + "' has no kind");
      }
      if (!IsPathKeyed(file.kind) && !kinds.insert(file.kind).second) {
        throw std::invalid_argument("Loose cooked index lists file kind "
          + std::to_string(static_cast<uint16_t>(file.kind)) + " twice");
      }
      RequireRelPath(file.relpath);
      RequireFileNames(file);
      if (!file.virtual_path.empty()) {
        RequireUnique(virtual_paths, file.virtual_path, "virtual path");
      }
      RequireUnique(content_paths, file.relpath, "content path");
      if (IsResourceDataFile(file.kind)) {
        RequireNoDigest(file.sha256, file.relpath);
      } else {
        RequireDigest(file.sha256, file.relpath);
      }
    }
  }

  [[nodiscard]] auto CountOf(const size_t count, const std::string_view what)
    -> uint32_t
  {
    if (count > std::numeric_limits<uint32_t>::max()) {
      throw std::invalid_argument(
        "Loose cooked index holds too many " + std::string(what));
    }
    return static_cast<uint32_t>(count);
  }

} // namespace

auto EncodeLooseCookedIndex(
  const IndexContent& content, serio::AnyWriter& writer) -> Result<void>
{
  auto assets
    = std::vector<AssetRef>(content.assets.begin(), content.assets.end());
  std::ranges::sort(assets, {},
    [](const AssetRef& asset) -> const AssetKey& { return asset.get().key; });
  auto files = std::vector<FileRef>(content.files.begin(), content.files.end());
  std::ranges::sort(files, {}, [](const FileRef& file) -> const std::string& {
    return file.get().relpath;
  });
  ValidateContent(assets, files);

  // Lay out every section first: the header names their offsets.
  auto strings = StringTable {};
  auto entries = std::vector<AssetEntry> {};
  entries.reserve(assets.size());
  for (const auto& ref : assets) {
    const auto& asset = ref.get();
    auto entry = AssetEntry {
      .asset_key = asset.key,
      .asset_type = static_cast<uint8_t>(asset.asset_type),
      .descriptor_relpath_offset = strings.Add(asset.descriptor_relpath),
      .virtual_path_offset = strings.Add(asset.virtual_path),
      .source_path_offset = strings.Add(asset.source_path),
      .descriptor_size = asset.descriptor_size,
    };
    std::ranges::copy(
      asset.descriptor_sha256, std::begin(entry.descriptor_sha256));
    entries.push_back(entry);
  }
  auto records = std::vector<FileRecord> {};
  records.reserve(files.size());
  for (const auto& ref : files) {
    const auto& file = ref.get();
    records.push_back(FileRecord {
      .kind = file.kind,
      .size = file.size,
      .relpath_offset = strings.Add(file.relpath),
      .virtual_path_offset = strings.Add(file.virtual_path),
      .source_path_offset = strings.Add(file.source_path),
      .sha256 = file.sha256,
    });
  }

  auto header = IndexHeader {
    .content_version = content.content_version,
    .flags
    = records.empty() ? kHasVirtualPaths : (kHasVirtualPaths | kHasFileRecords),
    .string_table_offset = sizeof(IndexHeader),
    .string_table_size = strings.Size(),
    .asset_count = CountOf(entries.size(), "assets"),
    .asset_entry_size = sizeof(AssetEntry),
    .file_record_count = CountOf(records.size(), "files"),
    .file_record_size = sizeof(FileRecord),
  };
  std::ranges::transform(data::as_bytes(content.source_key),
    std::begin(header.source_identity), [](const auto value) -> uint8_t {
      return std::to_integer<uint8_t>(value);
    });
  header.asset_entries_offset
    = header.string_table_offset + header.string_table_size;
  header.file_records_offset = header.asset_entries_offset
    + (uint64_t { header.asset_count } * sizeof(AssetEntry));

  // Reference blocks follow the file records, in asset order, without gaps.
  auto reference_cursor = header.file_records_offset
    + (uint64_t { header.file_record_count } * sizeof(FileRecord));
  for (size_t i = 0; i < entries.size(); ++i) {
    const auto& references = assets[i].get().references;
    auto& table = entries[i].references;
    table.resource_count
      = CountOf(references.Resources().size(), "asset resource references");
    table.key_count = CountOf(references.Keys().size(), "asset key references");
    if (const auto size
      = AssetReferences::EncodedSize(table.resource_count, table.key_count);
      size != 0U) {
      table.offset = reference_cursor;
      reference_cursor += size;
    }
  }

  CHECK_RESULT(writer.Write(header));
  CHECK_RESULT(strings.Write(writer));
  for (const auto& entry : entries) {
    CHECK_RESULT(writer.Write(entry));
  }
  for (const auto& record : records) {
    CHECK_RESULT(writer.Write(record));
  }
  for (const auto& ref : assets) {
    const auto encoded = ref.get().references.Encode();
    if (!encoded) {
      throw std::invalid_argument(encoded.error());
    }
    CHECK_RESULT(writer.WriteBlob(*encoded));
  }
  return writer.Flush();
}

} // namespace oxygen::data::loose_cooked
