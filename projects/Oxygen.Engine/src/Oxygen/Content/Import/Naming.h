//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace oxygen::content::import {

//! Kind of object being named during an import.
enum class ImportNameKind : uint8_t {
  //! A node in an imported scene graph.
  kSceneNode = 0,

  //! A geometry asset representing mesh data.
  kMesh,

  //! A material asset.
  kMaterial,

  //! A texture asset.
  kTexture,
};

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
    std::string texture_prefix = "T_";
  };

  NormalizeNamingStrategy() = default;
  explicit NormalizeNamingStrategy(Options options)
    : options_(std::move(options))
  {
  }

  [[nodiscard]] auto Rename(std::string_view authored_name,
    const NamingContext& context) const -> std::optional<std::string> override
  {
    auto normalized = Normalize(authored_name);

    if (normalized.empty()) {
      normalized = DefaultBaseName(context);
    }

    if (options_.apply_prefixes) {
      const auto prefix = PrefixFor(context.kind);
      if (!prefix.empty() && !normalized.starts_with(prefix)) {
        normalized = std::string(prefix) + normalized;
      }
    }

    if (normalized == authored_name) {
      return std::nullopt;
    }

    return normalized;
  }

private:
  [[nodiscard]] auto Normalize(std::string_view input) const -> std::string
  {
    auto s = std::string(input);

    if (options_.trim_whitespace) {
      const auto is_space
        = [](unsigned char ch) { return std::isspace(ch) != 0; };
      while (!s.empty() && is_space(static_cast<unsigned char>(s.front()))) {
        s.erase(s.begin());
      }
      while (!s.empty() && is_space(static_cast<unsigned char>(s.back()))) {
        s.pop_back();
      }
    }

    if (s.empty()) {
      return s;
    }

    std::string out;
    out.reserve(s.size());

    bool last_was_underscore = false;
    bool in_whitespace = false;

    for (const auto ch : s) {
      const auto uch = static_cast<unsigned char>(ch);
      const auto is_space = (std::isspace(uch) != 0);

      if (is_space) {
        if (options_.collapse_whitespace) {
          in_whitespace = true;
          continue;
        }
        out.push_back('_');
        last_was_underscore = true;
        continue;
      }

      if (in_whitespace) {
        out.push_back('_');
        last_was_underscore = true;
        in_whitespace = false;
      }

      const auto is_valid = (std::isalnum(uch) != 0) || (ch == '_');
      if (!is_valid && options_.replace_invalid_chars) {
        if (!last_was_underscore || !options_.collapse_underscores) {
          out.push_back('_');
          last_was_underscore = true;
        }
        continue;
      }

      if (ch == '_' && options_.collapse_underscores && last_was_underscore) {
        continue;
      }

      out.push_back(ch);
      last_was_underscore = (ch == '_');
    }

    if (options_.collapse_underscores) {
      while (!out.empty() && out.front() == '_') {
        out.erase(out.begin());
      }
      while (!out.empty() && out.back() == '_') {
        out.pop_back();
      }
    }

    return out;
  }

  [[nodiscard]] auto PrefixFor(ImportNameKind kind) const -> std::string_view
  {
    switch (kind) {
    case ImportNameKind::kMesh:
      return options_.mesh_prefix;
    case ImportNameKind::kMaterial:
      return options_.material_prefix;
    case ImportNameKind::kTexture:
      return options_.texture_prefix;
    case ImportNameKind::kSceneNode:
      return {};
    }
    return {};
  }

  [[nodiscard]] static auto DefaultBaseName(const NamingContext& context)
    -> std::string
  {
    switch (context.kind) {
    case ImportNameKind::kSceneNode:
      return "Node";
    case ImportNameKind::kMesh:
      return "Mesh";
    case ImportNameKind::kMaterial:
      return "Material";
    case ImportNameKind::kTexture:
      return "Texture";
    }
    return "Unnamed";
  }

  Options options_ = {};
};

} // namespace oxygen::content::import
