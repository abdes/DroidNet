//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/Data/PakFormat_physics.h>

namespace oxygen::content::import::internal {

//! A physics material descriptor, every authored value checked for the float
//! it cooks to. A value not authored keeps the cooked record's default.
struct PhysicsMaterialSource final {
  std::string name;
  std::optional<float> static_friction;
  std::optional<float> dynamic_friction;
  std::optional<float> restitution;
  //! Positive, in kg/m^3.
  std::optional<float> density;
  std::optional<data::pak::physics::PhysicsCombineMode> combine_mode_friction;
  std::optional<data::pak::physics::PhysicsCombineMode>
    combine_mode_restitution;
};

//! Validates a physics material descriptor; no file or cooked-resource I/O.
/*!
 Parses `bytes` as a JSON object, checks it against the descriptor schema,
 then checks that a 32-bit float holds every value and that the density stays
 positive once narrowed.

 @param bytes Descriptor document text.
 @param source_path Path the diagnostics name.
 @param diagnostics Receives an error for every refusal.
 @return The checked material, or nothing when an error was added.
*/
OXGN_COOK_NDAPI auto PreparePhysicsMaterialDescriptor(std::string_view bytes,
  const std::filesystem::path& source_path,
  std::vector<ImportDiagnostic>& diagnostics)
  -> std::optional<PhysicsMaterialSource>;

} // namespace oxygen::content::import::internal
