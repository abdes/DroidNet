//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Composition/ObjectMetaData.h>
#include <Oxygen/Renderer/RenderPass.h>

using oxygen::engine::RenderPass;

RenderPass::RenderPass(const std::string_view name)
{
  AddComponent<ObjectMetaData>(name);
}

auto RenderPass::GetName() const noexcept -> std::string_view
{
  return GetComponent<ObjectMetaData>().GetName();
}

auto RenderPass::SetName(std::string_view name) noexcept -> void
{
  GetComponent<ObjectMetaData>().SetName(name);
}
