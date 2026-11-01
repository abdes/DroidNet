//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Oxygen/Base/Hex.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedLayout.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedWriter.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Serio/AnyDevice.h>
#include <Oxygen/Serio/IoDriver.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Writer.h>

namespace oxygen::content::import {

//! A cooked root's resource table that an import cannot read or create, so
//! the import must not write the table over it.
class ResourceTableError final : public std::runtime_error {
public:
  ResourceTableError(
    const std::filesystem::path& table, const std::string& problem)
    : std::runtime_error("Resource table '" + table.string() + "' " + problem)
  {
  }
};

struct WriteReservation {
  uint64_t reservation_start = 0;
  uint64_t aligned_offset = 0;
  uint64_t padding_size = 0;
};

struct WriteRangeRequest {
  uint64_t alignment = 0;
  uint64_t payload_size = 0;
};

struct TextureTableTraits {
  using Descriptor = data::pak::core::TextureResourceDesc;
  using Reservation = WriteReservation;

  //! Textures use index 0 for the engine fallback texture (1x1 white
  //! texel), inserted by packing tooling.  No generic sentinel needed.
  static constexpr bool kReserveSentinelAtZero = false;

  static constexpr auto kTableRelPath = internal::loose_layout::kTexturesTable;
  static constexpr auto kDataRelPath = internal::loose_layout::kTexturesData;

  [[nodiscard]] static auto SignatureForDescriptor(const Descriptor& desc)
    -> std::string
  {
    std::string signature;
    signature.reserve(160);

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
    signature.append(";h=");
    signature.append(base::ToHexString(desc.content_hash));
    return signature;
  }
};

struct BufferTableTraits {
  using Descriptor = data::pak::core::BufferResourceDesc;
  using Reservation = WriteReservation;

  //! Buffer tables reserve index 0 as a zeroed sentinel meaning
  //! "absent / not assigned" (kNoResourceIndex).  Real buffers
  //! start at index 1.
  static constexpr bool kReserveSentinelAtZero = true;

  static constexpr auto kTableRelPath = internal::loose_layout::kBuffersTable;
  static constexpr auto kDataRelPath = internal::loose_layout::kBuffersData;

  [[nodiscard]] static auto SignatureForDescriptor(const Descriptor& desc)
    -> std::string
  {
    std::string signature;
    signature.reserve(160);

    signature.append("buf:");
    signature.append("u=");
    signature.append(std::to_string(desc.usage_flags));
    signature.append(";s=");
    signature.append(std::to_string(desc.element_stride));
    signature.append(";f=");
    signature.append(std::to_string(desc.element_format));
    signature.append(";n=");
    signature.append(std::to_string(desc.size_bytes));
    signature.append(";h=");
    signature.append(base::ToHexString(desc.content_hash));
    return signature;
  }
};

struct PhysicsTableTraits {
  using Descriptor = data::pak::physics::PhysicsResourceDesc;
  using Reservation = WriteReservation;

  //! Physics tables reserve index 0 for "absent / not assigned"
  //! (kNoResourceIndex). Real resources start at index 1.
  static constexpr bool kReserveSentinelAtZero = true;

  static constexpr auto kTableRelPath = internal::loose_layout::kPhysicsTable;
  static constexpr auto kDataRelPath = internal::loose_layout::kPhysicsData;

  [[nodiscard]] static auto SignatureForDescriptor(const Descriptor& desc)
    -> std::string
  {
    std::string signature;
    signature.reserve(160);

    signature.append("phys:");
    signature.append("f=");
    signature.append(std::to_string(static_cast<uint32_t>(desc.format)));
    signature.append(";n=");
    signature.append(std::to_string(desc.size_bytes));
    signature.append(";h=");
    signature.append(base::ToHexString(desc.content_hash));
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

  //! Loads the table of `cooked_root` held by `storage`. The table is
  //! published and the data file is opened through `storage`, awaited on
  //! `driver`, which outlives the aggregator.
  ResourceTableAggregator(serio::IoDriver& driver,
    cooked::ImportStorage& storage, const std::filesystem::path& cooked_root)
    : driver_(make_observer(&driver))
    , storage_(make_observer(&storage))
    , table_path_(cooked_root / Traits::kTableRelPath)
    , data_path_(cooked_root / Traits::kDataRelPath)
  {
    data_file_size_.store(
      storage_->SizeOf(data_path_).value_or(serio::ByteCount { 0 }).get(),
      std::memory_order_release);
    LoadExistingTable();
    EnsureSentinelAtZero();
  }

  ~ResourceTableAggregator() = default;

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

    EnsureTableFileExists();
    const uint32_t index = next_index_++;
    auto [descriptor, reservation] = std::forward<Builder>(builder)();
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

    const auto published = co_await storage_->PublishAsync(
      *driver_, table_path_, std::span<const std::byte>(stream.Data()));
    if (!published) {
      LOG_F(ERROR, "ResourceTableAggregator: failed to write '{}': {}",
        table_path_.string(), published.error().message());
      co_return false;
    }

    DLOG_F(
      INFO, "ResourceTableAggregator: wrote {} bytes", stream.Data().size());
    written_ = LooseCookedFileDigest {
      .size = stream.Data().size(),
      .sha256 = base::ComputeSha256(stream.Data()),
    };
    co_return true;
  }

  //! The size and digest of the table `Finalize` wrote; nothing before it
  //! writes one, or when it fails.
  [[nodiscard]] auto Written() const noexcept
    -> const std::optional<LooseCookedFileDigest>&
  {
    return written_;
  }

  auto Count() const noexcept -> uint32_t
  {
    return next_index_.load(std::memory_order_acquire);
  }

  auto TablePath() const -> const std::filesystem::path& { return table_path_; }

  auto ReserveDataRange(const WriteRangeRequest request) -> WriteReservation
  {
    uint64_t current_size = data_file_size_.load(std::memory_order_acquire);
    uint64_t aligned_offset = 0;
    uint64_t new_size = 0;

    while (true) {
      aligned_offset = AlignUp(current_size, request.alignment);
      new_size = aligned_offset + request.payload_size;
      if (data_file_size_.compare_exchange_weak(
            current_size, new_size, std::memory_order_acq_rel)) {
        break;
      }
    }

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

  //! The data file, opened on first use and held until the aggregator ends.
  /*!
   Emitters await `AsyncWriteAt` on it at the ranges `ReserveDataRange`
   reserved. The file is created when missing and never truncated, and it is
   open for writes while the aggregator lives.

   @return The device, or the error opening it; the pointer stays valid until
           the aggregator is destroyed.
  */
  [[nodiscard]] auto DataFile() -> Result<serio::AnyAsyncDevice*>
  {
    std::scoped_lock lock(data_file_mutex_);
    if (!data_file_) {
      auto opened = storage_->OpenData(*driver_, data_path_);
      if (!opened) {
        return ::oxygen::Err(opened.error());
      }
      data_file_.emplace(std::move(opened).value());
    }
    return ::oxygen::Ok(&*data_file_);
  }

  auto DataPath() const -> const std::filesystem::path& { return data_path_; }

  [[nodiscard]] auto TryGetDescriptor(uint32_t index) const
    -> std::optional<Descriptor>
  {
    std::scoped_lock lock(mutex_);
    if (index >= table_.size()) {
      return std::nullopt;
    }
    return table_.at(index);
  }

private:
  //! Reserve index 0 as an all-zero sentinel descriptor when the
  //! traits opt in via `kReserveSentinelAtZero`.
  //!
  //! For resource types without a meaningful fallback (e.g. buffers),
  //! index 0 means "absent / not assigned" (kNoResourceIndex).  The
  //! sentinel must occupy slot 0 in the table so that valid resources
  //! start at index 1.  Resource types that give index 0 a dedicated
  //! meaning (e.g. textures → fallback texture) must set the flag to
  //! `false` and manage index 0 externally.
  auto EnsureSentinelAtZero() -> void
  {
    if constexpr (Traits::kReserveSentinelAtZero) {
      std::scoped_lock lock(mutex_);
      if (!table_.empty()) {
        return; // Already loaded from disk.
      }
      table_.push_back(Descriptor {}); // all-zero sentinel
      next_index_.store(1, std::memory_order_release);
      DLOG_F(INFO,
        "ResourceTableAggregator: reserved index-0 sentinel for '{}'",
        table_path_.string());
    }
  }

  auto EnsureTableFileExists() -> void
  {
    if (storage_->KindOf(table_path_) != cooked::PathKind::kNone) {
      return;
    }

    if (const auto created
      = cooked::PublishWholeFile(*storage_, table_path_, {});
      !created) {
      throw ResourceTableError(
        table_path_, "cannot be created: " + created.error().message());
    }
  }

  //! Loads the table the cooked root holds. Only a missing table starts
  //! empty: starting over a table that cannot be read would publish a table
  //! that drops every resource the root already holds.
  auto LoadExistingTable() -> void
  {
    const auto kind = storage_->KindOf(table_path_);
    if (kind == cooked::PathKind::kNone) {
      return;
    }
    if (kind != cooked::PathKind::kFile) {
      throw ResourceTableError(table_path_, "is not a file");
    }

    const auto bytes = cooked::ReadWholeFile(*storage_, table_path_);
    if (!bytes) {
      throw ResourceTableError(
        table_path_, "cannot be read: " + bytes.error().message());
    }
    if (bytes->empty()) {
      return;
    }

    const auto size_bytes = bytes->size();
    if (size_bytes % sizeof(Descriptor) != 0) {
      throw ResourceTableError(table_path_,
        "holds " + std::to_string(size_bytes) + " bytes, not a whole number of "
          + std::to_string(sizeof(Descriptor)) + "-byte rows");
    }

    const auto count = size_bytes / sizeof(Descriptor);
    std::vector<Descriptor> loaded(count);
    std::ranges::copy(
      *bytes, std::as_writable_bytes(std::span(loaded)).begin());

    std::scoped_lock lock(mutex_);
    table_ = std::move(loaded);
    index_by_signature_.clear();
    index_by_signature_.reserve(table_.size());
    for (uint32_t i = 0; i < table_.size(); ++i) {
      const auto signature = Traits::SignatureForDescriptor(table_.at(i));
      if (!signature.empty()) {
        index_by_signature_.emplace(signature, i);
      }
    }

    next_index_.store(
      static_cast<uint32_t>(table_.size()), std::memory_order_release);
  }

  observer_ptr<serio::IoDriver> driver_;
  observer_ptr<cooked::ImportStorage> storage_;
  std::filesystem::path table_path_;
  std::filesystem::path data_path_;
  mutable std::mutex mutex_;
  std::mutex data_file_mutex_;
  std::optional<serio::AnyAsyncDevice> data_file_;
  std::vector<Descriptor> table_;
  std::unordered_map<std::string, uint32_t> index_by_signature_;
  std::atomic<uint32_t> next_index_ { 0 };
  std::atomic<uint64_t> data_file_size_ { 0 };
  std::atomic<uint64_t> requests_ { 0 };
  std::atomic<uint64_t> new_entries_this_run_ { 0 };
  std::atomic<bool> finalize_started_ { false };
  std::optional<LooseCookedFileDigest> written_;

  [[nodiscard]] static constexpr auto AlignUp(
    const uint64_t value, const uint64_t alignment) noexcept -> uint64_t
  {
    if (alignment <= 1) {
      return value;
    }
    const auto remainder = value % alignment;
    return (remainder == 0) ? value : (value + (alignment - remainder));
  }
};

using TextureTableAggregator = ResourceTableAggregator<TextureTableTraits>;
using BufferTableAggregator = ResourceTableAggregator<BufferTableTraits>;
using PhysicsTableAggregator = ResourceTableAggregator<PhysicsTableTraits>;

} // namespace oxygen::content::import
