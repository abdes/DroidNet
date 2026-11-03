//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <optional>
#include <span>

#include <Oxygen/Content/ContentLoadError.h>
#include <Oxygen/Data/CookedFormatError.h>
#include <Oxygen/Data/PakFormat_core.h>

namespace oxygen::cooker::testing {

//! The defect the product refuses the scene descriptor `bytes` with.
/*!
 Reads `bytes` with the scene reader (`data::SceneAsset`) and with the
 scene loader, which a mount uses, so a test of a rule covers both.

 ```cpp
 auto bytes = scene.Bytes();
 DescriptorDefects::DropEnvironmentBlock(bytes);
 EXPECT_EQ(SceneRefusal(bytes), CookedDefect::kMissingPart);
 ```

 @return No defect when both accept the scene.
 @throw std::logic_error When one refuses without a defect, or the two
   disagree.
*/
[[nodiscard]] auto SceneRefusal(std::span<const std::byte> bytes)
  -> std::optional<data::CookedDefect>;

//! The defect the product refuses the physics sidecar descriptor `bytes`
//! with.
/*!
 Reads `bytes` with the sidecar reader (`data::PhysicsSceneAsset`) and with
 the sidecar loader, which a mount uses, so a test of a rule covers both.

 @return No defect when both accept the sidecar.
 @throw std::logic_error When one refuses without a defect, or the two
   disagree.
*/
[[nodiscard]] auto SidecarRefusal(std::span<const std::byte> bytes)
  -> std::optional<data::CookedDefect>;

/*!
 Reads the geometry descriptor `bytes` with the geometry loader, which a
 mount uses; the meshes' buffers are bound after it, and checked then.

 @return No defect when the loader accepts the geometry.
 @throw std::logic_error When the loader refuses without a defect.
*/
[[nodiscard]] auto GeometryRefusal(std::span<const std::byte> bytes)
  -> std::optional<data::CookedDefect>;

//! The rule of the engine the geometry loader refuses the geometry
//! descriptor `bytes` for, when they follow their format.
/*!
 @return No failure when the loader accepts the geometry.
 @throw std::logic_error When the loader refuses for another reason, such as
   a defect of the format.
*/
[[nodiscard]] auto GeometryRuleRefusal(std::span<const std::byte> bytes)
  -> std::optional<content::ContentLoadFailure>;

//! The defect the product refuses the material descriptor `bytes` with.
/*!
 Reads `bytes` with the material loader, which a mount uses.

 @return No defect when the loader accepts the material.
 @throw std::logic_error When the loader refuses without a defect.
*/
[[nodiscard]] auto MaterialRefusal(std::span<const std::byte> bytes)
  -> std::optional<data::CookedDefect>;

//! The defect the product refuses the texture row `desc` and its `payload`
//! with.
/*!
 Reads them with the texture loader, which a mount uses, as it reads them:
 the row from the descriptor stream, the payload already read in full.

 @return No defect when the loader accepts the texture.
 @throw std::logic_error When the loader refuses without a defect.
*/
[[nodiscard]] auto TextureRefusal(
  const data::pak::core::TextureResourceDesc& desc,
  std::span<const std::byte> payload) -> std::optional<data::CookedDefect>;

//! The defect the product refuses the buffer row `desc` and its `payload`
//! with, read as `TextureRefusal` reads a texture.
/*!
 @return No defect when the loader accepts the buffer.
 @throw std::logic_error When the loader refuses without a defect.
*/
[[nodiscard]] auto BufferRefusal(
  const data::pak::core::BufferResourceDesc& desc,
  std::span<const std::byte> payload) -> std::optional<data::CookedDefect>;

} // namespace oxygen::cooker::testing
