//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/WriteGroup.h>
#include <Oxygen/Cooker/Import/Internal/ResourceTableAggregator.h>
#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/Serio/AnyDevice.h>

namespace oxygen::content::import {

//! Cooked payload consumed by `PhysicsResourceEmitter`.
struct CookedPhysicsResourcePayload final {
  std::vector<std::byte> data;
  data::pak::physics::PhysicsResourceFormat format
    = data::pak::physics::PhysicsResourceFormat::kJoltShapeBinary;
  uint64_t alignment = 16;
  //! The SHA-256 of `data`, computed by the payload's producer.
  base::Sha256Digest content_hash = {};
};

struct EmittedPhysicsResourceRef final {
  uint32_t resource_index = 0;
  data::AssetKey resource_asset_key;
};

//! Emits cooked physics resources to `physics.data` and `physics.table`.
/*!
 The data is written at its reserved offset to the data file the aggregator
 opened once, by tasks of the import session's nursery; `Finalize` joins them
 and returns the first error.
*/
class PhysicsResourceEmitter final {
public:
  //! `writes` is the nursery the writes run in; it outlives them.
  OXGN_COOK_API PhysicsResourceEmitter(co::Nursery& writes,
    PhysicsTableAggregator& table_aggregator,
    const std::filesystem::path& cooked_root);

  OXGN_COOK_API ~PhysicsResourceEmitter();

  OXYGEN_MAKE_NON_COPYABLE(PhysicsResourceEmitter)
  OXYGEN_MAKE_NON_MOVABLE(PhysicsResourceEmitter)

  //! Emits `cooked`, refusing a payload without its SHA-256.
  OXGN_COOK_NDAPI auto Emit(CookedPhysicsResourcePayload cooked)
    -> EmittedPhysicsResourceRef;

  OXGN_COOK_NDAPI auto Count() const noexcept -> uint32_t;
  OXGN_COOK_NDAPI auto ErrorCount() const noexcept -> size_t;
  OXGN_COOK_NDAPI auto DataFileSize() const noexcept -> uint64_t;

  OXGN_COOK_NDAPI auto TryGetDescriptor(uint32_t index) const
    -> std::optional<data::pak::physics::PhysicsResourceDesc>;

  OXGN_COOK_NDAPI auto Finalize() -> co::Co<Result<void, WriteFailure>>;

private:
  enum class WriteKind : uint8_t {
    kPadding,
    kPayload,
  };

  auto MakeTableEntry(const CookedPhysicsResourcePayload& cooked,
    uint64_t data_offset, const data::AssetKey& resource_asset_key)
    -> data::pak::physics::PhysicsResourceDesc;
  auto QueueDataWrite(WriteKind kind, std::optional<uint32_t> index,
    uint64_t offset, std::vector<std::byte> data) -> void;
  auto WriteData(serio::AnyAsyncDevice* file, WriteKind kind,
    std::optional<uint32_t> index, uint64_t offset, std::vector<std::byte> data)
    -> co::Co<Result<void>>;

  WriteGroup writes_;
  PhysicsTableAggregator& table_aggregator_;
  std::filesystem::path data_path_;
  std::atomic<bool> finalize_started_ { false };
  std::atomic<uint32_t> emitted_count_ { 0 };
};

} // namespace oxygen::content::import
