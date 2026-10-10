//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <stdexcept>
#include <string_view>

#include <Oxygen/Cooker/Test/Support/TestPaths.h>

#if !defined(OXYGEN_COOKER_SOURCE_DIR)                                         \
  || !defined(OXYGEN_COOKER_TEST_ASSETS_DIR)
#  error                                                                       \
    "OXYGEN_COOKER_SOURCE_DIR and OXYGEN_COOKER_TEST_ASSETS_DIR are required"
#endif

namespace oxygen::cooker::test {

namespace {

  auto RequireExisting(std::filesystem::path path) -> std::filesystem::path
  {
    if (!std::filesystem::exists(path)) {
      throw std::runtime_error(
        "Required test input is missing: " + path.string());
    }
    return path;
  }

} // namespace

auto CookerSourceDir() -> const std::filesystem::path&
{
  static const auto dir
    = std::filesystem::path(OXYGEN_COOKER_SOURCE_DIR).lexically_normal();
  return dir;
}

auto AssetsDir() -> const std::filesystem::path&
{
  static const auto dir = RequireExisting(
    std::filesystem::path(OXYGEN_COOKER_TEST_ASSETS_DIR).lexically_normal());
  return dir;
}

auto ModelPath(const std::string_view name) -> std::filesystem::path
{
  return RequireExisting(AssetsDir() / "Models" / std::filesystem::path(name));
}

auto SchemaPath(const std::string_view relative_to_cooker)
  -> std::filesystem::path
{
  return RequireExisting(
    CookerSourceDir() / std::filesystem::path(relative_to_cooker));
}

} // namespace oxygen::cooker::test
