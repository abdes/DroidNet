//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <latch>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Content/LoaderContext.h>
#include <Oxygen/Content/Loaders/GeometryLoader.h>
#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportJobId.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/test/Support/AuthoredFiles.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Cooker/test/Support/FileIo.h>
#include <Oxygen/Cooker/test/Support/ImportHarness.h>
#include <Oxygen/Cooker/test/Support/TestPaths.h>
#include <Oxygen/Serio/ByteReader.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/ScopedTempDir.h>

namespace oxygen::content::import::test {

using nlohmann::json;
using oxygen::cooker::test::DiagnosticSummary;
using oxygen::cooker::test::HasDiagnosticCode;
using oxygen::cooker::test::ReadBytes;
using oxygen::cooker::test::SubmitAndWait;
using oxygen::cooker::test::WriteBytes;
using oxygen::cooker::test::WriteText;
using oxygen::testing::ScopedTempDir;

inline constexpr std::string_view kAuthoredSurfaceId
  = "018f8f8f-1111-7111-8111-111111111111";

//! What a geometry descriptor import request is made from.
struct GeometryRequestInput {
  std::filesystem::path source_path;
  std::filesystem::path cooked_root;
  json descriptor_doc;
  std::vector<std::filesystem::path> cooked_context_roots;
  //! Defaults to `/.cooked/Geometry/<name>.ogeo`.
  std::string asset_virtual_path;
};

inline auto MakeGeometryRequest(GeometryRequestInput input) -> ImportRequest
{
  auto& asset_virtual_path = input.asset_virtual_path;
  const auto& descriptor_doc = input.descriptor_doc;
  if (asset_virtual_path.empty()) {
    asset_virtual_path = "/.cooked/Geometry/"
      + descriptor_doc.at("name").get<std::string>() + ".ogeo";
  }
  auto request = ImportRequest {};
  request.asset_virtual_path = std::move(asset_virtual_path);
  request.source_path = input.source_path.lexically_normal();
  request.source_mount_path
    = oxygen::cooker::test::SourceMountPath(request.source_path);
  request.cooked_root = input.cooked_root;
  request.cooked_context_roots = std::move(input.cooked_context_roots);
  request.source_mounts = oxygen::cooker::test::ContentMountTable(
    request.source_path.parent_path());
  request.geometry_descriptor = ImportRequest::GeometryDescriptorPayload {
    .normalized_descriptor_json = descriptor_doc.dump(),
  };
  return request;
}

//! A buffer entry reading `uri`; `with_view` adds the one view of three
//! elements the standard LOD draws.
inline auto MakeBuffer(const std::string& uri, const uint32_t usage_flags,
  const uint32_t element_stride, const bool with_view) -> json
{
  auto buffer = json {
    { "uri", uri },
    { "usage_flags", usage_flags },
    { "element_stride", element_stride },
  };
  if (with_view) {
    buffer["views"] = json::array({
      json {
        { "name", "lod0" },
        { "element_offset", 0U },
        { "element_count", 3U },
      },
    });
  }
  return buffer;
}

//! A buffer of a buffer container read from the file `source`, with the
//! view `lod0` of its first three elements.
inline auto MakeContainerBuffer(const std::string& source,
  const uint32_t usage_flags, const uint32_t element_stride) -> json
{
  return json {
    { "source", source },
    { "usage_flags", usage_flags },
    { "element_stride", element_stride },
    {
      "views",
      json::array({
        json {
          { "name", "lod0" },
          { "element_offset", 0U },
          { "element_count", 3U },
        },
      }),
    },
  };
}

//! A standard-mesh geometry whose one LOD draws the view `view` of the buffers
//! `vb` and `ib` with the material `material`.
inline auto MakeStandardGeometry(const std::string& identity,
  const std::string_view material, const std::string_view vb,
  const std::string_view ib, const std::string_view view = "lod0")
  -> oxygen::cooker::test::GeometryDescriptor
{
  const auto bounds = json {
    { "min", json::array({ -0.5F, -0.5F, -0.5F }) },
    { "max", json::array({ 0.5F, 0.5F, 0.5F }) },
  };
  auto geometry = oxygen::cooker::test::GeometryDescriptor { identity };
  geometry.Set("/bounds", bounds)
    .Set("/lods/0/mesh_type", "standard")
    .Remove("/lods/0/procedural")
    .Set("/lods/0/bounds", bounds)
    .Reference("/lods/0/buffers/vb_ref", vb)
    .Reference("/lods/0/buffers/ib_ref", ib)
    .SubmeshMaterial(0, 0, material)
    .Set("/lods/0/submeshes/0/views/0/view_ref", view);
  return geometry;
}

inline auto FindOutputByExtension(const ImportReport& report,
  std::string_view extension) -> std::optional<std::string>
{
  const auto it = std::ranges::find_if(
    report.outputs, [extension](const ImportOutputRecord& output) -> bool {
      return std::filesystem::path(output.path).extension().string()
        == extension;
    });
  if (it == report.outputs.end()) {
    return std::nullopt;
  }
  return it->path;
}

inline auto CanParseGeometryDescriptor(std::vector<std::byte> bytes) -> bool
{
  if (bytes.empty()) {
    return false;
  }

  auto stream = serio::ReadOnlyMemoryStream(std::span(bytes));
  auto reader = serio::Reader(stream);

  auto context = content::LoaderContext {};
  context.desc_reader = &reader;
  context.parse_only = true;

  try {
    const auto geometry = content::loaders::LoadGeometryAsset(context);
    return geometry != nullptr;
  } catch (...) {
    return false;
  }
}

} // namespace oxygen::content::import::test
