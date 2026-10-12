//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <system_error>
#include <type_traits>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex::upload {

class InlineTransfersCoordinator;
class StagingProvider;

//! One immutable constant record, bound as a root CBV by GPU address.
/*!
 The record shares ownership of its backing buffer, so a binding stays valid
 for as long as the code that records against it holds the record.
*/
struct ConstantBufferRecord {
  std::shared_ptr<const graphics::Buffer> buffer;
  std::uint64_t offset_bytes { 0U };

  [[nodiscard]] auto IsValid() const noexcept -> bool
  {
    return buffer != nullptr;
  }

  //! The GPU address a root CBV binds; requires a valid record.
  [[nodiscard]] auto GpuAddress() const -> std::uint64_t
  {
    return buffer->GetGPUVirtualAddress() + offset_bytes;
  }
};

//! Writes immutable root-CBV records into a shared staging provider.
/*!
 Each `Write` places a new record; nothing written is ever rewritten, so work
 recorded against an earlier record keeps reading the values it was recorded
 with, whatever is written after it in the same frame.

 Records come from the provider's current frame partition and live until the
 provider recycles that partition, frames-in-flight later, exactly like
 `TransientStructuredBuffer` allocations. No views are created, so there is no
 per-slot state to release. Root CBVs need 256-byte placement, which
 `StagingProvider::AllocateAligned` provides on any shared ring.
*/
class TransientConstantBuffer {
public:
  OXGN_VRTX_API TransientConstantBuffer(StagingProvider& staging,
    observer_ptr<InlineTransfersCoordinator> inline_transfers,
    std::string debug_label);

  //! Write `value` as a new record.
  template <typename T>
  auto Write(const T& value)
    -> std::expected<ConstantBufferRecord, std::error_code>
  {
    static_assert(std::is_trivially_copyable_v<T>,
      "TransientConstantBuffer::Write requires a trivially copyable record");
    return WriteBytes(std::as_bytes(std::span { &value, 1U }));
  }

  //! Write `bytes` as a new record, zero-padded to whole 256-byte blocks.
  OXGN_VRTX_API auto WriteBytes(std::span<const std::byte> bytes)
    -> std::expected<ConstantBufferRecord, std::error_code>;

private:
  observer_ptr<StagingProvider> staging_;
  observer_ptr<InlineTransfersCoordinator> inline_transfers_;
  std::string debug_label_;
};

} // namespace oxygen::vortex::upload
