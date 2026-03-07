//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <string>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Cooker/Pak/PakBuildRequest.h>
#include <Oxygen/Cooker/Tools/PakTool/ArtifactPublication.h>
#include <Oxygen/Cooker/Tools/PakTool/PakToolOptions.h>
#include <Oxygen/Cooker/Tools/PakTool/RequestSnapshot.h>

namespace oxygen::content::pak::tool {

struct RequestPreparationError {
  std::string error_code;
  std::string error_message;
  std::filesystem::path path;
};

struct PreparedPakToolRequest {
  pak::PakBuildRequest build_request;
  PakToolRequestSnapshot request_snapshot {};
  ArtifactPublicationPlan publication_plan {};
};

class IRequestPreparationFileSystem {
public:
  virtual ~IRequestPreparationFileSystem() = default;

  [[nodiscard]] virtual auto Exists(const std::filesystem::path& path) const
    -> bool
    = 0;
  [[nodiscard]] virtual auto IsDirectory(
    const std::filesystem::path& path) const -> bool
    = 0;
  [[nodiscard]] virtual auto IsRegularFile(
    const std::filesystem::path& path) const -> bool
    = 0;
  virtual auto CreateDirectories(const std::filesystem::path& path)
    -> std::error_code
    = 0;
};

class RealRequestPreparationFileSystem final
  : public IRequestPreparationFileSystem {
public:
  [[nodiscard]] auto Exists(const std::filesystem::path& path) const
    -> bool override;
  [[nodiscard]] auto IsDirectory(const std::filesystem::path& path) const
    -> bool override;
  [[nodiscard]] auto IsRegularFile(const std::filesystem::path& path) const
    -> bool override;
  auto CreateDirectories(const std::filesystem::path& path)
    -> std::error_code override;
};

[[nodiscard]] auto PreparePakToolRequest(const pak::BuildMode mode,
  const PakToolCliOptions& options, IRequestPreparationFileSystem& fs)
  -> Result<PreparedPakToolRequest, RequestPreparationError>;

} // namespace oxygen::content::pak::tool
