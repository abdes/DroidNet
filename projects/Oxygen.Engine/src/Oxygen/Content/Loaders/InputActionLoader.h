//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstring>
#include <memory>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/LoaderFunctions.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/InputActionAsset.h>
#include <Oxygen/Data/PakFormat.h>

namespace oxygen::content::loaders {

inline auto LoadInputActionAsset(const LoaderContext& context)
  -> std::unique_ptr<data::InputActionAsset>
{
  LOG_SCOPE_FUNCTION(INFO);

  DCHECK_NOTNULL_F(context.desc_reader, "expecting desc_reader not to be null");
  auto& reader = *context.desc_reader;

  auto check_result = [](auto&& result, const char* field) {
    if (!result) {
      LOG_F(
        ERROR, "-failed- on {}: {}", field, result.error().message().c_str());
      throw std::runtime_error(
        fmt::format("error reading input action asset ({}): {}", field,
          result.error().message()));
    }
  };

  auto pack = reader.ScopedAlignment(1);

  auto desc_blob
    = reader.ReadBlob(sizeof(data::pak::input::InputActionAssetDesc));
  check_result(desc_blob, "InputActionAssetDesc");
  data::pak::input::InputActionAssetDesc desc {};
  std::memcpy(&desc, desc_blob->data(), sizeof(desc));

  if (static_cast<data::AssetType>(desc.header.asset_type)
    != data::AssetType::kInputAction) {
    throw std::runtime_error("invalid asset type for input action descriptor");
  }

  // Value type IDs are defined by ActionValueType contract (0..2).
  if (desc.value_type > 2) {
    throw std::runtime_error("invalid input action value_type");
  }

  return std::make_unique<data::InputActionAsset>(
    context.current_asset_key, desc);
}

static_assert(oxygen::content::LoadFunction<decltype(LoadInputActionAsset)>);

} // namespace oxygen::content::loaders
