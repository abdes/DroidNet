//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <string_view>

namespace oxygen::cooker::test {

//! Root of `src/Oxygen/Cooker` in the source tree.
[[nodiscard]] auto CookerSourceDir() -> const std::filesystem::path&;

//! `Cooker/Test/Assets`. Throws `std::runtime_error` if it is missing.
[[nodiscard]] auto AssetsDir() -> const std::filesystem::path&;

//! `Cooker/Test/Assets/Models/<name>`. Throws if the file is missing.
[[nodiscard]] auto ModelPath(std::string_view name) -> std::filesystem::path;

//! A file under `Cooker/`, for example
//! `"Import/Schemas/oxygen.input.schema.json"`. Throws if it is missing.
[[nodiscard]] auto SchemaPath(std::string_view relative_to_cooker)
  -> std::filesystem::path;

} // namespace oxygen::cooker::test
