//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Cooker/Pak/PakBuildReport.h>
#include <Oxygen/Cooker/Pak/PakBuildRequest.h>
#include <Oxygen/Cooker/Pak/PakBuildResult.h>
#include <Oxygen/Cooker/Pak/PakPlanBuilder.h>
#include <Oxygen/Cooker/Pak/PakTextures.h>
#include <Oxygen/Cooker/Pak/PakWriter.h>
#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/MemoryFileSystem.h>
#include <Oxygen/Cooker/testing/PackedPak.h>
#include <Oxygen/Data/CookedSource.h>
#include <Oxygen/Data/PakCatalog.h>

namespace oxygen::cooker::testing {

//! What packing a request produced: the plan, and the write when the plan
//! succeeded.
struct PackResult final {
  content::pak::PakPlanBuilder::BuildResult planned;
  std::optional<content::pak::PakWriter::WriteResult> written;

  //! The planner's diagnostics, then the writer's.
  [[nodiscard]] auto Diagnostics() const
    -> std::vector<content::pak::PakDiagnostic>;
};

//! Whether `diagnostics` hold an error.
[[nodiscard]] auto HasPakError(
  const std::vector<content::pak::PakDiagnostic>& diagnostics) -> bool;

//! One line per diagnostic: its code and its message.
[[nodiscard]] auto PakDiagnosticSummary(
  const std::vector<content::pak::PakDiagnostic>& diagnostics) -> std::string;

//! The files of one pak test, in memory: the loose cooked roots it writes and
//! the paks, manifests and catalogs its builds publish.
/*!
 Its requests read and publish through the workspace, so what one build
 publishes is the next build's source, base or dependency.

 ```cpp
 auto workspace = PakWorkspace {};
 const auto engine = workspace.PackOrThrow(
   workspace.FullRequest({ workspace.Add(engine_root) }, "engine.pak"));
 auto request = workspace.FullRequest({ workspace.Add(game) }, "game.pak");
 request.dependencies = { workspace.Dependency(PakWorkspace::PakPath(
   "engine.pak")) };
 ```
*/
class PakWorkspace final {
public:
  //! Where loose roots are placed in the workspace.
  static constexpr auto kRootsFolder = std::string_view { "/cooked" };
  //! Where paks are placed in the workspace.
  static constexpr auto kPaksFolder = std::string_view { "/paks" };

  [[nodiscard]] auto Storage() noexcept -> MemoryFileSystem&
  {
    return storage_;
  }

  [[nodiscard]] auto Storage() const noexcept -> const MemoryFileSystem&
  {
    return storage_;
  }

  //! Whether the workspace holds a file or a folder at `path`.
  [[nodiscard]] auto Exists(const std::filesystem::path& path) const -> bool;

  //! Writes `loose` as a loose root; returns it as a source.
  auto Add(const LooseCookedRoot& loose) -> data::CookedSource;

  //! The path of the pak `name`.
  [[nodiscard]] static auto PakPath(std::string_view name)
    -> std::filesystem::path;

  //! The pak at `path` as a source.
  [[nodiscard]] static auto PakSource(const std::filesystem::path& path)
    -> data::CookedSource;

  //=== Requests ===--------------------------------------------------------//

  //! A deterministic full build of `sources` into the pak `name`.
  /*!
   Each request has an identity of its own, so a patch over a pak of the same
   workspace never shares its base's identity. Build one request twice to get
   the same bytes twice.
  */
  [[nodiscard]] auto FullRequest(std::vector<data::CookedSource> sources,
    std::string_view name = "out.pak") -> content::pak::PakBuildRequest;

  //! A deterministic patch of `sources` over `bases` into the pak `name`, with
  //! its manifest beside it.
  [[nodiscard]] auto PatchRequest(std::vector<data::CookedSource> sources,
    std::vector<data::PakCatalog> bases, std::string_view name = "patch.pak")
    -> content::pak::PakBuildRequest;

  //! The pak at `path` as a dependency of a request: its catalog and the
  //! textures its browse index lists, read through `PakFile`.
  [[nodiscard]] auto Dependency(const std::filesystem::path& path) const
    -> content::pak::PakDependency;

  //! The catalog of the pak at `path`, as the base of a patch.
  [[nodiscard]] auto Catalog(const std::filesystem::path& path) const
    -> data::PakCatalog;

  //! The textures of the pak at `path`, as a base of a patch.
  [[nodiscard]] auto Textures(const std::filesystem::path& path) const
    -> content::pak::PakTextures;

  //=== Building ===--------------------------------------------------------//

  //! Plans `request` and, when that succeeds, writes it.
  [[nodiscard]] static auto Pack(const content::pak::PakBuildRequest& request)
    -> PackResult;

  //! Builds `request` with the `PakBuilder`; throws when it reports no result,
  //! so a test asserts on the result's diagnostics.
  [[nodiscard]] static auto Build(const content::pak::PakBuildRequest& request)
    -> content::pak::PakBuildResult;

  //! Packs `request` and opens the pak; throws, with every diagnostic, when
  //! the build fails.
  auto PackOrThrow(const content::pak::PakBuildRequest& request) const
    -> PackedPak;

  //! Opens the pak at `path`.
  [[nodiscard]] auto Open(const std::filesystem::path& path) const -> PackedPak;

  //! The text of a published file, such as a manifest or a catalog.
  [[nodiscard]] auto Text(const std::filesystem::path& path) const
    -> std::string;

private:
  //! The identity seed of the first request; loose roots take small seeds and
  //! base catalogs start at 0xC1.
  static constexpr auto kFirstRequestSeed = uint8_t { 0x70U };

  MemoryFileSystem storage_;
  uint32_t roots_ { 0U };
  uint8_t requests_ { 0U };
};

} // namespace oxygen::cooker::testing
