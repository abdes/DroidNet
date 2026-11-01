//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <filesystem>
#include <span>

#include "LightBench/LightBenchSettings.h"

#include <Oxygen/Base/Result.h>
#include <Oxygen/Serio/AtomicFile.h>
#include <Oxygen/Serio/ByteReader.h>
#include <Oxygen/Serio/Device.h>

namespace oxygen::examples::light_bench {

namespace {
  constexpr std::size_t kMaximumBytes = 1'048'576U;
} // namespace

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
  const auto published
    = serio::WriteFileAtomically(path, std::as_bytes(std::span { *encoded }));
  if (!published) {
    return Err(SettingsError {
      "Cannot replace settings file: " + published.error().message(),
    });
  }
  return {};
}

auto LoadSettingsFile(const std::filesystem::path& path)
  -> Result<LightBenchSettings, SettingsError>
{
  const auto file = serio::OpenPublishedFile(path);
  if (!file) {
    return Err(SettingsError { "Cannot open settings file." });
  }
  // Bound the read before it allocates.
  const auto size = file->Size();
  if (!size || size->get() > kMaximumBytes) {
    return Err(
      SettingsError { "Settings file is unreadable or larger than 1 MiB." });
  }
  const auto bytes = serio::ReadAll(*file);
  if (!bytes) {
    return Err(SettingsError { "Settings file is unreadable." });
  }
  const auto encoded
    = serio::ByteReader { bytes->Bytes() }.ReadStringView(bytes->Size());
  if (!encoded) {
    return Err(SettingsError { "Settings file is unreadable." });
  }
  return DecodeSettings(*encoded);
}

} // namespace oxygen::examples::light_bench
