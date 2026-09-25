//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <system_error>

#include <Oxygen/Base/Result.h>

#ifdef _WIN32
#  include <Windows.h> // IWYU pragma: keep
#  include <errhandlingapi.h>
#  include <winbase.h>
#endif

#include "LightBench/LightBenchSettings.h"

#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Base/Uuid.h>

namespace oxygen::examples::light_bench {

auto SaveSettingsFile(const std::filesystem::path& path,
  const LightBenchSettings& settings) -> Result<void, SettingsError>
{
  if (path.empty()) {
    return Err(SettingsError { "No settings file path." });
  }
  const auto encoded = EncodeSettings(settings);
  if (!encoded) {
    return Err(encoded.error());
  }
  try {
    auto temporary = path;
    temporary += ".tmp." + Uuid::Generate().ToString();
    const auto cleanup = ScopeGuard([&temporary] noexcept -> void {
      std::error_code ignored;
      std::filesystem::remove(temporary, ignored);
    });
    std::ofstream output(temporary, std::ios::binary);
    if (!output) {
      return Err(SettingsError { "Cannot create temporary settings file." });
    }
    output << *encoded;
    output.close();
    if (!output) {
      return Err(SettingsError { "Could not finish writing settings file." });
    }
    // Same-directory replacement follows the engine's cache publication
    // pattern. Never delete the destination as a fallback on Windows.
    std::error_code error;
#ifdef _WIN32
    if (MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)
      == 0) {
      error = std::error_code(
        static_cast<int>(GetLastError()), std::system_category());
    }
#else
    std::filesystem::rename(temporary, path, error);
#endif
    if (error) {
      return Err(
        SettingsError { "Cannot replace settings file: " + error.message() });
    }
    return {};
  } catch (const std::exception& error) {
    return Err(SettingsError { error.what() });
  }
}

auto LoadSettingsFile(const std::filesystem::path& path)
  -> Result<LightBenchSettings, SettingsError>
{
  try {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
      return Err(SettingsError { "Cannot open settings file." });
    }
    constexpr std::size_t kMaximumBytes = 1'048'576U;
    // Bound the actual read, including files that grow after opening.
    std::string encoded(kMaximumBytes + 1, '\0');
    input.read(encoded.data(), static_cast<std::streamsize>(encoded.size()));
    const auto size = static_cast<std::size_t>(input.gcount());
    if (input.bad() || size > kMaximumBytes) {
      return Err(
        SettingsError { "Settings file is unreadable or larger than 1 MiB." });
    }
    encoded.resize(size);
    return DecodeSettings(encoded);
  } catch (const std::exception& error) {
    return Err(SettingsError { error.what() });
  }
}

} // namespace oxygen::examples::light_bench
