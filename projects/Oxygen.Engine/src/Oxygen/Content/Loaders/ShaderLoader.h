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
#include <Oxygen/Data/ShaderAsset.h>
#include <Oxygen/Data/ShaderType.h>

namespace oxygen::content::loaders {

//! Loader for shader assets.
template <oxygen::serio::Stream S>
auto LoadShaderAsset(oxygen::serio::Reader<S> reader)
  -> std::unique_ptr<data::ShaderAsset>
{
  LOG_SCOPE_FUNCTION(INFO);

  auto check_result = [](auto&& result, const char* field) {
    if (!result) {
      LOG_F(INFO, "-failed- on {}: {}", field, result.error().message());
      throw std::runtime_error(
        fmt::format("error reading shader asset ({}): {}", field,
          result.error().message()));
    }
  };

  // Read shader_type
  auto shader_type_result = reader.read<uint32_t>();
  check_result(shader_type_result, "shader type");
  auto shader_type = static_cast<data::ShaderType>(*shader_type_result);
  LOG_F(INFO, "shader type: {}", nostd::to_string(shader_type));

  // Read shader_name (length-prefixed)
  auto name_result = reader.read_string();
  check_result(name_result, "shader name");
  LOG_F(INFO, "shader name: {}", name_result.value());

  // Construct ShaderAsset using the new constructor
  return std::make_unique<data::ShaderAsset>(
    shader_type, std::move(*name_result));
}

} // namespace oxygen::content::loaders
