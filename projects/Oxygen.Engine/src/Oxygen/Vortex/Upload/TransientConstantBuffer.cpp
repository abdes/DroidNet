//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <system_error>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/ByteUnits.h>
#include <Oxygen/Vortex/Upload/Errors.h>
#include <Oxygen/Vortex/Upload/InlineTransfersCoordinator.h>
#include <Oxygen/Vortex/Upload/StagingProvider.h>
#include <Oxygen/Vortex/Upload/TransientConstantBuffer.h>

namespace oxygen::vortex::upload {

TransientConstantBuffer::TransientConstantBuffer(StagingProvider& staging,
  const observer_ptr<InlineTransfersCoordinator> inline_transfers,
  std::string debug_label)
  : staging_(observer_ptr { &staging })
  , inline_transfers_(inline_transfers)
  , debug_label_(std::move(debug_label))
{
}

auto TransientConstantBuffer::WriteBytes(const std::span<const std::byte> bytes)
  -> Result<ConstantBufferRecord>
{
  if (bytes.empty()) {
    return Err(make_error_code(UploadError::kInvalidRequest));
  }

  // Whole 256-byte blocks: a shader cbuffer declared larger than its CPU
  // mirror reads zeros, never a neighbouring record.
  constexpr std::uint64_t kBlock = packing::kConstantBufferAlignment;
  const auto record_bytes = ((bytes.size() + kBlock - 1U) / kBlock) * kBlock;
  auto allocation = staging_->AllocateAligned(
    SizeBytes { record_bytes }, kBlock, debug_label_);
  if (!allocation) {
    const auto ec = make_error_code(allocation.error());
    if (allocation.error() != UploadError::kBudgetExceeded) {
      LOG_F(ERROR, "{}: constant record allocation failed: {}", debug_label_,
        ec.message());
    }
    return Err(ec);
  }

  const auto record
    = std::span(allocation->Ptr(), static_cast<std::size_t>(record_bytes));
  std::ranges::copy(bytes, record.begin());
  std::ranges::fill(record.subspan(bytes.size()), std::byte { 0 });
  if (inline_transfers_ != nullptr) {
    inline_transfers_->NotifyInlineWrite(
      SizeBytes { record_bytes }, debug_label_);
  }
  return Ok(ConstantBufferRecord {
    .buffer = allocation->SharedBuffer(),
    .offset_bytes = allocation->Offset().get(),
  });
}

} // namespace oxygen::vortex::upload
