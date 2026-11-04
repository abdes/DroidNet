//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Import/CapturedInputSet.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportLimits.h>
#include <Oxygen/Cooker/Import/ImportRequestCodes.h>
#include <Oxygen/Cooker/Import/ImportSourceDocument.h>
#include <Oxygen/Cooker/Import/ImportSourceObservation.h>
#include <Oxygen/Serio/Device.h>
#include <Oxygen/Serio/NativeFile.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::content::import {

auto ImportSourceDocument::Load(const std::filesystem::path& path,
  const std::string_view kind, std::vector<ImportDiagnostic>& diagnostics,
  const CapturedInputSet* captured_inputs)
  -> std::optional<ImportSourceDocument>
{
  const auto refuse
    = [&](const std::string_view code, std::string message) -> std::nullopt_t {
    diagnostics.push_back(ImportDiagnostic {
      .severity = ImportSeverity::kError,
      .code = std::string { code },
      .message = std::move(message),
      .source_path = path.string(),
    });
    return std::nullopt;
  };
  const CapturedInputFile* captured_file = nullptr;
  if (captured_inputs) {
    const auto* captured = captured_inputs->Find(path);
    if (!captured || !captured->exists || !captured->file.has_value()) {
      return refuse(request_codes::kDocumentNotCaptured,
        fmt::format(
          "{} descriptor bytes were not captured: {}", kind, path.string()));
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
    return refuse(request_codes::kDocumentReadFailed,
      fmt::format("failed to open {} descriptor: {}: {}", kind, path.string(),
        file.error().message()));
  }
  const auto length = file->Size();
  if (!length || !std::in_range<size_t>(length->get())) {
    return refuse(request_codes::kDocumentReadFailed,
      fmt::format("invalid {} descriptor size: {}", kind, path.string()));
  }
  if (*length > kDefaultMaxSourceBytes) {
    return refuse(request_codes::kDocumentTooLarge,
      fmt::format("{} descriptor holds {} bytes, more than the import reads "
                  "from one source ({} bytes): {}",
        kind, length->get(), kDefaultMaxSourceBytes.get(), path.string()));
  }
  const auto capture_mismatch = [&] -> std::nullopt_t {
    return refuse(request_codes::kDocumentCaptureMismatch,
      fmt::format("captured {} descriptor size or digest mismatch: {}", kind,
        path.string()));
  };
  if (captured_file && captured_file->size != length->get()) {
    return capture_mismatch();
  }
  auto text = std::string(static_cast<size_t>(length->get()), '\0');
  if (!serio::ReadExactlyAt(*file, serio::ByteOffset { 0 },
        std::as_writable_bytes(std::span(text)))) {
    return refuse(request_codes::kDocumentReadFailed,
      fmt::format(
        "failed to read {} descriptor completely: {}", kind, path.string()));
  }
  const auto digest = base::ComputeSha256(std::as_bytes(std::span(text)));
  if (captured_file && captured_file->digest != digest) {
    return capture_mismatch();
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
