//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <string>

#include <fmt/format.h>

#include <Oxygen/Core/Types/ShaderRequestRules.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Data/CookedOpenError.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/ShaderReference.h>

using oxygen::data::ShaderReference;

namespace {

using oxygen::data::CookedDefect;
using oxygen::data::CookedOpenError;

//! The string at `offset` of `strings`; `field` names it in the refusal.
auto StringAt(const oxygen::data::DescriptorStrings& strings,
  const oxygen::data::pak::core::StringTableOffsetT offset, const char* field)
  -> std::string
{
  const auto value = strings.TryGet(offset);
  if (!value) {
    throw CookedOpenError(CookedDefect::kOutOfBounds,
      fmt::format(
        "shader {} offset {} is outside the string table", field, offset));
  }
  return std::string { *value };
}

//! The shader type `value`, which must name a pipeline stage.
auto ShaderTypeOf(const uint8_t value) -> oxygen::ShaderType
{
  if (!oxygen::IsShaderStage(value)) {
    throw CookedOpenError(CookedDefect::kInvalidValue,
      fmt::format("shader type {} is unknown", value));
  }
  return static_cast<oxygen::ShaderType>(value);
}

} // namespace

ShaderReference::ShaderReference(const pak::render::ShaderReferenceDesc& desc,
  const DescriptorStrings& strings)
  : shader_type_(ShaderTypeOf(desc.shader_type))
  , source_path_(StringAt(strings, desc.source_path_offset, "source path"))
  , entry_point_(StringAt(strings, desc.entry_point_offset, "entry point"))
  , defines_(StringAt(strings, desc.defines_offset, "defines"))
  , shader_hash_(desc.shader_hash)
{
  if (CanonicalShaderSourcePath(source_path_) != source_path_) {
    throw CookedOpenError(CookedDefect::kInvalidPath,
      fmt::format("shader source path '{}' is not canonical", source_path_));
  }
  if (!IsShaderEntryPoint(entry_point_)) {
    throw CookedOpenError(CookedDefect::kInvalidValue,
      fmt::format(
        "shader entry point '{}' is not an identifier", entry_point_));
  }
  const auto defines = CanonicalShaderDefines(defines_);
  if (!defines.has_value()) {
    throw CookedOpenError(CookedDefect::kInvalidValue,
      fmt::format("shader defines '{}' are malformed", defines_));
  }
  if (*defines != defines_) {
    throw CookedOpenError(CookedDefect::kNotCanonical,
      fmt::format("shader defines '{}' are not sorted by name", defines_));
  }
}
