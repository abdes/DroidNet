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

  //! Register a material and get its handle.
  /*!
   Performs deduplication - the same material will receive the same handle
   across multiple calls. The material asset must remain valid for the
   lifetime of the registry.

   @param material Shared pointer to the material asset
   @return Stable handle for this material
   */
  OXGN_RNDR_API auto RegisterMaterial(
    std::shared_ptr<const data::MaterialAsset> material) -> MaterialHandle;

  //! Get the handle for a previously registered material.
  /*!
   @param material Raw pointer to the material asset
   @return Handle if the material was registered, std::nullopt otherwise
   */
  [[nodiscard]] OXGN_RNDR_API auto GetHandle(
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

  //! Get the total number of registered materials.
  [[nodiscard]] OXGN_RNDR_API auto GetRegisteredMaterialCount() const
    -> std::size_t;

private:
  std::unordered_map<const data::MaterialAsset*, MaterialHandle>
    material_to_handle_;
  std::vector<std::shared_ptr<const data::MaterialAsset>> materials_;
  MaterialHandle next_handle_ { 0U };
};

} // namespace oxygen::engine::sceneprep
