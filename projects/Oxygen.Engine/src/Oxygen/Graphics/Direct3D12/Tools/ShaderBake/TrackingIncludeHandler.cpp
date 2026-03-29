//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Direct3D12/Tools/ShaderBake/TrackingIncludeHandler.h>

#include <system_error>

#include <windows.h>

namespace oxygen::graphics::d3d12::tools::shader_bake {

namespace {

  auto FindResolvedIncludePath(std::wstring_view requested_filename,
    std::span<const std::filesystem::path> include_dirs)
    -> std::optional<std::filesystem::path>
  {
    const std::filesystem::path candidate(requested_filename);

    std::error_code ec;
    if (candidate.is_absolute()) {
      if (std::filesystem::is_regular_file(candidate, ec) && !ec) {
        return candidate.lexically_normal();
      }
      return std::nullopt;
    }

    for (const auto& include_dir : include_dirs) {
      const auto resolved = (include_dir / candidate).lexically_normal();
      ec.clear();
      if (std::filesystem::is_regular_file(resolved, ec) && !ec) {
        return resolved;
      }
    }

    return std::nullopt;
  }

} // namespace

auto TrackingDependencyRecorder::RecordDependency(
  const DependencyFingerprint& dependency) -> void
{
  const auto [_, inserted] = seen_paths_.insert(dependency.path);
  if (!inserted) {
    return;
  }

  dependencies_.push_back(dependency);
}

auto ResolveTrackedInclude(std::wstring_view requested_filename,
  const std::filesystem::path& workspace_root,
  std::span<const std::filesystem::path> include_dirs)
  -> std::optional<ResolvedInclude>
{
  const auto resolved_path
    = FindResolvedIncludePath(requested_filename, include_dirs);
  if (!resolved_path.has_value()) {
    return std::nullopt;
  }

  return ResolvedInclude {
    .absolute_path = *resolved_path,
    .fingerprint = ComputeFileFingerprint(*resolved_path, workspace_root),
  };
}

TrackingIncludeHandler::TrackingIncludeHandler(IDxcUtils& utils,
  std::filesystem::path workspace_root,
  std::span<const std::filesystem::path> include_dirs)
  : utils_(&utils)
  , workspace_root_(std::move(workspace_root))
  , include_dirs_(include_dirs.begin(), include_dirs.end())
{
}

auto STDMETHODCALLTYPE TrackingIncludeHandler::QueryInterface(
  REFIID riid, void** ppvObject) -> HRESULT
{
  if (ppvObject == nullptr) {
    return E_POINTER;
  }
  *ppvObject = nullptr;

  if (riid == __uuidof(IDxcIncludeHandler) || riid == __uuidof(IUnknown)) {
    *ppvObject = static_cast<IDxcIncludeHandler*>(this);
    AddRef();
    return S_OK;
  }

  return E_NOINTERFACE;
}

auto STDMETHODCALLTYPE TrackingIncludeHandler::AddRef() -> ULONG
{
  return ++ref_count_;
}

auto STDMETHODCALLTYPE TrackingIncludeHandler::Release() -> ULONG
{
  const auto remaining = --ref_count_;
  if (remaining == 0) {
    delete this;
  }
  return remaining;
}

auto STDMETHODCALLTYPE TrackingIncludeHandler::LoadSource(
  LPCWSTR pFilename, IDxcBlob** ppIncludeSource) -> HRESULT
{
  if (ppIncludeSource == nullptr) {
    return E_POINTER;
  }
  *ppIncludeSource = nullptr;
  if (pFilename == nullptr) {
    return E_INVALIDARG;
  }

  const auto resolved
    = ResolveTrackedInclude(pFilename, workspace_root_, include_dirs_);
  if (!resolved.has_value()) {
    return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
  }

  UINT32 code_page = CP_UTF8;
  Microsoft::WRL::ComPtr<IDxcBlobEncoding> include_blob;
  const auto hr = utils_->LoadFile(
    resolved->absolute_path.c_str(), &code_page, include_blob.GetAddressOf());
  if (FAILED(hr)) {
    return hr;
  }

  recorder_.RecordDependency(resolved->fingerprint);
  *ppIncludeSource = include_blob.Detach();
  return S_OK;
}

} // namespace oxygen::graphics::d3d12::tools::shader_bake
