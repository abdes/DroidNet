//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Data/PakFormat.h>

namespace oxygen::content::import::util {

//! Converts a SHA256 digest to lowercase hex string.
/*!
 @param digest The 32-byte SHA256 digest.
 @return A 64-character lowercase hex string.
*/
[[nodiscard]] inline auto Sha256ToHex(const oxygen::base::Sha256Digest& digest)
  -> std::string
{
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out;
  out.resize(digest.size() * 2);
  for (size_t i = 0; i < digest.size(); ++i) {
    const auto b = digest[i];
    out[i * 2 + 0] = kHex[(b >> 4) & 0x0F];
    out[i * 2 + 1] = kHex[b & 0x0F];
  }
  return out;
}

//! Computes truncated 8-byte content hash from data.
/*!
 @param bytes The raw data bytes.
 @return First 8 bytes of SHA256 as uint64_t.
*/
[[nodiscard]] inline auto ComputeContentHash(
  const std::span<const std::byte> bytes) -> uint64_t
{
  const auto digest = oxygen::base::ComputeSha256(bytes);
  uint64_t hash = 0;
  for (size_t i = 0; i < 8; ++i) {
    hash |= static_cast<uint64_t>(digest[i]) << (i * 8);
  }
  return hash;
}

//! Computes a content signature for texture deduplication.
/*!
 The signature includes SHA256 of content plus descriptor metadata,
 ensuring identical textures with different metadata are not deduplicated.

 @param desc The texture resource descriptor.
 @param bytes The raw texture bytes.
 @return A unique signature string for this texture.
*/
[[nodiscard]] inline auto MakeTextureSignature(
  const oxygen::data::pak::TextureResourceDesc& desc,
  const std::span<const std::byte> bytes) -> std::string
{
  const auto content_hash = ComputeContentHash(bytes);

  std::string signature;
  signature.reserve(64);
  signature.append(std::to_string(content_hash));
  signature.push_back(':');
  signature.append(std::to_string(desc.width));
  signature.push_back('x');
  signature.append(std::to_string(desc.height));
  signature.push_back(':');
  signature.append(std::to_string(desc.mip_levels));
  signature.push_back(':');
  signature.append(std::to_string(desc.format));
  signature.push_back(':');
  signature.append(std::to_string(desc.alignment));
  signature.push_back(':');
  signature.append(std::to_string(desc.size_bytes));
  return signature;
}

//! Computes texture signature from stored content_hash (no data read).
/*!
 Uses the pre-computed content_hash stored in the descriptor.

 @param desc The texture resource descriptor with valid content_hash.
 @return A unique signature string for this texture.
*/
[[nodiscard]] inline auto MakeTextureSignatureFromStoredHash(
  const oxygen::data::pak::TextureResourceDesc& desc) -> std::string
{
  std::string signature;
  signature.reserve(64);
  signature.append(std::to_string(desc.content_hash));
  signature.push_back(':');
  signature.append(std::to_string(desc.width));
  signature.push_back('x');
  signature.append(std::to_string(desc.height));
  signature.push_back(':');
  signature.append(std::to_string(desc.mip_levels));
  signature.push_back(':');
  signature.append(std::to_string(desc.format));
  signature.push_back(':');
  signature.append(std::to_string(desc.alignment));
  signature.push_back(':');
  signature.append(std::to_string(desc.size_bytes));
  return signature;
}

//! Computes a content signature for buffer deduplication.
/*!
 The signature includes hash of content plus buffer metadata.

 @param desc The buffer resource descriptor.
 @param bytes The raw buffer bytes.
 @return A unique signature string for this buffer.
*/
[[nodiscard]] inline auto MakeBufferSignature(
  const oxygen::data::pak::BufferResourceDesc& desc,
  const std::span<const std::byte> bytes) -> std::string
{
  const auto content_hash = ComputeContentHash(bytes);

  std::string signature;
  signature.reserve(64);
  signature.append(std::to_string(content_hash));
  signature.push_back(':');
  signature.append(std::to_string(desc.usage_flags));
  signature.push_back(':');
  signature.append(std::to_string(desc.element_stride));
  signature.push_back(':');
  signature.append(std::to_string(desc.element_format));
  signature.push_back(':');
  signature.append(std::to_string(desc.size_bytes));
  return signature;
}

//! Computes buffer signature from stored content_hash (no data read).
/*!
 Uses the pre-computed content_hash stored in the descriptor.

 @param desc The buffer resource descriptor with valid content_hash.
 @return A unique signature string for this buffer.
*/
[[nodiscard]] inline auto MakeBufferSignatureFromStoredHash(
  const oxygen::data::pak::BufferResourceDesc& desc) -> std::string
{
  std::string signature;
  signature.reserve(64);
  signature.append(std::to_string(desc.content_hash));
  signature.push_back(':');
  signature.append(std::to_string(desc.usage_flags));
  signature.push_back(':');
  signature.append(std::to_string(desc.element_stride));
  signature.push_back(':');
  signature.append(std::to_string(desc.element_format));
  signature.push_back(':');
  signature.append(std::to_string(desc.size_bytes));
  return signature;
}

} // namespace oxygen::content::import::util
