//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Unreachable.h>
#include <Oxygen/Composition/ComponentMacros.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Composition/api_export.h>

namespace oxygen {

class ObjectMetaData final : public Component {
  OXYGEN_COMPONENT(ObjectMetaData)
public:
  OXGN_COM_API explicit ObjectMetaData(std::string_view name);

  OXGN_COM_API ~ObjectMetaData() override;

  OXYGEN_DEFAULT_COPYABLE(ObjectMetaData)
  OXYGEN_DEFAULT_MOVABLE(ObjectMetaData)

  [[nodiscard]] auto GetName() const noexcept -> std::string_view
  {
    return name_;
  }

  auto SetName(const std::string_view name) noexcept -> void
  {
    try {
      name_ = name;
    } catch (...) {
      // Setting the name should not throw, unless not enough memory to
      // allocate the new name. In such case, we can do nothing about it
      // except aborting.
      Unreachable();
    }
  }

  [[nodiscard]] auto IsCloneable() const noexcept -> bool override
  {
    return true;
  }

  [[nodiscard]] auto Clone() const -> std::unique_ptr<Component> override
  {
    return std::make_unique<ObjectMetaData>(this->name_);
  }

private:
  std::string name_;
};

static_assert(IsComponent<ObjectMetaData>);

} // namespace oxygen
