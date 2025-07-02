//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <span>

#include <Oxygen/Data/ShaderReference.h>

using oxygen::data::ShaderReference;

/*!
 The shader unique identifier encodes the shader type as the first component
 (before the '@` character), and therefore can be used to infer the shader type
 at runtime.

 @see MakeShaderIdentifier()
*/
auto ShaderReference::GetShaderUniqueId() const noexcept -> std::string_view
{
  auto span = std::span { desc_.shader_unique_id };
  auto it = std::ranges::find(span, '\0');
  if (it == span.end()) {
    // No null terminator found: treat as full buffer, instead of returning an
    // empty string_view. If the unique ID was truncated, and badly formatted,
    // it will create an error somewhere else.
    return { span.data(), span.size() };
  }
  return { span.data(), static_cast<size_t>(it - span.begin()) };
}
