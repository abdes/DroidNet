//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Scripting/Analysis/ScriptDiagnostic.h>
#include <Oxygen/Scripting/Analysis/api_export.h>

namespace oxygen::scripting {

//! Supplies source lines to the diagnostic renderer.
class SourceProvider {
public:
  SourceProvider() = default;
  virtual ~SourceProvider() = default;

  OXYGEN_DEFAULT_COPYABLE(SourceProvider)
  OXYGEN_DEFAULT_MOVABLE(SourceProvider)

  //! Returns 1-based line `line` of `source_name` without its line break.
  /*!
   @return The line, or nothing when the source or the line is unknown. The
   view stays valid while the provider is unchanged.
  */
  [[nodiscard]] virtual auto Line(std::string_view source_name,
    uint32_t line) const -> std::optional<std::string_view> = 0;
};

//! Source provider over texts held in memory, each indexed by line once.
/*!
 Adding a source is not safe concurrently with any other use; reading lines
 from several threads is.
*/
class SourceTextCache final : public SourceProvider {
public:
  //! Adds or replaces the text of `source_name`.
  OXGN_SCAN_API auto Add(std::string source_name, std::string text) -> void;

  OXGN_SCAN_NDAPI auto Line(std::string_view source_name, uint32_t line) const
    -> std::optional<std::string_view> override;

private:
  struct Source {
    std::string text;
    std::vector<std::size_t> line_starts;
  };

  struct NameHash {
    using is_transparent = void;
    auto operator()(std::string_view name) const noexcept -> std::size_t
    {
      return std::hash<std::string_view> {}(name);
    }
  };

  std::unordered_map<std::string, Source, NameHash, std::equal_to<>> sources_;
};

//! Renders a diagnostic as text in the layout of `rustc`.
/*!
 The output is the header (`error[S0302]: message`), the location arrow, the
 source lines of every label with underlines (`^` for the primary label, `-`
 for the others) and label text, then the slot context, notes and help as
 `= note:` and `= help:` lines. Lines whose source the provider does not know
 are left out, so a diagnostic without source renders its header, location
 and notes only.

 Columns count UTF-8 code points, and tabs advance four columns. A span over
 several lines is drawn in a margin (`/`, `|`, `|_^`); one such span per
 source is drawn that way, others underline their first line. The result ends
 with a line break and has no color.
*/
OXGN_SCAN_NDAPI auto RenderText(const ScriptDiagnostic& diagnostic,
  const SourceProvider& sources) -> std::string;

} // namespace oxygen::scripting
