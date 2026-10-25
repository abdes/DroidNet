//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/StableId.h>

namespace oxygen::data::detail {

auto GenerateStableId() -> StableIdBytes
{
  const auto uuid = Uuid::Generate();
  StableIdBytes bytes {};
  std::ranges::copy(uuid, bytes.begin());
  return bytes;
}

auto HashStableIdentity(const std::string_view identity)
  -> Result<StableIdBytes>
{
  if (identity.empty()) {
    return Err(std::errc::invalid_argument);
  }
  const auto digest = base::ComputeSha256(
    std::as_bytes(std::span(identity.data(), identity.size())));
  StableIdBytes bytes {};
  std::copy_n(digest.begin(), bytes.size(), bytes.begin());
  if (bytes == StableIdBytes {}) {
    return Err(std::errc::result_out_of_range);
  }
  return Result<StableIdBytes>::Ok(bytes);
}

auto ParseStableId(const std::string_view text) -> Result<StableIdBytes>
{
  // Share the native opaque-128-bit text codec, not asset identity semantics.
  const auto parsed = AssetKey::FromString(text);
  if (!parsed.has_value()) {
    return Result<StableIdBytes>::Err(parsed.error());
  }
  StableIdBytes bytes {};
  std::ranges::copy(parsed.value(), bytes.begin());
  return Result<StableIdBytes>::Ok(bytes);
}

auto FormatStableId(const StableIdBytes& bytes) -> std::string
{
  AssetKey::ByteArray key {};
  std::ranges::copy(bytes, key.begin());
  return to_string(AssetKey::FromBytes(key));
}

} // namespace oxygen::data::detail
