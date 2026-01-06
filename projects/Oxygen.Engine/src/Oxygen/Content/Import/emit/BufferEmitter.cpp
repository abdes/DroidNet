//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Content/Import/emit/BufferEmitter.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Import/util/Signature.h>

namespace oxygen::content::import::emit {

auto GetOrCreateBufferResourceIndex(BufferEmissionState& state,
  std::span<const std::byte> bytes, uint64_t alignment, uint32_t usage_flags,
  uint32_t element_stride, uint8_t element_format) -> uint32_t
{
  if (bytes.empty()) {
    return 0;
  }

  // Compute content hash for deduplication and storage
  const auto content_hash = util::ComputeContentHash(bytes);

  // Build descriptor for signature computation
  BufferEmissionState::BufferResourceDesc desc {};
  desc.data_offset = 0;
  desc.size_bytes = static_cast<uint32_t>(bytes.size());
  desc.usage_flags = usage_flags;
  desc.element_stride = element_stride;
  desc.element_format = element_format;
  desc.content_hash = content_hash;

  const auto signature = util::MakeBufferSignatureFromStoredHash(desc);

  // Check for duplicate
  if (const auto it = state.index_by_signature.find(signature);
    it != state.index_by_signature.end()) {
    const auto existing_index = it->second;
    LOG_F(INFO, "Reuse buffer (size={}, usage={}, stride={}) -> index {}",
      bytes.size(), usage_flags, element_stride, existing_index);
    return existing_index;
  }

  // Append new buffer to data file
  const auto data_offset = AppendResource(state.appender, bytes, alignment);

  desc.data_offset = data_offset;

  const auto index = static_cast<uint32_t>(state.table.size());
  state.table.push_back(desc);
  state.index_by_signature.emplace(signature, index);

  LOG_F(INFO, "Emit buffer (size={}, usage={}, stride={}) -> index {}",
    bytes.size(), usage_flags, element_stride, index);

  return index;
}

} // namespace oxygen::content::import::emit
