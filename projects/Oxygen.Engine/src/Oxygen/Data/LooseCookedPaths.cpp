//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string_view>

#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/LooseCookedPaths.h>

namespace oxygen::data::loose_cooked {

namespace {
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

auto RequireIndexRelPath(const std::string_view relpath) -> void
{
  if (relpath == kIndexFileName || relpath == kGenerationLeaseFileName
    || relpath == kIndexLockFileName) {
    throw std::runtime_error("Reserved path in loose cooked inventory");
  }
  if (relpath.empty()) {
    throw std::runtime_error("Index path must not be empty");
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
