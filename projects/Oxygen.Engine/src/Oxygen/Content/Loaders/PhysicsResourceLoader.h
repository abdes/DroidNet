//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Content/LoaderFunctions.h>
#include <Oxygen/Content/Loaders/Helpers.h>
#include <Oxygen/Data/CookedFormatError.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/PakFormatSerioLoaders.h>
#include <Oxygen/Data/PhysicsResource.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Serio/Stream.h>

namespace oxygen::content::loaders {

//! Loads a physics resource from a PAK file stream.
inline auto LoadPhysicsResource(const LoaderContext& context)
  -> std::unique_ptr<data::PhysicsResource>
{
  LOG_SCOPE_F(1, "Load Physics Resource");
  LOG_F(2, "offline mode   : {}", context.work_offline ? "yes" : "no");

  DCHECK_NOTNULL_F(context.desc_reader, "expecting desc_reader not to be null");
  auto& reader = *context.desc_reader;

  using data::pak::physics::PhysicsResourceDesc;

  auto check_result = [](auto&& result, const char* field) -> auto {
    if (!result) {
      LOG_F(
        ERROR, "-failed- on {}: {}", field, result.error().message().c_str());
      throw data::CookedFormatError(data::CookedDefect::kTruncated,
        fmt::format("error reading physics resource ({}): {}", field,
          result.error().message()));
    }
  };

  // Read PhysicsResourceDesc from the stream
  auto pack = reader.ScopedAlignment(1);
  auto result = reader.Read<PhysicsResourceDesc>();
  check_result(result, "PhysicsResourceDesc");
  const auto& desc = result.value();

  LOG_F(1, "data offset    : {}", desc.data_offset);
  LOG_F(1, "data size      : {}", desc.size_bytes);

  auto data_buffer = TakePayload(context, desc.size_bytes, "physics resource");

  return std::make_unique<data::PhysicsResource>(desc, std::move(data_buffer));
}

static_assert(oxygen::content::LoadFunction<decltype(LoadPhysicsResource)>);

} // namespace oxygen::content::loaders
