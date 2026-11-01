//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <string>
#include <system_error>

#include "DemoShell/Services/ShowInFolder.h"

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Base/Logging.h>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <Windows.h> // IWYU pragma: keep

#  include <shellapi.h>
#endif

namespace oxygen::examples {

auto ShowInFolder(const std::filesystem::path& path) -> void
{
  auto target = base::ToNativePath(path);
  std::error_code error;
  while (!target.empty() && !std::filesystem::exists(target, error)) {
    target = target.parent_path();
  }
  if (target.empty()) {
    LOG_F(
      WARNING, "Show in folder: '{}' and its parents are gone", path.string());
    return;
  }
#if defined(_WIN32)
  const auto arguments = L"/select,\"" + target.wstring() + L"\"";
  auto execute = SHELLEXECUTEINFOW {
    .cbSize = sizeof(SHELLEXECUTEINFOW),
    .lpVerb = L"open",
    .lpFile = L"explorer.exe",
    .lpParameters = arguments.c_str(),
    .nShow = SW_SHOWNORMAL,
  };
  if (ShellExecuteExW(&execute) == FALSE) {
    LOG_F(WARNING, "Show in folder: explorer did not open '{}' ({})",
      target.string(), GetLastError());
  }
#else
  LOG_F(WARNING, "Show in folder is not available on this platform: '{}'",
    target.string());
#endif
}

} // namespace oxygen::examples
