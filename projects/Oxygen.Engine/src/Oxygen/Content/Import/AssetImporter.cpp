//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Content/Import/AssetImporter.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Import/CookedContentWriter.h>
#include <Oxygen/Content/Import/ImportDiagnostics.h>
#include <Oxygen/Content/Import/LooseCookedWriter.h>

namespace oxygen::content::import {

auto CreateFbxImporter() -> std::unique_ptr<Importer>;

namespace {

  class LooseCookedContentWriter final : public CookedContentWriter {
  public:
    LooseCookedContentWriter(LooseCookedWriter& writer, ImportReport& report)
      : writer_(writer)
      , report_(report)
    {
    }

    auto AddDiagnostic(ImportDiagnostic diag) -> void override
    {
      report_.diagnostics.push_back(std::move(diag));
    }

    auto WriteAssetDescriptor(const data::AssetKey& key,
      const data::AssetType asset_type, const std::string_view virtual_path,
      const std::string_view descriptor_relpath,
      const std::span<const std::byte> bytes) -> void override
    {
      writer_.WriteAssetDescriptor(
        key, asset_type, virtual_path, descriptor_relpath, bytes);
    }

    auto WriteFile(const data::loose_cooked::v1::FileKind kind,
      const std::string_view relpath, const std::span<const std::byte> bytes)
      -> void override
    {
      writer_.WriteFile(kind, relpath, bytes);
    }

    auto RegisterExternalFile(const data::loose_cooked::v1::FileKind kind,
      const std::string_view relpath) -> void override
    {
      writer_.RegisterExternalFile(kind, relpath);
    }

    auto OnMaterialsWritten(const uint32_t count) -> void override
    {
      report_.materials_written += count;
    }

    auto OnGeometryWritten(const uint32_t count) -> void override
    {
      report_.geometry_written += count;
    }

    auto OnScenesWritten(const uint32_t count) -> void override
    {
      report_.scenes_written += count;
    }

  private:
    LooseCookedWriter& writer_;
    ImportReport& report_;
  };

  [[nodiscard]] auto ToLowerAscii(std::string s) -> std::string
  {
    std::transform(s.begin(), s.end(), s.begin(), [](const unsigned char ch) {
      return static_cast<char>(std::tolower(ch));
    });
    return s;
  }

} // namespace

struct AssetImporter::Impl final {
  explicit Impl(std::vector<std::unique_ptr<Importer>> backends)
    : backends_(std::move(backends))
  {
  }

  [[nodiscard]] auto FindBackend(const ImportFormat format) const
    -> const Importer*
  {
    for (const auto& backend : backends_) {
      if (backend && backend->Supports(format)) {
        return backend.get();
      }
    }
    return nullptr;
  }

  [[nodiscard]] auto FindBackend(const ImportFormat format) -> Importer*
  {
    for (auto& backend : backends_) {
      if (backend && backend->Supports(format)) {
        return backend.get();
      }
    }
    return nullptr;
  }

  std::vector<std::unique_ptr<Importer>> backends_;
};

AssetImporter::AssetImporter()
  : impl_([&]() {
    auto backends = std::vector<std::unique_ptr<Importer>> {};
    backends.push_back(CreateFbxImporter());
    return std::make_unique<Impl>(std::move(backends));
  }())
{
}

AssetImporter::AssetImporter(std::vector<std::unique_ptr<Importer>> backends)
  : impl_(std::make_unique<Impl>(std::move(backends)))
{
}

AssetImporter::~AssetImporter() = default;

auto AssetImporter::ImportToLooseCooked(const ImportRequest& request)
  -> ImportReport
{
  if (request.source_path.empty()) {
    throw std::runtime_error("ImportRequest.source_path must not be empty");
  }

  if (request.cooked_root.has_value() && !request.cooked_root->is_absolute()) {
    throw std::runtime_error("ImportRequest.cooked_root must be absolute");
  }

  const auto format = DetectFormat(request.source_path);
  if (format == ImportFormat::kUnknown) {
    throw std::runtime_error("Unknown import format");
  }

  auto* backend = impl_->FindBackend(format);
  if (backend == nullptr) {
    throw std::runtime_error("No importer backend supports this format");
  }

  const auto cooked_root = request.cooked_root.value_or(
    std::filesystem::absolute(request.source_path.parent_path()));

  auto source_path_str = request.source_path.string();
  auto cooked_root_str = cooked_root.string();

  LOG_SCOPE_F(INFO, "AssetImporter::ImportToLooseCooked {} -> {}",
    source_path_str.c_str(), cooked_root_str.c_str());
  LOG_F(INFO, "Using backend '{}'", backend->Name());

  ImportReport report {
    .cooked_root = cooked_root,
  };

  LooseCookedWriter writer(cooked_root);
  writer.SetSourceKey(request.source_key);

  LooseCookedContentWriter out(writer, report);

  backend->Import(request, out);

  const auto result = writer.Finish();

  report.cooked_root = result.cooked_root;
  report.source_key = result.source_key;
  report.success = true;

  return report;
}

auto AssetImporter::DetectFormat(const std::filesystem::path& path) const
  -> ImportFormat
{
  const auto ext = ToLowerAscii(path.extension().string());

  if (ext == ".gltf") {
    return ImportFormat::kGltf;
  }
  if (ext == ".glb") {
    return ImportFormat::kGlb;
  }
  if (ext == ".fbx") {
    return ImportFormat::kFbx;
  }

  return ImportFormat::kUnknown;
}

} // namespace oxygen::content::import
