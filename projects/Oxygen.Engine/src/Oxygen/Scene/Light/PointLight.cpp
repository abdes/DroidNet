//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <functional>

#include <Oxygen/Composition/Component.h>
#include <Oxygen/Composition/Typed.h>
#include <Oxygen/Scene/Detail/TransformComponent.h>
#include <Oxygen/Scene/Light/PointLight.h>

namespace oxygen::scene {

inline auto PointLight::UpdateDependencies(
  const std::function<Component&(TypeId)>& get_component) noexcept -> void
{
  transform_ = &static_cast<detail::TransformComponent&>(
    get_component(detail::TransformComponent::ClassTypeId()));
}

} // namespace oxygen::scene
