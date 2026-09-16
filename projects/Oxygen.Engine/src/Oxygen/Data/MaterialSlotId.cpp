//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>

#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/MaterialSlotId.h>

namespace oxygen::data {

auto MaterialSlotId::FromString(const std::string_view text)
  -> Result<MaterialSlotId>
{
  // Share the native opaque-128-bit text codec, not asset identity semantics.
  const auto parsed = AssetKey::FromString(text);
  if (!parsed.has_value()) {
    return Result<MaterialSlotId>::Err(parsed.error());
  }
  ByteArray bytes {};
  std::ranges::copy(parsed.value(), bytes.begin());
  return Result<MaterialSlotId>::Ok(FromBytes(bytes));
}

auto to_string(const MaterialSlotId& value) -> std::string
{
  AssetKey::ByteArray bytes {};
  std::ranges::copy(value, bytes.begin());
  return to_string(AssetKey::FromBytes(bytes));
}

} // namespace oxygen::data
