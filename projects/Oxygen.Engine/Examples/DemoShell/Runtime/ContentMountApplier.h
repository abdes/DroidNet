//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <Oxygen/Content/ContentMounts.h>
#include <Oxygen/Content/DependencyRequirementError.h>
#include <Oxygen/OxCo/Co.h>

namespace oxygen::content {
class IAssetLoader;
} // namespace oxygen::content

namespace oxygen::examples::runtime {

//! A pak the loader refused to mount: what it requires is not met below it.
struct RefusedMount final {
  std::filesystem::path pak;
  std::vector<content::FailedRequirement> failures;
  //! The refusal's message, which names the pak and every failed requirement.
  std::string message;
};

//! What applying a mount list did.
struct MountOutcome final {
  //! The mounts committed, lowest precedence first; the engine pak stays
  //! beneath them.
  std::vector<content::ContentMount> mounted;
  //! The paks left out because their requirements failed.
  std::vector<RefusedMount> refused;
  //! Why no set was committed; the mounted layers are then unchanged.
  std::optional<std::string> error;
  //! True when the set cannot be prepared now (a generation is being written
  //! or the mounts changed meanwhile); the caller applies it again later.
  bool retry { false };
};

//! Replaces the loader's mounts with `mounts`, lowest precedence first.
/*!
 This is the content panel's one atomic path: the whole set is prepared off
 thread and committed together. A pak whose requirements fail is left out and
 the set is prepared again without it, so the others still mount. The caller
 prevents new loads until the coroutine completes.
*/
auto ApplyContentMounts(content::IAssetLoader& loader,
  std::vector<content::ContentMount> mounts) -> co::Co<MountOutcome>;

//! A model import's new generation and the one it replaces.
struct GenerationPublication final {
  std::filesystem::path cooked_root;
  std::optional<std::filesystem::path> previous_root;
};

//! Mounts the generation `publication` names at the precedence of the
//! generation it replaces, and retires the older generations of its model.
/*!
 @throw std::system_error `std::errc::device_or_resource_busy` while the
   generation's writer still holds it; the caller retries later.
*/
auto MountGeneration(content::IAssetLoader& loader,
  const GenerationPublication& publication) -> void;

} // namespace oxygen::examples::runtime
