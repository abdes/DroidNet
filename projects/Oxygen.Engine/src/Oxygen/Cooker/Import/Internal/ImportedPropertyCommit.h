//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Data/Schema/PropertyRange.h>

namespace oxygen::content::import::detail {

//! The code of an imported property the scene schema rejects.
inline constexpr std::string_view kImportedPropertyInvalidCode
  = "scene.import.property_invalid";

//! Commits the camera and light values a model import reads through the scene
//! schema.
/*!
 An imported value is data from outside the scene. A valid value is kept, and
 a finite value outside a range the schema declares `commit: clamp` takes the
 nearest bound. Any other value is an optional input the importer skips: the
 property keeps its schema default and is recorded, and `Report` warns about
 each recorded property with `kImportedPropertyInvalidCode`.
*/
class ImportedPropertyCommit final {
public:
  //! The value `property` stores for the imported `value`.
  template <typename T>
    requires std::is_arithmetic_v<T>
  [[nodiscard]] auto operator()(const std::string_view property,
    const data::schema::PropertyRange<T>& range, const T value,
    const T schema_default) -> T
  {
    if (const auto committed = range.Commit(value)) {
      return *committed;
    }
    rejected_.push_back(property);
    return schema_default;
  }

  //! The properties whose imported value was rejected, in commit order.
  [[nodiscard]] auto Rejected() const noexcept
    -> std::span<const std::string_view>
  {
    return rejected_;
  }

  //! Appends a warning for each rejected property of the imported object at
  //! `object_path`.
  auto Report(const std::string_view source_id,
    const std::string_view object_path,
    std::vector<ImportDiagnostic>& diagnostics) const -> void
  {
    for (const auto property : rejected_) {
      diagnostics.push_back(ImportDiagnostic {
        .severity = ImportSeverity::kWarning,
        .code = std::string(kImportedPropertyInvalidCode),
        .message = "Imported property '" + std::string(property)
          + "' is not a valid value of the scene schema; it takes the "
            "schema default",
        .source_path = std::string(source_id),
        .object_path = std::string(object_path),
      });
    }
  }

private:
  std::vector<std::string_view> rejected_;
};

} // namespace oxygen::content::import::detail
