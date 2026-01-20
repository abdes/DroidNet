//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Content/api_export.h>

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

//! Context passed to node/asset naming strategies.
/*!
 Naming strategies may use this context to apply consistent conventions.

 @warning Oxygen does not require unique names. Naming strategies MUST NOT
  assume that names are unique, and MUST NOT enforce uniqueness unless that is
  explicitly desired.
*/
struct NamingContext final {
  //! What kind of object is being named.
  ImportNameKind kind = ImportNameKind::kSceneNode;

  //! Stable ordinal of this object within its kind (if applicable).
  /*!
   Importers may use this as a deterministic tiebreaker when generating
   fallback names.
  */
  uint32_t ordinal = 0;

  //! Optional parent name (for scene nodes).
  std::string_view parent_name = {};

  //! Optional source identifier for diagnostics (path, URI, or format id).
  std::string_view source_id = {};

  //! Optional scene namespace for asset namespacing.
  std::string_view scene_namespace = {};
};

//! Strategy for naming imported nodes and assets.
/*!
 This strategy is purely a rename hook:

 - Input: the authored name plus contextual information.
 - Output: an optional replacement string.

 If the strategy returns `std::nullopt`, the importer MUST keep the authored
 name as-is.

 @note This API does not distinguish stored vs. display names. Any rename is a
  semantic change to the imported name.
*/
class NamingStrategy {
public:
  virtual ~NamingStrategy() = default;

  //! Optionally returns a replacement name for an imported object.
  [[nodiscard]] virtual auto Rename(std::string_view authored_name,
    const NamingContext& context) const -> std::optional<std::string>
    = 0;
};

//! Naming strategy that never renames anything.
class NoOpNamingStrategy final : public NamingStrategy {
public:
  [[nodiscard]] auto Rename(
    std::string_view /*authored_name*/, const NamingContext& /*context*/) const
    -> std::optional<std::string> override
  {
    return std::nullopt;
  }
};

//! Naming strategy that normalizes names into a UE-style convention.
/*!
 The default behavior is intentionally non-destructive:

 - trims leading/trailing whitespace
 - collapses internal whitespace into single underscores
 - replaces non `[A-Za-z0-9_]` characters with underscores

 Prefixing is optional, and this strategy does not enforce uniqueness.

 Per request, mesh names use the `G_` prefix (instead of UE's `SM_`).
*/
class NormalizeNamingStrategy final : public NamingStrategy {
public:
  //! Options controlling normalization behavior.
  struct Options final {
    bool trim_whitespace = true;
    bool collapse_whitespace = true;
    bool replace_invalid_chars = true;
    bool collapse_underscores = true;
    bool apply_prefixes = true;

    std::string mesh_prefix = "G_";
    std::string material_prefix = "M_";
  };

  NormalizeNamingStrategy() = default;
  explicit NormalizeNamingStrategy(Options options)
    : options_(std::move(options))
  {
  }

  OXGN_CNTT_NDAPI auto Rename(std::string_view authored_name,
    const NamingContext& context) const -> std::optional<std::string> override;

  //! Generate a default base name for the given context.
  [[nodiscard]] static auto DefaultBaseName(const NamingContext& context)
    -> std::string
  {
    switch (context.kind) {
    case ImportNameKind::kSceneNode:
      return "Node";
    case ImportNameKind::kScene:
      return "Scene";
    case ImportNameKind::kMesh:
      return "Mesh";
    case ImportNameKind::kMaterial:
      return "Material";
    }
    return "Unnamed";
  }

private:
  [[nodiscard]] auto Normalize(std::string_view input) const -> std::string;
  [[nodiscard]] auto PrefixFor(ImportNameKind kind) const -> std::string_view;

  Options options_ = {};
};

//! Thread-safe naming service with uniqueness tracking.
/*!
 NamingService wraps a stateless NamingStrategy and adds session-scoped
 uniqueness tracking and optional scene namespacing.

 ### Design Principles

 1. **Stateless Strategy**: Delegates convention logic to pluggable strategy
 2. **Stateful Uniqueness**: Tracks used names per kind, assigns collision
    suffixes
 3. **Thread-Safe**: Uses per-kind registries with shared_mutex for concurrent
    access
 4. **Session-Scoped**: Intended for one import session; call Reset() between
    sessions

 @see NamingStrategy, NamingContext
*/
class NamingService final {
public:
  //! Configuration for the naming service.
  struct Config final {
    //! Strategy for applying naming conventions.
    std::shared_ptr<const NamingStrategy> strategy;

    //! Enable scene namespace prefixing for assets.
    bool enable_namespacing = true;

    //! Enforce uniqueness by appending collision suffixes.
    bool enforce_uniqueness = true;
  };

  //! Construct a naming service with the given configuration.
  /*!
   @param config Configuration specifying strategy and behavior.

   @pre config.strategy must not be null.
  */
  OXGN_CNTT_API explicit NamingService(Config config);

  ~NamingService() = default;

  OXYGEN_MAKE_NON_COPYABLE(NamingService)
  OXYGEN_DEFAULT_MOVABLE(NamingService)

  //! Generate a unique name for an imported object.
  /*!
   Applies the naming strategy, then enforces uniqueness if enabled.

   @param authored_name Original name from source file (may be empty).
   @param context Contextual information for naming conventions.
   @return Unique name ready for use in the import session.

   ### Thread Safety
   This method is thread-safe and may be called concurrently.
  */
  OXGN_CNTT_NDAPI auto MakeUniqueName(std::string_view authored_name,
    const NamingContext& context) -> std::string;

  //! Check if a name has been registered for a specific kind.
  OXGN_CNTT_NDAPI auto HasName(ImportNameKind kind, std::string_view name) const
    -> bool;

  //! Get the count of registered names for a specific kind.
  OXGN_CNTT_NDAPI auto GetNameCount(ImportNameKind kind) const -> size_t;
  //! Reset all registries for a new import session.
  /*!
   @warning Not thread-safe with respect to MakeUniqueName().
   Call this only when no naming operations are in progress.
  */
  OXGN_CNTT_API auto Reset() -> void;

private:
  struct NameRegistry {
    std::unordered_map<std::string, uint32_t> usage_counts;
    mutable std::shared_mutex mutex;
  };

  [[nodiscard]] auto ApplyNamespacing(
    std::string name, const NamingContext& context) const -> std::string;

  Config config_;
  std::array<NameRegistry, kImportNameKindCount> registries_;
};

} // namespace oxygen::content::import
