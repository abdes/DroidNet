//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <cstddef>
#include <functional>
#include <memory>
#include <string>

#include <Oxygen/Scene/Types/NodeHandle.h>

namespace oxygen::content {
class IAssetLoader;
class VirtualPathResolver;
} // namespace oxygen::content
namespace oxygen::data {
class GeometryAsset;
class MaterialAsset;
} // namespace oxygen::data
namespace oxygen::scene {
class Scene;
}

namespace oxygen::interop::module {

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

  SceneAssetRequests(content::IAssetLoader &loader,
                     content::VirtualPathResolver &resolver);
  SceneAssetRequests(GeometryLoader geometry_loader,
                     MaterialLoader material_loader, Diagnostic diagnostic);
  ~SceneAssetRequests();

  SceneAssetRequests(const SceneAssetRequests &) = delete;
  auto operator=(const SceneAssetRequests &) -> SceneAssetRequests & = delete;

  //! Begin before resolving or consulting caches, so failed requests supersede.
  auto BeginGeometry(scene::NodeHandle node, const std::string &uri)
      -> GeometryCompletion;
  void LoadGeometry(const std::string &uri, GeometryCompletion complete);
  void SetMaterial(scene::NodeHandle node, std::size_t slot,
                   const std::string &uri);
  void Detach(scene::NodeHandle node);

  //! Drain after authoring commands. Reject dead targets and obsolete results.
  void Drain(scene::Scene &scene);

private:
  struct State;
  std::unique_ptr<State> state_;
};

} // namespace oxygen::interop::module

#pragma managed(pop)
