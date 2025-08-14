//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Renderer/RenderItem.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::engine {

//! Managed container for RenderItem with invariants and auto-updates.
/*!
 Ensures each inserted or mutated item is validated and has its computed
 properties updated. Exposes a const span for consumption by render passes
 via RenderContext. Mutation helpers guarantee recomputation.

 @see RenderItem, Renderer
*/
class RenderItemsList {
public:
  OXGN_RNDR_API RenderItemsList() = default;
  OXGN_RNDR_API ~RenderItemsList() = default;

  OXYGEN_DEFAULT_COPYABLE(RenderItemsList)
  OXYGEN_DEFAULT_MOVABLE(RenderItemsList)

  //! Remove all items.
  OXGN_RNDR_API auto Clear() -> void;
  //! Reserve capacity for N items.
  OXGN_RNDR_API auto Reserve(std::size_t n) -> void;

  //! Add a new item (validated and recomputed). Returns index.
  OXGN_RNDR_API auto Add(RenderItem item) -> std::size_t;
  //! Remove item at index (stable order; shifts tail left).
  OXGN_RNDR_API auto RemoveAt(std::size_t index) -> void;

  //! Read-only view of items for draw submission.
  [[nodiscard]] OXGN_RNDR_API auto Items() const -> std::span<const RenderItem>;

  //! Replace item at index (validated and recomputed).
  OXGN_RNDR_API auto Update(std::size_t index, const RenderItem& item) -> void;

  //! Count of items.
  [[nodiscard]] OXGN_RNDR_API auto Size() const -> std::size_t;

private:
  static auto Validate(const RenderItem& item) -> void;
  static auto Recompute(RenderItem& item) -> void;

  std::vector<RenderItem> items_;
};

} // namespace oxygen::engine
