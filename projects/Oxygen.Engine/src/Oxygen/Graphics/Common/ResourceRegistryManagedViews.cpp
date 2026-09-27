//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <expected>
#include <memory>
#include <new>
#include <span>
#include <utility>

#include <Oxygen/Graphics/Common/ManagedResource.h>
#include <Oxygen/Graphics/Common/Registration.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>

namespace oxygen::graphics {

template <typename Bundle, typename Resource, typename Request>
auto ResourceRegistry::RegisterManagedViews(
  std::shared_ptr<Resource> resource, const std::span<const Request> requests)
  -> std::expected<Bundle, RegistrationError>
try {
  if (!resource) {
    return std::unexpected(RegistrationError::kStaleRegistration);
  }
  Bundle result;
  result.views.reserve(requests.size());
  auto registration = RegisterManaged(std::static_pointer_cast<void>(resource),
    Resource::ClassTypeId(), GetBackendResource(*resource),
    ManagedRegistrationMode::kNewOnly);
  if (!registration) {
    return std::unexpected(registration.error());
  }
  for (const auto& request : requests) {
    auto view = AcquireManagedView<Resource>(
      *registration, request.description, request.domain);
    if (!view) {
      return std::unexpected(view.error());
    }
    result.views.push_back(std::move(*view));
  }
  result.resource = std::move(resource);
  result.registration = registration->AllocationOwner();
  return result;
} catch (const std::bad_alloc&) {
  return std::unexpected(RegistrationError::kAllocationFailed);
}

auto ResourceRegistry::RegisterManagedTexture(std::shared_ptr<Texture> resource,
  const std::span<const TextureViewRequest> views)
  -> std::expected<ManagedTexture, RegistrationError>
{
  return RegisterManagedViews<ManagedTexture>(std::move(resource), views);
}

auto ResourceRegistry::RegisterManagedBuffer(std::shared_ptr<Buffer> resource,
  const std::span<const BufferViewRequest> views)
  -> std::expected<ManagedBuffer, RegistrationError>
{
  return RegisterManagedViews<ManagedBuffer>(std::move(resource), views);
}
} // namespace oxygen::graphics
