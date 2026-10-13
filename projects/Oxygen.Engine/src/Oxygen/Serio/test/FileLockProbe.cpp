//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <span>
#include <system_error>

#include <Oxygen/Base/Span.h>
#include <Oxygen/Serio/FileLock.h>

#ifdef _WIN32
auto wmain(int argc, wchar_t* argv[]) -> int
#else
auto main(int argc, char* argv[]) -> int
#endif
{
  constexpr auto kArgumentCount = 4;
  if (argc != kArgumentCount) {
    return EXIT_FAILURE;
  }
  const auto arguments = std::span(argv, static_cast<std::size_t>(argc));
  const auto mode_text
    = std::filesystem::path(oxygen::base::CheckedAt(arguments, 2)).string();
  const auto lifetime_text
    = std::filesystem::path(oxygen::base::CheckedAt(arguments, 3)).string();
  if ((mode_text != "shared" && mode_text != "exclusive")
    || (lifetime_text != "hold" && lifetime_text != "try")) {
    return EXIT_FAILURE;
  }
  const auto mode = mode_text == "shared"
    ? oxygen::serio::FileLockMode::kShared
    : oxygen::serio::FileLockMode::kExclusive;
  auto lock = oxygen::serio::FileLock::TryAcquire(
    oxygen::base::CheckedAt(arguments, 1), mode);
  if (!lock) {
    if (lock.error() == std::errc::device_or_resource_busy) {
      std::cout << "busy\n" << std::flush;
      return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
  }
  std::cout << "acquired\n" << std::flush;
  if (lifetime_text == "hold") {
    static_cast<void>(std::cin.get());
  }
  return EXIT_SUCCESS;
}
