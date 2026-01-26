//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <span>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Content/Import/IAsyncFileWriter.h>
#include <Oxygen/Content/Import/LooseCookedLayout.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Writer.h>

namespace oxygen::content::import {

struct WriteReservation {
  uint64_t reservation_start = 0;
  uint64_t aligned_offset = 0;
  uint64_t padding_size = 0;
};

struct TextureTableTraits {
  using Descriptor = data::pak::TextureResourceDesc;
  using Reservation = WriteReservation;

  [[nodiscard]] static auto TablePath(const LooseCookedLayout& layout)
    -> std::filesystem::path
  {
    return layout.TexturesTableRelPath();
  }

  [[nodiscard]] static auto DataPath(const LooseCookedLayout& layout)
    -> std::filesystem::path
  {
    return layout.TexturesDataRelPath();
  }

  [[nodiscard]] static auto SignatureForDescriptor(const Descriptor& desc)
    -> std::string
  {
    std::string signature;
    signature.reserve(96);

    signature.append("tex:");
    signature.append(";w=");
    signature.append(std::to_string(desc.width));
    signature.append("x");
    signature.append(std::to_string(desc.height));
    signature.append(";m=");
    signature.append(std::to_string(desc.mip_levels));
    signature.append(";f=");
    signature.append(std::to_string(desc.format));
    signature.append(";a=");
    signature.append(std::to_string(desc.alignment));
    signature.append(";n=");
    signature.append(std::to_string(desc.size_bytes));
    if (desc.content_hash != 0U) {
      signature.append(";h=");
      signature.append(std::to_string(desc.content_hash));
    }
    return signature;
  }
};

struct BufferTableTraits {
  using Descriptor = data::pak::BufferResourceDesc;
  using Reservation = WriteReservation;

  [[nodiscard]] static auto TablePath(const LooseCookedLayout& layout)
    -> std::filesystem::path
  {
    return layout.BuffersTableRelPath();
  }

  [[nodiscard]] static auto DataPath(const LooseCookedLayout& layout)
    -> std::filesystem::path
  {
    return layout.BuffersDataRelPath();
  }

  [[nodiscard]] static auto SignatureForDescriptor(const Descriptor& desc)
    -> std::string
  {
    std::string signature;
    signature.reserve(96);

    signature.append("buf:");
    signature.append("u=");
    signature.append(std::to_string(desc.usage_flags));
    signature.append(";s=");
    signature.append(std::to_string(desc.element_stride));
    signature.append(";f=");
    signature.append(std::to_string(desc.element_format));
    signature.append(";n=");
    signature.append(std::to_string(desc.size_bytes));
    if (desc.content_hash != 0U) {
      signature.append(";h=");
      signature.append(std::to_string(desc.content_hash));
    }
    return signature;
  }
};

template <typename Traits> class ResourceTableAggregator final {
public:
  using Descriptor = Traits::Descriptor;
  using Reservation = Traits::Reservation;

  struct AcquireResult {
    uint32_t index = 0;
    bool is_new = false;
    Reservation reservation {};
  };

  ResourceTableAggregator(IAsyncFileWriter& file_writer,
    const LooseCookedLayout& layout, const std::filesystem::path& cooked_root)
    : file_writer_(file_writer)
    , table_path_(cooked_root / Traits::TablePath(layout))
    , data_path_(cooked_root / Traits::DataPath(layout))
  {
    data_file_size_.store(
      GetExistingDataSize(data_path_), std::memory_order_release);
    LoadExistingTable();
  }

  OXYGEN_MAKE_NON_COPYABLE(ResourceTableAggregator)
  OXYGEN_MAKE_NON_MOVABLE(ResourceTableAggregator)

  template <typename Builder>
  auto AcquireOrInsert(const std::string& signature, Builder&& builder)
    -> AcquireResult
  {
    std::scoped_lock lock(mutex_);
    if (finalize_started_.load(std::memory_order_acquire)) {
      LOG_F(ERROR, "ResourceTableAggregator: AcquireOrInsert after finalize");
    }

    requests_.fetch_add(1, std::memory_order_acq_rel);

    if (const auto it = index_by_signature_.find(signature);
      it != index_by_signature_.end()) {
      return AcquireResult { .index = it->second, .is_new = false };
    }

    const uint32_t index = next_index_++;
    EnsureTableFileExists();
    auto [descriptor, reservation] = builder();
    table_.push_back(descriptor);
    index_by_signature_.emplace(signature, index);

    new_entries_this_run_.fetch_add(1, std::memory_order_acq_rel);

    return AcquireResult {
      .index = index,
      .is_new = true,
      .reservation = reservation,
    };
  }

  auto Finalize() -> co::Co<bool>
  {
    finalize_started_.store(true, std::memory_order_release);

    std::vector<Descriptor> snapshot;
    {
      std::scoped_lock lock(mutex_);
      snapshot = table_;
    }

    if (snapshot.empty()) {
      co_return true;
    }

    const auto requests = requests_.load(std::memory_order_acquire);
    const auto new_entries_this_run
      = new_entries_this_run_.load(std::memory_order_acquire);
    [[maybe_unused]] const auto deduped_total
      = (requests >= new_entries_this_run) ? (requests - new_entries_this_run)
                                           : 0;
    [[maybe_unused]] const auto unique_entries = snapshot.size();

    DLOG_F(INFO,
      "ResourceTableAggregator: finalize stats requests={} new={} deduped={} "
      "entries={}",
      requests, new_entries_this_run, deduped_total, unique_entries);

    DLOG_F(INFO, "ResourceTableAggregator: writing {} entries to '{}'",
      snapshot.size(), table_path_.string());

    serio::MemoryStream stream;
    serio::Writer writer(stream);
    const auto pack = writer.ScopedAlignment(1);
    auto write_result = writer.WriteBlob(std::as_bytes(std::span(snapshot)));
    if (!write_result.has_value()) {
      LOG_F(ERROR, "ResourceTableAggregator: serialization failed");
      co_return false;
    }

    auto result
      = co_await file_writer_.Write(table_path_, std::span(stream.Data()),
        WriteOptions { .create_directories = true, .overwrite = true });

    if (!result.has_value()) {
      LOG_F(ERROR, "ResourceTableAggregator: write failed: {}",
        result.error().ToString());
      co_return false;
    }

    DLOG_F(INFO, "ResourceTableAggregator: wrote {} bytes", result.value());
    co_return true;
  }

  auto Count() const noexcept -> uint32_t
  {
    return next_index_.load(std::memory_order_acquire);
  }

  auto TablePath() const -> const std::filesystem::path& { return table_path_; }

  auto ReserveDataRange(const uint64_t alignment, const uint64_t payload_size)
    -> WriteReservation
  {
    uint64_t current_size = data_file_size_.load(std::memory_order_acquire);
    uint64_t aligned_offset = 0;
    uint64_t new_size = 0;

    do {
      aligned_offset = AlignUp(current_size, alignment);
      new_size = aligned_offset + payload_size;
    } while (!data_file_size_.compare_exchange_weak(
      current_size, new_size, std::memory_order_acq_rel));

    return {
      .reservation_start = current_size,
      .aligned_offset = aligned_offset,
      .padding_size = aligned_offset - current_size,
    };
  }

  auto DataFileSize() const noexcept -> uint64_t
  {
    return data_file_size_.load(std::memory_order_acquire);
  }

private:
  auto EnsureTableFileExists() -> void
  {
    if (std::filesystem::exists(table_path_)) {
      return;
    }

    std::error_code ec;
    std::filesystem::create_directories(table_path_.parent_path(), ec);
    if (ec) {
      LOG_F(ERROR,
        "ResourceTableAggregator: failed to create directory '{}' ({})",
        table_path_.parent_path().string(), ec.message());
      return;
    }

    std::ofstream out(table_path_, std::ios::binary | std::ios::trunc);
    if (!out) {
      LOG_F(ERROR, "ResourceTableAggregator: failed to create table '{}'",
        table_path_.string());
    }
  }

  auto LoadExistingTable() -> void
  {
    if (!std::filesystem::exists(table_path_)) {
      return;
    }

    std::ifstream in(table_path_, std::ios::binary | std::ios::ate);
    if (!in) {
      LOG_F(WARNING,
        "ResourceTableAggregator: failed to open existing table '{}'",
        table_path_.string());
      return;
    }

    const auto size = in.tellg();
    if (size <= 0) {
      return;
    }

    const auto size_bytes = static_cast<size_t>(size);
    if (size_bytes % sizeof(Descriptor) != 0) {
      LOG_F(WARNING,
        "ResourceTableAggregator: invalid table size {} for '{}' (entry size "
        "{})",
        size_bytes, table_path_.string(), sizeof(Descriptor));
      return;
    }

    const auto count = size_bytes / sizeof(Descriptor);
    std::vector<Descriptor> loaded(count);
    in.seekg(0, std::ios::beg);
    in.read(reinterpret_cast<char*>(loaded.data()),
      static_cast<std::streamsize>(size_bytes));
    if (!in) {
      LOG_F(WARNING,
        "ResourceTableAggregator: failed to read existing table '{}'",
        table_path_.string());
      return;
    }

    std::scoped_lock lock(mutex_);
    table_ = std::move(loaded);
    index_by_signature_.clear();
    index_by_signature_.reserve(table_.size());
    for (uint32_t i = 0; i < table_.size(); ++i) {
      const auto signature = Traits::SignatureForDescriptor(table_[i]);
      if (!signature.empty()) {
        index_by_signature_.emplace(signature, i);
      }
    }

    next_index_.store(
      static_cast<uint32_t>(table_.size()), std::memory_order_release);
  }

  IAsyncFileWriter& file_writer_;
  std::filesystem::path table_path_;
  std::filesystem::path data_path_;
  mutable std::mutex mutex_;
  std::vector<Descriptor> table_;
  std::unordered_map<std::string, uint32_t> index_by_signature_;
  std::atomic<uint32_t> next_index_ { 0 };
  std::atomic<uint64_t> data_file_size_ { 0 };
  std::atomic<uint64_t> requests_ { 0 };
  std::atomic<uint64_t> new_entries_this_run_ { 0 };
  std::atomic<bool> finalize_started_ { false };

  [[nodiscard]] static constexpr auto AlignUp(
    const uint64_t value, const uint64_t alignment) noexcept -> uint64_t
  {
    if (alignment <= 1) {
      return value;
    }
    const auto remainder = value % alignment;
    return (remainder == 0) ? value : (value + (alignment - remainder));
  }

  [[nodiscard]] static auto GetExistingDataSize(
    const std::filesystem::path& data_path) -> uint64_t
  {
    std::error_code ec;
    const auto size = std::filesystem::file_size(data_path, ec);
    if (!ec) {
      return size;
    }

    return 0;
  }
};

using TextureTableAggregator = ResourceTableAggregator<TextureTableTraits>;
using BufferTableAggregator = ResourceTableAggregator<BufferTableTraits>;

} // namespace oxygen::content::import
