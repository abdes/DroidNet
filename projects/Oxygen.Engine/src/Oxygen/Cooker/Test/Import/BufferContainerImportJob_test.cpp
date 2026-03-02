//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <latch>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Base/Finally.h>
#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Data/PakFormat_core.h>

namespace oxygen::content::import::test {

namespace {

  using nlohmann::json;
  constexpr uint16_t kBufferSidecarVersion = 2;

#pragma pack(push, 1)
  struct BufferSidecarHeader final {
    char magic[4] = { 'O', 'B', 'U', 'F' };
    uint16_t version = kBufferSidecarVersion;
    uint16_t reserved = 0;
    data::pak::core::ResourceIndexT resource_index
      = data::pak::core::kNoResourceIndex;
    data::pak::core::BufferResourceDesc descriptor {};
    uint32_t view_count = 0;
    uint32_t reserved_views = 0;
  };

  struct BufferSidecarViewEntry final {
    char name[data::pak::core::kMaxNameSize] = {};
    uint64_t byte_offset = 0;
    uint64_t byte_length = 0;
    uint64_t element_offset = 0;
    uint64_t element_count = 0;
  };
#pragma pack(pop)

  [[nodiscard]] auto DecodeFixedName(const char* raw_name) -> std::string
  {
    size_t len = 0;
    while (len < data::pak::core::kMaxNameSize && raw_name[len] != '\0') {
      ++len;
    }
    return std::string(raw_name, len);
  }

  auto MakeTempCookedRoot(const std::string_view suffix)
    -> std::filesystem::path
  {
    auto root = std::filesystem::temp_directory_path()
      / "oxygen_buffer_container_import_job";
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

  NOLINT_TEST(BufferContainerImportJobTest, SuccessfulJobEmitsExpectedArtifacts)
  {
    const auto cooked_root = MakeTempCookedRoot("emits_expected_artifacts");
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
    WriteBytesFile(buffer_source, std::span<const std::byte>(buffer_bytes));

    auto descriptor_json = json {
      { "name", "CharacterBuffers" },
      { "buffers",
        json::array({
          json {
            { "source", buffer_source.generic_string() },
            { "virtual_path",
              "/.cooked/Resources/Buffers/character_vertices.obuf" },
            { "usage_flags", 3U },
            { "element_stride", 16U },
            { "alignment", 16U },
          },
        }) },
    };

    auto request = ImportRequest {};
    request.source_path = "inline://buffer-container";
    request.cooked_root = cooked_root;
    request.loose_cooked_layout.virtual_mount_root = "/.cooked";
    request.buffer_container = ImportRequest::BufferContainerPayload {
      .normalized_descriptor_json = descriptor_json.dump(),
    };

    auto service = AsyncImportService(AsyncImportService::Config {
      .thread_pool_size = 2U,
    });
    [[maybe_unused]] auto stop_service
      = oxygen::Finally([&service]() { service.Stop(); });

    const auto report = SubmitAndWait(service, std::move(request));
    EXPECT_TRUE(report.success);

    const auto has_error_diagnostic
      = std::ranges::any_of(report.diagnostics, [](const ImportDiagnostic& d) {
          return d.severity == ImportSeverity::kError;
        });
    EXPECT_FALSE(has_error_diagnostic);

    constexpr auto kSidecarRelPath
      = std::string_view { "Resources/Buffers/character_vertices.obuf" };
    constexpr auto kBuffersDataRelPath
      = std::string_view { "Resources/buffers.data" };
    constexpr auto kBuffersTableRelPath
      = std::string_view { "Resources/buffers.table" };

    const auto has_output = [&](const std::string_view relpath) {
      return std::ranges::any_of(
        report.outputs, [&](const ImportOutputRecord& output) {
          return output.path == relpath;
        });
    };

    EXPECT_TRUE(has_output(kSidecarRelPath));
    EXPECT_TRUE(has_output(kBuffersDataRelPath));
    EXPECT_TRUE(has_output(kBuffersTableRelPath));

    EXPECT_TRUE(std::filesystem::exists(
      cooked_root / std::filesystem::path { kSidecarRelPath }));
    EXPECT_TRUE(std::filesystem::exists(
      cooked_root / std::filesystem::path { kBuffersDataRelPath }));
    EXPECT_TRUE(std::filesystem::exists(
      cooked_root / std::filesystem::path { kBuffersTableRelPath }));

    const auto sidecar_bytes
      = ReadBinaryFile(cooked_root / std::filesystem::path { kSidecarRelPath });
    ASSERT_GE(sidecar_bytes.size(), sizeof(BufferSidecarHeader));

    auto sidecar = BufferSidecarHeader {};
    std::memcpy(&sidecar, sidecar_bytes.data(), sizeof(sidecar));

    EXPECT_EQ(sidecar.magic[0], 'O');
    EXPECT_EQ(sidecar.magic[1], 'B');
    EXPECT_EQ(sidecar.magic[2], 'U');
    EXPECT_EQ(sidecar.magic[3], 'F');
    EXPECT_EQ(sidecar.version, kBufferSidecarVersion);
    EXPECT_NE(sidecar.resource_index, data::pak::core::kNoResourceIndex);
    EXPECT_EQ(sidecar.descriptor.usage_flags, 3U);
    EXPECT_EQ(sidecar.descriptor.element_stride, 16U);
    EXPECT_EQ(sidecar.descriptor.size_bytes, buffer_bytes.size());
    ASSERT_EQ(sidecar.view_count, 1U);

    const auto views_offset = sizeof(BufferSidecarHeader);
    ASSERT_GE(sidecar_bytes.size(),
      views_offset + sizeof(BufferSidecarViewEntry) * sidecar.view_count);
    auto view0 = BufferSidecarViewEntry {};
    std::memcpy(&view0, sidecar_bytes.data() + views_offset, sizeof(view0));
    EXPECT_EQ(DecodeFixedName(view0.name), "__all__");
    EXPECT_EQ(view0.byte_offset, 0U);
    EXPECT_EQ(view0.byte_length, buffer_bytes.size());
    EXPECT_EQ(view0.element_offset, 0U);
    EXPECT_EQ(view0.element_count, buffer_bytes.size() / 16U);
  }

  NOLINT_TEST(
    BufferContainerImportJobTest, ExplicitViewsArePreservedAlongsideImplicitAll)
  {
    const auto cooked_root = MakeTempCookedRoot("preserves_explicit_views");
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
    WriteBytesFile(buffer_source, std::span<const std::byte>(buffer_bytes));

    auto descriptor_json = json {
      { "name", "MeshIndexBuffers" },
      { "buffers",
        json::array({
          json {
            { "source", buffer_source.generic_string() },
            { "virtual_path", "/.cooked/Resources/Buffers/mesh_indices.obuf" },
            { "usage_flags", 2U },
            { "element_format", 10U },
            { "views",
              json::array({
                json {
                  { "name", "lod0" },
                  { "element_offset", 0U },
                  { "element_count", 4U },
                },
              }) },
          },
        }) },
    };

    auto request = ImportRequest {};
    request.source_path = "inline://buffer-container";
    request.cooked_root = cooked_root;
    request.loose_cooked_layout.virtual_mount_root = "/.cooked";
    request.buffer_container = ImportRequest::BufferContainerPayload {
      .normalized_descriptor_json = descriptor_json.dump(),
    };

    auto service = AsyncImportService(AsyncImportService::Config {
      .thread_pool_size = 2U,
    });
    [[maybe_unused]] auto stop_service
      = oxygen::Finally([&service]() { service.Stop(); });
    const auto report = SubmitAndWait(service, std::move(request));
    EXPECT_TRUE(report.success);

    constexpr auto kSidecarRelPath
      = std::string_view { "Resources/Buffers/mesh_indices.obuf" };
    const auto sidecar_bytes
      = ReadBinaryFile(cooked_root / std::filesystem::path { kSidecarRelPath });
    ASSERT_GE(sidecar_bytes.size(), sizeof(BufferSidecarHeader));

    auto sidecar = BufferSidecarHeader {};
    std::memcpy(&sidecar, sidecar_bytes.data(), sizeof(sidecar));
    ASSERT_EQ(sidecar.view_count, 2U);

    const auto views_offset = sizeof(BufferSidecarHeader);
    auto view0 = BufferSidecarViewEntry {};
    auto view1 = BufferSidecarViewEntry {};
    std::memcpy(&view0, sidecar_bytes.data() + views_offset, sizeof(view0));
    std::memcpy(&view1,
      sidecar_bytes.data() + views_offset + sizeof(BufferSidecarViewEntry),
      sizeof(view1));

    EXPECT_EQ(DecodeFixedName(view0.name), "__all__");
    EXPECT_EQ(view0.byte_offset, 0U);
    EXPECT_EQ(view0.byte_length, buffer_bytes.size());
    EXPECT_EQ(view0.element_offset, 0U);
    EXPECT_EQ(view0.element_count, buffer_bytes.size() / 4U);

    EXPECT_EQ(DecodeFixedName(view1.name), "lod0");
    EXPECT_EQ(view1.byte_offset, 0U);
    EXPECT_EQ(view1.byte_length, 16U);
    EXPECT_EQ(view1.element_offset, 0U);
    EXPECT_EQ(view1.element_count, 4U);
  }

} // namespace

} // namespace oxygen::content::import::test
