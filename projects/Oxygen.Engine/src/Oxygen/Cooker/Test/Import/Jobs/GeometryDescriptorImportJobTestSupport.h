//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <latch>
#include <optional>
#include <span>
#include <sstream>
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
#include <Oxygen/Cooker/Test/Support/Diagnostics.h>
#include <Oxygen/Cooker/Test/Support/FileIo.h>
#include <Oxygen/Cooker/Test/Support/TempDir.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {

using nlohmann::json;
using oxygen::cooker::test::HasDiagnosticCode;
using oxygen::cooker::test::ReadBytes;
using oxygen::cooker::test::ScopedTempDir;
using oxygen::cooker::test::WriteBytes;
using oxygen::cooker::test::WriteText;

template <typename T>
auto ReadStructAt(const std::vector<std::byte>& bytes, const size_t offset) -> T
{
  auto out = T {};
  if (bytes.size() < offset + sizeof(T)) {
    return out;
  }
  std::memcpy(&out, bytes.data() + offset, sizeof(T));
  return out;
}

inline auto DiagnosticSummary(const std::vector<ImportDiagnostic>& diagnostics)
  -> std::string
{
  auto out = std::ostringstream {};
  for (const auto& diagnostic : diagnostics) {
    out << "[" << diagnostic.code << "] " << diagnostic.message;
    if (!diagnostic.object_path.empty()) {
      out << " (" << diagnostic.object_path << ")";
    }
    out << '\n';
  }
  return out.str();
}

inline auto SubmitAndWait(AsyncImportService& service, ImportRequest request)
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
  EXPECT_TRUE(submitted.has_value());
  done.wait();
  return report;
}

inline auto MakeBounds() -> json
{
  return json {
    { "min", json::array({ -0.5F, -0.5F, -0.5F }) },
    { "max", json::array({ 0.5F, 0.5F, 0.5F }) },
  };
}

inline constexpr std::string_view kAuthoredSlotId
  = "018f8f8f-1111-7111-8111-111111111111";

inline auto MakeGeometryRequest(const std::filesystem::path& source_path,
  const std::filesystem::path& cooked_root, const json& descriptor_doc,
  std::vector<std::filesystem::path> cooked_context_roots = {}) -> ImportRequest
{
  auto request = ImportRequest {};
  request.source_path = source_path.lexically_normal();
  request.cooked_root = cooked_root;
  request.cooked_context_roots = std::move(cooked_context_roots);
  request.loose_cooked_layout.virtual_mount_root = "/.cooked";
  request.geometry_descriptor = ImportRequest::GeometryDescriptorPayload {
    .normalized_descriptor_json = descriptor_doc.dump(),
  };
  return request;
}

inline auto MakeStandardDescriptorDoc(std::string_view name,
  std::string_view material_ref, std::string_view vb_ref,
  std::string_view ib_ref, std::string_view view_ref,
  std::optional<json> local_buffers) -> json
{
  auto descriptor_doc = json {
    { "name", name },
    { "bounds", MakeBounds() },
    {
      "lods",
      json::array({
        json {
          { "name", "LOD0" },
          { "mesh_type", "standard" },
          { "bounds", MakeBounds() },
          {
            "buffers",
            {
              { "vb_ref", vb_ref },
              { "ib_ref", ib_ref },
            },
          },
          {
            "submeshes",
            json::array({
              json {
                { "slot_id", kAuthoredSlotId },
                { "material_ref", material_ref },
                {
                  "views",
                  json::array({ json { { "view_ref", view_ref } } }),
                },
              },
            }),
          },
        },
      }),
    },
  };

  if (local_buffers.has_value()) {
    descriptor_doc.update({ { "buffers", std::move(*local_buffers) } });
  }
  return descriptor_doc;
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

  auto stream
    = serio::MemoryStream(std::span<std::byte>(bytes.data(), bytes.size()));
  auto reader = serio::Reader(stream);

  auto context = content::LoaderContext {};
  context.desc_reader = &reader;
  context.parse_only = true;

  try {
    const auto geometry
      = content::loaders::LoadGeometryAsset(std::move(context));
    return geometry != nullptr;
  } catch (...) {
    return false;
  }
}

} // namespace oxygen::content::import::test
