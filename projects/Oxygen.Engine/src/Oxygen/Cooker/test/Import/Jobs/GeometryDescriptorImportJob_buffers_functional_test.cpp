//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/Jobs/GeometryDescriptorImportJob.cpp,
//   Import/Internal/Jobs/BufferContainerImportJob.cpp

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "GeometryDescriptorImportJobTestSupport.h"
#include <nlohmann/json.hpp>

#include <Oxygen/Base/Finally.h>
#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Import/ImportCodes.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/test/Support/CookedFiles.h>
#include <Oxygen/Cooker/test/Support/TestPaths.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/MeshType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/ResourceDescriptor.h>
#include <Oxygen/Serio/ByteReader.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {

namespace {

  //! The buffer manifest to import and the root it is cooked into.
  struct BufferContainerPaths final {
    std::filesystem::path source;
    std::filesystem::path cooked;
  };

  auto MakeBufferContainerRequest(const BufferContainerPaths& paths,
    const json& descriptor_doc) -> ImportRequest
  {
    auto request = ImportRequest {};
    request.source_path = paths.source.lexically_normal();
    request.asset_virtual_path = "/Cooked/Resources/inline.buffers";
    request.source_mount_path
      = oxygen::cooker::test::SourceMountPath(request.source_path);
    request.cooked_root = paths.cooked;
    request.source_mounts = oxygen::cooker::test::ContentMountTable(
      request.source_path.parent_path());
    request.buffer_container = ImportRequest::BufferContainerPayload {
      .descriptor_json = descriptor_doc.dump(),
    };
    return request;
  }

  //! Authored inputs shared by the buffer sidecar tests. Each cooked root is
  //! produced from the same sources and differs only by its descriptor.
  struct LocalBufferSources final {
    std::filesystem::path root;
    std::filesystem::path cooked_root;
    std::filesystem::path buffer_manifest_path;
    std::filesystem::path geometry_path;
    std::filesystem::path vb_source;
    std::vector<std::byte> vb_bytes;
    json buffer_descriptor;
    json geometry_descriptor;
  };

  auto WriteLocalBufferSources(const std::filesystem::path& root)
    -> LocalBufferSources
  {
    std::filesystem::create_directories(root);
    const auto source_dir = root / "Sources";
    const auto cooked_root = root / ".cooked";
    const auto buffer_manifest_path = source_dir / "shared.buffers.json";
    const auto geometry_path = source_dir / "shared_ref.geometry.json";
    const auto vb_source = source_dir / "shared.vertices.buffer.bin";
    const auto ib_source = source_dir / "shared.indices.buffer.bin";

    CookDefaultMaterial(cooked_root);

    const auto vb_bytes = VertexBytes();
    const auto ib_bytes = std::array<std::byte, 12> {
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
    };
    WriteBytes(vb_source, std::span<const std::byte>(vb_bytes));
    WriteBytes(ib_source, std::span<const std::byte>(ib_bytes));

    auto container = oxygen::cooker::test::BufferContainer {
      "/Cooked/Resources/inline.buffers",
    };
    container.Set("/name", "SharedBuffers")
      .AppendBuffer(MakeContainerBuffer(
                      vb_source.filename().generic_string(), 1U, kVertexStride),
        "/Cooked/Resources/Buffers/shared_vertices.obuf")
      .AppendBuffer(
        MakeContainerBuffer(ib_source.filename().generic_string(), 2U, 4U),
        "/Cooked/Resources/Buffers/shared_indices.obuf");
    const auto buffer_descriptor = container.Json();
    WriteText(buffer_manifest_path, container.Text());

    const auto geometry_descriptor = MakeStandardGeometry(
      "/Cooked/Geometry/SharedCube.ogeo", "/Cooked/Materials/default.omat",
      "/Cooked/Resources/Buffers/shared_vertices.obuf",
      "/Cooked/Resources/Buffers/shared_indices.obuf");
    WriteText(geometry_path, geometry_descriptor.Text());

    return LocalBufferSources {
      .root = root,
      .cooked_root = cooked_root,
      .buffer_manifest_path = buffer_manifest_path,
      .geometry_path = geometry_path,
      .vb_source = vb_source,
      .vb_bytes = vb_bytes,
      .buffer_descriptor = buffer_descriptor,
      .geometry_descriptor = geometry_descriptor.Json(),
    };
  }

  //! Cooks the buffer manifest `descriptor` into `cooked_root`.
  auto CookLocalBuffers(AsyncImportService& service,
    const LocalBufferSources& sources, const std::filesystem::path& cooked_root,
    const json& descriptor) -> ImportReport
  {
    const auto report = SubmitAndWait(service,
      MakeBufferContainerRequest(
        { .source = sources.buffer_manifest_path, .cooked = cooked_root },
        descriptor));
    if (!report.has_value()) {
      return ImportReport {};
    }
    return *report;
  }

} // namespace

NOLINT_TEST(GeometryDescriptorImportJobBuffersTest,
  LocalBuffersEmitGeometryAndBufferArtifacts)
{
  const ScopedTempDir temp;
  const auto root = temp.Path() / "local_buffers_emit_artifacts";
  std::filesystem::create_directories(root);
  const auto source_dir = root / "Sources";
  const auto cooked_root = root / ".cooked";
  const auto descriptor_path = source_dir / "cube.geometry.json";
  const auto vb_source = source_dir / "cube.vertices.buffer.bin";
  const auto ib_source = source_dir / "cube.indices.buffer.bin";

  CookDefaultMaterial(cooked_root);

  const auto vb_bytes = VertexBytes();
  const auto ib_bytes = std::array<std::byte, 12> {
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
  };
  WriteBytes(vb_source, std::span<const std::byte>(vb_bytes));
  WriteBytes(ib_source, std::span<const std::byte>(ib_bytes));

  auto descriptor_doc = MakeStandardGeometry("/Cooked/Geometry/Cube.ogeo",
    "/Cooked/Materials/default.omat",
    "/Cooked/Resources/Buffers/cube_vertices.obuf",
    "/Cooked/Resources/Buffers/cube_indices.obuf");
  descriptor_doc
    .AppendBuffer(MakeBuffer(vb_source.filename().generic_string(), 1U,
                    kVertexStride, true),
      "/Cooked/Resources/Buffers/cube_vertices.obuf")
    .AppendBuffer(
      MakeBuffer(ib_source.filename().generic_string(), 2U, 4U, true),
      "/Cooked/Resources/Buffers/cube_indices.obuf");
  WriteText(descriptor_path, descriptor_doc.Text());

  auto service = AsyncImportService(AsyncImportService::Config {
    .thread_pool_size = 2U,
  });
  [[maybe_unused]] auto stop_service
    = oxygen::Finally([&service] -> void { service.Stop(); });

  const auto report = SubmitAndWait(service,
    MakeGeometryRequest({
      .source_path = descriptor_path,
      .cooked_root = cooked_root,
      .descriptor_doc = descriptor_doc.Json(),
    }));
  ASSERT_HAS_VALUE(report);
  ASSERT_TRUE(report->success) << DiagnosticSummary(report->diagnostics);
  EXPECT_EQ(report->geometry_written, 1U)
    << DiagnosticSummary(report->diagnostics);
  EXPECT_FALSE(HasDiagnosticCode(
    report->diagnostics, import_codes::kGeometryMaterialMissing));

  const auto geometry_relpath = FindOutputByExtension(*report, ".ogeo");
  ASSERT_HAS_VALUE(geometry_relpath)
    << "Expected geometry_relpath to contain a value";
  ASSERT_TRUE(std::filesystem::exists(
    cooked_root / std::filesystem::path(*geometry_relpath)));

  const auto has_output = [&](const std::string_view relpath) -> bool {
    return std::ranges::any_of(
      report->outputs, [&](const ImportOutputRecord& output) -> bool {
        return oxygen::cooker::test::LayoutPathOf(output.path) == relpath;
      });
  };
  EXPECT_TRUE(has_output("Resources/Buffers/cube_vertices.obuf"));
  EXPECT_TRUE(has_output("Resources/Buffers/cube_indices.obuf"));
  EXPECT_TRUE(has_output("Resources/buffers.data"));
  EXPECT_TRUE(has_output("Resources/buffers.table"));

  const auto descriptor_bytes
    = ReadBytes(cooked_root / std::filesystem::path(*geometry_relpath));
  ASSERT_GE(descriptor_bytes.size(),
    sizeof(data::pak::geometry::GeometryAssetDesc)
      + sizeof(data::pak::geometry::MeshDesc)
      + sizeof(data::pak::geometry::SubMeshDesc)
      + sizeof(data::pak::geometry::MeshViewDesc));

  auto offset = sizeof(data::pak::geometry::GeometryAssetDesc);
  const auto mesh_desc_read
    = oxygen::serio::CopyAt<data::pak::geometry::MeshDesc>(
      std::span { descriptor_bytes }, oxygen::serio::ByteOffset { offset });
  ASSERT_HAS_VALUE(mesh_desc_read);
  const auto& mesh_desc = *mesh_desc_read;
  EXPECT_EQ(
    mesh_desc.mesh_type, static_cast<uint8_t>(data::MeshType::kStandard));
  EXPECT_NE(mesh_desc.info.standard.vertex_buffer, data::kNoResourceReference);
  EXPECT_NE(mesh_desc.info.standard.index_buffer, data::kNoResourceReference);
  EXPECT_EQ(mesh_desc.submesh_count, 1U);
  EXPECT_EQ(mesh_desc.mesh_view_count, 1U);

  offset += sizeof(data::pak::geometry::MeshDesc);
  const auto submesh_desc_read
    = oxygen::serio::CopyAt<data::pak::geometry::SubMeshDesc>(
      std::span { descriptor_bytes }, oxygen::serio::ByteOffset { offset });
  ASSERT_HAS_VALUE(submesh_desc_read);
  const auto& submesh_desc = *submesh_desc_read;
  EXPECT_EQ(submesh_desc.material_asset_key,
    oxygen::data::AssetKey::FromVirtualPath("/Cooked/Materials/default.omat"));
  EXPECT_TRUE(CanParseGeometryDescriptor(descriptor_bytes));
}

NOLINT_TEST(GeometryDescriptorImportJobBuffersTest, ResolvesLocalBufferSidecars)
{
  const ScopedTempDir temp;
  const auto sources
    = WriteLocalBufferSources(temp.Path() / "pre_cooked_buffer_sidecars");
  const auto& cooked_root = sources.cooked_root;
  const auto& geometry_path = sources.geometry_path;
  const auto& geometry_descriptor = sources.geometry_descriptor;

  auto service = AsyncImportService(AsyncImportService::Config {
    .thread_pool_size = 2U,
  });
  [[maybe_unused]] auto stop_service
    = oxygen::Finally([&service] -> void { service.Stop(); });

  const auto buffer_report = CookLocalBuffers(
    service, sources, cooked_root, sources.buffer_descriptor);
  ASSERT_TRUE(buffer_report.success);

  const auto geometry_report = SubmitAndWait(service,
    MakeGeometryRequest({
      .source_path = geometry_path,
      .cooked_root = cooked_root,
      .descriptor_doc = geometry_descriptor,
    }));
  ASSERT_HAS_VALUE(geometry_report);
  ASSERT_TRUE(geometry_report->success)
    << DiagnosticSummary(geometry_report->diagnostics);
  EXPECT_EQ(geometry_report->geometry_written, 1U);
  EXPECT_FALSE(HasDiagnosticCode(
    geometry_report->diagnostics, import_codes::kGeometryBufferSidecarMissing));

  const auto geometry_relpath
    = FindOutputByExtension(*geometry_report, ".ogeo");
  ASSERT_HAS_VALUE(geometry_relpath)
    << "Expected geometry_relpath to contain a value";
  ASSERT_TRUE(std::filesystem::exists(
    cooked_root / std::filesystem::path(*geometry_relpath)));
}

NOLINT_TEST(GeometryDescriptorImportJobBuffersTest,
  RejectsBufferSidecarsOwnedByForeignRoot)
{
  const ScopedTempDir temp;
  const auto sources
    = WriteLocalBufferSources(temp.Path() / "pre_cooked_buffer_sidecars");
  const auto& root = sources.root;
  const auto& cooked_root = sources.cooked_root;
  const auto& geometry_path = sources.geometry_path;
  const auto& geometry_descriptor = sources.geometry_descriptor;
  const auto& vb_source = sources.vb_source;
  const auto& vb_bytes = sources.vb_bytes;

  auto service = AsyncImportService(AsyncImportService::Config {
    .thread_pool_size = 2U,
  });
  [[maybe_unused]] auto stop_service
    = oxygen::Finally([&service] -> void { service.Stop(); });

  const auto buffer_report = CookLocalBuffers(
    service, sources, cooked_root, sources.buffer_descriptor);
  ASSERT_TRUE(buffer_report.success);

  const auto other_root = root / "other-cooked";
  auto other_descriptor = sources.buffer_descriptor;
  other_descriptor.at("buffers").at(0).at("virtual_path")
    = "Buffers/other_vertices.obuf";
  other_descriptor.at("buffers").at(1).at("virtual_path")
    = "Buffers/other_indices.obuf";
  auto other_vertices = vb_bytes;
  other_vertices.front() = std::byte { 0x7F };
  WriteBytes(vb_source, std::span<const std::byte>(other_vertices));
  const auto other_buffers
    = CookLocalBuffers(service, sources, other_root, other_descriptor);
  ASSERT_TRUE(other_buffers.success);

  // Both roots have populated buffer tables, but only the foreign root owns
  // the requested sidecars. Its numeric indices cannot identify local bytes.
  const auto rejected = SubmitAndWait(service,
    MakeGeometryRequest({
      .source_path = geometry_path,
      .cooked_root = other_root,
      .descriptor_doc = geometry_descriptor,
      .cooked_context_roots = { cooked_root },
    }));
  ASSERT_HAS_VALUE(rejected);
  EXPECT_FALSE(rejected->success);
  EXPECT_EQ(rejected->geometry_written, 0U);
  EXPECT_TRUE(HasDiagnosticCode(
    rejected->diagnostics, import_codes::kGeometryBufferForeignRoot));
}

NOLINT_TEST(GeometryDescriptorImportJobBuffersTest,
  RejectsBufferSidecarsFromAdditionalCookedContextRoot)
{
  const ScopedTempDir temp;
  const auto main_root = temp.Path() / "resolve_from_context_root_main";
  std::filesystem::create_directories(main_root);
  const auto context_root = temp.Path() / "resolve_from_context_root_ctx";
  std::filesystem::create_directories(context_root);
  const auto source_dir = main_root / "Sources";
  const auto cooked_root = main_root / ".cooked";
  const auto context_cooked_root = context_root / ".cooked";
  const auto buffer_manifest_path = source_dir / "context_shared.buffers.json";
  const auto geometry_path = source_dir / "context_ref.geometry.json";
  const auto vb_source = source_dir / "context.vertices.buffer.bin";
  const auto ib_source = source_dir / "context.indices.buffer.bin";

  CookDefaultMaterial(cooked_root);

  const auto vb_bytes = VertexBytes();
  const auto ib_bytes = IndexBytes({ 0U, 1U, 2U });
  WriteBytes(vb_source, std::span<const std::byte>(vb_bytes));
  WriteBytes(ib_source, std::span<const std::byte>(ib_bytes));

  auto container = oxygen::cooker::test::BufferContainer {
    "/Cooked/Resources/inline.buffers",
  };
  container.Set("/name", "ContextSharedBuffers")
    .AppendBuffer(MakeContainerBuffer(
                    vb_source.filename().generic_string(), 1U, kVertexStride),
      "/Cooked/Resources/Buffers/context_vertices.obuf")
    .AppendBuffer(
      MakeContainerBuffer(ib_source.filename().generic_string(), 2U, 4U),
      "/Cooked/Resources/Buffers/context_indices.obuf");
  const auto buffer_descriptor = container.Json();
  WriteText(buffer_manifest_path, container.Text());

  const auto geometry_descriptor = MakeStandardGeometry(
    "/Cooked/Geometry/ContextSharedCube.ogeo", "/Cooked/Materials/default.omat",
    "/Cooked/Resources/Buffers/context_vertices.obuf",
    "/Cooked/Resources/Buffers/context_indices.obuf");
  WriteText(geometry_path, geometry_descriptor.Text());

  auto service = AsyncImportService(AsyncImportService::Config {
    .thread_pool_size = 2U,
  });
  [[maybe_unused]] auto stop_service
    = oxygen::Finally([&service] -> void { service.Stop(); });

  const auto buffer_report = SubmitAndWait(service,
    MakeBufferContainerRequest(
      { .source = buffer_manifest_path, .cooked = context_cooked_root },
      buffer_descriptor));
  ASSERT_HAS_VALUE(buffer_report);
  ASSERT_TRUE(buffer_report->success);

  const auto geometry_report = SubmitAndWait(service,
    MakeGeometryRequest({
      .source_path = geometry_path,
      .cooked_root = cooked_root,
      .descriptor_doc = geometry_descriptor.Json(),
      .cooked_context_roots = { context_cooked_root },
    }));
  ASSERT_HAS_VALUE(geometry_report);
  EXPECT_FALSE(geometry_report->success);
  EXPECT_EQ(geometry_report->geometry_written, 0U);
  EXPECT_TRUE(HasDiagnosticCode(
    geometry_report->diagnostics, import_codes::kGeometryBufferForeignRoot));
}

NOLINT_TEST(GeometryDescriptorImportJobBuffersTest,
  DuplicateMountedBufferSidecarsFailWithAmbiguousDiagnostic)
{
  const ScopedTempDir temp;
  const auto main_root = temp.Path() / "ambiguous_sidecar_main";
  std::filesystem::create_directories(main_root);
  const auto context_root = temp.Path() / "ambiguous_sidecar_ctx";
  std::filesystem::create_directories(context_root);
  const auto source_dir = main_root / "Sources";
  const auto cooked_root = main_root / ".cooked";
  const auto context_cooked_root = context_root / ".cooked";
  const auto buffer_manifest_path = source_dir / "ambiguous.buffers.json";
  const auto geometry_path = source_dir / "ambiguous.geometry.json";
  const auto vb_source = source_dir / "ambiguous.vertices.buffer.bin";
  const auto ib_source = source_dir / "ambiguous.indices.buffer.bin";

  CookDefaultMaterial(cooked_root);

  const auto vb_bytes = VertexBytes();
  const auto ib_bytes = IndexBytes({ 0U, 1U, 2U });
  WriteBytes(vb_source, std::span<const std::byte>(vb_bytes));
  WriteBytes(ib_source, std::span<const std::byte>(ib_bytes));

  auto container = oxygen::cooker::test::BufferContainer {
    "/Cooked/Resources/inline.buffers",
  };
  container.Set("/name", "AmbiguousBuffers")
    .AppendBuffer(MakeContainerBuffer(
                    vb_source.filename().generic_string(), 1U, kVertexStride),
      "/Cooked/Resources/Buffers/ambiguous_vertices.obuf")
    .AppendBuffer(
      MakeContainerBuffer(ib_source.filename().generic_string(), 2U, 4U),
      "/Cooked/Resources/Buffers/ambiguous_indices.obuf");
  const auto buffer_descriptor = container.Json();
  WriteText(buffer_manifest_path, container.Text());

  const auto geometry_descriptor = MakeStandardGeometry(
    "/Cooked/Geometry/AmbiguousCube.ogeo", "/Cooked/Materials/default.omat",
    "/Cooked/Resources/Buffers/ambiguous_vertices.obuf",
    "/Cooked/Resources/Buffers/ambiguous_indices.obuf");
  WriteText(geometry_path, geometry_descriptor.Text());

  auto service = AsyncImportService(AsyncImportService::Config {
    .thread_pool_size = 2U,
  });
  [[maybe_unused]] auto stop_service
    = oxygen::Finally([&service] -> void { service.Stop(); });

  const auto main_report = SubmitAndWait(service,
    MakeBufferContainerRequest(
      { .source = buffer_manifest_path, .cooked = cooked_root },
      buffer_descriptor));
  ASSERT_HAS_VALUE(main_report);
  ASSERT_TRUE(main_report->success);
  const auto context_report = SubmitAndWait(service,
    MakeBufferContainerRequest(
      { .source = buffer_manifest_path, .cooked = context_cooked_root },
      buffer_descriptor));
  ASSERT_HAS_VALUE(context_report);
  ASSERT_TRUE(context_report->success);

  const auto geometry_report = SubmitAndWait(service,
    MakeGeometryRequest({
      .source_path = geometry_path,
      .cooked_root = cooked_root,
      .descriptor_doc = geometry_descriptor.Json(),
      .cooked_context_roots = { context_cooked_root },
    }));
  ASSERT_HAS_VALUE(geometry_report);
  EXPECT_FALSE(geometry_report->success);
  EXPECT_TRUE(HasDiagnosticCode(geometry_report->diagnostics,
    import_codes::kGeometryBufferSidecarAmbiguous));
}

NOLINT_TEST(GeometryDescriptorImportJobBuffersTest,
  BufferReferenceOutsideMountedRootsFailsWithHelpfulDiagnostic)
{
  const ScopedTempDir temp;
  const auto root = temp.Path() / "unmounted_buffer_reference";
  std::filesystem::create_directories(root);
  const auto source_dir = root / "Sources";
  const auto cooked_root = root / ".cooked";
  const auto descriptor_path = source_dir / "unmounted.geometry.json";

  CookDefaultMaterial(cooked_root);

  const auto descriptor_doc = MakeStandardGeometry(
    "/Cooked/Geometry/UnmountedBuffers.ogeo", "/Cooked/Materials/default.omat",
    "/foreign/Resources/Buffers/foreign_vertices.obuf",
    "/foreign/Resources/Buffers/foreign_indices.obuf");
  WriteText(descriptor_path, descriptor_doc.Text());

  auto service = AsyncImportService(AsyncImportService::Config {
    .thread_pool_size = 2U,
  });
  [[maybe_unused]] auto stop_service
    = oxygen::Finally([&service] -> void { service.Stop(); });

  const auto report = SubmitAndWait(service,
    MakeGeometryRequest({
      .source_path = descriptor_path,
      .cooked_root = cooked_root,
      .descriptor_doc = descriptor_doc.Json(),
    }));
  ASSERT_HAS_VALUE(report);
  EXPECT_FALSE(report->success);
  EXPECT_TRUE(
    HasDiagnosticCode(report->diagnostics, import_codes::kReferenceUnmounted));
}

NOLINT_TEST(GeometryDescriptorImportJobBuffersTest,
  UnknownBufferViewReferenceFailsWithHelpfulDiagnostic)
{
  const ScopedTempDir temp;
  const auto root = temp.Path() / "missing_buffer_view_reference";
  std::filesystem::create_directories(root);
  const auto source_dir = root / "Sources";
  const auto cooked_root = root / ".cooked";
  const auto descriptor_path = source_dir / "missing_view.geometry.json";
  const auto vb_source = source_dir / "cube.vertices.buffer.bin";
  const auto ib_source = source_dir / "cube.indices.buffer.bin";

  CookDefaultMaterial(cooked_root);

  const auto vb_bytes = VertexBytes();
  const auto ib_bytes = IndexBytes({ 0U, 1U, 2U });
  WriteBytes(vb_source, std::span<const std::byte>(vb_bytes));
  WriteBytes(ib_source, std::span<const std::byte>(ib_bytes));

  auto descriptor_doc = MakeStandardGeometry(
    "/Cooked/Geometry/CubeMissingView.ogeo", "/Cooked/Materials/default.omat",
    "/Cooked/Resources/Buffers/cube_vertices.obuf",
    "/Cooked/Resources/Buffers/cube_indices.obuf", "lod_missing");
  descriptor_doc
    .AppendBuffer(MakeBuffer(vb_source.filename().generic_string(), 1U,
                    kVertexStride, true),
      "/Cooked/Resources/Buffers/cube_vertices.obuf")
    .AppendBuffer(
      MakeBuffer(ib_source.filename().generic_string(), 2U, 4U, true),
      "/Cooked/Resources/Buffers/cube_indices.obuf");
  WriteText(descriptor_path, descriptor_doc.Text());

  auto service = AsyncImportService(AsyncImportService::Config {
    .thread_pool_size = 2U,
  });
  [[maybe_unused]] auto stop_service
    = oxygen::Finally([&service] -> void { service.Stop(); });

  const auto report = SubmitAndWait(service,
    MakeGeometryRequest({
      .source_path = descriptor_path,
      .cooked_root = cooked_root,
      .descriptor_doc = descriptor_doc.Json(),
    }));
  ASSERT_HAS_VALUE(report);
  EXPECT_FALSE(report->success);
  EXPECT_TRUE(HasDiagnosticCode(
    report->diagnostics, import_codes::kGeometryBufferViewMissing));
}

namespace {

  constexpr auto kMaterial = "/Cooked/Materials/default.omat";
  constexpr auto kVertices = "/Cooked/Resources/Buffers/vertices.obuf";
  constexpr auto kIndices = "/Cooked/Resources/Buffers/indices.obuf";

  //! The buffers a geometry declares, by default a triangle it can draw.
  struct LocalMesh final {
    std::vector<std::byte> vertices = VertexBytes();
    std::vector<std::byte> indices = IndexBytes({ 0U, 1U, 2U });
    uint32_t vertex_stride = kVertexStride;
    uint32_t index_stride = sizeof(uint32_t);
    //! The view the LOD draws; `lod0` is the first three elements.
    std::string_view view = "lod0";
    std::string_view name = "Mesh";
  };

  [[nodiscard]] auto CookLocalMesh(AsyncImportService& service,
    const std::filesystem::path& root, const LocalMesh& mesh)
    -> std::optional<ImportReport>
  {
    const auto source_dir = root / "Sources";
    const auto vb_source = source_dir / "vertices.bin";
    const auto ib_source = source_dir / "indices.bin";
    const auto descriptor_path = source_dir / "mesh.geometry.json";
    WriteBytes(vb_source, std::span<const std::byte>(mesh.vertices));
    WriteBytes(ib_source, std::span<const std::byte>(mesh.indices));

    const auto with_views = mesh.view == "lod0";
    auto geometry = MakeStandardGeometry(
      "/Cooked/Geometry/" + std::string { mesh.name } + ".ogeo", kMaterial,
      kVertices, kIndices, mesh.view);
    geometry
      .AppendBuffer(
        MakeBuffer("vertices.bin", 1U, mesh.vertex_stride, with_views),
        kVertices)
      .AppendBuffer(
        MakeBuffer("indices.bin", 2U, mesh.index_stride, with_views), kIndices);
    WriteText(descriptor_path, geometry.Text());
    return SubmitAndWait(service,
      MakeGeometryRequest({
        .source_path = descriptor_path,
        .cooked_root = root / ".cooked",
        .descriptor_doc = geometry.Json(),
      }));
  }

  //! Runs `CookLocalMesh` in a new root, with the default material cooked.
  [[nodiscard]] auto CookLocalMesh(const LocalMesh& mesh)
    -> std::optional<ImportReport>
  {
    const ScopedTempDir temp;
    const auto root = temp.Path() / "local_mesh";
    std::filesystem::create_directories(root / "Sources");
    CookDefaultMaterial(root / ".cooked");
    auto service = AsyncImportService(AsyncImportService::Config {
      .thread_pool_size = 2U,
    });
    [[maybe_unused]] auto stop_service
      = oxygen::Finally([&service] -> void { service.Stop(); });
    return CookLocalMesh(service, root, mesh);
  }

  //! A service whose buffer pipeline holds one item in each queue, on one
  //! worker: more buffers than that only pass if results are collected while
  //! buffers are submitted.
  [[nodiscard]] auto SingleSlotBufferConfig() -> AsyncImportService::Config
  {
    auto config = AsyncImportService::Config {
      .thread_pool_size = 2U,
    };
    config.concurrency.buffer.workers = 1U;
    config.concurrency.buffer.queue_capacity = 1U;
    return config;
  }

} // namespace

//! The format holds one row for equal bytes, whichever products name them, and
//! each product keeps its own sidecar: two geometries may share index bytes.
NOLINT_TEST(GeometryDescriptorImportJobBuffersTest,
  EqualBuffersAcrossGeometryJobsShareOneRow)
{
  const ScopedTempDir temp;
  const auto root = temp.Path() / "shared_row_across_jobs";
  const auto source_dir = root / "Sources";
  const auto cooked_root = root / ".cooked";
  std::filesystem::create_directories(source_dir);
  CookDefaultMaterial(cooked_root);
  const auto vb_bytes = VertexBytes();
  const auto ib_bytes = IndexBytes({ 0U, 1U, 2U });
  WriteBytes(source_dir / "vertices.bin", std::span<const std::byte>(vb_bytes));
  WriteBytes(source_dir / "indices.bin", std::span<const std::byte>(ib_bytes));

  const auto cook
    = [&](AsyncImportService& service,
        const std::string_view name) -> std::optional<ImportReport> {
    const auto vertices
      = "/Cooked/Resources/Buffers/" + std::string { name } + "_vertices.obuf";
    const auto indices
      = "/Cooked/Resources/Buffers/" + std::string { name } + "_indices.obuf";
    auto geometry = MakeStandardGeometry(
      "/Cooked/Geometry/" + std::string { name } + ".ogeo", kMaterial, vertices,
      indices);
    geometry
      .AppendBuffer(
        MakeBuffer("vertices.bin", 1U, kVertexStride, true), vertices)
      .AppendBuffer(MakeBuffer("indices.bin", 2U, 4U, true), indices);
    const auto path = source_dir / (std::string { name } + ".geometry.json");
    WriteText(path, geometry.Text());
    return SubmitAndWait(service,
      MakeGeometryRequest({
        .source_path = path,
        .cooked_root = cooked_root,
        .descriptor_doc = geometry.Json(),
      }));
  };

  auto service = AsyncImportService(AsyncImportService::Config {
    .thread_pool_size = 2U,
  });
  [[maybe_unused]] auto stop_service
    = oxygen::Finally([&service] -> void { service.Stop(); });

  const auto first = cook(service, "First");
  ASSERT_HAS_VALUE(first);
  ASSERT_TRUE(first->success) << DiagnosticSummary(first->diagnostics);
  const auto table_size = std::filesystem::file_size(
    oxygen::cooker::test::CookedFile(cooked_root, "Resources/buffers.table"));

  const auto second = cook(service, "Second");
  ASSERT_HAS_VALUE(second);
  ASSERT_TRUE(second->success) << DiagnosticSummary(second->diagnostics);

  EXPECT_EQ(std::filesystem::file_size(oxygen::cooker::test::CookedFile(
              cooked_root, "Resources/buffers.table")),
    table_size);
  EXPECT_TRUE(oxygen::cooker::test::HasCookedFile(
    cooked_root, "Resources/Buffers/First_indices.obuf"));
  EXPECT_TRUE(oxygen::cooker::test::HasCookedFile(
    cooked_root, "Resources/Buffers/Second_indices.obuf"));
}

//! Equal buffers declared twice in one descriptor share a row the same way.
NOLINT_TEST(GeometryDescriptorImportJobBuffersTest,
  EqualBuffersInOneDescriptorKeepBothSidecars)
{
  const ScopedTempDir temp;
  const auto root = temp.Path() / "shared_row_in_job";
  const auto source_dir = root / "Sources";
  const auto cooked_root = root / ".cooked";
  std::filesystem::create_directories(source_dir);
  CookDefaultMaterial(cooked_root);
  const auto vb_bytes = VertexBytes();
  const auto ib_bytes = IndexBytes({ 0U, 1U, 2U });
  WriteBytes(source_dir / "vertices.bin", std::span<const std::byte>(vb_bytes));
  WriteBytes(source_dir / "indices.bin", std::span<const std::byte>(ib_bytes));

  constexpr auto kAlias = "/Cooked/Resources/Buffers/alias_indices.obuf";
  auto geometry = MakeStandardGeometry(
    "/Cooked/Geometry/Aliased.ogeo", kMaterial, kVertices, kIndices);
  geometry
    .AppendBuffer(
      MakeBuffer("vertices.bin", 1U, kVertexStride, true), kVertices)
    .AppendBuffer(MakeBuffer("indices.bin", 2U, 4U, true), kIndices)
    .AppendBuffer(MakeBuffer("indices.bin", 2U, 4U, true), kAlias);
  const auto path = source_dir / "aliased.geometry.json";
  WriteText(path, geometry.Text());

  auto service = AsyncImportService(AsyncImportService::Config {
    .thread_pool_size = 2U,
  });
  [[maybe_unused]] auto stop_service
    = oxygen::Finally([&service] -> void { service.Stop(); });
  const auto report = SubmitAndWait(service,
    MakeGeometryRequest({
      .source_path = path,
      .cooked_root = cooked_root,
      .descriptor_doc = geometry.Json(),
    }));

  ASSERT_HAS_VALUE(report);
  ASSERT_TRUE(report->success) << DiagnosticSummary(report->diagnostics);
  EXPECT_TRUE(oxygen::cooker::test::HasCookedFile(
    cooked_root, "Resources/Buffers/indices.obuf"));
  EXPECT_TRUE(oxygen::cooker::test::HasCookedFile(
    cooked_root, "Resources/Buffers/alias_indices.obuf"));
}

//! More buffers than the bounded queues hold pass through a geometry job.
NOLINT_TEST(
  GeometryDescriptorImportJobBuffersTest, BuffersPastTheQueueCapacityAllCook)
{
  const ScopedTempDir temp;
  const auto root = temp.Path() / "past_capacity";
  const auto source_dir = root / "Sources";
  const auto cooked_root = root / ".cooked";
  std::filesystem::create_directories(source_dir);
  CookDefaultMaterial(cooked_root);
  const auto vb_bytes = VertexBytes();
  const auto ib_bytes = IndexBytes({ 0U, 1U, 2U });
  const auto first_extra = IndexBytes({ 2U, 1U, 0U });
  const auto second_extra = IndexBytes({ 1U, 2U, 0U });
  WriteBytes(source_dir / "vertices.bin", std::span<const std::byte>(vb_bytes));
  WriteBytes(source_dir / "indices.bin", std::span<const std::byte>(ib_bytes));
  WriteBytes(source_dir / "a.bin", std::span<const std::byte>(first_extra));
  WriteBytes(source_dir / "b.bin", std::span<const std::byte>(second_extra));

  auto geometry = MakeStandardGeometry(
    "/Cooked/Geometry/Many.ogeo", kMaterial, kVertices, kIndices);
  geometry
    .AppendBuffer(
      MakeBuffer("vertices.bin", 1U, kVertexStride, true), kVertices)
    .AppendBuffer(MakeBuffer("indices.bin", 2U, 4U, true), kIndices)
    .AppendBuffer(
      MakeBuffer("a.bin", 2U, 4U, false), "/Cooked/Resources/Buffers/a.obuf")
    .AppendBuffer(
      MakeBuffer("b.bin", 2U, 4U, false), "/Cooked/Resources/Buffers/b.obuf");
  const auto path = source_dir / "many.geometry.json";
  WriteText(path, geometry.Text());

  auto service = AsyncImportService(SingleSlotBufferConfig());
  [[maybe_unused]] auto stop_service
    = oxygen::Finally([&service] -> void { service.Stop(); });
  const auto report = SubmitAndWait(service,
    MakeGeometryRequest({
      .source_path = path,
      .cooked_root = cooked_root,
      .descriptor_doc = geometry.Json(),
    }));

  ASSERT_HAS_VALUE(report);
  ASSERT_TRUE(report->success) << DiagnosticSummary(report->diagnostics);
  EXPECT_TRUE(oxygen::cooker::test::HasCookedFile(
    cooked_root, "Resources/Buffers/b.obuf"));
}

//! A 16-bit index buffer is as good as a 32-bit one: the runtime reads both.
NOLINT_TEST(GeometryDescriptorImportJobBuffersTest, SixteenBitIndicesCook)
{
  const auto report = CookLocalMesh(LocalMesh {
    .indices = {
      std::byte { 0U },
      std::byte { 0U },
      std::byte { 1U },
      std::byte { 0U },
      std::byte { 2U },
      std::byte { 0U },
    },
    .index_stride = sizeof(uint16_t),
  });

  ASSERT_HAS_VALUE(report);
  EXPECT_TRUE(report->success) << DiagnosticSummary(report->diagnostics);
}

//! The stride says what an element is, so `__all__` of a buffer of bytes would
//! claim a vertex per byte.
NOLINT_TEST(
  GeometryDescriptorImportJobBuffersTest, ByteStrideVerticesAreRefused)
{
  const auto report = CookLocalMesh(LocalMesh {
    .vertex_stride = 1U,
    .view = data::kWholeBufferViewName,
  });

  ASSERT_HAS_VALUE(report);
  EXPECT_FALSE(report->success);
  EXPECT_TRUE(HasDiagnosticCode(
    report->diagnostics, import_codes::kGeometryBufferLayoutInvalid));
}

NOLINT_TEST(
  GeometryDescriptorImportJobBuffersTest, OtherVertexRecordsAreRefused)
{
  const auto report = CookLocalMesh(LocalMesh {
    .vertices = std::vector<std::byte>(96U),
    .vertex_stride = 32U,
  });

  ASSERT_HAS_VALUE(report);
  EXPECT_FALSE(report->success);
  EXPECT_TRUE(HasDiagnosticCode(
    report->diagnostics, import_codes::kGeometryBufferLayoutInvalid));
}

//! No reader takes a buffer of part of an element, so no cook writes one.
NOLINT_TEST(GeometryDescriptorImportJobBuffersTest, PartialVerticesAreRefused)
{
  const auto report = CookLocalMesh(LocalMesh {
    .vertices = std::vector<std::byte>((3U * kVertexStride) + 1U),
  });

  ASSERT_HAS_VALUE(report);
  EXPECT_FALSE(report->success);
  EXPECT_TRUE(HasDiagnosticCode(
    report->diagnostics, import_codes::kBufferContainerRowInvalid));
}

NOLINT_TEST(GeometryDescriptorImportJobBuffersTest, OtherIndexWidthsAreRefused)
{
  const auto report = CookLocalMesh(LocalMesh {
    .indices = IndexBytes({ 0U, 0U, 1U, 0U, 2U, 0U }),
    .index_stride = sizeof(uint64_t),
  });

  ASSERT_HAS_VALUE(report);
  EXPECT_FALSE(report->success);
  EXPECT_TRUE(HasDiagnosticCode(
    report->diagnostics, import_codes::kGeometryBufferLayoutInvalid));
}

//! The runtime refuses this mesh when it binds it; the cook names it first.
NOLINT_TEST(GeometryDescriptorImportJobBuffersTest, IndexPastItsViewIsRefused)
{
  const auto report = CookLocalMesh(LocalMesh {
    .indices = IndexBytes({ 0U, 1U, 99U }),
  });

  ASSERT_HAS_VALUE(report);
  EXPECT_FALSE(report->success);
  EXPECT_TRUE(HasDiagnosticCode(
    report->diagnostics, import_codes::kGeometryBufferViewInvalid));
}

//! The indices of a buffer an earlier job cooked are read from the root.
NOLINT_TEST(GeometryDescriptorImportJobBuffersTest,
  IndexPastItsViewInAnEarlierCookIsRefused)
{
  const ScopedTempDir temp;
  const auto root = temp.Path() / "bad_cooked_indices";
  const auto source_dir = root / "Sources";
  const auto cooked_root = root / ".cooked";
  std::filesystem::create_directories(source_dir);
  CookDefaultMaterial(cooked_root);
  const auto vb_bytes = VertexBytes();
  const auto ib_bytes = IndexBytes({ 0U, 1U, 99U });
  WriteBytes(source_dir / "vertices.bin", std::span<const std::byte>(vb_bytes));
  WriteBytes(source_dir / "indices.bin", std::span<const std::byte>(ib_bytes));

  auto container = oxygen::cooker::test::BufferContainer {
    "/Cooked/Resources/inline.buffers",
  };
  container
    .AppendBuffer(
      MakeContainerBuffer("vertices.bin", 1U, kVertexStride), kVertices)
    .AppendBuffer(MakeContainerBuffer("indices.bin", 2U, 4U), kIndices);
  const auto container_path = source_dir / "mesh.buffers.json";
  WriteText(container_path, container.Text());

  auto service = AsyncImportService(AsyncImportService::Config {
    .thread_pool_size = 2U,
  });
  [[maybe_unused]] auto stop_service
    = oxygen::Finally([&service] -> void { service.Stop(); });
  const auto cooked = SubmitAndWait(service,
    MakeBufferContainerRequest(
      { .source = container_path, .cooked = cooked_root }, container.Json()));
  ASSERT_HAS_VALUE(cooked);
  ASSERT_TRUE(cooked->success) << DiagnosticSummary(cooked->diagnostics);

  const auto geometry = MakeStandardGeometry(
    "/Cooked/Geometry/Bad.ogeo", kMaterial, kVertices, kIndices);
  const auto geometry_path = source_dir / "bad.geometry.json";
  WriteText(geometry_path, geometry.Text());
  const auto report = SubmitAndWait(service,
    MakeGeometryRequest({
      .source_path = geometry_path,
      .cooked_root = cooked_root,
      .descriptor_doc = geometry.Json(),
    }));

  ASSERT_HAS_VALUE(report);
  EXPECT_FALSE(report->success);
  EXPECT_TRUE(HasDiagnosticCode(
    report->diagnostics, import_codes::kGeometryBufferViewInvalid));
}

namespace {

  //! The skin streams of a three-vertex mesh of three joints, by default ones
  //! it can bind.
  struct LocalSkin final {
    std::vector<std::byte> joint_indices
      = IndexBytes({ 0U, 1U, 2U, 0U, 1U, 2U, 0U, 1U, 2U, 0U, 1U, 2U });
    std::vector<std::byte> joint_weights = std::vector<std::byte>(48U);
    std::vector<std::byte> inverse_bind = std::vector<std::byte>(192U);
    std::vector<std::byte> joint_remap = std::vector<std::byte>(12U);
  };

  [[nodiscard]] auto CookSkinnedMesh(const LocalSkin& skin)
    -> std::optional<ImportReport>
  {
    const ScopedTempDir temp;
    const auto root = temp.Path() / "skinned_mesh";
    const auto source_dir = root / "Sources";
    const auto cooked_root = root / ".cooked";
    std::filesystem::create_directories(source_dir);
    CookDefaultMaterial(cooked_root);
    const auto vb_bytes = VertexBytes();
    const auto ib_bytes = IndexBytes({ 0U, 1U, 2U });
    WriteBytes(
      source_dir / "vertices.bin", std::span<const std::byte>(vb_bytes));
    WriteBytes(
      source_dir / "indices.bin", std::span<const std::byte>(ib_bytes));
    WriteBytes(source_dir / "joint_index.bin",
      std::span<const std::byte>(skin.joint_indices));
    WriteBytes(source_dir / "joint_weight.bin",
      std::span<const std::byte>(skin.joint_weights));
    WriteBytes(source_dir / "inverse_bind.bin",
      std::span<const std::byte>(skin.inverse_bind));
    WriteBytes(source_dir / "joint_remap.bin",
      std::span<const std::byte>(skin.joint_remap));

    const auto buffer = [](const std::string_view leaf) -> std::string {
      return "/Cooked/Resources/Buffers/" + std::string { leaf } + ".obuf";
    };
    auto geometry = MakeStandardGeometry(
      "/Cooked/Geometry/Skinned.ogeo", kMaterial, kVertices, kIndices);
    geometry.Set("/lods/0/mesh_type", "skinned")
      .Reference("/lods/0/skinning/joint_index_ref", buffer("joint_index"))
      .Reference("/lods/0/skinning/joint_weight_ref", buffer("joint_weight"))
      .Reference("/lods/0/skinning/inverse_bind_ref", buffer("inverse_bind"))
      .Reference("/lods/0/skinning/joint_remap_ref", buffer("joint_remap"))
      .Set("/lods/0/skinning/joint_count", 3U)
      .Set("/lods/0/skinning/influences_per_vertex", 4U)
      .AppendBuffer(
        MakeBuffer("vertices.bin", 1U, kVertexStride, true), kVertices)
      .AppendBuffer(MakeBuffer("indices.bin", 2U, 4U, true), kIndices)
      .AppendBuffer(
        MakeBuffer("joint_index.bin", 8U, 16U, false), buffer("joint_index"))
      .AppendBuffer(
        MakeBuffer("joint_weight.bin", 8U, 16U, false), buffer("joint_weight"))
      .AppendBuffer(
        MakeBuffer("inverse_bind.bin", 8U, 64U, false), buffer("inverse_bind"))
      .AppendBuffer(
        MakeBuffer("joint_remap.bin", 8U, 4U, false), buffer("joint_remap"));
    const auto path = source_dir / "skinned.geometry.json";
    WriteText(path, geometry.Text());

    auto service = AsyncImportService(AsyncImportService::Config {
      .thread_pool_size = 2U,
    });
    [[maybe_unused]] auto stop_service
      = oxygen::Finally([&service] -> void { service.Stop(); });
    return SubmitAndWait(service,
      MakeGeometryRequest({
        .source_path = path,
        .cooked_root = cooked_root,
        .descriptor_doc = geometry.Json(),
      }));
  }

} // namespace

NOLINT_TEST(GeometryDescriptorImportJobBuffersTest, BindableSkinStreamsCook)
{
  const auto report = CookSkinnedMesh(LocalSkin {});

  ASSERT_HAS_VALUE(report);
  EXPECT_TRUE(report->success) << DiagnosticSummary(report->diagnostics);
}

//! Every skin stream must serve every vertex or joint the mesh names.
NOLINT_TEST(
  GeometryDescriptorImportJobBuffersTest, UndersizedSkinStreamsAreRefused)
{
  auto too_few_indices = LocalSkin {};
  too_few_indices.joint_indices = IndexBytes({ 0U, 1U, 2U, 0U });
  auto too_few_weights = LocalSkin {};
  too_few_weights.joint_weights = std::vector<std::byte>(32U);
  auto too_few_matrices = LocalSkin {};
  too_few_matrices.inverse_bind = std::vector<std::byte>(128U);
  auto too_few_remaps = LocalSkin {};
  too_few_remaps.joint_remap = std::vector<std::byte>(8U);

  for (const auto& skin :
    { too_few_indices, too_few_weights, too_few_matrices, too_few_remaps }) {
    const auto report = CookSkinnedMesh(skin);
    ASSERT_HAS_VALUE(report);
    EXPECT_FALSE(report->success);
    EXPECT_TRUE(HasDiagnosticCode(
      report->diagnostics, import_codes::kGeometrySkinningBuffersInvalid));
  }
}

NOLINT_TEST(GeometryDescriptorImportJobBuffersTest, JointPastTheMeshIsRefused)
{
  auto skin = LocalSkin {};
  skin.joint_indices
    = IndexBytes({ 0U, 1U, 2U, 0U, 1U, 2U, 0U, 1U, 2U, 0U, 1U, 3U });

  const auto report = CookSkinnedMesh(skin);

  ASSERT_HAS_VALUE(report);
  EXPECT_FALSE(report->success);
  EXPECT_TRUE(HasDiagnosticCode(
    report->diagnostics, import_codes::kGeometrySkinningBuffersInvalid));
}

//! More buffers than the bounded queues hold pass through a container job.
NOLINT_TEST(
  BufferContainerImportJobBoundedQueuesTest, BuffersPastTheQueueCapacityAllCook)
{
  const ScopedTempDir temp;
  const auto root = temp.Path() / "container_past_capacity";
  const auto source_dir = root / "Sources";
  std::filesystem::create_directories(source_dir);

  auto container = oxygen::cooker::test::BufferContainer {
    "/Cooked/Resources/inline.buffers",
  };
  for (uint32_t i = 0; i < 4U; ++i) {
    const auto leaf = "b" + std::to_string(i);
    const auto bytes = IndexBytes({ i, i + 1U, i + 2U });
    WriteBytes(source_dir / (leaf + ".bin"), std::span<const std::byte>(bytes));
    container.AppendBuffer(MakeContainerBuffer(leaf + ".bin", 2U, 4U),
      "/Cooked/Resources/Buffers/" + leaf + ".obuf");
  }
  const auto path = source_dir / "many.buffers.json";
  WriteText(path, container.Text());

  auto service = AsyncImportService(SingleSlotBufferConfig());
  [[maybe_unused]] auto stop_service
    = oxygen::Finally([&service] -> void { service.Stop(); });
  const auto report = SubmitAndWait(service,
    MakeBufferContainerRequest(
      { .source = path, .cooked = root / ".cooked" }, container.Json()));

  ASSERT_HAS_VALUE(report);
  ASSERT_TRUE(report->success) << DiagnosticSummary(report->diagnostics);
  for (uint32_t i = 0; i < 4U; ++i) {
    EXPECT_TRUE(oxygen::cooker::test::HasCookedFile(
      root / ".cooked", "Resources/Buffers/b" + std::to_string(i) + ".obuf"));
  }
}

//! A row no reader takes is refused where it is declared.
NOLINT_TEST(
  BufferContainerImportJobBoundedQueuesTest, RowNoReaderTakesIsRefused)
{
  const ScopedTempDir temp;
  const auto root = temp.Path() / "container_bad_row";
  const auto source_dir = root / "Sources";
  std::filesystem::create_directories(source_dir);
  const auto bytes = IndexBytes({ 0U, 1U, 2U });
  WriteBytes(source_dir / "a.bin", std::span<const std::byte>(bytes));

  // Two update frequencies.
  auto container = oxygen::cooker::test::BufferContainer {
    "/Cooked/Resources/inline.buffers",
  };
  container.AppendBuffer(MakeContainerBuffer("a.bin", 0x180U, 4U),
    "/Cooked/Resources/Buffers/a.obuf");
  const auto path = source_dir / "bad.buffers.json";
  WriteText(path, container.Text());

  auto service = AsyncImportService(AsyncImportService::Config {
    .thread_pool_size = 2U,
  });
  [[maybe_unused]] auto stop_service
    = oxygen::Finally([&service] -> void { service.Stop(); });
  const auto report = SubmitAndWait(service,
    MakeBufferContainerRequest(
      { .source = path, .cooked = root / ".cooked" }, container.Json()));

  ASSERT_HAS_VALUE(report);
  EXPECT_FALSE(report->success);
  EXPECT_TRUE(HasDiagnosticCode(
    report->diagnostics, import_codes::kBufferContainerRowInvalid));
}

} // namespace oxygen::content::import::test
