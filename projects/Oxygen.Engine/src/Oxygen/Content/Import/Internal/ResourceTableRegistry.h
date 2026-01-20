//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Content/Import/Internal/ResourceTableAggregator.h>
#include <Oxygen/Content/Import/LooseCookedLayout.h>
#include <Oxygen/Content/api_export.h>
#include <Oxygen/OxCo/Co.h>

namespace oxygen::content::import {

class IAsyncFileWriter;

class ResourceTableRegistry final {
public:
  OXGN_CNTT_API explicit ResourceTableRegistry(IAsyncFileWriter& file_writer);

  OXYGEN_MAKE_NON_COPYABLE(ResourceTableRegistry)
  OXYGEN_MAKE_NON_MOVABLE(ResourceTableRegistry)

  OXGN_CNTT_NDAPI auto TextureAggregator(
    const std::filesystem::path& cooked_root, const LooseCookedLayout& layout)
    -> TextureTableAggregator&;

  OXGN_CNTT_NDAPI auto BufferAggregator(
    const std::filesystem::path& cooked_root, const LooseCookedLayout& layout)
    -> BufferTableAggregator&;

  OXGN_CNTT_NDAPI auto FinalizeAll() -> co::Co<bool>;

private:
  auto NormalizeKey(const std::filesystem::path& cooked_root) const
    -> std::string;

  IAsyncFileWriter& file_writer_;
  std::mutex mutex_;
  std::unordered_map<std::string, std::unique_ptr<TextureTableAggregator>>
    texture_tables_;
  std::unordered_map<std::string, std::unique_ptr<BufferTableAggregator>>
    buffer_tables_;
};

} // namespace oxygen::content::import
