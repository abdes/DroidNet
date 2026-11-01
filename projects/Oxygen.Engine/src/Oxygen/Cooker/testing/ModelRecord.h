//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <Oxygen/Base/Uuid.h>

namespace oxygen::cooker::testing {

//! A model import record at its identity, `<identity>.import.json`, as the
//! record's serializer writes it before the model's first import.
/*!
 The record names its source from the record, imports it with `settings` and
 carries a source identity derived from `seed`. A record with provenance or
 published nodes comes only from a real import.

 ```cpp
 const auto text
   = ModelRecord("/Props/Models/Truck.glb").Source("Truck.fbx").Text();
 ```
*/
class ModelRecord final {
public:
  //! The record of the model `identity`, such as `/Props/Models/Truck.glb`,
  //! imported from the file at its identity.
  explicit ModelRecord(std::string identity, uint8_t seed = 1U);

  //! Imports the model from `source`: a mount path, such as
  //! `/Props/Models/Truck.fbx`, or a file name beside the model.
  auto Source(std::string_view source) -> ModelRecord&;

  //! Imports the model with `settings`, a JSON object in the vocabulary of a
  //! model import manifest job.
  auto Settings(std::string settings) -> ModelRecord&;

  [[nodiscard]] auto Identity() const noexcept -> const std::string&
  {
    return identity_;
  }

  //! The mount path of the record's source.
  [[nodiscard]] auto SourcePath() const noexcept -> const std::string&
  {
    return source_;
  }

  [[nodiscard]] auto SourceIdentity() const noexcept -> const Uuid&
  {
    return source_identity_;
  }

  //! The record's text, written by `ModelImportRecord::Serialize`.
  [[nodiscard]] auto Text() const -> std::string;

private:
  std::string identity_;
  std::string source_;
  std::string settings_ = "{}";
  Uuid source_identity_;
};

} // namespace oxygen::cooker::testing
