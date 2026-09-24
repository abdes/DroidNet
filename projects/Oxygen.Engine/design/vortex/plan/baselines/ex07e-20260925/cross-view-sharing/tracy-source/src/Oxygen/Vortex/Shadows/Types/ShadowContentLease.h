//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at <https://opensource.org/licenses/BSD-3-Clause>.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
#pragma once
#include <expected>
#include <memory>
#include <span>

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Registration.h>
#include <Oxygen/Vortex/Shadows/Types/ShadowUseError.h>
#include <Oxygen/Vortex/api_export.h>
namespace oxygen::graphics {
class CommandRecorder;
class Texture;
class ResourceRegistry;
}
namespace oxygen::vortex {
class ShadowService;
namespace shadows::internal {
  struct ShadowMapOwner;
}
//! Retains immutable local-map content, independently of transient frame
//! bindings.
class ShadowContentLease final {
public:
  ShadowContentLease() = default;
  [[nodiscard]] explicit operator bool() const noexcept
  {
    return state_ != nullptr;
  }
  OXGN_VRTX_API auto Attach(graphics::CommandRecorder& recorder,
    graphics::ResourceRegistry& registry) const
    -> std::expected<void, ShadowUseError>;
  //! Exclusive state-changing capture on the graphics queue; restore SRV state
  //! before submit.
  OXGN_VRTX_API auto AttachReadback(graphics::CommandRecorder& recorder,
    graphics::ResourceRegistry& registry) const
    -> std::expected<void, ShadowUseError>;
  [[nodiscard]] OXGN_VRTX_API auto Texture() const
    -> std::shared_ptr<const graphics::Texture>;
  [[nodiscard]] OXGN_VRTX_API auto FirstLayer() const -> std::uint32_t;

private:
  friend class ShadowService;
  struct State;
  OXGN_VRTX_API ShadowContentLease(
    std::shared_ptr<shadows::internal::ShadowMapOwner> map,
    graphics::RegistrationLease registration);
  std::shared_ptr<State> state_;
};
struct ShadowReadAttachmentStats {
  size_t map_uses { 0 };
  size_t backing_uses { 0 };
};
//! Current publication permission. Closing it revokes new frame attachments.
class ShadowFrameReadSet final {
public:
  OXGN_VRTX_API ShadowFrameReadSet(frame::SequenceNumber frame,
    std::uint64_t preparation_revision, ShaderVisibleIndex binding,
    std::span<const std::shared_ptr<shadows::internal::ShadowMapOwner>> maps);
  OXGN_VRTX_API ~ShadowFrameReadSet();
  OXGN_VRTX_API auto Close() noexcept -> void;
  OXGN_VRTX_API auto Attach(frame::SequenceNumber frame,
    std::uint64_t preparation_revision, graphics::CommandRecorder& recorder,
    graphics::ResourceRegistry& registry) const
    -> std::expected<ShadowReadAttachmentStats, ShadowUseError>;
  [[nodiscard]] OXGN_VRTX_API auto Binding() const noexcept
    -> ShaderVisibleIndex;

private:
  struct State;
  std::shared_ptr<State> state_;
};
}
