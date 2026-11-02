//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <Oxygen/Core/api_export.h>

namespace oxygen {

//! Whether `name` can name a shader entry point: an ASCII identifier,
//! `[A-Za-z_][A-Za-z0-9_]*`.
OXGN_CORE_NDAPI auto IsShaderEntryPoint(std::string_view name) noexcept -> bool;

//! Whether `value` is a shader type that names a pipeline stage: a defined
//! ShaderType other than `kUnknown`.
OXGN_CORE_NDAPI auto IsShaderStage(std::uint32_t value) noexcept -> bool;

//! Whether `name` can name a shader define: `[A-Z][A-Z0-9_]*`.
OXGN_CORE_NDAPI auto IsShaderDefineName(std::string_view name) noexcept -> bool;

//! Whether `value` can be a shader define's value: it holds no whitespace.
OXGN_CORE_NDAPI auto IsShaderDefineValue(std::string_view value) noexcept
  -> bool;

//! The canonical form of a shader defines string, if it has one.
/*!
 A defines string lists defines separated by commas, each `NAME` or
 `NAME=VALUE`: the name follows IsShaderDefineName and the value, everything
 after the first `=`, follows IsShaderDefineValue. The canonical form is sorted
 by name; an empty string is canonical and defines nothing. A string with a
 malformed entry or a name defined twice has no canonical form.
*/
OXGN_CORE_NDAPI auto CanonicalShaderDefines(std::string_view defines)
  -> std::optional<std::string>;

//! The canonical form of a shader source path, if it has one.
/*!
 A canonical path is relative to the engine shaders directory: its names are
 separated by single forward slashes, with no `.` or `..` name and no trailing
 slash. Backslashes become forward slashes and `name/..` cancels; a path that
 is empty, names a drive, starts at a root or climbs above the shaders
 directory has no canonical form. Names are bytes, so UTF-8 passes through.
*/
OXGN_CORE_NDAPI auto CanonicalShaderSourcePath(std::string_view path)
  -> std::optional<std::string>;

} // namespace oxygen
