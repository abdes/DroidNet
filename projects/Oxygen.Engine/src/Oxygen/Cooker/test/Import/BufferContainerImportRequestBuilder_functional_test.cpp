//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/BufferContainerImportRequestBuilder.cpp

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Cooker/Import/BufferContainerImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/BufferContainerImportSettings.h>
#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportRequestCodes.h>
#include <Oxygen/Cooker/test/Support/AuthoredFiles.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Cooker/test/Support/FileIo.h>
#include <Oxygen/Cooker/test/Support/TestPaths.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/ScopedTempDir.h>

namespace {

using oxygen::content::import::ImportDiagnostic;
namespace request_codes = oxygen::content::import::request_codes;
using oxygen::cooker::test::DiagnosticSummary;
using oxygen::cooker::test::HasDiagnostic;

using nlohmann::json;
using oxygen::content::import::BufferContainerImportSettings;
using oxygen::content::import::internal::BuildBufferContainerRequest;
using oxygen::cooker::test::BufferContainer;
using oxygen::cooker::test::ContentMountTable;
using oxygen::cooker::test::WriteText;
using oxygen::testing::ScopedTempDir;

constexpr auto kIdentity = std::string_view { "/Content/Buffers/ok.buffers" };

auto MakeBaseSettings(const std::filesystem::path& descriptor_path)
  -> BufferContainerImportSettings
{
  auto settings = BufferContainerImportSettings {};
  settings.descriptor_path = descriptor_path.string();
  settings.cooked_root = (descriptor_path.parent_path() / ".cooked").string();
  settings.job_name = "manifest-buffer-container";
  return settings;
}

//! A container with one buffer, as authored files name it.
auto OneBuffer(json buffer = { { "source", "mesh.buffer.bin" } })
  -> BufferContainer
{
  auto container = BufferContainer { std::string(kIdentity) };
  container.AppendBuffer(std::move(buffer), "/Content/Buffers/ok.obuf");
  return container;
}

NOLINT_TEST(BufferContainerImportRequestBuilderTest,
  BuildsRequestFromValidDescriptorWithNormalizedPayload)
{
  const ScopedTempDir temp;
  const auto descriptor_path = temp.Path() / "Buffers" / "ok.buffers.json";
  auto container = OneBuffer({
    { "source", "mesh_vertices.buffer.bin" },
    { "usage_flags", 3 },
    { "element_stride", 32 },
    { "alignment", 16 },
  });
  container.Set("/name", "CharacterBuffers");
  WriteText(descriptor_path, container.Text());

  const auto settings = MakeBaseSettings(descriptor_path);
  auto errors = std::vector<ImportDiagnostic> {};
  const auto mounts = ContentMountTable(temp.Path());

  const auto request
    = BuildBufferContainerRequest(settings, kIdentity, *mounts, errors);

  ASSERT_HAS_VALUE(request) << DiagnosticSummary(errors);
  EXPECT_TRUE(errors.empty());
  ASSERT_HAS_VALUE(request->cooked_root);
  EXPECT_TRUE(request->cooked_root->is_absolute());
  EXPECT_EQ(request->source_path, descriptor_path.lexically_normal());
  EXPECT_EQ(request->job_name,
    std::optional<std::string> { "manifest-buffer-container" });
  ASSERT_HAS_VALUE(request->buffer_container);

  const auto normalized
    = json::parse(request->buffer_container->descriptor_json);
  EXPECT_EQ(normalized.at("name").get<std::string>(), "CharacterBuffers");
  ASSERT_TRUE(normalized.at("buffers").is_array());
  ASSERT_EQ(normalized.at("buffers").size(), 1U);
  EXPECT_EQ(
    normalized.at("buffers").at(0).at("virtual_path").get<std::string>(),
    "ok.obuf");
}

NOLINT_TEST(BufferContainerImportRequestBuilderTest,
  RejectsDescriptorWithSchemaViolations)
{
  const ScopedTempDir temp;
  const auto descriptor_path = temp.Path() / "Buffers" / "bad.buffers.json";
  auto container = OneBuffer();
  container.Set("/buffers/0/unexpected", true);
  WriteText(descriptor_path, container.Text());

  const auto settings = MakeBaseSettings(descriptor_path);
  auto errors = std::vector<ImportDiagnostic> {};
  const auto mounts = ContentMountTable(temp.Path());

  const auto request
    = BuildBufferContainerRequest(settings, kIdentity, *mounts, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_THAT(errors,
    HasDiagnostic(oxygen::content::import::import_codes::
        kBufferContainerSchemaValidationFailed));
}

NOLINT_TEST(BufferContainerImportRequestBuilderTest, RejectsInvalidJson)
{
  const ScopedTempDir temp;
  const auto descriptor_path = temp.Path() / "Buffers" / "broken.buffers.json";
  WriteText(descriptor_path, "{ \"buffers\": [ ");

  const auto settings = MakeBaseSettings(descriptor_path);
  auto errors = std::vector<ImportDiagnostic> {};
  const auto mounts = ContentMountTable(temp.Path());

  const auto request
    = BuildBufferContainerRequest(settings, kIdentity, *mounts, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_THAT(errors,
    HasDiagnostic(oxygen::content::import::import_codes::
        kBufferContainerRequestInvalidJson));
}

NOLINT_TEST(BufferContainerImportRequestBuilderTest, RejectsMissingFile)
{
  const ScopedTempDir temp;
  const auto descriptor_path = temp.Path() / "Buffers" / "missing.buffers.json";
  const auto settings = MakeBaseSettings(descriptor_path);
  auto errors = std::vector<ImportDiagnostic> {};
  const auto mounts = ContentMountTable(temp.Path());

  const auto request
    = BuildBufferContainerRequest(settings, kIdentity, *mounts, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_THAT(errors, HasDiagnostic(request_codes::kDocumentReadFailed));
}

NOLINT_TEST(BufferContainerImportRequestBuilderTest, RejectsRelativeCookedRoot)
{
  const ScopedTempDir temp;
  const auto descriptor_path = temp.Path() / "Buffers" / "ok.buffers.json";
  WriteText(descriptor_path,
    OneBuffer({ { "source", "mesh.buffer.bin" }, { "element_stride", 16 } })
      .Text());

  auto settings = MakeBaseSettings(descriptor_path);
  settings.cooked_root = "relative/output";
  auto errors = std::vector<ImportDiagnostic> {};
  const auto mounts = ContentMountTable(temp.Path());

  const auto request
    = BuildBufferContainerRequest(settings, kIdentity, *mounts, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_THAT(errors, HasDiagnostic(request_codes::kCookedRootNotAbsolute));
}

NOLINT_TEST(
  BufferContainerImportRequestBuilderTest, RejectsVirtualPathWithBackslashes)
{
  const ScopedTempDir temp;
  const auto descriptor_path = temp.Path() / "Buffers" / "bad.buffers.json";
  auto container
    = OneBuffer({ { "source", "mesh.buffer.bin" }, { "element_stride", 16 } });
  container.Set("/buffers/0/virtual_path", "Resources\\Buffers\\bad.obuf");
  WriteText(descriptor_path, container.Text());

  const auto settings = MakeBaseSettings(descriptor_path);
  auto errors = std::vector<ImportDiagnostic> {};
  const auto mounts = ContentMountTable(temp.Path());

  const auto request
    = BuildBufferContainerRequest(settings, kIdentity, *mounts, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_THAT(errors,
    HasDiagnostic(oxygen::content::import::import_codes::
        kBufferContainerSchemaValidationFailed));
}

NOLINT_TEST(
  BufferContainerImportRequestBuilderTest, AcceptsBufferViewsWithElementRanges)
{
  const ScopedTempDir temp;
  const auto descriptor_path = temp.Path() / "Buffers" / "views.buffers.json";
  WriteText(descriptor_path,
    OneBuffer({
                { "source", "mesh.buffer.bin" },
                { "element_format", 10 },
                {
                  "views",
                  json::array({
                    {
                      { "name", "lod0" },
                      { "element_offset", 0 },
                      { "element_count", 12 },
                    },
                  }),
                },
              })
      .Text());

  const auto settings = MakeBaseSettings(descriptor_path);
  auto errors = std::vector<ImportDiagnostic> {};
  const auto mounts = ContentMountTable(temp.Path());

  const auto request
    = BuildBufferContainerRequest(settings, kIdentity, *mounts, errors);

  ASSERT_HAS_VALUE(request) << DiagnosticSummary(errors);
  EXPECT_TRUE(errors.empty());
}

NOLINT_TEST(BufferContainerImportRequestBuilderTest,
  RejectsExplicitImplicitAllViewDeclaration)
{
  const ScopedTempDir temp;
  const auto descriptor_path
    = temp.Path() / "Buffers" / "bad_view.buffers.json";
  WriteText(descriptor_path,
    OneBuffer({
                { "source", "mesh.buffer.bin" },
                { "element_stride", 16 },
                {
                  "views",
                  json::array({
                    {
                      { "name", "__all__" },
                      { "byte_offset", 0 },
                      { "byte_length", 64 },
                    },
                  }),
                },
              })
      .Text());

  const auto settings = MakeBaseSettings(descriptor_path);
  auto errors = std::vector<ImportDiagnostic> {};
  const auto mounts = ContentMountTable(temp.Path());

  const auto request
    = BuildBufferContainerRequest(settings, kIdentity, *mounts, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_THAT(errors,
    HasDiagnostic(oxygen::content::import::import_codes::
        kBufferContainerSchemaValidationFailed));
}

NOLINT_TEST(BufferContainerImportRequestBuilderTest,
  RejectsViewWithBothByteAndElementRanges)
{
  const ScopedTempDir temp;
  const auto descriptor_path
    = temp.Path() / "Buffers" / "mixed_view.buffers.json";
  WriteText(descriptor_path,
    OneBuffer({
                { "source", "mesh.buffer.bin" },
                { "element_stride", 16 },
                {
                  "views",
                  json::array({
                    {
                      { "name", "lod0" },
                      { "byte_offset", 0 },
                      { "byte_length", 64 },
                      { "element_offset", 0 },
                      { "element_count", 4 },
                    },
                  }),
                },
              })
      .Text());

  const auto settings = MakeBaseSettings(descriptor_path);
  auto errors = std::vector<ImportDiagnostic> {};
  const auto mounts = ContentMountTable(temp.Path());

  const auto request
    = BuildBufferContainerRequest(settings, kIdentity, *mounts, errors);

  EXPECT_FALSE(request.has_value());
  EXPECT_THAT(errors,
    HasDiagnostic(oxygen::content::import::import_codes::
        kBufferContainerSchemaValidationFailed));
}

} // namespace
