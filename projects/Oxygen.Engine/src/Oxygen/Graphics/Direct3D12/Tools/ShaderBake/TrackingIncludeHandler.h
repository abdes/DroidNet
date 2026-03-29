//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <unknwn.h>
#include <windows.h>

#include <dxcapi.h>
#include <wrl/client.h>

#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/FileFingerprint.h>

namespace oxygen::graphics::d3d12::tools::shader_bake {

struct ResolvedInclude {
  std::filesystem::path absolute_path;
  DependencyFingerprint fingerprint;
};

class TrackingDependencyRecorder {
public:
  auto RecordDependency(const DependencyFingerprint& dependency) -> void;

  [[nodiscard]] auto Dependencies() const
    -> const std::vector<DependencyFingerprint>&
  {
    return dependencies_;
  }

private:
  std::unordered_set<std::string> seen_paths_;
  std::vector<DependencyFingerprint> dependencies_;
};

[[nodiscard]] auto ResolveTrackedInclude(std::wstring_view requested_filename,
  const std::filesystem::path& workspace_root,
  std::span<const std::filesystem::path> include_dirs)
  -> std::optional<ResolvedInclude>;

class TrackingIncludeHandler final : public IDxcIncludeHandler {
public:
  TrackingIncludeHandler(IDxcUtils& utils, std::filesystem::path workspace_root,
    std::span<const std::filesystem::path> include_dirs);

  auto STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject)
    -> HRESULT override;
  auto STDMETHODCALLTYPE AddRef() -> ULONG override;
  auto STDMETHODCALLTYPE Release() -> ULONG override;
  auto STDMETHODCALLTYPE LoadSource(
    LPCWSTR pFilename, IDxcBlob** ppIncludeSource) -> HRESULT override;

  [[nodiscard]] auto Dependencies() const
    -> const std::vector<DependencyFingerprint>&
  {
    return recorder_.Dependencies();
  }

private:
  std::atomic<ULONG> ref_count_ { 1 };
  Microsoft::WRL::ComPtr<IDxcUtils> utils_;
  std::filesystem::path workspace_root_;
  std::vector<std::filesystem::path> include_dirs_;
  TrackingDependencyRecorder recorder_;
};

} // namespace oxygen::graphics::d3d12::tools::shader_bake
