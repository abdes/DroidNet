//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <concepts>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/StaticVector.h>
#include <Oxygen/Profiling/api_export.h>

namespace oxygen::profiling {

enum class ProfileGranularity : uint8_t {
  kTelemetry,
  kDiagnostic,
};

enum class ProfileCategory : uint8_t {
  kGeneral,
  kPass,
  kCompute,
  kRaster,
  kUpload,
  kSynchronization,
};

template <typename T>
concept ProfileVariableValue = requires(const T& value) {
  { nostd::to_string(value) } -> std::convertible_to<std::string>;
};

struct ScopeVariable {
  observer_ptr<const char> key {};
  std::string value {};

  auto operator==(const ScopeVariable&) const -> bool = default;
};

using ScopeVariables = oxygen::StaticVector<ScopeVariable, 6>;

template <ProfileVariableValue T>
auto Var(const char* key, const T& value) -> ScopeVariable
{
  return ScopeVariable {
    .key = make_observer(key),
    .value = nostd::to_string(value),
  };
}

inline auto Var(const char* key, std::string_view value) -> ScopeVariable
{
  return ScopeVariable {
    .key = make_observer(key),
    .value = std::string(value),
  };
}

inline auto Var(const char* key, const char* value) -> ScopeVariable
{
  return ScopeVariable {
    .key = make_observer(key),
    .value = value != nullptr ? std::string(value) : std::string(),
  };
}

template <typename... TVariables>
  requires(
    (std::same_as<std::remove_cvref_t<TVariables>, ScopeVariable>) && ...)
auto Vars(TVariables&&... vars) -> ScopeVariables
{
  ScopeVariables out {};
  (out.push_back(std::forward<TVariables>(vars)), ...);
  return out;
}

struct ProfileColor {
  uint32_t argb { 0U };

  [[nodiscard]] static constexpr auto Argb(
    uint8_t a, uint8_t r, uint8_t g, uint8_t b) -> ProfileColor
  {
    return ProfileColor {
      .argb = (static_cast<uint32_t>(a) << 24U)
        | (static_cast<uint32_t>(r) << 16U) | (static_cast<uint32_t>(g) << 8U)
        | static_cast<uint32_t>(b),
    };
  }

  [[nodiscard]] static constexpr auto Rgb(uint8_t r, uint8_t g, uint8_t b)
    -> ProfileColor
  {
    return Argb(0xFFU, r, g, b);
  }

  [[nodiscard]] constexpr auto IsSpecified() const -> bool
  {
    return argb != 0U;
  }

  [[nodiscard]] constexpr auto Rgb24() const -> uint32_t
  {
    return IsSpecified() ? (argb & 0x00FFFFFFU) : 0U;
  }

  auto operator==(const ProfileColor&) const -> bool = default;
};

struct ProfileScopeDesc {
  std::string label {};
  ScopeVariables variables {};
  ProfileGranularity granularity { ProfileGranularity::kTelemetry };
  ProfileCategory category { ProfileCategory::kGeneral };
  ProfileColor color {};

  auto operator==(const ProfileScopeDesc&) const -> bool = default;
};

using GpuProfileScopeDesc = ProfileScopeDesc;
using CpuProfileScopeDesc = ProfileScopeDesc;

[[nodiscard]] constexpr auto DefaultProfileColor(ProfileCategory category)
  -> ProfileColor
{
  switch (category) {
  case ProfileCategory::kPass:
    return ProfileColor::Rgb(0x4D, 0x96, 0xFF);
  case ProfileCategory::kCompute:
    return ProfileColor::Rgb(0x00, 0xB8, 0x5C);
  case ProfileCategory::kRaster:
    return ProfileColor::Rgb(0xFF, 0x8C, 0x42);
  case ProfileCategory::kUpload:
    return ProfileColor::Rgb(0x8A, 0x63, 0xD2);
  case ProfileCategory::kSynchronization:
    return ProfileColor::Rgb(0xE0, 0x3E, 0x36);
  case ProfileCategory::kGeneral:
  default:
    return {};
  }
}

OXGN_PROF_NDAPI auto FormatScopeName(const ProfileScopeDesc& desc)
  -> std::string;

} // namespace oxygen::profiling
