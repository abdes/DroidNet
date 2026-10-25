//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Data/SceneIds.h>

namespace oxygen::content::import::internal {

//! A JSON UUID string as a non-nil ID of kind `Id`, or nothing.
template <typename Id>
[[nodiscard]] auto ParseNonNilId(const nlohmann::json& value)
  -> std::optional<Id>
{
  if (!value.is_string()) {
    return std::nullopt;
  }
  const auto id = Id::FromString(value.get<std::string>());
  if (!id.has_value() || id.value().IsNil()) {
    return std::nullopt;
  }
  return id.value();
}

//! A node path: one non-nil node ID per nesting level below an instance root,
//! shallowest first.
/*!
 The error is the index of the first element that is not a non-nil node ID, or
 zero when `value` is not a nonempty array.
*/
[[nodiscard]] inline auto ParseNodePath(const nlohmann::json& value)
  -> std::expected<std::vector<data::NodeId>, size_t>
{
  if (!value.is_array() || value.empty()) {
    return std::unexpected(size_t { 0 });
  }
  auto path = std::vector<data::NodeId> {};
  path.reserve(value.size());
  for (size_t i = 0; i < value.size(); ++i) {
    const auto id = ParseNonNilId<data::NodeId>(value.at(i));
    if (!id.has_value()) {
      return std::unexpected(i);
    }
    path.push_back(*id);
  }
  return path;
}

} // namespace oxygen::content::import::internal
