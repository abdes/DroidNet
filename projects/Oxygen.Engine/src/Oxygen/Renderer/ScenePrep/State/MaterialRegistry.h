//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Renderer/ScenePrep/Types.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::data {
class MaterialAsset;
} // namespace oxygen::data

namespace oxygen::engine::sceneprep {

//! Persistent material registry with deduplication.
/*!
 Manages material handle allocation across frames with deduplication.
 Materials are registered once and receive stable handles that can
 be reused across multiple frames for consistent referencing.
 */
class MaterialRegistry {
public:
  OXGN_RNDR_API MaterialRegistry() = default;
  OXGN_RNDR_API ~MaterialRegistry() = default;

  OXYGEN_MAKE_NON_COPYABLE(MaterialRegistry)
  OXYGEN_DEFAULT_MOVABLE(MaterialRegistry)

  //! Deprecated: use GetOrRegisterMaterial.
  /*! Kept for transitional compatibility. */
  OXGN_RNDR_API auto RegisterMaterial(
    std::shared_ptr<const data::MaterialAsset> material) -> MaterialHandle;

  //! Get (or register) a material and return its stable handle.
  /*!
   Idempotent: returns existing handle on subsequent calls with the same
   underlying asset pointer. Accepts nullptr and returns the sentinel
   handle (value 0) in that case.

   @param material Shared pointer to the material asset (may be nullptr)
   @return Stable handle (sentinel 0 when material is nullptr)

  ### Performance Characteristics

  - Time Complexity: Expected O(1) average (unordered_map lookup)
  - Memory: Single entry in map + vector slot on first registration
  - Optimization: Null and already-registered fast paths

  @see LookupMaterialHandle, GetMaterial, IsSentinelHandle
   */
  OXGN_RNDR_API auto GetOrRegisterMaterial(
    std::shared_ptr<const data::MaterialAsset> material) -> MaterialHandle;

  //! Get the handle for a previously registered material.
  /*!
   @param material Raw pointer to the material asset
   @return Handle if the material was registered, std::nullopt otherwise
   */
  [[nodiscard]] OXGN_RNDR_API auto GetHandle(
    const data::MaterialAsset* material) const -> std::optional<MaterialHandle>;

  //! Lookup a material handle without registration side-effects.
  /*! Wrapper synonym for GetHandle for naming consistency.

   @param material Raw pointer (may be nullptr)
   @return Handle if registered, std::nullopt otherwise
   */
  [[nodiscard]] OXGN_RNDR_API auto LookupMaterialHandle(
    const data::MaterialAsset* material) const -> std::optional<MaterialHandle>;

  //! Get the material asset for a given handle.
  /*!
   @param handle The material handle
   @return Shared pointer to the material, or nullptr if handle is invalid
   */
  [[nodiscard]] OXGN_RNDR_API auto GetMaterial(MaterialHandle handle) const
    -> std::shared_ptr<const data::MaterialAsset>;

  //! Check if a handle is valid.
  [[nodiscard]] OXGN_RNDR_API auto IsValidHandle(MaterialHandle handle) const
    -> bool;

  //! Check if a handle is the sentinel (null) handle.
  [[nodiscard]] static constexpr auto IsSentinelHandle(
    MaterialHandle handle) noexcept -> bool
  {
    return handle.get() == 0U;
  }

  //! Get the total number of registered materials.
  [[nodiscard]] OXGN_RNDR_API auto GetRegisteredMaterialCount() const
    -> std::size_t;

private:
  std::unordered_map<const data::MaterialAsset*, MaterialHandle>
    material_to_handle_;
  std::vector<std::shared_ptr<const data::MaterialAsset>> materials_;
  // Next handle to assign. 0 is reserved as sentinel (null material).
  MaterialHandle next_handle_ { 1U };
};

} // namespace oxygen::engine::sceneprep
