//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
#include <latch>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

#include <Oxygen/Base/Finally.h>
#include <Oxygen/Content/LooseCookedIndex.h>
#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportJobId.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {

namespace {

  using data::pak::core::kNoResourceIndex;
  using data::pak::core::ResourceIndexT;
  using data::pak::core::TextureResourceDesc;

#pragma pack(push, 1)
  struct TextureSidecarFile final {
    // NOLINTNEXTLINE(*-avoid-c-arrays) - fixed on-disk layout
    char magic[4] = { 'O', 'T', 'E', 'X' };
    uint16_t version = 1;
    uint16_t reserved = 0;
    ResourceIndexT resource_index = kNoResourceIndex;
    TextureResourceDesc descriptor {};
  };
#pragma pack(pop)

  static_assert(std::is_trivially_copyable_v<TextureSidecarFile>);

  auto MakeTempCookedRoot(const std::string_view suffix)
    -> std::filesystem::path
  {
    auto root = std::filesystem::temp_directory_path()
      / "oxygen_material_descriptor_import_job";
    root /= std::filesystem::path { std::string { suffix } };
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root);
    return root;
  }

  auto WriteBytesFile(
    const std::filesystem::path& path, std::span<const std::byte> bytes) -> void
  {
    std::filesystem::create_directories(path.parent_path());
    // NOLINTNEXTLINE(*-signed-bitwise)
    auto out = std::ofstream(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.is_open());
    // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
    out.write(reinterpret_cast<const char*>(bytes.data()),
      static_cast<std::streamsize>(bytes.size()));
    ASSERT_TRUE(out.good());
  }

  auto WriteTextureSidecar(const std::filesystem::path& path,
    const ResourceIndexT resource_index) -> void
  {
    auto file = TextureSidecarFile {};
    file.resource_index = resource_index;
    WriteBytesFile(
      path, std::as_bytes(std::span<const TextureSidecarFile, 1>(&file, 1)));
  }

  auto ReadBinaryFile(const std::filesystem::path& path)
    -> std::vector<std::byte>
  {
    auto in = std::ifstream(path, std::ios::binary);
    EXPECT_TRUE(in.is_open());
    if (!in.is_open()) {
      return {};
    }
    in.seekg(0, std::ios::end);
    const auto size = static_cast<size_t>(in.tellg());
    in.seekg(0, std::ios::beg);
    auto bytes = std::vector<std::byte>(size);
    // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
    in.read(reinterpret_cast<char*>(bytes.data()),
      static_cast<std::streamsize>(bytes.size()));
    EXPECT_TRUE(in.good() || in.eof());
    return bytes;
  }

  auto ReadMaterialDesc(const std::vector<std::byte>& bytes)
    -> data::pak::render::MaterialAssetDesc
  {
    auto desc = data::pak::render::MaterialAssetDesc {};
    if (bytes.size() < sizeof(desc)) {
      return desc;
    }
    std::memcpy(&desc, bytes.data(), sizeof(desc));
    return desc;
  }

  auto HasDiagnosticCode(const std::vector<ImportDiagnostic>& diagnostics,
    const std::string_view code) -> bool
  {
    return std::ranges::any_of(diagnostics,
      [code](const ImportDiagnostic& d) -> bool { return d.code == code; });
  }

  auto SubmitAndWait(AsyncImportService& service, ImportRequest request)
    -> ImportReport
  {
    auto report = ImportReport {};
    std::latch done(1);
    const auto submitted = service.SubmitImport(
      std::move(request),
      [&report, &done](
        const ImportJobId /*job_id*/, const ImportReport& completed) -> void {
        report = completed;
        done.count_down();
      },
      nullptr);
    if (!submitted.has_value()) {
      ADD_FAILURE() << "Material import submission failed";
      return report;
    }
    done.wait();
    return report;
  }

  class MaterialDescriptorImportJobTest : public testing::Test {
  protected:
    auto MakeRequest(const std::filesystem::path& cooked_root,
      std::string descriptor_json, std::string_view job_name = "woodfloor007")
      -> ImportRequest
    {
      auto request = ImportRequest {};
      request.source_path = "inline://material-descriptor";
      request.job_name = std::string(job_name);
      request.cooked_root = cooked_root;
      request.loose_cooked_layout.virtual_mount_root = "/.cooked";
      request.material_descriptor = ImportRequest::MaterialDescriptorPayload {
        .normalized_descriptor_json = std::move(descriptor_json),
      };
      return request;
    }
  };

  NOLINT_TEST_F(MaterialDescriptorImportJobTest,
    CompilesEmissionColorAndIntensityWithoutBinary16Rounding)
  {
    const auto cooked_root = MakeTempCookedRoot("float32_emission");
    auto service = AsyncImportService(AsyncImportService::Config {
      .thread_pool_size = 2U,
    });
    const auto stop_service
      = oxygen::Finally([&service] -> void { service.Stop(); });
    const auto report = SubmitAndWait(service,
      MakeRequest(cooked_root, R"({
      "name": "HdrEmission",
      "parameters": {
        "emissive_color": [1.0, 0.25, 0.0],
        "emissive_intensity": 9.7
      }
    })",
        "HdrEmission"));
    ASSERT_TRUE(report.success);
    const auto bytes
      = ReadBinaryFile(cooked_root / "Materials" / "HdrEmission.omat");
    ASSERT_GE(bytes.size(), sizeof(data::pak::render::MaterialAssetDesc));
    const auto desc = ReadMaterialDesc(bytes);
    EXPECT_EQ(desc.header.version, data::pak::render::kMaterialAssetVersion);
    EXPECT_EQ(desc.emissive_factor[0], 9.7F);
    EXPECT_EQ(desc.emissive_factor[1], 9.7F * 0.25F);
    EXPECT_EQ(desc.emissive_factor[2], 0.0F);
  }

  NOLINT_TEST_F(MaterialDescriptorImportJobTest,
    ResolvesHashedTextureDescriptorVirtualPathAndEmitsMaterial)
  {
    const auto cooked_root
      = MakeTempCookedRoot("resolve_hashed_texture_virtual_path");
    const auto sidecar_path = cooked_root / "Resources" / "Textures"
      / "woodfloor007_color_1234567890abcdef.otex";
    constexpr auto kTextureIndex = ResourceIndexT { 7U };
    WriteTextureSidecar(sidecar_path, kTextureIndex);

    auto service = AsyncImportService(AsyncImportService::Config {
      .thread_pool_size = 2U,
    });
    const auto stop_service
      = oxygen::Finally([&service] -> void { service.Stop(); });

    const auto report = SubmitAndWait(service, MakeRequest(cooked_root, R"({
      "name": "woodfloor007",
      "domain": "opaque",
      "textures": {
        "base_color": {
          "virtual_path": "/.cooked/Resources/Textures/woodfloor007_color.otex"
        }
      }
    })"));

    EXPECT_TRUE(report.success);
    EXPECT_EQ(report.materials_written, 1U);
    EXPECT_FALSE(HasDiagnosticCode(
      report.diagnostics, "material.descriptor.texture_descriptor_missing"));

    const auto material_path = cooked_root / "Materials" / "woodfloor007.omat";
    ASSERT_TRUE(std::filesystem::exists(material_path));
    const auto bytes = ReadBinaryFile(material_path);
    ASSERT_GE(bytes.size(), sizeof(data::pak::render::MaterialAssetDesc));
    const auto desc = ReadMaterialDesc(bytes);
    EXPECT_EQ(desc.base_color_texture.get(), 0U);
    const auto index = lc::LooseCookedIndex::LoadFromRoot(cooked_root);
    const auto key
      = index.FindAssetKeyByVirtualPath("/.cooked/Materials/woodfloor007.omat");
    ASSERT_TRUE(key.has_value());
    // NOLINTBEGIN(bugprone-unchecked-optional-access) - asserted above
    const auto references = index.FindAssetReferences(key.value());
    ASSERT_TRUE(references.has_value());
    EXPECT_THAT(references.value().Resources(),
      ::testing::ElementsAre(data::ResourceBinding {
        .kind = data::ResourceKind::kTexture,
        .index = kTextureIndex,
      }));
    // NOLINTEND(bugprone-unchecked-optional-access)
  }

  //! A deleted texture leaves its slot unbound with a warning, so the material
  //! and everything that depends on it still cook.
  NOLINT_TEST_F(MaterialDescriptorImportJobTest,
    MissingTextureDescriptorCooksMaterialWithoutTextureAndWarns)
  {
    const auto cooked_root = MakeTempCookedRoot("missing_texture_descriptor");
    auto service = AsyncImportService(AsyncImportService::Config {
      .thread_pool_size = 2U,
    });
    const auto stop_service
      = oxygen::Finally([&service] -> void { service.Stop(); });

    const auto report = SubmitAndWait(service, MakeRequest(cooked_root, R"({
      "name": "woodfloor007",
      "domain": "opaque",
      "textures": {
        "base_color": {
          "virtual_path": "/.cooked/Resources/Textures/woodfloor007_color.otex"
        }
      }
    })"));

    EXPECT_TRUE(report.success);
    EXPECT_EQ(report.materials_written, 1U);
    const auto missing = std::ranges::find_if(
      report.diagnostics, [](const ImportDiagnostic& diagnostic) -> bool {
        return diagnostic.code
          == "material.descriptor.texture_descriptor_missing";
      });
    ASSERT_NE(missing, report.diagnostics.end());
    EXPECT_EQ(missing->severity, ImportSeverity::kWarning);

    const auto bytes
      = ReadBinaryFile(cooked_root / "Materials" / "woodfloor007.omat");
    ASSERT_GE(bytes.size(), sizeof(data::pak::render::MaterialAssetDesc));
    EXPECT_EQ(
      ReadMaterialDesc(bytes).base_color_texture, data::kNoResourceReference);
  }

} // namespace

} // namespace oxygen::content::import::test
