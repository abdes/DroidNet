//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at <https://opensource.org/licenses/BSD-3-Clause>.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <utility>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Graphics/Common/BackendLifetime.h>
#include <Oxygen/Graphics/Common/NativeObject.h>

namespace oxygen {
class Graphics;
}
namespace oxygen::graphics {
class ResourceRegistry;
namespace detail {
  struct RegistrationCore;
  struct RegistrationAllocationOwner;
  struct ResourceRegistryState;
}

using RegistrationId
  = NamedType<uint64_t, struct RegistrationIdTag, Comparable, Hashable>;
struct RegistrationIdentity {
  BackendIncarnationId backend { 0 };
  RegistrationId registration { 0 };
  auto operator==(const RegistrationIdentity&) const -> bool = default;
};
enum class RegistrationError : uint8_t {
  kClosed,
  kWrongBackend,
  kStaleRegistration,
  kOwnershipConflict,
  kAllocationFailed
};

//! Internal allocation ownership; never retains the Graphics facade.
/*!
 Keep this token in a physical allocation owner, including allocation state that
 may be pinned by a submitted batch. It keeps acquisition open, independently of
 existing use pins. Public consumers retain RegistrationLease instead.
*/
class RegistrationOwner final {
public:
  RegistrationOwner() = default;
  [[nodiscard]] OXGN_GFX_API auto Identity() const noexcept
    -> RegistrationIdentity;
  [[nodiscard]] explicit operator bool() const noexcept
  {
    return owner_ != nullptr;
  }

private:
  friend class ResourceRegistry;
  friend class RegistrationLease;
  explicit RegistrationOwner(
    std::shared_ptr<detail::RegistrationAllocationOwner> owner)
    : owner_(std::move(owner))
  {
  }
  std::shared_ptr<detail::RegistrationAllocationOwner> owner_;
};

//! External allocation ownership plus the canonical backend owner token.
class RegistrationLease final {
public:
  RegistrationLease() = default;
  RegistrationLease(const RegistrationLease&) noexcept = default;
  RegistrationLease(RegistrationLease&&) noexcept = default;
  auto operator=(RegistrationLease other) noexcept -> RegistrationLease&
  {
    backend_owner_.swap(other.backend_owner_);
    std::swap(owner_, other.owner_);
    return *this;
  }
  [[nodiscard]] auto Identity() const noexcept -> RegistrationIdentity
  {
    return owner_.Identity();
  }
  [[nodiscard]] auto AllocationOwner() const noexcept -> RegistrationOwner
  {
    return owner_;
  }
  [[nodiscard]] explicit operator bool() const noexcept
  {
    return static_cast<bool>(owner_);
  }

private:
  friend class ResourceRegistry;
  RegistrationLease(
    std::shared_ptr<Graphics> backend_owner, RegistrationOwner owner)
    : backend_owner_(std::move(backend_owner))
    , owner_(std::move(owner))
  {
  }
  std::shared_ptr<Graphics> backend_owner_;
  RegistrationOwner owner_;
};

//! A previously admitted use; it cannot reopen allocation acquisition.
class RegistrationUse final {
public:
  RegistrationUse() = default;
  OXGN_GFX_API ~RegistrationUse();
  OXGN_GFX_API RegistrationUse(RegistrationUse&&) noexcept;
  OXGN_GFX_API auto operator=(RegistrationUse&&) noexcept -> RegistrationUse&;
  OXYGEN_MAKE_NON_COPYABLE(RegistrationUse)
  [[nodiscard]] OXGN_GFX_API auto CanSubmit() const noexcept -> bool;
  [[nodiscard]] OXGN_GFX_API auto Identity() const noexcept
    -> RegistrationIdentity;
  [[nodiscard]] explicit operator bool() const noexcept
  {
    return core_ != nullptr;
  }

private:
  friend class ResourceRegistry;
  explicit RegistrationUse(
    std::shared_ptr<detail::RegistrationCore> core) noexcept
    : core_(std::move(core))
  {
  }
  auto Reset() noexcept -> void;
  std::shared_ptr<detail::RegistrationCore> core_;
};

struct ManagedView {
  NativeView view;
  ShaderVisibleIndex shader_visible_index { kInvalidShaderVisibleIndex };
};
} // namespace oxygen::graphics
