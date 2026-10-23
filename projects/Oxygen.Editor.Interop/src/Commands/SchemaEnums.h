//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause.
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Types/CameraAspectMode.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Light/LightCommon.h>

namespace oxygen::interop::module {

  // Typed conversions of the int32 wire value of a schema enumeration field.
  //
  // The schema owns each enumeration and its members' wire values
  // (`Generated.SceneComponents.h`). A native enumeration an applier writes is
  // bound here, member for member, to its schema enumeration; a schema change
  // that breaks the binding fails to compile. A wire value converts only when
  // it names a member, never by a cast.

  using data::schema::ShadowSource;
  using data::schema::VisibilitySource;

  namespace detail {

    //! Whether each `(native, schema)` pair has the wire value at its position
    //! in `wire`, and the pairs cover every schema member.
    template <typename TNative, typename TSchema, std::size_t N, std::size_t M>
    [[nodiscard]] consteval auto BindsMembers(
      const std::array<std::pair<TNative, TSchema>, N>& pairs,
      const std::array<std::uint8_t, M>& wire) -> bool
    {
      static_assert(std::is_same_v<std::underlying_type_t<TNative>,
        std::underlying_type_t<TSchema>>);
      if (N != M) {
        return false;
      }
      for (std::size_t i = 0; i < N; ++i) {
        if (static_cast<std::underlying_type_t<TNative>>(pairs[i].first)
            != wire[i]
          || static_cast<std::underlying_type_t<TSchema>>(pairs[i].second)
            != wire[i]) {
          return false;
        }
      }
      return true;
    }

    //! The member of `TEnum` whose wire value is `wire`, or nullopt when
    //! `members` has none.
    template <typename TEnum, std::size_t N>
    [[nodiscard]] constexpr auto ToMember(
      const std::array<std::uint8_t, N>& members, const std::int32_t wire)
      noexcept -> std::optional<TEnum>
    {
      for (const auto member : members) {
        if (std::cmp_equal(member, wire)) {
          return static_cast<TEnum>(member);
        }
      }
      return std::nullopt;
    }

  } // namespace detail

  static_assert(detail::BindsMembers(
    std::array {
      std::pair { CameraAspectMode::kAuto,
        data::schema::CameraAspectMode::kAuto },
      std::pair { CameraAspectMode::kFixed,
        data::schema::CameraAspectMode::kFixed },
    },
    data::schema::kCameraAspectModeMembers));

  static_assert(detail::BindsMembers(
    std::array {
      std::pair { scene::ShadowResolutionHint::kLow,
        data::schema::ShadowResolutionHint::kLow },
      std::pair { scene::ShadowResolutionHint::kMedium,
        data::schema::ShadowResolutionHint::kMedium },
      std::pair { scene::ShadowResolutionHint::kHigh,
        data::schema::ShadowResolutionHint::kHigh },
      std::pair { scene::ShadowResolutionHint::kUltra,
        data::schema::ShadowResolutionHint::kUltra },
    },
    data::schema::kShadowResolutionHintMembers));

  static_assert(detail::BindsMembers(
    std::array {
      std::pair { scene::AtmosphereLightSlot::kNone,
        data::schema::AtmosphereLightSlot::kNone },
      std::pair { scene::AtmosphereLightSlot::kPrimary,
        data::schema::AtmosphereLightSlot::kPrimary },
      std::pair { scene::AtmosphereLightSlot::kSecondary,
        data::schema::AtmosphereLightSlot::kSecondary },
    },
    data::schema::kAtmosphereLightSlotMembers));

  static_assert(detail::BindsMembers(
    std::array {
      std::pair { scene::DirectionalCsmSplitMode::kGenerated,
        data::schema::DirectionalCsmSplitMode::kGenerated },
      std::pair { scene::DirectionalCsmSplitMode::kManualDistances,
        data::schema::DirectionalCsmSplitMode::kManualDistances },
    },
    data::schema::kDirectionalCsmSplitModeMembers));

  //! The camera aspect mode `wire` names, or nullopt.
  [[nodiscard]] constexpr auto ToCameraAspectMode(const std::int32_t wire)
    noexcept -> std::optional<CameraAspectMode>
  {
    return detail::ToMember<CameraAspectMode>(
      data::schema::kCameraAspectModeMembers, wire);
  }

  //! The shadow resolution hint `wire` names, or nullopt.
  [[nodiscard]] constexpr auto ToShadowResolutionHint(const std::int32_t wire)
    noexcept -> std::optional<scene::ShadowResolutionHint>
  {
    return detail::ToMember<scene::ShadowResolutionHint>(
      data::schema::kShadowResolutionHintMembers, wire);
  }

  //! The atmosphere light slot `wire` names, or nullopt.
  [[nodiscard]] constexpr auto ToAtmosphereLightSlot(const std::int32_t wire)
    noexcept -> std::optional<scene::AtmosphereLightSlot>
  {
    return detail::ToMember<scene::AtmosphereLightSlot>(
      data::schema::kAtmosphereLightSlotMembers, wire);
  }

  //! The cascade split mode `wire` names, or nullopt.
  [[nodiscard]] constexpr auto ToDirectionalCsmSplitMode(
    const std::int32_t wire) noexcept
    -> std::optional<scene::DirectionalCsmSplitMode>
  {
    return detail::ToMember<scene::DirectionalCsmSplitMode>(
      data::schema::kDirectionalCsmSplitModeMembers, wire);
  }

  //! The visibility source `wire` names, or nullopt.
  [[nodiscard]] constexpr auto ToVisibilitySource(const std::int32_t wire)
    noexcept -> std::optional<VisibilitySource>
  {
    return detail::ToMember<VisibilitySource>(
      data::schema::kVisibilitySourceMembers, wire);
  }

  //! The shadow source `wire` names, or nullopt.
  [[nodiscard]] constexpr auto ToShadowSource(const std::int32_t wire) noexcept
    -> std::optional<ShadowSource>
  {
    return detail::ToMember<ShadowSource>(
      data::schema::kShadowSourceMembers, wire);
  }

  //! The member of a wire value already validated against its field
  //! (`IsInRange`); an absent member is a broken caller contract.
  template <typename TEnum>
  [[nodiscard]] auto ValidatedMember(const std::optional<TEnum> member) -> TEnum
  {
    DCHECK_F(member.has_value(),
      "an enumeration value was converted before its field was validated");
    return member.value();
  }

} // namespace oxygen::interop::module

#pragma managed(pop)
