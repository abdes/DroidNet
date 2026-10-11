//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Data/Asset.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/api_export.h>

namespace oxygen::data {

class InputActionAsset final : public Asset {
  OXYGEN_TYPED(InputActionAsset)

public:
  OXGN_DATA_API InputActionAsset(AssetKey asset_key,
    pak::input::InputActionAssetDesc desc, SourceOrigin source_origin = {});

  ~InputActionAsset() override = default;

  OXYGEN_MAKE_NON_COPYABLE(InputActionAsset)
  OXYGEN_DEFAULT_MOVABLE(InputActionAsset)

  [[nodiscard]] auto GetHeader() const noexcept
    -> const pak::core::AssetHeader& override
  {
    return desc_.header;
  }

  [[nodiscard]] auto GetFlags() const noexcept
    -> pak::input::InputActionAssetFlags
  {
    return desc_.flags;
  }

  [[nodiscard]] auto ConsumesInput() const noexcept -> bool
  {
    return (desc_.flags & pak::input::InputActionAssetFlags::kConsumesInput)
      == pak::input::InputActionAssetFlags::kConsumesInput;
  }

  //! True when the action sums its bindings' values instead of keeping the
  //! one with the highest magnitude per component.
  [[nodiscard]] auto AccumulatesCumulatively() const noexcept -> bool
  {
    return (desc_.flags
             & pak::input::InputActionAssetFlags::kCumulativeAccumulation)
      == pak::input::InputActionAssetFlags::kCumulativeAccumulation;
  }

  [[nodiscard]] auto GetValueTypeId() const noexcept -> uint8_t
  {
    return desc_.value_type;
  }

private:
  pak::input::InputActionAssetDesc desc_ {};
};

} // namespace oxygen::data
