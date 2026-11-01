//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <filesystem>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Import/CapturedInputSet.h>
#include <Oxygen/Cooker/Import/ImportLimits.h>
#include <Oxygen/Cooker/Import/ImportSourceDocument.h>
#include <Oxygen/Cooker/Import/ImportSourceObservation.h>
#include <Oxygen/Serio/Device.h>
#include <Oxygen/Serio/NativeFile.h>
#include <Oxygen/Serio/Types.h>

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
  const auto file = serio::NativeFile::Open(physical_path,
    serio::OpenSpec {
      .sharing = serio::Sharing::kReadDelete,
      .hint = serio::AccessHint::kSequential,
    });
  if (!file) {
    errors << "ERROR: failed to open " << kind
           << " descriptor: " << path.string() << ": " << file.error().message()
           << '\n';
    return std::nullopt;
  }
  const auto length = file->Size();
  if (!length || !std::in_range<size_t>(length->get())) {
    errors << "ERROR: invalid " << kind << " descriptor size: " << path.string()
           << '\n';
    return std::nullopt;
  }
  if (*length > kDefaultMaxSourceBytes) {
    errors << "ERROR: " << kind << " descriptor holds " << length->get()
           << " bytes, more than the import reads from one source ("
           << kDefaultMaxSourceBytes.get() << " bytes): " << path.string()
           << '\n';
    return std::nullopt;
  }
  if (captured_file && captured_file->size != length->get()) {
    errors << "ERROR: captured " << kind
           << " descriptor size or digest mismatch: " << path.string() << '\n';
    return std::nullopt;
  }
  auto text = std::string(static_cast<size_t>(length->get()), '\0');
  if (!serio::ReadExactlyAt(*file, serio::ByteOffset { 0 },
        std::as_writable_bytes(std::span(text)))) {
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
      .offset = 0U, .max_bytes = 0U, .digest = digest, }, },
  };
}

} // namespace oxygen::content::import
