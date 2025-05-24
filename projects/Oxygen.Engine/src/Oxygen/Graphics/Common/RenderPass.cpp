//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Composition/ObjectMetaData.h>
#include <Oxygen/Graphics/Common/RenderPass.h>

using oxygen::graphics::RenderPass;

RenderPass::RenderPass(const std::string_view name)
{
    AddComponent<oxygen::ObjectMetaData>(name);
}

auto RenderPass::GetName() const noexcept -> std::string_view
{
    return GetComponent<oxygen::ObjectMetaData>().GetName();
}

void RenderPass::SetName(std::string_view name) noexcept
{
    GetComponent<oxygen::ObjectMetaData>().SetName(name);
}
