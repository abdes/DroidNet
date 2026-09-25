//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at <https://opensource.org/licenses/BSD-3-Clause>.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
#pragma once
#include <array>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include <Oxygen/Graphics/Common/RecordingUseBatch.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Nexus/IndexReuse.h>
#include <Oxygen/Vortex/Shadows/Internal/LocalShadowRequest.h>
#include <Oxygen/Vortex/Shadows/Types/ShadowUseError.h>

namespace oxygen::graphics {
class CommandRecorder;
class ResourceRegistry;
}
namespace oxygen::vortex::shadows::internal {
class ShadowUseUnavailable final : public std::exception {
public:
  explicit ShadowUseUnavailable(ShadowUseError error) noexcept
    : error_(error)
  {
  }
  auto Error() const noexcept -> ShadowUseError { return error_; }
  auto what() const noexcept -> const char* override
  {
    return "Shadow use is unavailable";
  }

private:
  ShadowUseError error_;
};
struct ShadowSlotCore;
//! Stable physical-index target. No callback retains the allocator or renderer.
struct ShadowSlotPool final {
  std::mutex mutex;
  nexus::IndexReuse<ShadowSlotIndex> reuse;
  std::vector<std::weak_ptr<ShadowSlotCore>> slots;
  std::vector<ShadowSlotIndex> free;
  bool closed { false };
};
enum class ShadowUseMode : uint8_t { kRead, kWrite, kReadback };
struct SharedShadowBacking final {
  struct QueueUse {
    graphics::QueueIdentity queue { 0 };
    size_t pending_reads { 0 };
    size_t pending_exclusive { 0 };
    size_t submitted { 0 };
    std::optional<graphics::CompletionReceipt> latest;
  };
  mutable std::mutex mutex;
  std::shared_ptr<graphics::Texture> texture;
  graphics::RegistrationOwner registration;
  ShaderVisibleIndex srv { kInvalidShaderVisibleIndex };
  std::vector<graphics::NativeView> dsvs;
  std::vector<QueueUse> queues;
  std::optional<graphics::CompletionReceipt> latest_exclusive;
  uint32_t resolution { 0 };
  uint32_t map_capacity { 0 };
  bool cube { false };
  bool has_submission { false };
  bool quarantined { false };
};
//! Allocation ownership and recording/GPU pins are deliberately separate.
struct ShadowSlotCore final {
  std::weak_ptr<ShadowSlotPool> pool;
  std::shared_ptr<SharedShadowBacking> backing;
  nexus::VersionedIndex<ShadowSlotIndex> handle;
  uint32_t offset { 0 };
  mutable std::mutex mutex;
  size_t owners { 0 };
  size_t uses { 0 };
  std::optional<nexus::RetirementTicket<ShadowSlotIndex>> retirement;
  bool finalized { false };
  ~ShadowSlotCore();
  auto AddOwner() -> void;
  auto ReleaseOwner() noexcept -> void;
  auto AddUse() -> void;
  auto ReleaseUse() noexcept -> void;

private:
  auto RetireAndFinalize() noexcept -> void;
};
struct ShadowMapVersion final {
  enum class State { kRecording, kSubmitted, kInvalid, kQuarantined };
  struct UseContext {
    ShadowMapVersion* version;
    ShadowUseMode mode;
  };
  explicit ShadowMapVersion(
    std::shared_ptr<ShadowSlotCore> allocation, LocalShadowContentKey key);
  std::shared_ptr<ShadowSlotCore> slot;
  const LocalShadowContentKey content;
  State state { State::kRecording }; // protected by backing.mutex
  bool accepting { true };
  size_t read_capabilities { 0 };
  std::optional<graphics::CompletionReceipt> producer;
  std::array<UseContext, 3> use_contexts;
};
struct ShadowMapOwner final {
  explicit ShadowMapOwner(std::shared_ptr<ShadowMapVersion> map);
  ~ShadowMapOwner();
  ShadowMapOwner(const ShadowMapOwner&) = delete;
  auto operator=(const ShadowMapOwner&) -> ShadowMapOwner& = delete;
  const std::shared_ptr<ShadowMapVersion> version;
};
//! A permission to attach a future reader; prevents overwriting this version.
class ShadowReadCapability final {
public:
  ShadowReadCapability() = default;
  explicit ShadowReadCapability(std::shared_ptr<ShadowMapOwner> owner);
  ~ShadowReadCapability();
  ShadowReadCapability(const ShadowReadCapability& other);
  ShadowReadCapability(ShadowReadCapability&&) noexcept = default;
  auto operator=(ShadowReadCapability other) noexcept -> ShadowReadCapability&;
  [[nodiscard]] auto Owner() const -> const std::shared_ptr<ShadowMapOwner>&
  {
    return owner_;
  }

private:
  std::shared_ptr<ShadowMapOwner> owner_;
};
//! Attaches one version; batch deduplication makes repeated stage attachment
//! cheap.
OXGN_VRTX_API auto AttachShadowUse(
  const std::shared_ptr<ShadowMapVersion>& version, ShadowUseMode mode,
  graphics::CommandRecorder& recorder, graphics::ResourceRegistry& registry)
  -> void;
[[nodiscard]] auto CanWriteBacking(const SharedShadowBacking& backing) -> bool;
[[nodiscard]] auto CanReplaceVersion(const ShadowMapVersion& version) -> bool;
}
