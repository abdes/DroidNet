//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <span>
#include <stdexcept>

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Cooker/Import/CapturedInputSet.h>

namespace oxygen::content::import {

CapturedInputSet::CapturedInputSet(const std::span<const CapturedInput> inputs)
{
  inputs_.reserve(inputs.size());
  for (const auto& input : inputs) {
    if (input.logical_path.empty() || !input.logical_path.is_absolute()) {
      throw std::invalid_argument("Captured logical paths must be absolute");
    }
    if (!input.exists
      && (input.metadata.has_value() || input.file.has_value())) {
      throw std::invalid_argument(
        "Absent captured sources cannot contain metadata or bytes");
    }
    if (input.file.has_value()) {
      if (input.file->path.empty() || !input.file->path.is_absolute()) {
        throw std::invalid_argument("Captured byte paths must be absolute");
      }
      if (!input.metadata.has_value() || input.metadata->is_directory
        || input.metadata->size != input.file->size) {
        throw std::invalid_argument(
          "Captured bytes require matching source-file metadata");
      }
    }
    if (!inputs_.emplace(base::PathIdentityKey(input.logical_path), input)
          .second) {
      throw std::invalid_argument(
        "Duplicate captured source identity: " + input.logical_path.string());
    }
  }
}

auto CapturedInputSet::Find(const std::filesystem::path& logical_path) const
  -> const CapturedInput*
{
  const auto entry = inputs_.find(base::PathIdentityKey(logical_path));
  return entry == inputs_.end() ? nullptr : &entry->second;
}

} // namespace oxygen::content::import
