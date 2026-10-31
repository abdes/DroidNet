//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Pak/PakBuilder.h>
#include <Oxygen/Cooker/testing/PakWorkspace.h>
#include <Oxygen/Cooker/testing/Seeds.h>
#include <Oxygen/Data/AssetReferences.h>

namespace oxygen::cooker::testing {

namespace pak = content::pak;

auto PackResult::Diagnostics() const -> std::vector<pak::PakDiagnostic>
{
  auto all = planned.diagnostics;
  if (written) {
    all.insert(
      all.end(), written->diagnostics.begin(), written->diagnostics.end());
  }
  return all;
}

auto HasPakError(const std::vector<pak::PakDiagnostic>& diagnostics) -> bool
{
  return std::ranges::any_of(
    diagnostics, [](const pak::PakDiagnostic& diagnostic) -> bool {
      return diagnostic.severity == pak::PakDiagnosticSeverity::kError;
    });
}

auto PakDiagnosticSummary(const std::vector<pak::PakDiagnostic>& diagnostics)
  -> std::string
{
  auto out = std::ostringstream {};
  for (const auto& diagnostic : diagnostics) {
    out << diagnostic.code << ": " << diagnostic.message << "\n";
  }
  return out.str();
}

auto PakWorkspace::Exists(const std::filesystem::path& path) const -> bool
{
  return storage_.KindOf(path) != content::cooked::PathKind::kNone;
}

auto PakWorkspace::Add(const LooseCookedRoot& loose) -> data::CookedSource
{
  const auto root = kRoots / (loose.Name() + "-" + std::to_string(++roots_));
  storage_.AddFiles(root, loose.Files());
  return { .kind = data::CookedSourceKind::kLooseCooked, .path = root };
}

auto PakWorkspace::PakPath(const std::string_view name) -> std::filesystem::path
{
  return kPaks / std::filesystem::path(name);
}

auto PakWorkspace::PakSource(const std::filesystem::path& path)
  -> data::CookedSource
{
  return { .kind = data::CookedSourceKind::kPak, .path = path };
}

auto PakWorkspace::FullRequest(std::vector<data::CookedSource> sources,
  const std::string_view name) -> pak::PakBuildRequest
{
  return pak::PakBuildRequest {
    .mode = pak::BuildMode::kFull,
    .sources = std::move(sources),
    .output_pak_path = PakPath(name),
    .output_manifest_path = {},
    .content_version = 1U,
    .source_key
    = SourceKeyOf(static_cast<uint8_t>(kFirstRequestSeed + requests_++)),
    .base_catalogs = {},
    .base_texture_keys = {},
    .dependencies = {},
    .options = {},
    .storage = make_observer<content::cooked::CookerStorage>(&storage_),
  };
}

auto PakWorkspace::PatchRequest(std::vector<data::CookedSource> sources,
  std::vector<data::PakCatalog> bases, const std::string_view name)
  -> pak::PakBuildRequest
{
  auto request = FullRequest(std::move(sources), name);
  request.mode = pak::BuildMode::kPatch;
  request.output_manifest_path
    = PakPath(name).replace_extension(".manifest.json");
  request.base_catalogs = std::move(bases);
  return request;
}

auto PakWorkspace::Dependency(const std::filesystem::path& path) const
  -> pak::PakDependency
{
  const auto pak = Open(path);
  auto dependency = pak::PakDependency {
    .catalog = pak.Pak().Catalog(),
    .texture_keys = {},
  };
  for (const auto& texture : pak.Pak().BrowseTextures()) {
    dependency.texture_keys.insert(
      data::TextureKeyFromVirtualPath(texture.virtual_path));
  }
  return dependency;
}

auto PakWorkspace::Catalog(const std::filesystem::path& path) const
  -> data::PakCatalog
{
  return Open(path).Pak().Catalog();
}

auto PakWorkspace::Pack(const pak::PakBuildRequest& request) -> PackResult
{
  auto result = PackResult { .planned = pak::PakPlanBuilder {}.Build(request) };
  if (result.planned.plan && !HasPakError(result.planned.diagnostics)) {
    result.written = pak::PakWriter {}.Write(request, *result.planned.plan);
  }
  return result;
}

auto PakWorkspace::Build(const pak::PakBuildRequest& request)
  -> pak::PakBuildResult
{
  auto built = pak::PakBuilder {}.Build(request);
  if (!built) {
    throw std::system_error(built.error(),
      "build '" + request.output_pak_path.generic_string() + "'");
  }
  return std::move(built).value();
}

auto PakWorkspace::PackOrThrow(const pak::PakBuildRequest& request) -> PackedPak
{
  const auto result = Pack(request);
  const auto diagnostics = result.Diagnostics();
  if (!result.written || HasPakError(diagnostics)) {
    throw std::runtime_error("cannot pack '"
      + request.output_pak_path.generic_string() + "':\n"
      + PakDiagnosticSummary(diagnostics));
  }
  return Open(request.output_pak_path);
}

auto PakWorkspace::Open(const std::filesystem::path& path) const -> PackedPak
{
  return { storage_, path };
}

auto PakWorkspace::Text(const std::filesystem::path& path) const -> std::string
{
  const auto bytes = storage_.Bytes(path);
  auto text = std::string(bytes.size(), ' ');
  std::memcpy(text.data(), bytes.data(), bytes.size());
  return text;
}

} // namespace oxygen::cooker::testing
