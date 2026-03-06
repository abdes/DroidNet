//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/Internal/ResourceTableAggregator.h>
#include <Oxygen/Cooker/Loose/LooseCookedLayout.h>
#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/OxCo/Co.h>

namespace oxygen::content::import {

class IAsyncFileWriter;
struct FileErrorInfo;

//! Cooked payload consumed by `PhysicsResourceEmitter`.
struct CookedPhysicsResourcePayload final {
  std::vector<std::byte> data;
  data::pak::physics::PhysicsResourceFormat format
    = data::pak::physics::PhysicsResourceFormat::kJoltShapeBinary;
  uint64_t alignment = 16;
  base::Sha256Digest content_hash = {};
};

//! Emits cooked physics resources to `physics.data` and `physics.table`.
class PhysicsResourceEmitter final {
public:
  struct Config final {
    DedupCollisionPolicy collision_policy
      = DedupCollisionPolicy::kWarnKeepFirst;
    std::function<void(ImportDiagnostic)> on_dedup_diagnostic;
  };

  OXGN_COOK_API PhysicsResourceEmitter(IAsyncFileWriter& file_writer,
    PhysicsTableAggregator& table_aggregator, const LooseCookedLayout& layout,
    const std::filesystem::path& cooked_root);

  OXGN_COOK_API PhysicsResourceEmitter(IAsyncFileWriter& file_writer,
    PhysicsTableAggregator& table_aggregator, const LooseCookedLayout& layout,
    const std::filesystem::path& cooked_root, Config config);

  OXGN_COOK_API ~PhysicsResourceEmitter();

  OXYGEN_MAKE_NON_COPYABLE(PhysicsResourceEmitter)
  OXYGEN_MAKE_NON_MOVABLE(PhysicsResourceEmitter)

  OXGN_COOK_NDAPI auto Emit(CookedPhysicsResourcePayload cooked,
    std::string_view signature_salt) -> uint32_t;

  OXGN_COOK_NDAPI auto Count() const noexcept -> uint32_t;
  OXGN_COOK_NDAPI auto PendingCount() const noexcept -> size_t;
  OXGN_COOK_NDAPI auto ErrorCount() const noexcept -> size_t;
  OXGN_COOK_NDAPI auto DataFileSize() const noexcept -> uint64_t;

  OXGN_COOK_NDAPI auto TryGetDescriptor(uint32_t index) const
    -> std::optional<data::pak::physics::PhysicsResourceDesc>;

  OXGN_COOK_NDAPI auto Finalize() -> co::Co<bool>;

private:
  enum class WriteKind : uint8_t {
    kPadding,
    kPayload,
  };

  auto MakeTableEntry(const CookedPhysicsResourcePayload& cooked,
    uint64_t data_offset) -> data::pak::physics::PhysicsResourceDesc;
  auto QueueDataWrite(WriteKind kind, std::optional<uint32_t> index,
    uint64_t offset, std::shared_ptr<std::vector<std::byte>> data) -> void;
  auto OnWriteComplete(WriteKind kind, std::optional<uint32_t> index,
    const FileErrorInfo& error) -> void;

  IAsyncFileWriter& file_writer_;
  PhysicsTableAggregator& table_aggregator_;
  Config config_ {};
  std::filesystem::path data_path_;
  std::atomic<bool> finalize_started_ { false };
  std::atomic<uint32_t> emitted_count_ { 0 };
  std::atomic<size_t> pending_count_ { 0 };
  std::atomic<size_t> error_count_ { 0 };
  std::unordered_map<std::string, std::string> identity_by_key_;
  std::unordered_map<std::string, uint32_t> index_by_key_;
};

} // namespace oxygen::content::import
