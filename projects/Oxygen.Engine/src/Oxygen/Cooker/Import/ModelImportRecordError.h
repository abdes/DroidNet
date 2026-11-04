//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>

namespace oxygen::content::import {

//! The stable codes a model import record refuses with, besides the codes of
//! the mount table and the virtual path rules that locate it and its source.
namespace model_record_codes {
  //! A record file is not named after the source it records.
  inline constexpr std::string_view kMisnamed = "import.model_record_misnamed";
  //! The record breaks its schema or a rule the schema cannot state: its
  //! settings, provenance, nodes, digests or source format.
  inline constexpr std::string_view kInvalid = "import.model_record_invalid";
  //! The record or a file it needs cannot be read.
  inline constexpr std::string_view kUnreadable
    = "import.model_record_unreadable";
  //! Another record of the mount table has the record's source identity.
  inline constexpr std::string_view kDuplicate
    = "import.model_record_duplicate";
  //! Another writer holds the record.
  inline constexpr std::string_view kBusy = "import.model_record_busy";
  //! The record or its selected generation changed while an import ran.
  inline constexpr std::string_view kChanged = "import.model_record_changed";
  //! The record's primary source file is missing.
  inline constexpr std::string_view kSourceMissing = "import.source_missing";
  //! The generation the record selects is gone.
  inline constexpr std::string_view kGenerationMissing
    = "import.generation_missing";
  //! The generation the record selects cannot be trusted: its head is
  //! malformed, its index changed or it cannot be leased.
  inline constexpr std::string_view kGenerationInvalid
    = "import.generation_invalid";
} // namespace model_record_codes

//! A model import record operation's refusal: the diagnostic that names its
//! stable code, the file and the record field it is about.
/*!
 Every refusal of `ModelImportRecord` is one, so a caller acts on
 `Diagnostic().code` and shows `Diagnostic().json_pointer`, never parses the
 message. `what()` is `[code] message`.
*/
class ModelImportRecordError final : public std::runtime_error {
public:
  explicit ModelImportRecordError(ImportDiagnostic diagnostic)
    : std::runtime_error("[" + diagnostic.code + "] " + diagnostic.message)
    , diagnostic_(
        std::make_shared<const ImportDiagnostic>(std::move(diagnostic)))
  {
  }

  [[nodiscard]] auto Diagnostic() const noexcept -> const ImportDiagnostic&
  {
    return *diagnostic_;
  }

private:
  // Shared, so that copying the exception never throws.
  std::shared_ptr<const ImportDiagnostic> diagnostic_;
};

} // namespace oxygen::content::import
