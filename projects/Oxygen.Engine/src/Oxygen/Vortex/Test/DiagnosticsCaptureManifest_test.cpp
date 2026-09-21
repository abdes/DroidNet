//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsCaptureManifest.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsTypes.h>
#include <Oxygen/Vortex/ShaderDebugMode.h>

namespace {

using nlohmann::json;
using oxygen::frame::SequenceNumber;
using oxygen::vortex::BuildDiagnosticsCaptureManifestJson;
using oxygen::vortex::DiagnosticsCaptureManifestOptions;
using oxygen::vortex::DiagnosticsFeature;
using oxygen::vortex::DiagnosticsFrameSnapshot;
using oxygen::vortex::DiagnosticsIssue;
using oxygen::vortex::DiagnosticsPassKind;
using oxygen::vortex::DiagnosticsPassRecord;
using oxygen::vortex::DiagnosticsProductRecord;
using oxygen::vortex::DiagnosticsSeverity;
using oxygen::vortex::kDiagnosticsCaptureManifestSchema;
using oxygen::vortex::ShaderDebugMode;
using oxygen::vortex::WriteDiagnosticsCaptureManifest;

auto MakeSnapshot() -> DiagnosticsFrameSnapshot
{
  auto snapshot = DiagnosticsFrameSnapshot {};
  snapshot.frame_index = SequenceNumber {
    17U,
  };
  snapshot.active_shader_debug_mode = ShaderDebugMode::kDirectionalShadowMask;
  snapshot.requested_features
    = DiagnosticsFeature::kFrameLedger | DiagnosticsFeature::kCaptureManifest;
  snapshot.enabled_features = snapshot.requested_features;
  snapshot.gpu_timeline_enabled = true;
  snapshot.gpu_timeline_frame_available = true;
  snapshot.passes.push_back(DiagnosticsPassRecord {
    .name = "Vortex.Stage12.DeferredLighting",
    .kind = DiagnosticsPassKind::kGraphics,
    .executed = true,
    .inputs = { "Vortex.SceneColor", "Vortex.GBuffer" },
    .outputs = { "Vortex.SceneColor" },
    .missing_inputs = {},
    .gpu_duration_ms = 0.25F,
  });
  snapshot.products.push_back(DiagnosticsProductRecord {
    .name = "Vortex.SceneColor",
    .producer_pass = "Vortex.Stage12.DeferredLighting",
    .resource_name = "SceneColorTexture",
    .descriptor = "bindless:999",
    .published = true,
    .valid = true,
  });
  snapshot.products.push_back(DiagnosticsProductRecord {
    .name = "Vortex.BasePassDrawCommands",
    .producer_pass = "Vortex.Stage9.BasePass",
    .resource_name = {},
    .descriptor = "draws=42 occlusion_culled=7",
    .published = true,
    .valid = true,
  });
  snapshot.issues.push_back(DiagnosticsIssue {
    .severity = DiagnosticsSeverity::kWarning,
    .code = "debug-mode.missing-product",
    .message = "missing debug product",
    .view_name = {},
    .pass_name = "Vortex.Stage12.DeferredLighting",
    .product_name = "Vortex.DebugDirectionalShadowMask",
    .occurrences = 2U,
  });
  return snapshot;
}

NOLINT_TEST(
  DiagnosticsCaptureManifestTest, EmitsStableSchemaAndRoundTripsAsJson)
{
  const auto manifest = json::parse(BuildDiagnosticsCaptureManifestJson(
    MakeSnapshot(),
    DiagnosticsCaptureManifestOptions {
      .gpu_timeline_export_path = std::filesystem::path {
        "captures/gpu-timeline.json",
      },
    }));

  EXPECT_EQ(manifest.at("schema"), kDiagnosticsCaptureManifestSchema);
  EXPECT_EQ(manifest.at("version"), 1);
  EXPECT_EQ(manifest.at("frame").at("index"), 17);
  EXPECT_EQ(manifest.at("frame").at("active_shader_debug_mode"),
    "DirectionalShadowMask");
  EXPECT_EQ(
    manifest.at("gpu_timeline_export_path"), "captures/gpu-timeline.json");
  ASSERT_EQ(manifest.at("passes").size(), 1U);
  EXPECT_EQ(manifest.at("passes").at(0).at("kind"), "Graphics");
  ASSERT_EQ(manifest.at("products").size(), 2U);
  EXPECT_FALSE(manifest.at("products").at(0).contains("descriptor"));
  EXPECT_EQ(
    manifest.at("products").at(0).at("resource_name"), "SceneColorTexture");
  EXPECT_EQ(manifest.at("products").at(1).at("descriptor"),
    "draws=42 occlusion_culled=7");
  ASSERT_EQ(manifest.at("issues").size(), 1U);
  EXPECT_EQ(manifest.at("issues").at(0).at("occurrences"), 2);
}

NOLINT_TEST(DiagnosticsCaptureManifestTest, WritesManifestFile)
{
  const auto unique_suffix = std::to_string(
    std::chrono::steady_clock::now().time_since_epoch().count());
  const auto output_dir = std::filesystem::temp_directory_path()
    / ("oxygen_vortex_capture_manifest_test_" + unique_suffix);
  const auto output_path = output_dir / "manifest.json";

  WriteDiagnosticsCaptureManifest(output_path, MakeSnapshot());

  auto input = std::ifstream(output_path, std::ios::binary);
  ASSERT_TRUE(input.is_open()) << output_path.string();
  const auto manifest = json::parse(input);
  EXPECT_EQ(manifest.at("schema"), kDiagnosticsCaptureManifestSchema);

  input.close();
  EXPECT_EQ(std::filesystem::remove_all(output_dir), 2U);
}

} // namespace
