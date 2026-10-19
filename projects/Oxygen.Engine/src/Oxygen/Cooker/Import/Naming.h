//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Cooker/api_export.h>

namespace oxygen::content::import {

//! Kind of object being named during an import.
enum class ImportNameKind : uint8_t {
  //! A node in an imported scene graph.
  kSceneNode = 0,

  //! A scene asset.
  kScene,

  //! A geometry asset representing mesh data.
  kMesh,

  //! A material asset.
  kMaterial,
};

//! Number of distinct ImportNameKind values.
inline constexpr size_t kImportNameKindCount = 4;

//! What is being named, and where it sits in its source.
struct NamingContext final {
  //! What kind of object is being named.
  ImportNameKind kind = ImportNameKind::kSceneNode;

  //! Index of the object in its source; an unnamed object's fallback uses it.
  uint32_t ordinal = 0;

  //! Appended to the name, such as a mesh variant (`__baked_3`).
  std::string_view variant = {};

  //! Scene namespace that asset names are placed under.
  std::string_view scene_namespace = {};
};

//! Assigns the names of one import's products.
/*!
 Implements the product naming rule of the asset identity contract:

 - **Name.** An asset keeps its authored name. An unnamed one takes
   `Mesh_<ordinal>`, `Material_<ordinal>` or `Scene_<ordinal>`; the context's
   variant follows the name. No prefix or naming convention is applied.
 - **Replacement.** Only what the virtual path segment rule refuses is changed
   (`content::ToSegmentName`). A name whose leaf, with its variant, `_N` suffix
   and extension, exceeds the segment bound is shortened at a code point
   boundary.
 - **Uniqueness.** One registry per kind compares asset names case-folded
   (Unicode simple case folding). A taken name gets `_N`, probing past every
   occupied name.
 - **Scene nodes.** A node name is a label, not a path segment: it is kept
   verbatim, takes `Node_<ordinal>` when unnamed, and is unique by exact
   comparison, because the node's key is derived from it.

 A service names one import session; analysis and production each start a
 fresh one and register in source order, so both assign the same names.

 ### Thread Safety
 `MakeUniqueName`, `HasName` and `GetNameCount` may be called concurrently;
 concurrent registrations are serialized, and their order decides suffixes.
*/
class NamingService final {
public:
  NamingService() = default;
  ~NamingService() = default;

  OXYGEN_MAKE_NON_COPYABLE(NamingService)
  OXYGEN_MAKE_NON_MOVABLE(NamingService)

  //! Assigns and registers the name of one imported object.
  OXGN_COOK_NDAPI auto MakeUniqueName(std::string_view authored_name,
    const NamingContext& context) -> std::string;

  //! True when a name of this kind is taken, by the kind's comparison rule.
  OXGN_COOK_NDAPI auto HasName(ImportNameKind kind, std::string_view name) const
    -> bool;

  //! Number of names registered for a kind.
  OXGN_COOK_NDAPI auto GetNameCount(ImportNameKind kind) const -> size_t;

  //! Releases every registered name.
  /*!
   @warning Not thread-safe with respect to MakeUniqueName().
  */
  OXGN_COOK_API auto Reset() -> void;

private:
  struct NameRegistry {
    //! Comparison key of each taken name, and the next suffix to probe for it.
    std::unordered_map<std::string, uint32_t> next_suffix;
    mutable std::shared_mutex mutex;
  };

  std::array<NameRegistry, kImportNameKindCount> registries_;
};

} // namespace oxygen::content::import
