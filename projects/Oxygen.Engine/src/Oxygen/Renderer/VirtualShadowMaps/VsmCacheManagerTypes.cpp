//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManagerTypes.h>

#include <unordered_set>

namespace {

[[nodiscard]] constexpr auto HasAny(
  const oxygen::renderer::vsm::VsmPageRequestFlags value,
  const oxygen::renderer::vsm::VsmPageRequestFlags mask) noexcept -> bool
{
  return static_cast<
           std::underlying_type_t<oxygen::renderer::vsm::VsmPageRequestFlags>>(
           value & mask)
    != 0;
}

} // namespace

namespace oxygen::renderer::vsm {

auto to_string(const VsmCacheDataState value) noexcept -> std::string_view
{
  // clang-format off
  switch (value) {
  case VsmCacheDataState::kUnavailable: return "Unavailable";
  case VsmCacheDataState::kAvailable: return "Available";
  case VsmCacheDataState::kInvalidated: return "Invalidated";
  }
  // clang-format on

  return "__NotSupported__";
}

auto to_string(const VsmCacheBuildState value) noexcept -> std::string_view
{
  // clang-format off
  switch (value) {
  case VsmCacheBuildState::kIdle: return "Idle";
  case VsmCacheBuildState::kFrameOpen: return "FrameOpen";
  case VsmCacheBuildState::kPlanned: return "Planned";
  case VsmCacheBuildState::kReady: return "Ready";
  }
  // clang-format on

  return "__NotSupported__";
}

auto to_string(const VsmPageRequestFlags value) -> std::string
{
  if (value == VsmPageRequestFlags::kNone) {
    return "None";
  }

  auto text = std::string {};
  const auto append_flag = [&text](const char* flag_name) {
    if (!text.empty()) {
      text.append("|");
    }
    text.append(flag_name);
  };

  if (HasAny(value, VsmPageRequestFlags::kRequired)) {
    append_flag("Required");
  }
  if (HasAny(value, VsmPageRequestFlags::kCoarse)) {
    append_flag("Coarse");
  }
  if (HasAny(value, VsmPageRequestFlags::kStaticOnly)) {
    append_flag("StaticOnly");
  }

  if (text.empty()) {
    return "__NotSupported__";
  }
  return text;
}

auto to_string(const VsmPageRequestValidationResult value) noexcept
  -> std::string_view
{
  // clang-format off
  switch (value) {
  case VsmPageRequestValidationResult::kValid: return "Valid";
  case VsmPageRequestValidationResult::kMissingMapId: return "MissingMapId";
  case VsmPageRequestValidationResult::kMissingFlags: return "MissingFlags";
  }
  // clang-format on

  return "__NotSupported__";
}

auto to_string(const VsmRemapKeyListValidationResult value) noexcept
  -> std::string_view
{
  // clang-format off
  switch (value) {
  case VsmRemapKeyListValidationResult::kValid: return "Valid";
  case VsmRemapKeyListValidationResult::kEmptyList: return "EmptyList";
  case VsmRemapKeyListValidationResult::kEmptyKey: return "EmptyKey";
  case VsmRemapKeyListValidationResult::kDuplicateKey: return "DuplicateKey";
  }
  // clang-format on

  return "__NotSupported__";
}

auto Validate(const VsmPageRequest& request) noexcept
  -> VsmPageRequestValidationResult
{
  if (request.map_id == 0) {
    return VsmPageRequestValidationResult::kMissingMapId;
  }
  if (request.flags == VsmPageRequestFlags::kNone) {
    return VsmPageRequestValidationResult::kMissingFlags;
  }

  return VsmPageRequestValidationResult::kValid;
}

auto IsValid(const VsmPageRequest& request) noexcept -> bool
{
  return Validate(request) == VsmPageRequestValidationResult::kValid;
}

auto Validate(const VsmRemapKeyList& remap_keys) noexcept
  -> VsmRemapKeyListValidationResult
{
  if (remap_keys.empty()) {
    return VsmRemapKeyListValidationResult::kEmptyList;
  }

  auto seen_keys = std::unordered_set<std::string> {};
  seen_keys.reserve(remap_keys.size());
  for (const auto& remap_key : remap_keys) {
    if (remap_key.empty()) {
      return VsmRemapKeyListValidationResult::kEmptyKey;
    }
    if (!seen_keys.insert(remap_key).second) {
      return VsmRemapKeyListValidationResult::kDuplicateKey;
    }
  }

  return VsmRemapKeyListValidationResult::kValid;
}

auto IsValid(const VsmRemapKeyList& remap_keys) noexcept -> bool
{
  return Validate(remap_keys) == VsmRemapKeyListValidationResult::kValid;
}

auto to_string(const VsmCacheInvalidationReason value) noexcept
  -> std::string_view
{
  // clang-format off
  switch (value) {
  case VsmCacheInvalidationReason::kNone: return "None";
  case VsmCacheInvalidationReason::kExplicitReset: return "ExplicitReset";
  case VsmCacheInvalidationReason::kExplicitInvalidateAll: return "ExplicitInvalidateAll";
  case VsmCacheInvalidationReason::kTargetedInvalidate: return "TargetedInvalidate";
  case VsmCacheInvalidationReason::kIncompatiblePool: return "IncompatiblePool";
  case VsmCacheInvalidationReason::kIncompatibleFrameShape: return "IncompatibleFrameShape";
  }
  // clang-format on

  return "__NotSupported__";
}

auto to_string(const VsmCacheInvalidationScope value) noexcept
  -> std::string_view
{
  // clang-format off
  switch (value) {
  case VsmCacheInvalidationScope::kDynamicOnly: return "DynamicOnly";
  case VsmCacheInvalidationScope::kStaticOnly: return "StaticOnly";
  case VsmCacheInvalidationScope::kStaticAndDynamic: return "StaticAndDynamic";
  }
  // clang-format on

  return "__NotSupported__";
}

auto to_string(const VsmLightCacheKind value) noexcept -> std::string_view
{
  // clang-format off
  switch (value) {
  case VsmLightCacheKind::kLocal: return "Local";
  case VsmLightCacheKind::kDirectional: return "Directional";
  }
  // clang-format on

  return "__NotSupported__";
}

auto to_string(const VsmAllocationAction value) noexcept -> std::string_view
{
  // clang-format off
  switch (value) {
  case VsmAllocationAction::kReuseExisting: return "ReuseExisting";
  case VsmAllocationAction::kAllocateNew: return "AllocateNew";
  case VsmAllocationAction::kInitializeOnly: return "InitializeOnly";
  case VsmAllocationAction::kEvict: return "Evict";
  case VsmAllocationAction::kReject: return "Reject";
  }
  // clang-format on

  return "__NotSupported__";
}

auto to_string(const VsmAllocationFailureReason value) noexcept
  -> std::string_view
{
  // clang-format off
  switch (value) {
  case VsmAllocationFailureReason::kNone: return "None";
  case VsmAllocationFailureReason::kInvalidRequest: return "InvalidRequest";
  case VsmAllocationFailureReason::kNoAvailablePhysicalPages: return "NoAvailablePhysicalPages";
  case VsmAllocationFailureReason::kEntryInvalidated: return "EntryInvalidated";
  case VsmAllocationFailureReason::kRemapRejected: return "RemapRejected";
  case VsmAllocationFailureReason::kCacheUnavailable: return "CacheUnavailable";
  }
  // clang-format on

  return "__NotSupported__";
}

auto to_string(const VsmPageInitializationAction value) noexcept
  -> std::string_view
{
  // clang-format off
  switch (value) {
  case VsmPageInitializationAction::kClearDepth: return "ClearDepth";
  case VsmPageInitializationAction::kCopyStaticSlice: return "CopyStaticSlice";
  }
  // clang-format on

  return "__NotSupported__";
}

} // namespace oxygen::renderer::vsm
