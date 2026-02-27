//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Content/Internal/IContentSource.h>

namespace oxygen::content::internal {

class PakFileSource final : public IContentSource {
public:
  OXYGEN_TYPED(PakFileSource)

public:
  explicit PakFileSource(
    const std::filesystem::path& pak_path, const bool verify_content_hashes)
    : pak_(pak_path)
    , debug_name_(pak_.FilePath().string())
    , footer_(ReadFooter(pak_path))
  {
    if (verify_content_hashes) {
      pak_.ValidateCrc32Integrity();
    }
  }

  ~PakFileSource() override = default;

  OXYGEN_MAKE_NON_COPYABLE(PakFileSource)
  OXYGEN_DEFAULT_MOVABLE(PakFileSource)

  [[nodiscard]] auto DebugName() const noexcept -> std::string_view override
  {
    return debug_name_;
  }
  [[nodiscard]] auto SourcePath() const noexcept
    -> std::filesystem::path override
  {
    return pak_.FilePath();
  }

  [[nodiscard]] auto GetSourceKey() const noexcept -> data::SourceKey override
  {
    return pak_.Guid();
  }

  [[nodiscard]] auto HasAsset(const data::AssetKey& key) const noexcept
    -> bool override
  {
    return pak_.FindEntry(key).has_value();
  }

  [[nodiscard]] auto GetAssetCount() const noexcept -> size_t override
  {
    return pak_.Directory().size();
  }

  [[nodiscard]] auto GetAssetKeyByIndex(const uint32_t index) const noexcept
    -> std::optional<data::AssetKey> override
  {
    const auto directory = pak_.Directory();
    if (index >= directory.size()) {
      return std::nullopt;
    }
    return directory[index].asset_key;
  }

  [[nodiscard]] auto CreateAssetDescriptorReader(
    const data::AssetKey& key) const
    -> std::unique_ptr<serio::AnyReader> override
  {
    const auto entry = pak_.FindEntry(key);
    if (!entry.has_value()) {
      return nullptr;
    }

    return std::make_unique<OwningPakSectionReader>(
      pak_.FilePath(), static_cast<size_t>(entry->desc_offset));
  }

  [[nodiscard]] auto CreateBufferTableReader() const
    -> std::unique_ptr<serio::AnyReader> override
  {
    if (pak_.GetResourceTable<data::BufferResource>() == nullptr) {
      return nullptr;
    }
    return std::make_unique<OwningPakSectionReader>(pak_.FilePath(), 0);
  }

  [[nodiscard]] auto CreateTextureTableReader() const
    -> std::unique_ptr<serio::AnyReader> override
  {
    if (pak_.GetResourceTable<data::TextureResource>() == nullptr) {
      return nullptr;
    }
    return std::make_unique<OwningPakSectionReader>(pak_.FilePath(), 0);
  }

  [[nodiscard]] auto CreateScriptTableReader() const
    -> std::unique_ptr<serio::AnyReader> override
  {
    if (pak_.GetResourceTable<data::ScriptResource>() == nullptr) {
      return nullptr;
    }
    return std::make_unique<OwningPakSectionReader>(pak_.FilePath(), 0);
  }

  [[nodiscard]] auto CreatePhysicsTableReader() const
    -> std::unique_ptr<serio::AnyReader> override
  {
    if (pak_.GetResourceTable<data::PhysicsResource>() == nullptr) {
      return nullptr;
    }
    return std::make_unique<OwningPakSectionReader>(pak_.FilePath(), 0);
  }

  [[nodiscard]] auto GetBufferTable() const noexcept
    -> const ResourceTable<data::BufferResource>* override
  {
    return pak_.GetResourceTable<data::BufferResource>();
  }

  [[nodiscard]] auto GetTextureTable() const noexcept
    -> const ResourceTable<data::TextureResource>* override
  {
    return pak_.GetResourceTable<data::TextureResource>();
  }

  [[nodiscard]] auto GetScriptTable() const noexcept
    -> const ResourceTable<data::ScriptResource>* override
  {
    return pak_.GetResourceTable<data::ScriptResource>();
  }

  [[nodiscard]] auto GetPhysicsTable() const noexcept
    -> const ResourceTable<data::PhysicsResource>* override
  {
    return pak_.GetResourceTable<data::PhysicsResource>();
  }

  [[nodiscard]] auto CreateBufferDataReader() const
    -> std::unique_ptr<serio::AnyReader> override
  {
    if (!footer_ || pak_.GetResourceTable<data::BufferResource>() == nullptr) {
      return nullptr;
    }
    return std::make_unique<OwningPakSectionReader>(
      pak_.FilePath(), static_cast<size_t>(footer_->buffer_region.offset));
  }

  [[nodiscard]] auto CreateTextureDataReader() const
    -> std::unique_ptr<serio::AnyReader> override
  {
    if (!footer_ || pak_.GetResourceTable<data::TextureResource>() == nullptr) {
      return nullptr;
    }
    return std::make_unique<OwningPakSectionReader>(
      pak_.FilePath(), static_cast<size_t>(footer_->texture_region.offset));
  }

  [[nodiscard]] auto CreateScriptDataReader() const
    -> std::unique_ptr<serio::AnyReader> override
  {
    if (!footer_ || pak_.GetResourceTable<data::ScriptResource>() == nullptr) {
      return nullptr;
    }
    return std::make_unique<OwningPakSectionReader>(
      pak_.FilePath(), static_cast<size_t>(footer_->script_region.offset));
  }

  [[nodiscard]] auto CreatePhysicsDataReader() const
    -> std::unique_ptr<serio::AnyReader> override
  {
    if (!footer_ || pak_.GetResourceTable<data::PhysicsResource>() == nullptr) {
      return nullptr;
    }
    return std::make_unique<OwningPakSectionReader>(
      pak_.FilePath(), static_cast<size_t>(footer_->physics_region.offset));
  }

  [[nodiscard]] auto ScriptSlotCount() const noexcept -> uint32_t override
  {
    return pak_.ScriptSlotCount();
  }

  [[nodiscard]] auto ReadScriptSlotRecords(
    uint32_t start_index, uint32_t count) const
    -> std::vector<data::pak::scripting::ScriptSlotRecord> override
  {
    return pak_.ReadScriptSlotRecords(start_index, count);
  }

  [[nodiscard]] auto ReadScriptParamRecords(
    data::pak::core::OffsetT absolute_offset, uint32_t count) const
    -> std::vector<data::pak::scripting::ScriptParamRecord> override
  {
    return pak_.ReadScriptParamRecords(PakFile::ScriptParamReadRequest {
      .absolute_offset = absolute_offset, .count = count });
  }

  [[nodiscard]] auto ResolveVirtualPath(
    const data::AssetKey& key) const noexcept
    -> std::optional<std::string> override
  {
    if (!pak_.HasBrowseIndex()) {
      return std::nullopt;
    }
    for (const auto& entry : pak_.BrowseIndex()) {
      if (entry.asset_key == key) {
        return entry.virtual_path;
      }
    }
    return std::nullopt;
  }

  [[nodiscard]] auto Pak() const noexcept -> const PakFile& { return pak_; }

private:
  class OwningPakSectionReader final : public serio::AnyReader {
  public:
    OwningPakSectionReader(const std::filesystem::path& path, size_t offset)
      : stream_(path, std::ios::in)
      , reader_(stream_)
    {
      (void)reader_.Seek(offset);
    }

    ~OwningPakSectionReader() override = default;

    OXYGEN_MAKE_NON_COPYABLE(OwningPakSectionReader)
    OXYGEN_MAKE_NON_MOVABLE(OwningPakSectionReader)

    [[nodiscard]] auto ReadBlob(size_t size) noexcept
      -> oxygen::Result<std::vector<std::byte>> override
    {
      return reader_.ReadBlob(size);
    }

    [[nodiscard]] auto ReadBlobInto(std::span<std::byte> buffer) noexcept
      -> oxygen::Result<void> override
    {
      return reader_.ReadBlobInto(buffer);
    }

    [[nodiscard]] auto Position() noexcept -> oxygen::Result<size_t> override
    {
      return reader_.Position();
    }

    [[nodiscard]] auto AlignTo(size_t alignment) noexcept
      -> oxygen::Result<void> override
    {
      return reader_.AlignTo(alignment);
    }

    [[nodiscard]] auto ScopedAlignment(uint16_t alignment) noexcept(false)
      -> serio::AlignmentGuard override
    {
      return reader_.ScopedAlignment(alignment);
    }

    [[nodiscard]] auto Forward(size_t num_bytes) noexcept
      -> oxygen::Result<void> override
    {
      return reader_.Forward(num_bytes);
    }

    [[nodiscard]] auto Seek(size_t pos) noexcept
      -> oxygen::Result<void> override
    {
      return reader_.Seek(pos);
    }

  private:
    serio::FileStream<> stream_;
    PakFile::Reader reader_;
  };

  [[nodiscard]] static auto ReadFooter(const std::filesystem::path& pak_path)
    -> std::optional<data::pak::core::PakFooter>
  {
    std::error_code ec;
    const auto file_size = std::filesystem::file_size(pak_path, ec);
    if (ec || file_size < sizeof(data::pak::core::PakFooter)) {
      return std::nullopt;
    }

    serio::FileStream<> stream(pak_path, std::ios::in);
    if (!stream.Seek(static_cast<size_t>(
          file_size - sizeof(data::pak::core::PakFooter)))) {
      return std::nullopt;
    }

    data::pak::core::PakFooter footer {};
    const auto footer_bytes = std::as_writable_bytes(std::span { &footer, 1 });
    auto read_result = stream.Read(footer_bytes.data(), footer_bytes.size());
    if (!read_result) {
      return std::nullopt;
    }

    // Basic magic check.
    if (!std::ranges::equal(std::span { footer.footer_magic },
          data::pak::core::kPakFooterMagic)) {
      return std::nullopt;
    }

    return footer;
  }

  PakFile pak_;
  std::string debug_name_;
  std::optional<data::pak::core::PakFooter> footer_;
};

} // namespace oxygen::content::internal
