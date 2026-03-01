//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Content/api_export.h>

namespace oxygen::content {
class IAssetLoader;
} // namespace oxygen::content

namespace oxygen::data {
class InputMappingContextAsset;
} // namespace oxygen::data

namespace oxygen::engine {
class InputSystem;
} // namespace oxygen::engine

namespace oxygen::input {
class InputMappingContext;
} // namespace oxygen::input

namespace oxygen::content {

[[nodiscard]] OXGN_CNTT_API auto HydrateInputContext(
  const data::InputMappingContextAsset& asset, IAssetLoader& loader,
  engine::InputSystem& input_system)
  -> std::shared_ptr<input::InputMappingContext>;

} // namespace oxygen::content
