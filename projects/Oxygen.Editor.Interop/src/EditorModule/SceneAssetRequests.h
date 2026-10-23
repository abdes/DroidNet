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
#include <variant>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Scene/Types/NodeHandle.h>
#include <api_export.h>

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

//! New edits validate their observed revision; saved overrides retain identity.
enum class OverrideIntent : std::uint8_t {
  kObservedEdit = 0,
  kRetainedOverride = 1,
};

//! Texture an environment edit binds. Each slot loads independently; the edit
//! applies once every slot has settled.
enum class EnvironmentTextureSlot : std::uint8_t {
  kMeteringMask = 0,
  kSkySphereCubemap = 1,
  kSkyLightCubemap = 2,
};
inline constexpr std::size_t kEnvironmentTextureSlotCount = 3U;

//! Authored source of one environment texture: the virtual path of its cooked
//! texture descriptor. No path clears the slot.
struct EnvironmentTextureSource final {
  std::optional<std::string> virtual_path;
};

using EnvironmentTextureSources
  = std::array<EnvironmentTextureSource, kEnvironmentTextureSlotCount>;
using EnvironmentTextureKeys
  = std::array<content::ResourceKey, kEnvironmentTextureSlotCount>;

//! The part an override set addresses, captured from the currently observed
//! authored geometry: one of its surfaces, or with a nil `surface_id` and a zero
//! `layout_revision` the geometry component itself.
struct OverrideTarget final {
  std::string geometry_uri;
  data::SurfaceId surface_id;
  base::Sha256Digest layout_revision {};
};

//! The virtual path of the asset an override names.
struct OverrideAsset final {
  std::string virtual_path;

  auto operator==(const OverrideAsset&) const -> bool = default;
};

//! An override value, one alternative per value type the schema's override
//! properties use.
using OverrideValue
  = std::variant<bool, OverrideAsset, data::pak::geometry::LodPolicyRecord>;

//! One override: the property its id names and a value of that property's
//! value type.
struct OverrideEntry final {
  data::schema::OverrideId id = data::schema::OverrideId::kInvalid;
  OverrideValue value;
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
  using TextureLoader = std::function<void(const std::string&, TextureCompletion)>;
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

  OXGN_EI_API SceneAssetRequests(content::IAssetLoader &loader,
                     content::VirtualPathResolver &resolver);
  OXGN_EI_API SceneAssetRequests(GeometryLoader geometry_loader,
                     MaterialLoader material_loader, Diagnostic diagnostic,
                     AssetAvailability available, TextureLoader texture_loader);
  OXGN_EI_API ~SceneAssetRequests();

  OXYGEN_MAKE_NON_COPYABLE(SceneAssetRequests)
  OXYGEN_MAKE_NON_MOVABLE(SceneAssetRequests)

  //! Begin before resolving or consulting caches, so failed requests supersede.
  OXGN_EI_API auto BeginGeometry(scene::NodeHandle node, const std::string &uri,
                     FailureCallback on_failure = {}, SuccessCallback on_success = {})
      -> GeometryCompletion;
  OXGN_EI_API void LoadGeometry(const std::string &uri, GeometryCompletion complete);
  //! Sets the whole override state of one part: a property of the part with
  //! no entry takes the geometry's asset value. The entries were validated
  //! against the schema at the command boundary. A material loads first; the
  //! part applies once it has, and again whenever its geometry is replaced.
  OXGN_EI_API void SetOverrides(scene::NodeHandle node, OverrideTarget target,
                   std::vector<OverrideEntry> entries, OverrideIntent intent,
                   FailureCallback on_failure = {},
                   SuccessCallback on_success = {});
  OXGN_EI_API void Detach(scene::NodeHandle node);

  //! Supersede pending environment texture work and apply the complete
  //! revision, with every slot's key, once all slots have loaded. A failed slot
  //! rejects the revision and retains the previously applied one.
  OXGN_EI_API void SetEnvironmentTextures(scene::Scene& scene,
    EnvironmentTextureSources sources, EnvironmentApply apply,
    FailureCallback on_failure = {}, SuccessCallback on_success = {});
  OXGN_EI_NDAPI auto InspectEnvironmentTexture(
    EnvironmentTextureSlot slot) const -> EnvironmentTextureStatus;

  //! Re-request current bindings after mounted sources have been refreshed.
  //! Existing scene objects remain usable until their replacements arrive.
  OXGN_EI_API void Refresh(scene::Scene &scene);
  OXGN_EI_API void SuspendLoads();
  OXGN_EI_API void ResumeLoads(scene::Scene &scene);
  OXGN_EI_NDAPI auto IsRefreshPending() const -> bool;
  OXGN_EI_NDAPI auto RefreshError() const -> std::string;

  //! Drain after authoring commands. Reject dead targets and obsolete results.
  //! Returns whether a completed load changed the scene.
  OXGN_EI_API auto Drain(scene::Scene &scene) -> bool;

private:
  void QueueOverrides(scene::NodeHandle node, OverrideTarget target,
    std::vector<OverrideEntry> entries, OverrideIntent intent,
    FailureCallback on_failure, SuccessCallback on_success,
    std::optional<data::AssetKey> accepted_geometry);
  struct State;
  std::unique_ptr<State> state_;
};

} // namespace oxygen::interop::module

#pragma managed(pop)
