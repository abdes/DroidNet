//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cerrno>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Import/CapturedInputSet.h>
#include <Oxygen/Cooker/Import/ImportSourceDocument.h>
#include <Oxygen/Cooker/Import/ImportSourceObservation.h>

namespace oxygen::content::import {

auto ImportSourceDocument::Load(const std::filesystem::path& path,
  const std::string_view kind, std::ostream& errors,
  const CapturedInputSet* captured_inputs)
  -> std::optional<ImportSourceDocument>
{
  const CapturedInputFile* captured_file = nullptr;
  if (captured_inputs) {
    const auto* captured = captured_inputs->Find(path);
    if (!captured || !captured->exists || !captured->file.has_value()) {
      errors << "ERROR: " << kind
             << " descriptor bytes were not captured: " << path.string()
             << '\n';
      return std::nullopt;
    }
    captured_file = &captured->file.value();
  }
  const auto physical_path = captured_file ? captured_file->path : path;
  auto input = std::ifstream(
    base::ToNativePath(physical_path), std::ios::binary | std::ios::ate);
  if (!input) {
    const auto error = std::error_code(errno, std::generic_category());
    errors << "ERROR: failed to open " << kind
           << " descriptor: " << path.string() << ": " << error.message()
           << '\n';
    return std::nullopt;
  }
  const auto length = static_cast<std::streamoff>(input.tellg());
  if (!std::in_range<size_t>(length)
    || !std::in_range<std::streamsize>(length)) {
    errors << "ERROR: invalid " << kind << " descriptor size: " << path.string()
           << '\n';
    return std::nullopt;
  }
  if (captured_file && captured_file->size != static_cast<size_t>(length)) {
    errors << "ERROR: captured " << kind
           << " descriptor size or digest mismatch: " << path.string() << '\n';
    return std::nullopt;
  }
  auto text = std::string(static_cast<size_t>(length), '\0');
  input.seekg(0);
  if (!input.read(text.data(), static_cast<std::streamsize>(length))) {
    errors << "ERROR: failed to read " << kind
           << " descriptor completely: " << path.string() << '\n';
    return std::nullopt;
  }
  const auto digest = base::ComputeSha256(std::as_bytes(std::span(text)));
  if (captured_file
    && (captured_file->size != text.size()
      || captured_file->digest != digest)) {
    errors << "ERROR: captured " << kind
           << " descriptor size or digest mismatch: " << path.string() << '\n';
    return std::nullopt;
  }
  return ImportSourceDocument {
    .path = std::filesystem::absolute(path).lexically_normal(),
    .text = std::move(text),
    .digest = digest,
  };
}

auto ImportSourceDocument::Observation() const -> ImportSourceObservation
{
  return {
    .path = path,
    .exists = true,
    .metadata = {},
    .reads = { ImportSourceReadProof {
      .offset = 0U, .max_bytes = 0U, .digest = digest } },
  };
}

} // namespace oxygen::content::import
