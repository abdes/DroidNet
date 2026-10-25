//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <limits>
#include <map>
#include <span>
#include <stdexcept>
#include <vector>

#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneIds.h>

namespace oxygen::content::import::internal {

//! The node path tables of a scene being cooked: one record per distinct path,
//! in the order the paths are first interned.
class NodePathTable final {
public:
  //! The index of the record of `path`, a nonempty path, added when new.
  auto Intern(std::span<const data::NodeId> path)
    -> data::pak::world::NodePathIndexT
  {
    auto key = std::vector<data::NodeId>(path.begin(), path.end());
    if (const auto found = index_.find(key); found != index_.end()) {
      return found->second;
    }
    if (records_.size() >= data::pak::world::kNoNodePath
      || elements_.size() + path.size()
        > std::numeric_limits<uint32_t>::max()) {
      throw std::length_error("Scene node path tables exceed the format limit");
    }
    const auto index
      = static_cast<data::pak::world::NodePathIndexT>(records_.size());
    records_.push_back({
      .first = static_cast<uint32_t>(elements_.size()),
      .count = static_cast<uint32_t>(path.size()),
    });
    for (const auto& id : path) {
      elements_.push_back({ .node_id = id });
    }
    index_.emplace(std::move(key), index);
    return index;
  }

  [[nodiscard]] auto Records() const noexcept
    -> std::span<const data::pak::world::NodePathRecord>
  {
    return records_;
  }

  [[nodiscard]] auto Elements() const noexcept
    -> std::span<const data::pak::world::NodePathElement>
  {
    return elements_;
  }

private:
  std::vector<data::pak::world::NodePathRecord> records_;
  std::vector<data::pak::world::NodePathElement> elements_;
  std::map<std::vector<data::NodeId>, data::pak::world::NodePathIndexT> index_;
};

} // namespace oxygen::content::import::internal
