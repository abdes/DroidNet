//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Content/TextureResourceLocator.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/MaterialSlotId.h>
#include <Oxygen/Scene/Types/NodeHandle.h>

namespace oxygen::content {
class IAssetLoader;
class VirtualPathResolver;
} // namespace oxygen::content
namespace oxygen::data {
class GeometryAsset;
class MaterialAsset;
class TextureResource;
} // namespace oxygen::data
namespace oxygen::scene {
class Scene;
}

namespace oxygen::interop::module {
struct CookedRootBinding;

//! New edits validate their observed revision; saved assignments retain identity.
enum class MaterialSlotAssignmentIntent : std::uint8_t {
  kObservedEdit = 0,
  kRetainedAssignment = 1,
};

//! Texture an environment edit binds. Each slot loads independently; the edit
//! applies once every slot has settled.
enum class EnvironmentTextureSlot : std::uint8_t {
  kMeteringMask = 0,
  kSkySphereCubemap = 1,
  kSkyLightCubemap = 2,
};
inline constexpr std::size_t kEnvironmentTextureSlotCount = 3U;

//! Authored source of one environment texture; no locator clears the slot.
struct EnvironmentTextureSource final {
  std::optional<content::TextureResourceLocator> locator;
  //! Project mount whose current cooked root replaces the locator's root.
  std::optional<std::wstring> project_mount;
};

using EnvironmentTextureSources
  = std::array<EnvironmentTextureSource, kEnvironmentTextureSlotCount>;
using EnvironmentTextureKeys
  = std::array<content::ResourceKey, kEnvironmentTextureSlotCount>;

//! Native target captured from the currently observed authored geometry.
struct MaterialSlotTarget final {
  std::string geometry_uri;
  data::MaterialSlotId slot_id;
  base::Sha256Digest layout_revision {};
};

//! Scene-session request authority. Only completion callbacks are thread-safe;
//! all other operations belong to the editor's scene-mutation phase.
class SceneAssetRequests final {
public:
  using Geometry = std::shared_ptr<const data::GeometryAsset>;
  using Material = std::shared_ptr<const data::MaterialAsset>;
  using GeometryCompletion = std::function<void(Geometry, std::string)>;
  using MaterialCompletion = std::function<void(Material, std::string)>;
  using GeometryLoader =
      std::function<void(const std::string &, GeometryCompletion)>;
  using MaterialLoader =
      std::function<void(const std::string &, MaterialCompletion)>;
  using Diagnostic = std::function<void(const std::string &)>;
  using AssetAvailability = std::function<bool(const std::string &, bool)>;
  using Texture = std::shared_ptr<const data::TextureResource>;
  using TextureCompletion = std::function<void(content::ResourceKey, Texture, std::string)>;
  using TextureLoader = std::function<void(const content::TextureResourceLocator&, TextureCompletion)>;
  using EnvironmentApply
    = std::function<void(scene::Scene&, const EnvironmentTextureKeys&)>;
  struct EnvironmentTextureStatus {
    content::ResourceKey accepted {};
    bool pending { false };
    std::string error;
  };
  //! Reports a current failure with the native request generation.
  using FailureCallback =
      std::function<void(uint64_t, const std::string &)>;
  //! Reports application of a current request, including native refresh retries.
  using SuccessCallback = std::function<void(uint64_t)>;

  SceneAssetRequests(content::IAssetLoader &loader,
                     content::VirtualPathResolver &resolver);
  SceneAssetRequests(GeometryLoader geometry_loader,
                     MaterialLoader material_loader, Diagnostic diagnostic,
                     AssetAvailability available, TextureLoader texture_loader);
  ~SceneAssetRequests();

  OXYGEN_MAKE_NON_COPYABLE(SceneAssetRequests)
  OXYGEN_MAKE_NON_MOVABLE(SceneAssetRequests)

  //! Begin before resolving or consulting caches, so failed requests supersede.
  auto BeginGeometry(scene::NodeHandle node, const std::string &uri,
                     FailureCallback on_failure = {}, SuccessCallback on_success = {})
      -> GeometryCompletion;
  void LoadGeometry(const std::string &uri, GeometryCompletion complete);
  void SetMaterial(scene::NodeHandle node, MaterialSlotTarget target,
                   const std::optional<std::string>& uri, MaterialSlotAssignmentIntent intent,
                   FailureCallback on_failure = {},
                   SuccessCallback on_success = {});
  void Detach(scene::NodeHandle node);

  //! Supersede pending environment texture work and apply the complete
  //! revision, with every slot's key, once all slots have loaded. A failed slot
  //! rejects the revision and retains the previously applied one.
  void SetEnvironmentTextures(scene::Scene& scene,
    EnvironmentTextureSources sources, EnvironmentApply apply,
    FailureCallback on_failure = {}, SuccessCallback on_success = {});
  [[nodiscard]] auto InspectEnvironmentTexture(
    EnvironmentTextureSlot slot) const -> EnvironmentTextureStatus;

  //! Accept the same immutable bindings as the native mount owners.
  void SetCookedRoots(
    std::shared_ptr<const std::vector<CookedRootBinding>> roots) noexcept;

  //! Re-request current bindings after mounted sources have been refreshed.
  //! Existing scene objects remain usable until their replacements arrive.
  void Refresh(scene::Scene &scene);
  void SuspendLoads();
  void ResumeLoads(scene::Scene &scene);
  [[nodiscard]] auto IsRefreshPending() const -> bool;
  [[nodiscard]] auto RefreshError() const -> std::string;

  //! Drain after authoring commands. Reject dead targets and obsolete results.
  //! Returns whether a completed load changed the scene.
  auto Drain(scene::Scene &scene) -> bool;

private:
  void QueueMaterial(scene::NodeHandle node, MaterialSlotTarget target,
    const std::optional<std::string>& uri, MaterialSlotAssignmentIntent intent, FailureCallback on_failure,
    SuccessCallback on_success, std::optional<data::AssetKey> accepted_geometry);
  struct State;
  std::unique_ptr<State> state_;
};

} // namespace oxygen::interop::module

#pragma managed(pop)
