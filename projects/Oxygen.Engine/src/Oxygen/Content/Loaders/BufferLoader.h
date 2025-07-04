//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/Reader.h>
#include <Oxygen/Base/Stream.h>
#include <Oxygen/Content/LoaderFunctions.h>
#include <Oxygen/Data/BufferResource.h>
#include <Oxygen/Data/PakFormat.h>

namespace oxygen::content::loaders {

//! Loader for buffer assets.

//! Loads a buffer resource from a PAK file stream.
template <oxygen::serio::Stream S>
auto LoadBufferResource(LoaderContext<S> context)
  -> std::unique_ptr<data::BufferResource>
{
  LOG_SCOPE_F(1, "Load Buffer Resource");
  LOG_F(2, "offline mode   : {}", context.offline ? "yes" : "no");

  auto& reader = context.reader.get();

  using oxygen::data::pak::BufferResourceDesc;

  auto check_result = [](auto&& result, const char* field) {
    if (!result) {
      LOG_F(ERROR, "-failed- on {}: {}", field, result.error().message());
      throw std::runtime_error(
        fmt::format("error reading buffer resource ({}): {}", field,
          result.error().message()));
    }
  };

  // Read BufferResourceDesc from the stream
  LOG_F(2, "-- buufer desc reader pos = {}", reader.position().value());
  auto result = reader.read<BufferResourceDesc>();
  check_result(result, "BufferResourceDesc");
  const auto& desc = result.value();

  auto buf_format = static_cast<oxygen::Format>(desc.element_format);
  auto flags = static_cast<data::BufferResource::UsageFlags>(desc.usage_flags);
  LOG_F(1, "data offset    : {}", desc.data_offset);
  LOG_F(1, "data size      : {}", desc.size_bytes);
  LOG_F(2, "element format : {}", nostd::to_string(buf_format));
  LOG_F(2, "usage flags    : {}", nostd::to_string(flags));
  LOG_F(2, "element stride : {}", desc.element_stride);

  // Construct BufferResource using the new struct-based constructor
  // Note: In offline mode, we skip any GPU resource creation
  return std::make_unique<data::BufferResource>(desc);
}

} // namespace oxygen::content::loaders
