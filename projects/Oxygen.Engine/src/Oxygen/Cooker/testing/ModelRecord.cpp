//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include <Oxygen/Cooker/Import/ModelImportRecord.h>
#include <Oxygen/Cooker/testing/ModelRecord.h>
#include <Oxygen/Cooker/testing/Seeds.h>

namespace oxygen::cooker::testing {

ModelRecord::ModelRecord(std::string identity, const uint8_t seed)
  : identity_(std::move(identity))
  , source_(identity_)
  , source_identity_(UuidOf(seed))
{
}

auto ModelRecord::Source(const std::string_view source) -> ModelRecord&
{
  if (source.starts_with('/')) {
    source_ = std::string(source);
  } else {
    source_
      = identity_.substr(0U, identity_.rfind('/') + 1U) + std::string(source);
  }
  return *this;
}

auto ModelRecord::Settings(std::string settings) -> ModelRecord&
{
  settings_ = std::move(settings);
  return *this;
}

auto ModelRecord::Text() const -> std::string
{
  return content::import::ModelImportRecord::Serialize(
    {
      .source = {},
      .source_mount_path = source_,
      .settings_json = settings_,
      .source_identity = source_identity_,
    },
    identity_);
}

} // namespace oxygen::cooker::testing
