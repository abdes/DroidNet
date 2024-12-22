//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Oxygen/Renderers/Direct3d12/Content.h"

#include <filesystem>
#include <fstream>

#include "oxygen/base/logging.h"
#include "Oxygen/Renderers/Direct3d12/Shaders.h"

namespace {

  constexpr auto kShadersArchive = "shaders.bin";

  auto GetExecutablePath() -> std::filesystem::path
  {
    char buffer[MAX_PATH];
    GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    return std::filesystem::path(buffer).parent_path();
  }

  auto GetShaderArchivePath() -> std::filesystem::path
  {
    // Get the path to the directory where the executable is located.
    const std::filesystem::path exe_path = GetExecutablePath();
    return exe_path / kShadersArchive;
  }

  bool read_file(const std::filesystem::path& path, std::unique_ptr<uint8_t[]>& data, uint64_t& size)
  {
    if (!exists(path)) return false;

    size = file_size(path);
    DCHECK_GT_F(size, 0);
    if (!size) return false;

    data = std::make_unique<uint8_t[]>(size);
    std::ifstream file{ path, std::ios::in | std::ios::binary };
    if (!file || !file.read(reinterpret_cast<char*>(data.get()), static_cast<std::streamsize>(size)))
    {
      file.close();
      return false;
    }

    file.close();
    return true;
  }

}  // namespace

auto oxygen::content::LoadEngineShaders(
  std::unique_ptr<uint8_t[]>& shaders,
  uint64_t& size) -> bool
{
  const auto path = GetShaderArchivePath();
  return read_file(path, shaders, size);
}
