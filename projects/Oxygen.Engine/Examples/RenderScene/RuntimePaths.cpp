// Distributed under the 3-Clause BSD License. See accompanying file LICENSE.
// SPDX-License-Identifier: BSD-3-Clause

#include <source_location>
#include <stdexcept>
#include <string>

#include "RenderScene/RuntimePaths.h"
#include <Windows.h>

namespace oxygen::examples::render_scene {

auto ResolveRuntimePaths() -> RuntimePaths
{
  std::wstring executable(512, L'\0');
  for (;;) {
    const auto length = GetModuleFileNameW(
      nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (length == 0) {
      throw std::runtime_error("Cannot locate the RenderScene executable");
    }
    if (length < executable.size()) {
      executable.resize(length);
      break;
    }
    executable.resize(executable.size() * 2);
  }
  const auto install_root
    = std::filesystem::path(executable).parent_path().parent_path();
  if (std::filesystem::is_regular_file(
        install_root / "share/oxygen/oxygen-source.json")) {
    return { true, install_root, install_root / "share/oxygen/RenderScene",
      install_root / "share/oxygen/Content" };
  }
  const auto demo_root
    = std::filesystem::path(std::source_location::current().file_name())
        .parent_path();
  return { false, demo_root.parent_path().parent_path(), demo_root,
    demo_root.parent_path() / "Content" };
}

} // namespace oxygen::examples::render_scene
