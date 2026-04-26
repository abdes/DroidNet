//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Diagnostics/DiagnosticsCaptureManifest.h>

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace oxygen::vortex {

namespace {
  constexpr auto kIndentSpaces = 2;

  auto EscapeJson(const std::string_view value) -> std::string
  {
    auto escaped = std::string {};
    escaped.reserve(value.size());
    for (const char c : value) {
      switch (c) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        escaped.push_back(c);
        break;
      }
    }
    return escaped;
  }

  auto Quote(const std::string_view value) -> std::string
  {
    return "\"" + EscapeJson(value) + "\"";
  }

  auto Indent(std::ostream& out, const int level) -> std::ostream&
  {
    for (auto i = 0; i < level * kIndentSpaces; ++i) {
      out.put(' ');
    }
    return out;
  }

  auto WriteStringArray(std::ostream& out, const std::vector<std::string>& values)
    -> void
  {
    out << "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
      out << (i == 0U ? "" : ", ") << Quote(values[i]);
    }
    out << "]";
  }

  auto WritePass(std::ostream& out, const DiagnosticsPassRecord& pass)
    -> void
  {
    out << "{\n";
    Indent(out, 4) << "\"name\": " << Quote(pass.name) << ",\n";
    Indent(out, 4) << "\"kind\": " << Quote(to_string(pass.kind)) << ",\n";
    Indent(out, 4) << "\"executed\": "
                   << (pass.executed ? "true" : "false") << ",\n";
    Indent(out, 4) << "\"inputs\": ";
    WriteStringArray(out, pass.inputs);
    out << ",\n";
    Indent(out, 4) << "\"outputs\": ";
    WriteStringArray(out, pass.outputs);
    out << ",\n";
    Indent(out, 4) << "\"missing_inputs\": ";
    WriteStringArray(out, pass.missing_inputs);
    if (pass.gpu_duration_ms.has_value()) {
      out << ",\n";
      Indent(out, 4) << "\"gpu_duration_ms\": " << *pass.gpu_duration_ms
                     << "\n";
    } else {
      out << "\n";
    }
    Indent(out, 3) << "}";
  }

  auto WriteProduct(std::ostream& out, const DiagnosticsProductRecord& product)
    -> void
  {
    out << "{\n";
    Indent(out, 4) << "\"name\": " << Quote(product.name) << ",\n";
    Indent(out, 4) << "\"producer_pass\": "
                   << Quote(product.producer_pass) << ",\n";
    Indent(out, 4) << "\"resource_name\": "
                   << Quote(product.resource_name) << ",\n";
    Indent(out, 4) << "\"published\": "
                   << (product.published ? "true" : "false") << ",\n";
    Indent(out, 4) << "\"valid\": " << (product.valid ? "true" : "false")
                   << ",\n";
    Indent(out, 4) << "\"stale\": " << (product.stale ? "true" : "false")
                   << "\n";
    Indent(out, 3) << "}";
  }

  auto WriteIssue(std::ostream& out, const DiagnosticsIssue& issue) -> void
  {
    out << "{\n";
    Indent(out, 4) << "\"severity\": " << Quote(to_string(issue.severity))
                   << ",\n";
    Indent(out, 4) << "\"code\": " << Quote(issue.code) << ",\n";
    Indent(out, 4) << "\"message\": " << Quote(issue.message) << ",\n";
    Indent(out, 4) << "\"view_name\": " << Quote(issue.view_name) << ",\n";
    Indent(out, 4) << "\"pass_name\": " << Quote(issue.pass_name) << ",\n";
    Indent(out, 4) << "\"product_name\": " << Quote(issue.product_name)
                   << ",\n";
    Indent(out, 4) << "\"occurrences\": " << issue.occurrences << "\n";
    Indent(out, 3) << "}";
  }

} // namespace

auto BuildDiagnosticsCaptureManifestJson(
  const DiagnosticsFrameSnapshot& snapshot,
  const DiagnosticsCaptureManifestOptions& options) -> std::string
{
  auto out = std::ostringstream {};
  out << "{\n";
  Indent(out, 1) << "\"schema\": "
                 << Quote(kDiagnosticsCaptureManifestSchema) << ",\n";
  Indent(out, 1) << "\"version\": 1,\n";
  Indent(out, 1) << "\"frame\": {\n";
  Indent(out, 2) << "\"index\": " << snapshot.frame_index.get() << ",\n";
  Indent(out, 2) << "\"active_shader_debug_mode\": "
                 << Quote(to_string(snapshot.active_shader_debug_mode))
                 << ",\n";
  Indent(out, 2) << "\"requested_features\": "
                 << Quote(to_string(snapshot.requested_features)) << ",\n";
  Indent(out, 2) << "\"enabled_features\": "
                 << Quote(to_string(snapshot.enabled_features)) << ",\n";
  Indent(out, 2) << "\"gpu_timeline_enabled\": "
                 << (snapshot.gpu_timeline_enabled ? "true" : "false")
                 << ",\n";
  Indent(out, 2) << "\"gpu_timeline_frame_available\": "
                 << (snapshot.gpu_timeline_frame_available ? "true" : "false")
                 << ",\n";
  Indent(out, 2) << "\"capture_manifest_available\": "
                 << (snapshot.capture_manifest_available ? "true" : "false")
                 << "\n";
  Indent(out, 1) << "},\n";

  Indent(out, 1) << "\"passes\": [\n";
  for (std::size_t i = 0; i < snapshot.passes.size(); ++i) {
    Indent(out, 2);
    WritePass(out, snapshot.passes[i]);
    out << (i + 1U == snapshot.passes.size() ? "\n" : ",\n");
  }
  Indent(out, 1) << "],\n";

  Indent(out, 1) << "\"products\": [\n";
  for (std::size_t i = 0; i < snapshot.products.size(); ++i) {
    Indent(out, 2);
    WriteProduct(out, snapshot.products[i]);
    out << (i + 1U == snapshot.products.size() ? "\n" : ",\n");
  }
  Indent(out, 1) << "],\n";

  Indent(out, 1) << "\"issues\": [\n";
  for (std::size_t i = 0; i < snapshot.issues.size(); ++i) {
    Indent(out, 2);
    WriteIssue(out, snapshot.issues[i]);
    out << (i + 1U == snapshot.issues.size() ? "\n" : ",\n");
  }
  Indent(out, 1) << "]";

  if (options.gpu_timeline_export_path.has_value()) {
    out << ",\n";
    Indent(out, 1) << "\"gpu_timeline_export_path\": "
                   << Quote(options.gpu_timeline_export_path->generic_string());
  }

  out << "\n}\n";
  return out.str();
}

auto WriteDiagnosticsCaptureManifest(const std::filesystem::path& path,
  const DiagnosticsFrameSnapshot& snapshot,
  const DiagnosticsCaptureManifestOptions& options) -> void
{
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path());
  }
  auto out = std::ofstream(path, std::ios::binary | std::ios::trunc);
  out << BuildDiagnosticsCaptureManifestJson(snapshot, options);
  if (!out.good()) {
    throw std::runtime_error("failed to write diagnostics capture manifest");
  }
}

} // namespace oxygen::vortex
