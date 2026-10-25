//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/SceneComposition.h>
#include <Oxygen/Data/SceneIds.h>
#include <Oxygen/Data/Schema/Generated.SceneComponents.h>
#include <Oxygen/Data/SurfaceId.h>
#include <Oxygen/Scene/Types/NodeHandle.h>

#include <EditorModule/SceneAssetRequests.h>
#include <api_export.h>

namespace oxygen::interop::module {

//! A material a change names, by the virtual path of its cooked asset.
struct InstanceMaterial final {
  std::string virtual_path;

  auto operator==(const InstanceMaterial&) const -> bool = default;
};

//! A change's value in the schema's units; a material by its path.
using InstanceValue = std::variant<bool, std::int32_t, float,
  std::array<float, 3>, InstanceMaterial,
  data::pak::geometry::LodPolicyRecord>;

//! A property change the editor makes of a node an instance brings in.
struct HostInstanceChange final {
  //! One node ID per nesting level below the instance root, shallowest first.
  std::vector<data::NodeId> path;
  data::schema::OverrideId override_id = data::schema::OverrideId::kInvalid;
  //! The surface, for a surface property.
  data::SurfaceId part_id;
  InstanceValue value;

  //! The address the change sets.
  OXGN_EI_NDAPI auto Address() const -> data::ChangeAddress;
};

//! The instances the editor builds in its scene, and the order in which their
//! operations reach the engine.
/*!
 Each instance root keeps the request it was made with: its source, by the
 virtual path of the cooked scene, and every change set since. A request whose
 build failed stays, so mounting the content again builds it. Operations run
 in the order they were requested; a change set before its instance is built
 goes into the build instead. Every outcome reaches the observers the request
 was made with, with a generation that grows in execution order; an outcome
 of an operation older than the instance's last build is dropped.
 All operations belong to the editor's scene-mutation phase.
*/
class SceneInstances final {
public:
  using FailureCallback = SceneAssetRequests::FailureCallback;
  using SuccessCallback = SceneAssetRequests::SuccessCallback;

  //! Builds the instance of `root` from its request, replacing one it has.
  struct BuildOperation {
    scene::NodeHandle root;
    std::string source;
    std::vector<HostInstanceChange> changes;
    std::uint64_t generation = 0;
  };

  //! Sets one change of the built instance of `root`.
  struct SetOperation {
    scene::NodeHandle root;
    HostInstanceChange change;
    std::uint64_t generation = 0;
  };

  //! Clears one change of the built instance of `root`.
  struct ClearOperation {
    scene::NodeHandle root;
    data::ChangeAddress address;
    std::uint64_t generation = 0;
  };

  //! Removes the instance of `root`; its owned children stay.
  struct DetachOperation {
    scene::NodeHandle root;
  };

  using Operation = std::variant<BuildOperation, SetOperation, ClearOperation,
    DetachOperation>;

  //! Instances `source` under `root` with `changes`, replacing the request
  //! `root` had.
  OXGN_EI_API void Instantiate(scene::NodeHandle root, std::string source,
    std::vector<HostInstanceChange> changes, FailureCallback on_failure,
    SuccessCallback on_success);

  //! Sets a change of the instance of `root`, replacing the one with its
  //! address. A root with no request is ignored.
  OXGN_EI_API void SetChange(scene::NodeHandle root, HostInstanceChange change);

  //! Clears the change of the instance of `root` with `address`.
  OXGN_EI_API void ClearChange(
    scene::NodeHandle root, const data::ChangeAddress& address);

  //! Removes the instance of `root` and forgets its request.
  OXGN_EI_API void Detach(scene::NodeHandle root);

  //! Builds every instance again, after the mounted content changed.
  OXGN_EI_API void Refresh();

  //! Forgets the request of a root that no longer exists.
  OXGN_EI_API void Forget(scene::NodeHandle root);

  //! The operations to run now, in request order; each takes the next
  //! generation.
  OXGN_EI_NDAPI auto TakeOperations() -> std::vector<Operation>;

  //! Reports the outcome of the operation of `generation` on `root`: no
  //! error is a success.
  OXGN_EI_API void Report(scene::NodeHandle root, std::uint64_t generation,
    std::string_view error) const;

  //! True when the request of `root` is kept.
  OXGN_EI_NDAPI auto Contains(scene::NodeHandle root) const -> bool;

private:
  struct Request {
    std::string source;
    std::vector<HostInstanceChange> changes;
    FailureCallback on_failure;
    SuccessCallback on_success;
    //! The generation of the last build taken.
    std::uint64_t built = 0;
    //! A build is queued; changes go into it.
    bool build_queued = false;
  };

  //! Drops the queued operations of `root`.
  void Unqueue(scene::NodeHandle root);

  std::unordered_map<scene::NodeHandle, Request> requests_;
  std::vector<Operation> queue_;
  std::uint64_t generation_ = 0;
};

} // namespace oxygen::interop::module

#pragma managed(pop)
