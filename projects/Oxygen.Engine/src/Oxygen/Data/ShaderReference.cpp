//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <span>

#include <Oxygen/Data/ShaderReference.h>

using oxygen::data::ShaderReference;

namespace {

auto NullTerminatedView(const std::span<const char> buffer) -> std::string_view
{
  const auto it = std::ranges::find(buffer, '\0');
  if (it == buffer.end()) {
    return { buffer.data(), buffer.size() };
  }
  return { buffer.data(), static_cast<size_t>(it - buffer.begin()) };
}

} // namespace

auto ShaderReference::GetSourcePath() const noexcept -> std::string_view
{
  return NullTerminatedView(std::span<const char> { desc_.source_path });
}

auto ShaderReference::GetEntryPoint() const noexcept -> std::string_view
{
  return NullTerminatedView(std::span<const char> { desc_.entry_point });
}

auto ShaderReference::GetDefines() const noexcept -> std::string_view
{
  return NullTerminatedView(std::span<const char> { desc_.defines });
}
