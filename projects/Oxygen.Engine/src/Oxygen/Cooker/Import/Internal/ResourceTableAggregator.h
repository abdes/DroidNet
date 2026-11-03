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
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/LooseCookedPaths.h>
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
  //! texel), which the texture emitter inserts first. No sentinel needed.
  static constexpr bool kReserveSentinelAtZero = false;

  static constexpr auto kTableKind
    = data::loose_cooked::FileKind::kTexturesTable;
  static constexpr auto kTableRelPath = internal::loose_layout::kTexturesTable;
  static constexpr auto kDataRelPath = internal::loose_layout::kTexturesData;

  //! The deduplication key: every field of the row but its `data_offset`.
  [[nodiscard]] static auto SignatureForDescriptor(const Descriptor& desc)
    -> std::string
  {
    std::string signature;
    signature.reserve(160);

    signature.append("tex:");
    signature.append(";t=");
    signature.append(std::to_string(desc.texture_type));
    signature.append(";c=");
    signature.append(std::to_string(desc.compression_type));
    signature.append(";w=");
    signature.append(std::to_string(desc.width));
    signature.append("x");
    signature.append(std::to_string(desc.height));
    signature.append("x");
    signature.append(std::to_string(desc.depth));
    signature.append(";l=");
    signature.append(std::to_string(desc.array_layers));
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

  static constexpr auto kTableKind
    = data::loose_cooked::FileKind::kBuffersTable;
  static constexpr auto kTableRelPath = internal::loose_layout::kBuffersTable;
  static constexpr auto kDataRelPath = internal::loose_layout::kBuffersData;

  //! The deduplication key: every field of the row but its `data_offset`.
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

  static constexpr auto kTableKind
    = data::loose_cooked::FileKind::kPhysicsTable;
  static constexpr auto kTableRelPath = internal::loose_layout::kPhysicsTable;
  static constexpr auto kDataRelPath = internal::loose_layout::kPhysicsData;

  //! The deduplication key: every field of the row but its `data_offset` and
  //! the `resource_asset_key` derived from this key.
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

//! What the latest commit of a cooked root records of one resource kind.
struct CommittedResource final {
  //! The path of its table, which is content-named; none before its first.
  std::optional<std::string> table_relpath;
  //! The length of its data file the commit records: its next bytes go after.
  uint64_t data_length = 0;
};

//! Reads what the latest commit of `cooked_root` records of the table kind
//! `table_kind` and its data file; nothing for a root without an index.
/*!
 @throw ResourceTableError when the index cannot be read.
*/
OXGN_COOK_NDAPI auto ReadCommittedResource(
  const cooked::CookerStorageReader& storage,
  const std::filesystem::path& cooked_root,
  data::loose_cooked::FileKind table_kind) -> CommittedResource;

//! A table `Finalize` wrote: its content-named path and what its writer
//! hashed.
struct WrittenResourceTable final {
  std::string relpath;
  LooseCookedFileDigest digest;
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

  //! Loads the table the latest commit of `cooked_root` lists, held by
  //! `storage`. The table is published and the data file is opened through
  //! `storage`, awaited on `driver`, which outlives the aggregator.
  /*!
   The cook holds the root's writer lease, which dropped the data file's
   bytes past its committed length, so new payloads go after that length.
  */
  ResourceTableAggregator(serio::IoDriver& driver,
    cooked::ImportStorage& storage, const std::filesystem::path& cooked_root)
    : driver_(make_observer(&driver))
    , storage_(make_observer(&storage))
    , cooked_root_(cooked_root)
    , data_path_(cooked_root / Traits::kDataRelPath)
  {
    const auto committed
      = ReadCommittedResource(*storage_, cooked_root_, Traits::kTableKind);
    data_file_size_.store(committed.data_length, std::memory_order_release);
    if (committed.table_relpath.has_value()) {
      LoadExistingTable(cooked_root_ / *committed.table_relpath);
    }
    EnsureSentinelAtZero();
  }

  ~ResourceTableAggregator() = default;

  OXYGEN_MAKE_NON_COPYABLE(ResourceTableAggregator)
  OXYGEN_MAKE_NON_MOVABLE(ResourceTableAggregator)

  //! Finds the row that `row` duplicates, or adds `row` with its data range
  //! reserved.
  /*!
   `row` holds every field but `data_offset`, and its `size_bytes` is the size
   of its payload. A row with the same `Traits::SignatureForDescriptor` is
   reused only when its data offset satisfies `alignment`; otherwise `row` is
   added at an aligned offset and becomes the row later requests reuse.

   @throw std::logic_error When `Finalize` has started.
  */
  auto Acquire(Descriptor row, const uint64_t alignment) -> AcquireResult
  {
    const auto signature = Traits::SignatureForDescriptor(row);
    std::scoped_lock lock(mutex_);
    if (finalize_started_.load(std::memory_order_acquire)) {
      throw std::logic_error("a resource row is acquired after finalization");
    }

    requests_.fetch_add(1, std::memory_order_acq_rel);

    if (const auto it = index_by_signature_.find(signature);
      it != index_by_signature_.end()
      && IsAligned(table_.at(it->second).data_offset, alignment)) {
      return AcquireResult { .index = it->second, .is_new = false };
    }

    const auto reservation = ReserveDataRange({
      .alignment = alignment,
      .payload_size = row.size_bytes,
    });
    row.data_offset = reservation.aligned_offset;
    const uint32_t index = next_index_++;
    table_.push_back(row);
    index_by_signature_.insert_or_assign(signature, index);

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

    // A commit that records the new rows records the data file's new
    // length: its bytes reach the disk first.
    if (data_file_) {
      if (const auto flushed = data_file_->Flush(serio::Durability::kStable);
        !flushed) {
        LOG_F(ERROR, "ResourceTableAggregator: failed to flush '{}': {}",
          data_path_.string(), flushed.error().message());
        co_return false;
      }
    }

    serio::MemoryStream stream;
    serio::Writer writer(stream);
    const auto pack = writer.ScopedAlignment(1);
    auto write_result = writer.WriteBlob(std::as_bytes(std::span(snapshot)));
    if (!write_result.has_value()) {
      LOG_F(ERROR, "ResourceTableAggregator: serialization failed");
      co_return false;
    }

    const auto digest = LooseCookedFileDigest {
      .size = stream.Data().size(),
      .sha256 = base::ComputeSha256(stream.Data()),
    };
    auto relpath = data::loose_cooked::ContentRelPath(
      Traits::kTableRelPath, digest.sha256);
    const auto table_path = cooked_root_ / std::filesystem::path(relpath);
    DLOG_F(INFO, "ResourceTableAggregator: writing {} entries to '{}'",
      snapshot.size(), table_path.string());
    // A table of the same rows is already there under the same name.
    if (const auto size = storage_->SizeOf(table_path);
      !size.has_value() || size->get() != digest.size) {
      const auto published = co_await storage_->PublishAsync(
        *driver_, table_path, std::span<const std::byte>(stream.Data()));
      if (!published) {
        LOG_F(ERROR, "ResourceTableAggregator: failed to write '{}': {}",
          table_path.string(), published.error().message());
        co_return false;
      }
    }

    DLOG_F(
      INFO, "ResourceTableAggregator: wrote {} bytes", stream.Data().size());
    written_ = WrittenResourceTable {
      .relpath = std::move(relpath),
      .digest = digest,
    };
    co_return true;
  }

  //! The table `Finalize` wrote; nothing before it writes one, or when it
  //! fails.
  [[nodiscard]] auto Written() const noexcept
    -> const std::optional<WrittenResourceTable>&
  {
    return written_;
  }

  auto Count() const noexcept -> uint32_t
  {
    return next_index_.load(std::memory_order_acquire);
  }

  auto DataFileSize() const noexcept -> uint64_t
  {
    return data_file_size_.load(std::memory_order_acquire);
  }

  //! The data file, opened on first use and held until the aggregator ends.
  /*!
   Emitters await `AsyncWriteAt` on it at the ranges `Acquire` reserved. The
   file is created when missing and never truncated, and it is open for writes
   while the aggregator lives.

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
  //! Reserves the next aligned range of the data file. Requires `mutex_`.
  auto ReserveDataRange(const WriteRangeRequest request) -> WriteReservation
  {
    const auto current_size = data_file_size_.load(std::memory_order_acquire);
    const auto aligned_offset = AlignUp(current_size, request.alignment);
    data_file_size_.store(
      aligned_offset + request.payload_size, std::memory_order_release);
    return {
      .reservation_start = current_size,
      .aligned_offset = aligned_offset,
      .padding_size = aligned_offset - current_size,
    };
  }

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
        cooked_root_.string());
    }
  }

  //! Loads the table the latest commit lists at `table_path`. A table that
  //! cannot be read is refused: starting over would publish a table that
  //! drops every resource the root already holds.
  auto LoadExistingTable(const std::filesystem::path& table_path) -> void
  {
    if (storage_->KindOf(table_path) != cooked::PathKind::kFile) {
      throw ResourceTableError(table_path, "is not a file");
    }

    const auto bytes
      = cooked::ReadWholeFile(*storage_, table_path, cooked::kMaxMetadataBytes);
    if (!bytes) {
      throw ResourceTableError(
        table_path, "cannot be read: " + bytes.error().message());
    }
    if (bytes->empty()) {
      return;
    }

    const auto size_bytes = bytes->size();
    if (size_bytes % sizeof(Descriptor) != 0) {
      throw ResourceTableError(table_path,
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
  std::filesystem::path cooked_root_;
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
  std::optional<WrittenResourceTable> written_;

  [[nodiscard]] static constexpr auto AlignUp(
    const uint64_t value, const uint64_t alignment) noexcept -> uint64_t
  {
    if (alignment <= 1) {
      return value;
    }
    const auto remainder = value % alignment;
    return (remainder == 0) ? value : (value + (alignment - remainder));
  }

  [[nodiscard]] static constexpr auto IsAligned(
    const uint64_t value, const uint64_t alignment) noexcept -> bool
  {
    return AlignUp(value, alignment) == value;
  }
};

using TextureTableAggregator = ResourceTableAggregator<TextureTableTraits>;
using BufferTableAggregator = ResourceTableAggregator<BufferTableTraits>;
using PhysicsTableAggregator = ResourceTableAggregator<PhysicsTableTraits>;

} // namespace oxygen::content::import
