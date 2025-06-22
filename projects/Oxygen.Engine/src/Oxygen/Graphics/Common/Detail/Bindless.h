//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string_view>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/ComponentMacros.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Composition/ObjectMetaData.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>

namespace oxygen::graphics::detail {

class Bindless final : public Component {
  OXYGEN_COMPONENT(Bindless)
  OXYGEN_COMPONENT_REQUIRES(oxygen::ObjectMetaData)

public:
  explicit Bindless(std::unique_ptr<DescriptorAllocator> allocator)
    : allocator_(std::move(allocator))
  {
    CHECK_NOTNULL_F(allocator_, "Allocator must not be null");
  }

  ~Bindless() override = default;

  OXYGEN_MAKE_NON_COPYABLE(Bindless)
  OXYGEN_DEFAULT_MOVABLE(Bindless)

  [[nodiscard]] auto GetAllocator() const -> const DescriptorAllocator&
  {
    return *allocator_;
  }
  [[nodiscard]] auto GetAllocator() -> DescriptorAllocator&
  {
    return *allocator_;
  }

  [[nodiscard]] auto GetRegistry() const -> const ResourceRegistry&
  {
    return *registry_;
  }
  [[nodiscard]] auto GetRegistry() -> ResourceRegistry& { return *registry_; }

protected:
  void UpdateDependencies(
    const std::function<Component&(TypeId)>& get_component) override
  {
    const auto& meta_data = static_cast<ObjectMetaData&>(
      get_component(ObjectMetaData::ClassTypeId()));
    LOG_SCOPE_F(INFO, "Bindless component init");
    registry_ = std::make_unique<ResourceRegistry>(meta_data.GetName());
  }

private:
  std::unique_ptr<DescriptorAllocator> allocator_;
  std::unique_ptr<ResourceRegistry> registry_ {};
};

} // namespace oxygen::graphics::detail
