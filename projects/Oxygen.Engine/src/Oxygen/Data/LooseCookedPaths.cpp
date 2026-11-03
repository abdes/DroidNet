//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Base/StringUtils.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/LooseCookedPaths.h>
#include <Oxygen/Serio/AtomicFile.h>

namespace oxygen::data::loose_cooked {

namespace {
  //! Hexadecimal digits of the digest a content name carries.
  constexpr size_t kContentDigestDigits = 16;
  constexpr std::string_view kLeaseExtension = ".lock";

  //! Where the file name of `relpath` starts.
  [[nodiscard]] auto FileNameStart(const std::string_view relpath) noexcept
    -> size_t
  {
    const auto slash = relpath.rfind('/');
    return slash == std::string_view::npos ? 0 : slash + 1;
  }

  [[nodiscard]] auto IsLowerHex(const std::string_view text) noexcept -> bool
  {
    return std::ranges::all_of(text, [](const char c) -> bool {
      return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
  }

  auto RequireNoDotSegments(const std::string_view path) -> void
  {
    size_t pos = 0;
    while (pos <= path.size()) {
      const auto next = path.find('/', pos);
      const auto len
        = (next == std::string_view::npos) ? (path.size() - pos) : (next - pos);
      const auto segment = path.substr(pos, len);
      if (segment == ".") {
        throw std::runtime_error("Index path must not contain '.'");
      }
      if (segment == "..") {
        throw std::runtime_error("Index path must not contain '..'");
      }
      if (next == std::string_view::npos) {
        break;
      }
      pos = next + 1;
    }
  }
} // namespace

auto IsReservedRelPath(const std::string_view relpath) noexcept -> bool
{
  return relpath == kIndexFileName || relpath == kGenerationLeaseFileName
    || relpath == kWriterLockFileName || relpath == kLeasesFolderName
    || (relpath.starts_with(kLeasesFolderName)
      && relpath.substr(kLeasesFolderName.size()).starts_with('/'));
}

auto LeaseRelPath(const uint64_t commit) -> std::string
{
  return std::string(kLeasesFolderName) + "/" + std::to_string(commit)
    + std::string(kLeaseExtension);
}

auto LeasedCommitOf(const std::string_view file_name) noexcept
  -> std::optional<uint64_t>
{
  if (!file_name.ends_with(kLeaseExtension)) {
    return std::nullopt;
  }
  const auto digits
    = file_name.substr(0, file_name.size() - kLeaseExtension.size());
  auto commit = uint64_t { 0 };
  const auto [end, error]
    = std::from_chars(digits.data(), digits.data() + digits.size(), commit);
  if (digits.empty() || error != std::errc {}
    || end != digits.data() + digits.size()) {
    return std::nullopt;
  }
  return commit;
}

auto ContentRelPath(const std::string_view relpath,
  const base::Sha256Digest& sha256) -> std::string
{
  const auto digest = base::ToHexString(sha256).substr(0, kContentDigestDigits);
  const auto name_start = FileNameStart(relpath);
  const auto dot = relpath.rfind('.');
  // A leading dot starts a hidden name, not an extension.
  if (dot == std::string_view::npos || dot <= name_start) {
    return std::string(relpath) + "." + digest;
  }
  return std::string(relpath.substr(0, dot)) + "." + digest
    + std::string(relpath.substr(dot));
}

auto IsContentRelPath(const std::string_view relpath) noexcept -> bool
{
  const auto name = relpath.substr(FileNameStart(relpath));
  const auto digest_of = [](const std::string_view prefix_and_digest) -> bool {
    const auto dot = prefix_and_digest.rfind('.');
    if (dot == std::string_view::npos || dot == 0) {
      return false;
    }
    const auto digest = prefix_and_digest.substr(dot + 1);
    return digest.size() == kContentDigestDigits && IsLowerHex(digest);
  };
  if (digest_of(name)) {
    return true;
  }
  const auto extension = name.rfind('.');
  return extension != std::string_view::npos && extension != 0
    && digest_of(name.substr(0, extension));
}

auto IsWriterOutputRelPath(const std::string_view relpath) noexcept -> bool
{
  return IsContentRelPath(relpath)
    || serio::IsAtomicSiblingName(relpath.substr(FileNameStart(relpath)));
}

auto RequireIndexRelPath(const std::string_view relpath) -> void
{
  if (IsReservedRelPath(relpath)) {
    throw std::runtime_error("Reserved path in loose cooked inventory");
  }
  if (relpath.empty()) {
    throw std::runtime_error("Index path must not be empty");
  }
  // The index stores paths NUL-terminated, so a NUL would cut one short.
  if (relpath.contains('\0')) {
    throw std::runtime_error("Index path must not contain NUL");
  }
  if (!string_utils::IsValidUtf8(relpath)) {
    throw std::runtime_error("Index path must be UTF-8");
  }
  if (relpath.contains('\\')) {
    throw std::runtime_error("Index path must use '/' as the separator");
  }
  if (relpath.contains(':')) {
    throw std::runtime_error("Index path must not contain ':'");
  }
  if (relpath.front() == '/') {
    throw std::runtime_error("Index path must be container-relative");
  }
  if (relpath.back() == '/') {
    throw std::runtime_error("Index path must not end with '/'");
  }
  if (relpath.contains("//")) {
    throw std::runtime_error("Index path must not contain '//'");
  }
  RequireNoDotSegments(relpath);
  // A root name (a drive) is the one root form the checks above let through.
  if (const auto path = std::filesystem::path(relpath);
    path.has_root_name() || path.is_absolute()) {
    throw std::runtime_error("Index path must be container-relative");
  }
}

} // namespace oxygen::data::loose_cooked
