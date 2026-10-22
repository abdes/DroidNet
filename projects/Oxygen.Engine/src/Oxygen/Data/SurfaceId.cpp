//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/SurfaceId.h>

namespace oxygen::data {

auto SurfaceId::Generate() -> SurfaceId
{
  const auto uuid = Uuid::Generate();
  ByteArray bytes {};
  std::ranges::copy(uuid, bytes.begin());
  return FromBytes(bytes);
}

auto SurfaceId::FromStableIdentity(const std::string_view identity) -> SurfaceId
{
  if (identity.empty()) {
    throw std::invalid_argument("Surface identity must not be empty");
  }
  const auto digest = base::ComputeSha256(
    std::as_bytes(std::span(identity.data(), identity.size())));
  ByteArray bytes {};
  std::copy_n(digest.begin(), bytes.size(), bytes.begin());
  const auto id = FromBytes(bytes);
  if (id.IsNil()) {
    throw std::invalid_argument("Surface identity hash must not be nil");
  }
  return id;
}

auto SurfaceId::FromString(const std::string_view text) -> Result<SurfaceId>
{
  // Share the native opaque-128-bit text codec, not asset identity semantics.
  const auto parsed = AssetKey::FromString(text);
  if (!parsed.has_value()) {
    return Result<SurfaceId>::Err(parsed.error());
  }
  ByteArray bytes {};
  std::ranges::copy(parsed.value(), bytes.begin());
  return Result<SurfaceId>::Ok(FromBytes(bytes));
}

auto to_string(const SurfaceId& value) -> std::string
{
  AssetKey::ByteArray bytes {};
  std::ranges::copy(value, bytes.begin());
  return to_string(AssetKey::FromBytes(bytes));
}

} // namespace oxygen::data
