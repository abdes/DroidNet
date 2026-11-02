//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <optional>
#include <span>

#include <Oxygen/Data/CookedOpenError.h>

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

} // namespace oxygen::cooker::testing
