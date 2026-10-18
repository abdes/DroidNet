//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/Jobs/BufferContainerImportJob.cpp

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Cooker/test/Support/FileIo.h>
#include <Oxygen/Cooker/test/Support/ImportHarness.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {

namespace {

  using nlohmann::json;
  using oxygen::cooker::test::ImportServiceTest;
  using oxygen::cooker::test::ReadBytes;
  using oxygen::cooker::test::WriteBytes;

  // The cooker-internal `.obuf` sidecar layout (version 3), mirrored to read
  // the cooked output.
#pragma pack(push, 1)
  struct BufferSidecarHeader final {
    char magic[4] = { 'O', 'B', 'U', 'F' };
    uint16_t version = 3;
    uint16_t reserved = 0;
    data::pak::core::ResourceIndexT resource_index
      = data::pak::core::kNoResourceIndex;
    data::pak::core::BufferResourceDesc descriptor {};
    uint32_t view_count = 0;
    uint32_t strings_size = 0;
  };

  struct BufferSidecarViewEntry final {
    uint32_t name_offset = 0;
    uint64_t byte_offset = 0;
    uint64_t byte_length = 0;
    uint64_t element_offset = 0;
    uint64_t element_count = 0;
  };
#pragma pack(pop)

  struct SidecarView final {
    std::string name;
    uint64_t byte_offset = 0;
    uint64_t byte_length = 0;
    uint64_t element_offset = 0;
    uint64_t element_count = 0;
  };

  struct Sidecar final {
    data::pak::core::ResourceIndexT resource_index
      = data::pak::core::kNoResourceIndex;
    data::pak::core::BufferResourceDesc descriptor {};
    std::vector<SidecarView> views;
  };

  //! Reads a cooked buffer sidecar; throws when it is not version 3.
  auto ParseSidecar(const std::vector<std::byte>& bytes) -> Sidecar
  {
    auto header = BufferSidecarHeader {};
    if (bytes.size() < sizeof(header)) {
      throw std::runtime_error("buffer sidecar is truncated");
    }
    std::memcpy(&header, bytes.data(), sizeof(header));
    if (std::string_view(std::data(header.magic), 4U) != "OBUF"
      || header.version != 3U) {
      throw std::runtime_error("buffer sidecar magic or version is wrong");
    }
    const auto entries_size
      = size_t { header.view_count } * sizeof(BufferSidecarViewEntry);
    if (bytes.size() != sizeof(header) + entries_size + header.strings_size) {
      throw std::runtime_error("buffer sidecar size is wrong");
    }
    const auto table = std::span(bytes).subspan(sizeof(header) + entries_size);
    auto chars = std::vector<char>(table.size());
    std::memcpy(chars.data(), table.data(), table.size());
    const auto strings = data::DescriptorStrings { std::move(chars) };

    auto sidecar = Sidecar {
      .resource_index = header.resource_index,
      .descriptor = header.descriptor,
      .views = {},
    };
    for (uint32_t i = 0; i < header.view_count; ++i) {
      auto entry = BufferSidecarViewEntry {};
      std::memcpy(&entry,
        bytes.data() + sizeof(header) + (i * sizeof(BufferSidecarViewEntry)),
        sizeof(entry));
      sidecar.views.push_back({
        .name = std::string { strings.Get(entry.name_offset) },
        .byte_offset = entry.byte_offset,
        .byte_length = entry.byte_length,
        .element_offset = entry.element_offset,
        .element_count = entry.element_count,
      });
    }
    return sidecar;
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
            { "source", buffer_source.generic_string() },
            {
              "virtual_path",
              "/.cooked/Resources/Buffers/character_vertices.obuf",
            },
            { "usage_flags", 3U },
            { "element_stride", 16U },
            { "alignment", 16U },
          },
        }),
      },
    };

    auto request = ImportRequest {};
    request.source_path = "inline://buffer-container";
    request.cooked_root = cooked_root;
    request.loose_cooked_layout.virtual_mount_root = "/.cooked";
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

    const auto sidecar = ParseSidecar(
      ReadBytes(cooked_root / std::filesystem::path { kSidecarRelPath }));
    EXPECT_NE(sidecar.resource_index, data::pak::core::kNoResourceIndex);
    EXPECT_EQ(sidecar.descriptor.usage_flags, 3U);
    EXPECT_EQ(sidecar.descriptor.element_stride, 16U);
    EXPECT_EQ(sidecar.descriptor.size_bytes, buffer_bytes.size());
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
            { "source", buffer_source.generic_string() },
            { "virtual_path", "/.cooked/Resources/Buffers/mesh_indices.obuf" },
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
    request.source_path = "inline://buffer-container";
    request.cooked_root = cooked_root;
    request.loose_cooked_layout.virtual_mount_root = "/.cooked";
    request.buffer_container = ImportRequest::BufferContainerPayload {
      .normalized_descriptor_json = descriptor_json.dump(),
    };

    const auto report = Import(std::move(request));
    ASSERT_HAS_VALUE(report);
    ASSERT_TRUE(report->success)
      << oxygen::cooker::test::DiagnosticSummary(report->diagnostics);

    constexpr auto kSidecarRelPath
      = std::string_view { "Resources/Buffers/mesh_indices.obuf" };
    const auto sidecar = ParseSidecar(
      ReadBytes(cooked_root / std::filesystem::path { kSidecarRelPath }));
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

} // namespace

} // namespace oxygen::content::import::test
