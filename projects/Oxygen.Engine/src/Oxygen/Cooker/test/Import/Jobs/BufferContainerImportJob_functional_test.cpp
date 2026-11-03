//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/Jobs/BufferContainerImportJob.cpp

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/test/Support/AuthoredFiles.h>
#include <Oxygen/Cooker/test/Support/CookedFiles.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Cooker/test/Support/FileIo.h>
#include <Oxygen/Cooker/test/Support/ImportHarness.h>
#include <Oxygen/Cooker/test/Support/TestPaths.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/ResourceDescriptor.h>
#include <Oxygen/Serio/FileLock.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {

namespace {

  using nlohmann::json;
  using oxygen::cooker::test::ImportServiceTest;
  using oxygen::cooker::test::ReadBytes;
  using oxygen::cooker::test::WriteBytes;

  //! Reads a cooked buffer sidecar; throws when it is not the current format.
  auto ParseSidecar(const std::vector<std::byte>& bytes)
    -> data::BufferResourceDescriptor
  {
    auto decoded = data::DecodeBufferResourceDescriptor(bytes);
    if (!decoded) {
      throw std::runtime_error(decoded.error().what());
    }
    return std::move(*decoded);
  }

  class BufferContainerImportJobTest : public ImportServiceTest { };

  NOLINT_TEST_F(
    BufferContainerImportJobTest, SuccessfulJobEmitsExpectedArtifacts)
  {
    const auto cooked_root = TempDir() / "emits_expected_artifacts";
    std::filesystem::create_directories(cooked_root);
    const auto source_root = cooked_root.parent_path() / "source_data";
    const auto buffer_source = source_root / "character_vertices.buffer.bin";
    const auto buffer_bytes = std::array<std::byte, 32> {
      std::byte { 0x00 },
      std::byte { 0x01 },
      std::byte { 0x02 },
      std::byte { 0x03 },
      std::byte { 0x04 },
      std::byte { 0x05 },
      std::byte { 0x06 },
      std::byte { 0x07 },
      std::byte { 0x08 },
      std::byte { 0x09 },
      std::byte { 0x0A },
      std::byte { 0x0B },
      std::byte { 0x0C },
      std::byte { 0x0D },
      std::byte { 0x0E },
      std::byte { 0x0F },
      std::byte { 0x10 },
      std::byte { 0x11 },
      std::byte { 0x12 },
      std::byte { 0x13 },
      std::byte { 0x14 },
      std::byte { 0x15 },
      std::byte { 0x16 },
      std::byte { 0x17 },
      std::byte { 0x18 },
      std::byte { 0x19 },
      std::byte { 0x1A },
      std::byte { 0x1B },
      std::byte { 0x1C },
      std::byte { 0x1D },
      std::byte { 0x1E },
      std::byte { 0x1F },
    };
    WriteBytes(buffer_source, std::span<const std::byte>(buffer_bytes));

    auto descriptor_json = json {
      { "name", "CharacterBuffers" },
      {
        "buffers",
        json::array({
          json {
            {
              "source",
              oxygen::cooker::test::SpellReference(
                oxygen::cooker::test::SourceMountPath(
                  TempDir() / "inline.buffers.json"),
                oxygen::cooker::test::MountPathIn(TempDir(), buffer_source)),
            },
            {
              "virtual_path",
              "Buffers/character_vertices.obuf",
            },
            { "usage_flags", 3U },
            { "element_stride", 16U },
            { "alignment", 16U },
          },
        }),
      },
    };

    auto request = ImportRequest {};
    request.source_path = TempDir() / "inline.buffers.json";
    request.source_mount_path
      = oxygen::cooker::test::SourceMountPath(request.source_path);
    request.asset_virtual_path = "/.cooked/Resources/inline.buffers";
    request.cooked_root = cooked_root;
    request.source_mounts = oxygen::cooker::test::ContentMountTable(TempDir());
    request.buffer_container = ImportRequest::BufferContainerPayload {
      .normalized_descriptor_json = descriptor_json.dump(),
    };

    const auto report = Import(std::move(request));
    ASSERT_HAS_VALUE(report);
    ASSERT_TRUE(report->success)
      << oxygen::cooker::test::DiagnosticSummary(report->diagnostics);

    const auto has_error_diagnostic = std::ranges::any_of(
      report->diagnostics, [](const ImportDiagnostic& d) -> bool {
        return d.severity == ImportSeverity::kError;
      });
    EXPECT_FALSE(has_error_diagnostic);

    constexpr auto kSidecarRelPath
      = std::string_view { "Resources/Buffers/character_vertices.obuf" };
    constexpr auto kBuffersDataRelPath
      = std::string_view { "Resources/buffers.data" };
    constexpr auto kBuffersTableRelPath
      = std::string_view { "Resources/buffers.table" };

    const auto has_output = [&](const std::string_view relpath) -> bool {
      return std::ranges::any_of(
        report->outputs, [&](const ImportOutputRecord& output) -> bool {
          return oxygen::cooker::test::LayoutPathOf(output.path) == relpath;
        });
    };

    EXPECT_TRUE(has_output(kSidecarRelPath));
    EXPECT_TRUE(has_output(kBuffersDataRelPath));
    EXPECT_TRUE(has_output(kBuffersTableRelPath));

    EXPECT_TRUE(
      oxygen::cooker::test::HasCookedFile(cooked_root, kSidecarRelPath));
    EXPECT_TRUE(std::filesystem::exists(
      cooked_root / std::filesystem::path { kBuffersDataRelPath }));
    EXPECT_TRUE(
      oxygen::cooker::test::HasCookedFile(cooked_root, kBuffersTableRelPath));

    const auto sidecar = ParseSidecar(ReadBytes(
      oxygen::cooker::test::CookedFile(cooked_root, kSidecarRelPath)));
    EXPECT_NE(sidecar.index, data::pak::core::kNoResourceIndex);
    EXPECT_EQ(sidecar.descriptor.usage_flags, 3U);
    EXPECT_EQ(sidecar.descriptor.element_stride, 16U);
    EXPECT_EQ(sidecar.descriptor.size_bytes, buffer_bytes.size());
    EXPECT_EQ(sidecar.descriptor.content_hash,
      oxygen::base::ComputeSha256(std::as_bytes(std::span(buffer_bytes))));
    ASSERT_EQ(sidecar.views.size(), 1U);

    const auto& view0 = sidecar.views.at(0);
    EXPECT_EQ(view0.name, "__all__");
    EXPECT_EQ(view0.byte_offset, 0U);
    EXPECT_EQ(view0.byte_length, buffer_bytes.size());
    EXPECT_EQ(view0.element_offset, 0U);
    EXPECT_EQ(view0.element_count, buffer_bytes.size() / 16U);
  }

  NOLINT_TEST_F(
    BufferContainerImportJobTest, ExplicitViewsArePreservedAlongsideImplicitAll)
  {
    const auto cooked_root = TempDir() / "preserves_explicit_views";
    std::filesystem::create_directories(cooked_root);
    const auto source_root = cooked_root.parent_path() / "source_data";
    const auto buffer_source = source_root / "mesh_indices.buffer.bin";
    const auto buffer_bytes = std::array<std::byte, 32> {
      std::byte { 0x00 },
      std::byte { 0x00 },
      std::byte { 0x00 },
      std::byte { 0x00 },
      std::byte { 0x01 },
      std::byte { 0x00 },
      std::byte { 0x00 },
      std::byte { 0x00 },
      std::byte { 0x02 },
      std::byte { 0x00 },
      std::byte { 0x00 },
      std::byte { 0x00 },
      std::byte { 0x03 },
      std::byte { 0x00 },
      std::byte { 0x00 },
      std::byte { 0x00 },
      std::byte { 0x04 },
      std::byte { 0x00 },
      std::byte { 0x00 },
      std::byte { 0x00 },
      std::byte { 0x05 },
      std::byte { 0x00 },
      std::byte { 0x00 },
      std::byte { 0x00 },
      std::byte { 0x06 },
      std::byte { 0x00 },
      std::byte { 0x00 },
      std::byte { 0x00 },
      std::byte { 0x07 },
      std::byte { 0x00 },
      std::byte { 0x00 },
      std::byte { 0x00 },
    };
    WriteBytes(buffer_source, std::span<const std::byte>(buffer_bytes));

    auto descriptor_json = json {
      { "name", "MeshIndexBuffers" },
      {
        "buffers",
        json::array({
          json {
            {
              "source",
              oxygen::cooker::test::SpellReference(
                oxygen::cooker::test::SourceMountPath(
                  TempDir() / "inline.buffers.json"),
                oxygen::cooker::test::MountPathIn(TempDir(), buffer_source)),
            },
            { "virtual_path", "Buffers/mesh_indices.obuf" },
            { "usage_flags", 2U },
            { "element_format", 10U },
            {
              "views",
              json::array({
                json {
                  { "name", "lod0" },
                  { "element_offset", 0U },
                  { "element_count", 4U },
                },
              }),
            },
          },
        }),
      },
    };

    auto request = ImportRequest {};
    request.source_path = TempDir() / "inline.buffers.json";
    request.source_mount_path
      = oxygen::cooker::test::SourceMountPath(request.source_path);
    request.asset_virtual_path = "/.cooked/Resources/inline.buffers";
    request.cooked_root = cooked_root;
    request.source_mounts = oxygen::cooker::test::ContentMountTable(TempDir());
    request.buffer_container = ImportRequest::BufferContainerPayload {
      .normalized_descriptor_json = descriptor_json.dump(),
    };

    const auto report = Import(std::move(request));
    ASSERT_HAS_VALUE(report);
    ASSERT_TRUE(report->success)
      << oxygen::cooker::test::DiagnosticSummary(report->diagnostics);

    constexpr auto kSidecarRelPath
      = std::string_view { "Resources/Buffers/mesh_indices.obuf" };
    const auto sidecar = ParseSidecar(ReadBytes(
      oxygen::cooker::test::CookedFile(cooked_root, kSidecarRelPath)));
    ASSERT_EQ(sidecar.views.size(), 2U);

    const auto& view0 = sidecar.views.at(0);
    const auto& view1 = sidecar.views.at(1);
    EXPECT_EQ(view0.name, "__all__");
    EXPECT_EQ(view0.byte_offset, 0U);
    EXPECT_EQ(view0.byte_length, buffer_bytes.size());
    EXPECT_EQ(view0.element_offset, 0U);
    EXPECT_EQ(view0.element_count, buffer_bytes.size() / 4U);

    EXPECT_EQ(view1.name, "lod0");
    EXPECT_EQ(view1.byte_offset, 0U);
    EXPECT_EQ(view1.byte_length, 16U);
    EXPECT_EQ(view1.element_offset, 0U);
    EXPECT_EQ(view1.element_count, 4U);
  }

  //! A buffer whose source holds no bytes is refused with its code: a cooked
  //! resource always has a payload.
  NOLINT_TEST_F(BufferContainerImportJobTest, EmptyBufferSourceIsRefused)
  {
    const auto cooked_root = TempDir() / "refuses_empty_source";
    std::filesystem::create_directories(cooked_root);
    const auto buffer_source
      = cooked_root.parent_path() / "source_data" / "empty.buffer.bin";
    WriteBytes(buffer_source, std::span<const std::byte> {});

    auto descriptor_json = json {
      { "name", "EmptyBuffers" },
      {
        "buffers",
        json::array({
          json {
            {
              "source",
              oxygen::cooker::test::SpellReference(
                oxygen::cooker::test::SourceMountPath(
                  TempDir() / "inline.buffers.json"),
                oxygen::cooker::test::MountPathIn(TempDir(), buffer_source)),
            },
            { "virtual_path", "Buffers/empty.obuf" },
            { "usage_flags", 1U },
          },
        }),
      },
    };

    auto request = ImportRequest {};
    request.source_path = TempDir() / "inline.buffers.json";
    request.source_mount_path
      = oxygen::cooker::test::SourceMountPath(request.source_path);
    request.asset_virtual_path = "/.cooked/Resources/inline.buffers";
    request.cooked_root = cooked_root;
    request.source_mounts = oxygen::cooker::test::ContentMountTable(TempDir());
    request.buffer_container = ImportRequest::BufferContainerPayload {
      .normalized_descriptor_json = descriptor_json.dump(),
    };

    const auto report = Import(std::move(request));
    ASSERT_HAS_VALUE(report);
    EXPECT_FALSE(report->success);
    EXPECT_TRUE(oxygen::cooker::test::HasDiagnosticCode(
      report->diagnostics, "buffer.container.source_empty"))
      << oxygen::cooker::test::DiagnosticSummary(report->diagnostics);
  }

  /*!
   Scenario: another process is cooking the root. The import is refused with
   `import.cooked_root_busy` before it writes anything.
  */
  NOLINT_TEST_F(
    BufferContainerImportJobTest, ACookOfABusyRootIsRefusedUnwritten)
  {
    const auto cooked_root = TempDir() / "busy_root";
    std::filesystem::create_directories(cooked_root);
    const auto buffer_source
      = cooked_root.parent_path() / "source_data" / "busy.buffer.bin";
    const auto payload = std::array {
      std::byte { 1 },
      std::byte { 2 },
      std::byte { 3 },
      std::byte { 4 },
    };
    WriteBytes(buffer_source, payload);
    auto descriptor_json = json {
      { "name", "BusyBuffers" },
      {
        "buffers",
        json::array({
          json {
            {
              "source",
              oxygen::cooker::test::SpellReference(
                oxygen::cooker::test::SourceMountPath(
                  TempDir() / "busy.buffers.json"),
                oxygen::cooker::test::MountPathIn(TempDir(), buffer_source)),
            },
            { "virtual_path", "Buffers/busy.obuf" },
            { "usage_flags", 1U },
          },
        }),
      },
    };
    auto request = ImportRequest {};
    request.source_path = TempDir() / "busy.buffers.json";
    request.source_mount_path
      = oxygen::cooker::test::SourceMountPath(request.source_path);
    request.asset_virtual_path = "/.cooked/Resources/busy.buffers";
    request.cooked_root = cooked_root;
    request.source_mounts = oxygen::cooker::test::ContentMountTable(TempDir());
    request.buffer_container = ImportRequest::BufferContainerPayload {
      .normalized_descriptor_json = descriptor_json.dump(),
    };
    const auto other_writer = serio::FileLock::TryAcquire(
      cooked_root / std::string(data::loose_cooked::kWriterLockFileName),
      serio::FileLockMode::kExclusive, serio::FileLockOpenMode::kTemporary);
    ASSERT_HAS_VALUE(other_writer);

    const auto report = Import(std::move(request));

    ASSERT_HAS_VALUE(report);
    EXPECT_FALSE(report->success);
    EXPECT_TRUE(oxygen::cooker::test::HasDiagnosticCode(
      report->diagnostics, "import.cooked_root_busy"))
      << oxygen::cooker::test::DiagnosticSummary(report->diagnostics);
    EXPECT_FALSE(std::filesystem::exists(
      cooked_root / std::string(data::loose_cooked::kIndexFileName)));
    EXPECT_FALSE(std::filesystem::exists(cooked_root / "Resources"));
  }

} // namespace

} // namespace oxygen::content::import::test
