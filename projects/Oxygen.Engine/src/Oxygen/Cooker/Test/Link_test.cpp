//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdlib>
#include <filesystem>

#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Cooker/Loose/Validation.h>

auto main(int /*argc*/, char** /*argv*/) -> int
{
  oxygen::content::lc::Inspection inspection {};

  [[maybe_unused]] const auto assets = inspection.Assets();
  [[maybe_unused]] const auto files = inspection.Files();
  [[maybe_unused]] const auto guid = inspection.Guid();

  using ValidateRootFn = auto (*)(const std::filesystem::path&)->void;
  volatile ValidateRootFn validate_root = &oxygen::content::lc::ValidateRoot;

  return validate_root == nullptr ? EXIT_FAILURE : EXIT_SUCCESS;
}
