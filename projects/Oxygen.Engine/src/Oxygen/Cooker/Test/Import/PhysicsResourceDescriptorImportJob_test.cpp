//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <latch>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <Oxygen/Base/Finally.h>
#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Data/PakFormat.h>

namespace oxygen::content::import::test {

namespace {

  constexpr uint16_t kPhysicsResourceSidecarVersion = 1;

#pragma pack(push, 1)
  struct PhysicsResourceSidecarFile final {
    char magic[4] = { 'O', 'P', 'R', 'S' };
    uint16_t version = kPhysicsResourceSidecarVersion;
    uint16_t reserved = 0;
    data::pak::core::ResourceIndexT resource_index
      = data::pak::core::kNoResourceIndex;
    data::pak::physics::PhysicsResourceDesc descriptor {};
  };
#pragma pack(pop)

  static_assert(std::is_trivially_copyable_v<PhysicsResourceSidecarFile>);

  auto HasDiagnosticCode(const std::vector<ImportDiagnostic>& diagnostics,
    const std::string_view code) -> bool
  {
    return std::ranges::any_of(
      diagnostics, [code](const ImportDiagnostic& diagnostic) {
        return diagnostic.code == code;
      });
  }

  auto MakeTempCookedRoot(const std::string_view suffix)
    -> std::filesystem::path
  {
    auto root = std::filesystem::temp_directory_path()
      / "oxygen_physics_resource_descriptor_import_job";
    root /= std::filesystem::path { std::string { suffix } };
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root);
    return root;
  }

  auto WriteBytesFile(const std::filesystem::path& path,
    const std::span<const std::byte> bytes) -> void
  {
    std::filesystem::create_directories(path.parent_path());
    auto out = std::ofstream(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.is_open());
    out.write(reinterpret_cast<const char*>(bytes.data()),
      static_cast<std::streamsize>(bytes.size()));
    ASSERT_TRUE(out.good());
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
    in.read(reinterpret_cast<char*>(bytes.data()),
      static_cast<std::streamsize>(bytes.size()));
    EXPECT_TRUE(in.good() || in.eof());
    return bytes;
  }

  auto SubmitAndWait(AsyncImportService& service, ImportRequest request)
    -> ImportReport
  {
    auto report = ImportReport {};
    std::latch done(1);
    const auto submitted = service.SubmitImport(
      std::move(request),
      [&report, &done](
        const ImportJobId /*job_id*/, const ImportReport& completed) {
        report = completed;
        done.count_down();
      },
      nullptr);
    EXPECT_TRUE(submitted.has_value());
    done.wait();
    return report;
  }

  auto MakePhysicsResourceRequest(const std::filesystem::path& cooked_root,
    const std::filesystem::path& source_root,
    const std::filesystem::path& source_path, std::string virtual_path)
    -> ImportRequest
  {
    auto request = ImportRequest {};
    request.source_path = source_root / "resource_descriptor.resource.json";
    request.cooked_root = cooked_root;
    request.loose_cooked_layout.virtual_mount_root = "/.cooked";
    request.physics_resource_descriptor
      = ImportRequest::PhysicsResourceDescriptorPayload {
          .normalized_descriptor_json = std::string { R"({
            "name": "physics_resource_test",
            "source": ")" }
            + source_path.generic_string() + R"(",
            "format": "jolt_constraint_binary",
            "virtual_path": ")"
            + virtual_path + R"("
          })",
        };
    return request;
  }

  NOLINT_TEST(
    PhysicsResourceDescriptorImportJobTest, SuccessfulJobEmitsExpectedArtifacts)
  {
    const auto cooked_root = MakeTempCookedRoot("emits_expected_artifacts");
    const auto source_root = cooked_root.parent_path() / "source_data";
    const auto source_path = source_root / "park_hinge_joint_a.jphbin";
    const auto payload = std::array<std::byte, 12> {
      std::byte { 0x11 },
      std::byte { 0x22 },
      std::byte { 0x33 },
      std::byte { 0x44 },
      std::byte { 0x55 },
      std::byte { 0x66 },
      std::byte { 0x77 },
      std::byte { 0x88 },
      std::byte { 0x99 },
      std::byte { 0xAA },
      std::byte { 0xBB },
      std::byte { 0xCC },
    };
    WriteBytesFile(source_path, payload);

    auto request = ImportRequest {};
    request.source_path = source_root / "park_hinge_joint_a.resource.json";
    request.cooked_root = cooked_root;
    request.loose_cooked_layout.virtual_mount_root = "/.cooked";
    request.physics_resource_descriptor
      = ImportRequest::PhysicsResourceDescriptorPayload {
          .normalized_descriptor_json = std::string { R"({
            "name": "park_hinge_joint_a",
            "source": ")" }
            + source_path.generic_string() + R"(",
            "format": "jolt_constraint_binary",
            "virtual_path": "/.cooked/Physics/Resources/park_hinge_joint_a.opres"
          })",
        };

    auto service = AsyncImportService(AsyncImportService::Config {
      .thread_pool_size = 2U,
    });
    [[maybe_unused]] auto stop_service
      = oxygen::Finally([&service]() { service.Stop(); });

    const auto report = SubmitAndWait(service, std::move(request));
    EXPECT_TRUE(report.success);

    constexpr auto kSidecarRelPath
      = std::string_view { "Physics/Resources/park_hinge_joint_a.opres" };
    constexpr auto kPhysicsDataRelPath
      = std::string_view { "Physics/Resources/physics.data" };
    constexpr auto kPhysicsTableRelPath
      = std::string_view { "Physics/Resources/physics.table" };

    const auto has_output = [&](const std::string_view relpath) {
      return std::ranges::any_of(
        report.outputs, [&](const ImportOutputRecord& output) {
          return output.path == relpath;
        });
    };
    EXPECT_TRUE(has_output(kSidecarRelPath));
    EXPECT_TRUE(has_output(kPhysicsDataRelPath));
    EXPECT_TRUE(has_output(kPhysicsTableRelPath));

    const auto sidecar_full_path
      = cooked_root / std::filesystem::path { kSidecarRelPath };
    ASSERT_TRUE(std::filesystem::exists(sidecar_full_path));
    ASSERT_TRUE(std::filesystem::exists(
      cooked_root / std::filesystem::path { kPhysicsDataRelPath }));
    ASSERT_TRUE(std::filesystem::exists(
      cooked_root / std::filesystem::path { kPhysicsTableRelPath }));

    const auto sidecar_bytes = ReadBinaryFile(sidecar_full_path);
    ASSERT_GE(sidecar_bytes.size(), sizeof(PhysicsResourceSidecarFile));
    auto sidecar = PhysicsResourceSidecarFile {};
    std::memcpy(&sidecar, sidecar_bytes.data(), sizeof(sidecar));
    EXPECT_EQ(sidecar.magic[0], 'O');
    EXPECT_EQ(sidecar.magic[1], 'P');
    EXPECT_EQ(sidecar.magic[2], 'R');
    EXPECT_EQ(sidecar.magic[3], 'S');
    EXPECT_EQ(sidecar.version, kPhysicsResourceSidecarVersion);
    EXPECT_NE(sidecar.resource_index, data::pak::core::kNoResourceIndex);
    EXPECT_EQ(sidecar.descriptor.format,
      data::pak::physics::PhysicsResourceFormat::kJoltConstraintBinary);
    EXPECT_EQ(sidecar.descriptor.size_bytes, payload.size());
  }

  NOLINT_TEST(
    PhysicsResourceDescriptorImportJobTest, MissingSourceProducesDiagnostic)
  {
    const auto cooked_root = MakeTempCookedRoot("missing_source");
    const auto source_root = cooked_root.parent_path() / "source_data";
    const auto missing_source_path = source_root / "missing_payload.jphbin";

    auto request = ImportRequest {};
    request.source_path = source_root / "missing.resource.json";
    request.cooked_root = cooked_root;
    request.loose_cooked_layout.virtual_mount_root = "/.cooked";
    request.physics_resource_descriptor
      = ImportRequest::PhysicsResourceDescriptorPayload {
          .normalized_descriptor_json = std::string { R"({
            "name": "missing_resource",
            "source": ")" }
            + missing_source_path.generic_string() + R"(",
            "format": "jolt_shape_binary",
            "virtual_path": "/.cooked/Physics/Resources/missing_resource.opres"
          })",
        };

    auto service = AsyncImportService(AsyncImportService::Config {
      .thread_pool_size = 2U,
    });
    [[maybe_unused]] auto stop_service
      = oxygen::Finally([&service]() { service.Stop(); });

    const auto report = SubmitAndWait(service, std::move(request));
    EXPECT_FALSE(report.success);
    EXPECT_TRUE(HasDiagnosticCode(
      report.diagnostics, "physics.resource.source_read_failed"));
  }

  NOLINT_TEST(PhysicsResourceDescriptorImportJobTest,
    EquivalentPayloadWithCanonicalVirtualPathReimportSucceeds)
  {
    const auto cooked_root
      = MakeTempCookedRoot("canonical_virtual_path_reimport_succeeds");
    const auto source_root = cooked_root.parent_path() / "source_data";
    const auto source_path = source_root / "park_hinge_joint_a.jphbin";
    const auto payload = std::array<std::byte, 12> {
      std::byte { 0x41 },
      std::byte { 0x42 },
      std::byte { 0x43 },
      std::byte { 0x44 },
      std::byte { 0x45 },
      std::byte { 0x46 },
      std::byte { 0x47 },
      std::byte { 0x48 },
      std::byte { 0x49 },
      std::byte { 0x4A },
      std::byte { 0x4B },
      std::byte { 0x4C },
    };
    WriteBytesFile(source_path, payload);

    constexpr auto kCanonicalVirtualPath
      = "/.cooked/Physics/Resources/park_hinge_joint_a.opres";

    auto service = AsyncImportService(AsyncImportService::Config {
      .thread_pool_size = 2U,
    });
    [[maybe_unused]] auto stop_service
      = oxygen::Finally([&service]() { service.Stop(); });

    const auto first = SubmitAndWait(service,
      MakePhysicsResourceRequest(
        cooked_root, source_root, source_path, kCanonicalVirtualPath));
    EXPECT_TRUE(first.success);

    const auto second = SubmitAndWait(service,
      MakePhysicsResourceRequest(
        cooked_root, source_root, source_path, kCanonicalVirtualPath));
    EXPECT_TRUE(second.success);
  }

  NOLINT_TEST(PhysicsResourceDescriptorImportJobTest,
    EquivalentPayloadWithDifferentVirtualPathFailsCanonicalConflict)
  {
    const auto cooked_root
      = MakeTempCookedRoot("different_virtual_path_fails_canonical_conflict");
    const auto source_root = cooked_root.parent_path() / "source_data";
    const auto source_path = source_root / "park_hinge_joint_a.jphbin";
    const auto payload = std::array<std::byte, 16> {
      std::byte { 0x31 },
      std::byte { 0x32 },
      std::byte { 0x33 },
      std::byte { 0x34 },
      std::byte { 0x35 },
      std::byte { 0x36 },
      std::byte { 0x37 },
      std::byte { 0x38 },
      std::byte { 0x39 },
      std::byte { 0x3A },
      std::byte { 0x3B },
      std::byte { 0x3C },
      std::byte { 0x3D },
      std::byte { 0x3E },
      std::byte { 0x3F },
      std::byte { 0x40 },
    };
    WriteBytesFile(source_path, payload);

    constexpr auto kCanonicalVirtualPath
      = "/.cooked/Physics/Resources/shared_resource.opres";
    constexpr auto kConflictingVirtualPath
      = "/.cooked/Physics/Resources/shared_resource_alias.opres";

    auto service = AsyncImportService(AsyncImportService::Config {
      .thread_pool_size = 2U,
    });
    [[maybe_unused]] auto stop_service
      = oxygen::Finally([&service]() { service.Stop(); });

    const auto first = SubmitAndWait(service,
      MakePhysicsResourceRequest(
        cooked_root, source_root, source_path, kCanonicalVirtualPath));
    EXPECT_TRUE(first.success);

    const auto second = SubmitAndWait(service,
      MakePhysicsResourceRequest(
        cooked_root, source_root, source_path, kConflictingVirtualPath));
    EXPECT_FALSE(second.success);
    EXPECT_TRUE(HasDiagnosticCode(
      second.diagnostics, "physics.resource.dedup_virtual_path_conflict"));
  }

} // namespace

} // namespace oxygen::content::import::test
