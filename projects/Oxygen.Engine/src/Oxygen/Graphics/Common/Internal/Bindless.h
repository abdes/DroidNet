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
#include <Oxygen/Composition/Component.h>
#include <Oxygen/Composition/ObjectMetaData.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>

namespace oxygen::graphics::internal {

class Bindless final : public Component {
  OXYGEN_COMPONENT(Bindless)
  OXYGEN_COMPONENT_REQUIRES(oxygen::ObjectMetaData)

public:
  // Default-construct with no allocator; backend must set allocator later.
  Bindless() = default;

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
    DCHECK_NOTNULL_F(allocator_, "Bindless allocator not initialized");
    return *allocator_;
  }
  [[nodiscard]] auto GetAllocator() -> DescriptorAllocator&
  {
    DCHECK_NOTNULL_F(allocator_, "Bindless allocator not initialized");
    return *allocator_;
  }

  //! Install the backend-provided descriptor allocator.
  /*!
   Installs the device-level descriptor allocator used by the bindless
   system.

   Contract
   - Must be called exactly once per device (single-assignment).
   - Call after the native graphics device is fully created and before any
     descriptor allocations or calls to @ref GetAllocator().
   - The pointer must be non-null; ownership is transferred to this
     component.
   - Not thread-safe; invoke during single-threaded device initialization.

   Preconditions
   - `allocator != nullptr`
   - No allocator has been installed yet.

   Postconditions
   - Subsequent calls to @ref GetAllocator() are valid.

   Error handling
   - Violations trigger debug checks and terminate in debug builds; no
     exceptions are thrown.

   @param allocator Backend-specific descriptor allocator instance. Ownership
                    is transferred.
   @return void

   @warning Once installed, the allocator cannot be changed.
   @see GetAllocator, ResourceRegistry,
        oxygen::graphics::Graphics::SetDescriptorAllocator
  */
  auto SetAllocator(std::unique_ptr<DescriptorAllocator> allocator) -> void
  {
    CHECK_NOTNULL_F(allocator, "Allocator must not be null");
    CHECK_F(allocator_ == nullptr,
      "Bindless allocator has already been set and cannot be changed");
    allocator_ = std::move(allocator);
  }

  [[nodiscard]] auto GetRegistry() const -> const ResourceRegistry&
  {
    return *registry_;
  }
  [[nodiscard]] auto GetRegistry() -> ResourceRegistry& { return *registry_; }

protected:
  auto UpdateDependencies(
    const std::function<Component&(TypeId)>& get_component) noexcept
    -> void override
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

} // namespace oxygen::graphics::internal
