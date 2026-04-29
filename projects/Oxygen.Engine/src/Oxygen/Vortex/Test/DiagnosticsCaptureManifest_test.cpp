//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#include <Oxygen/Vortex/Diagnostics/DiagnosticsCaptureManifest.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsTypes.h>

namespace {

using nlohmann::json;
using oxygen::frame::SequenceNumber;
using oxygen::vortex::DiagnosticsCaptureManifestOptions;
using oxygen::vortex::DiagnosticsFeature;
using oxygen::vortex::DiagnosticsFrameSnapshot;
using oxygen::vortex::DiagnosticsIssue;
using oxygen::vortex::DiagnosticsPassKind;
using oxygen::vortex::DiagnosticsPassRecord;
using oxygen::vortex::DiagnosticsProductRecord;
using oxygen::vortex::DiagnosticsSeverity;
using oxygen::vortex::BuildDiagnosticsCaptureManifestJson;
using oxygen::vortex::ShaderDebugMode;
using oxygen::vortex::WriteDiagnosticsCaptureManifest;
using oxygen::vortex::kDiagnosticsCaptureManifestSchema;

auto MakeSnapshot() -> DiagnosticsFrameSnapshot
{
  auto snapshot = DiagnosticsFrameSnapshot {};
  snapshot.frame_index = SequenceNumber { 17U };
  snapshot.active_shader_debug_mode = ShaderDebugMode::kDirectionalShadowMask;
  snapshot.requested_features = DiagnosticsFeature::kFrameLedger
    | DiagnosticsFeature::kCaptureManifest;
  snapshot.enabled_features = snapshot.requested_features;
  snapshot.gpu_timeline_enabled = true;
  snapshot.gpu_timeline_frame_available = true;
  snapshot.passes.push_back(DiagnosticsPassRecord {
    .name = "Vortex.Stage12.DeferredLighting",
    .kind = DiagnosticsPassKind::kGraphics,
    .executed = true,
    .inputs = { "Vortex.SceneColor", "Vortex.GBuffer" },
    .outputs = { "Vortex.SceneColor" },
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
    .descriptor = "draws=42 occlusion_culled=7",
    .published = true,
    .valid = true,
  });
  snapshot.issues.push_back(DiagnosticsIssue {
    .severity = DiagnosticsSeverity::kWarning,
    .code = "debug-mode.missing-product",
    .message = "missing debug product",
    .pass_name = "Vortex.Stage12.DeferredLighting",
    .product_name = "Vortex.DebugDirectionalShadowMask",
    .occurrences = 2U,
  });
  return snapshot;
}

NOLINT_TEST(DiagnosticsCaptureManifestTest,
  EmitsStableSchemaAndRoundTripsAsJson)
{
  const auto manifest = json::parse(BuildDiagnosticsCaptureManifestJson(
    MakeSnapshot(),
    DiagnosticsCaptureManifestOptions {
      .gpu_timeline_export_path = std::filesystem::path {
        "captures/gpu-timeline.json",
      },
    }));

  EXPECT_EQ(manifest["schema"], kDiagnosticsCaptureManifestSchema);
  EXPECT_EQ(manifest["version"], 1);
  EXPECT_EQ(manifest["frame"]["index"], 17);
  EXPECT_EQ(manifest["frame"]["active_shader_debug_mode"],
    "DirectionalShadowMask");
  EXPECT_EQ(manifest["gpu_timeline_export_path"],
    "captures/gpu-timeline.json");
  ASSERT_EQ(manifest["passes"].size(), 1U);
  EXPECT_EQ(manifest["passes"][0]["kind"], "Graphics");
  ASSERT_EQ(manifest["products"].size(), 2U);
  EXPECT_FALSE(manifest["products"][0].contains("descriptor"));
  EXPECT_EQ(manifest["products"][0]["resource_name"], "SceneColorTexture");
  EXPECT_EQ(manifest["products"][1]["descriptor"],
    "draws=42 occlusion_culled=7");
  ASSERT_EQ(manifest["issues"].size(), 1U);
  EXPECT_EQ(manifest["issues"][0]["occurrences"], 2);
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
  const auto manifest = json::parse(input);
  EXPECT_EQ(manifest["schema"], kDiagnosticsCaptureManifestSchema);

  input.close();
  EXPECT_EQ(std::filesystem::remove_all(output_dir), 2U);
}

} // namespace
