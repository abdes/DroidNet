//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "MainModule.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/StringUtils.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Data/ShaderReference.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/ImGui/ImGuiModule.h>
#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/ActionTriggers.h>
#include <Oxygen/Input/InputActionMapping.h>
#include <Oxygen/Input/InputMappingContext.h>
#include <Oxygen/Input/InputSystem.h>
#include <Oxygen/Platform/Input.h>
#include <Oxygen/Renderer/Passes/ShaderPass.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyLight.h>
#include <Oxygen/Scene/Environment/SkySphere.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Light/PointLight.h>

#if defined(OXYGEN_WINDOWS)
#  include <shobjidl_core.h>
#  include <wincodec.h>
#  include <windows.h>
#  include <wrl/client.h>
#endif

namespace {

using oxygen::Quat;
using oxygen::Vec3;

#if defined(OXYGEN_WINDOWS)

class ScopedCoInitialize {
public:
  ScopedCoInitialize()
  {
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    initialized_ = SUCCEEDED(hr);
    // If COM is already initialized in a different mode, we proceed without
    // owning CoUninitialize() for this scope.
    if (hr == RPC_E_CHANGED_MODE) {
      initialized_ = false;
    }
  }

  ~ScopedCoInitialize()
  {
    if (initialized_) {
      CoUninitialize();
    }
  }

  ScopedCoInitialize(const ScopedCoInitialize&) = delete;
  ScopedCoInitialize& operator=(const ScopedCoInitialize&) = delete;
  ScopedCoInitialize(ScopedCoInitialize&&) = delete;
  ScopedCoInitialize& operator=(ScopedCoInitialize&&) = delete;

private:
  bool initialized_ { false };
};

auto TryBrowseForPngFile(std::string& out_utf8_path) -> bool
{
  ScopedCoInitialize com;

  Microsoft::WRL::ComPtr<IFileOpenDialog> dlg;
  const HRESULT hr = CoCreateInstance(
    CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg));
  if (FAILED(hr) || !dlg) {
    return false;
  }

  constexpr COMDLG_FILTERSPEC kFilters[] = {
    { L"PNG images (*.png)", L"*.png" },
    { L"All files (*.*)", L"*.*" },
  };
  (void)dlg->SetFileTypes(static_cast<UINT>(std::size(kFilters)), kFilters);
  (void)dlg->SetDefaultExtension(L"png");

  const HRESULT show_hr = dlg->Show(nullptr);
  if (FAILED(show_hr)) {
    return false;
  }

  Microsoft::WRL::ComPtr<IShellItem> item;
  if (FAILED(dlg->GetResult(&item)) || !item) {
    return false;
  }

  PWSTR wide_path = nullptr;
  const HRESULT name_hr = item->GetDisplayName(SIGDN_FILESYSPATH, &wide_path);
  if (FAILED(name_hr) || !wide_path) {
    return false;
  }

  std::string utf8;
  oxygen::string_utils::WideToUtf8(wide_path, utf8);
  CoTaskMemFree(wide_path);

  if (utf8.empty()) {
    return false;
  }

  out_utf8_path = std::move(utf8);
  return true;
}

auto TryBrowseForImageFile(std::string& out_utf8_path) -> bool
{
  ScopedCoInitialize com;

  Microsoft::WRL::ComPtr<IFileOpenDialog> dlg;
  const HRESULT hr = CoCreateInstance(
    CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg));
  if (FAILED(hr) || !dlg) {
    return false;
  }

  constexpr COMDLG_FILTERSPEC kFilters[] = {
    { L"Images (*.jpg;*.jpeg;*.png)", L"*.jpg;*.jpeg;*.png" },
    { L"All files (*.*)", L"*.*" },
  };
  (void)dlg->SetFileTypes(static_cast<UINT>(std::size(kFilters)), kFilters);
  (void)dlg->SetDefaultExtension(L"jpg");

  const HRESULT show_hr = dlg->Show(nullptr);
  if (FAILED(show_hr)) {
    return false;
  }

  Microsoft::WRL::ComPtr<IShellItem> item;
  if (FAILED(dlg->GetResult(&item)) || !item) {
    return false;
  }

  PWSTR wide_path = nullptr;
  const HRESULT name_hr = item->GetDisplayName(SIGDN_FILESYSPATH, &wide_path);
  if (FAILED(name_hr) || !wide_path) {
    return false;
  }

  std::string utf8;
  oxygen::string_utils::WideToUtf8(wide_path, utf8);
  CoTaskMemFree(wide_path);

  if (utf8.empty()) {
    return false;
  }

  out_utf8_path = std::move(utf8);
  return true;
}

auto DecodeImageRgba8Wic(const std::filesystem::path& file_path,
  std::vector<std::byte>& out_rgba8, std::uint32_t& out_width,
  std::uint32_t& out_height, std::string& out_error) -> bool
{
  out_rgba8.clear();
  out_width = 0;
  out_height = 0;

  ScopedCoInitialize com;

  std::wstring wide_path;
  oxygen::string_utils::Utf8ToWide(file_path.u8string(), wide_path);

  Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
  HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
    CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
  if (FAILED(hr) || !factory) {
    out_error = "WIC factory unavailable";
    return false;
  }

  Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
  hr = factory->CreateDecoderFromFilename(wide_path.c_str(), nullptr,
    GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
  if (FAILED(hr) || !decoder) {
    out_error = "Failed to open/decode image";
    return false;
  }

  Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
  hr = decoder->GetFrame(0, &frame);
  if (FAILED(hr) || !frame) {
    out_error = "Failed to read image frame";
    return false;
  }

  UINT w = 0;
  UINT h = 0;
  hr = frame->GetSize(&w, &h);
  if (FAILED(hr) || w == 0 || h == 0) {
    out_error = "Invalid image size";
    return false;
  }

  Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
  hr = factory->CreateFormatConverter(&converter);
  if (FAILED(hr) || !converter) {
    out_error = "Failed to create WIC format converter";
    return false;
  }

  hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
    WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
  if (FAILED(hr)) {
    out_error = "Failed to convert to RGBA8";
    return false;
  }

  const std::uint32_t stride = static_cast<std::uint32_t>(w) * 4U;
  const std::size_t size_bytes
    = static_cast<std::size_t>(stride) * static_cast<std::size_t>(h);
  out_rgba8.resize(size_bytes);

  hr = converter->CopyPixels(nullptr, stride, static_cast<UINT>(size_bytes),
    reinterpret_cast<BYTE*>(out_rgba8.data()));
  if (FAILED(hr)) {
    out_error = "Failed to copy pixels";
    out_rgba8.clear();
    return false;
  }

  out_width = static_cast<std::uint32_t>(w);
  out_height = static_cast<std::uint32_t>(h);
  return true;
}

#endif

auto AlignUpSize(const std::size_t value, const std::size_t alignment)
  -> std::size_t
{
  if (alignment == 0U) {
    return value;
  }
  const auto mask = alignment - 1U;
  return (value + mask) & ~mask;
}

const Vec3 kDefaultSunRayDirWs = glm::normalize(Vec3 { 0.35F, -0.45F, -1.0F });

auto TryEstimateSunRayDirFromCubemapFace(const std::vector<std::byte>& rgba8,
  const std::uint32_t face_size, const std::uint32_t face_index,
  Vec3& out_sun_ray_dir_ws) -> bool
{
  if (face_size == 0U || face_index >= 6U) {
    return false;
  }

  constexpr std::size_t kBytesPerPixel = 4U;
  constexpr std::size_t kRowPitchAlignment = 256U;
  const std::size_t face_bytes_per_row
    = static_cast<std::size_t>(face_size) * kBytesPerPixel;
  const std::size_t row_pitch
    = AlignUpSize(face_bytes_per_row, kRowPitchAlignment);
  const std::size_t slice_pitch
    = row_pitch * static_cast<std::size_t>(face_size);
  const std::size_t required_bytes = slice_pitch * 6U;
  if (rgba8.size() < required_bytes) {
    return false;
  }

  // Find the brightest pixel on the chosen face.
  std::uint32_t best_x = face_size / 2U;
  std::uint32_t best_y = face_size / 2U;
  float best_luma = -1.0f;

  const std::size_t face_base
    = static_cast<std::size_t>(face_index) * slice_pitch;
  for (std::uint32_t y = 0U; y < face_size; ++y) {
    const std::size_t row_base
      = face_base + static_cast<std::size_t>(y) * row_pitch;
    for (std::uint32_t x = 0U; x < face_size; ++x) {
      const std::size_t off = row_base + static_cast<std::size_t>(x) * 4U;
      const auto r = std::to_integer<std::uint8_t>(rgba8[off + 0U]);
      const auto g = std::to_integer<std::uint8_t>(rgba8[off + 1U]);
      const auto b = std::to_integer<std::uint8_t>(rgba8[off + 2U]);
      const float rf = static_cast<float>(r) / 255.0f;
      const float gf = static_cast<float>(g) / 255.0f;
      const float bf = static_cast<float>(b) / 255.0f;
      const float luma = 0.2126f * rf + 0.7152f * gf + 0.0722f * bf;
      if (luma > best_luma) {
        best_luma = luma;
        best_x = x;
        best_y = y;
      }
    }
  }

  // Convert that pixel to a world-space sun direction using the standard
  // DirectX cubemap face orientation so +Z maps to world up.
  const float u
    = (static_cast<float>(best_x) + 0.5f) / static_cast<float>(face_size);
  const float v
    = (static_cast<float>(best_y) + 0.5f) / static_cast<float>(face_size);
  const float u_ndc = 2.0f * u - 1.0f;
  const float v_ndc = 2.0f * v - 1.0f;

  const Vec3 dir_to_sun = [&]() -> Vec3 {
    switch (face_index) {
    case 0U: // +X
      return glm::normalize(Vec3 { 1.0f, v_ndc, -u_ndc });
    case 1U: // -X
      return glm::normalize(Vec3 { -1.0f, v_ndc, u_ndc });
    case 2U: // +Y
      return glm::normalize(Vec3 { u_ndc, 1.0f, -v_ndc });
    case 3U: // -Y
      return glm::normalize(Vec3 { u_ndc, -1.0f, v_ndc });
    case 4U: // +Z
      return glm::normalize(Vec3 { u_ndc, -v_ndc, 1.0f });
    case 5U: // -Z
      return glm::normalize(Vec3 { -u_ndc, -v_ndc, -1.0f });
    default:
      return glm::normalize(Vec3 { 0.0f, 1.0f, 0.0f });
    }
  }();
  if (!std::isfinite(dir_to_sun.x) || !std::isfinite(dir_to_sun.y)
    || !std::isfinite(dir_to_sun.z)) {
    return false;
  }

  // DirectionalLight direction convention in this demo: use the direction
  // *toward* the sun (so the light appears to come from that direction).
  out_sun_ray_dir_ws = dir_to_sun;
  return true;
}

auto MakeRotationFromForwardToDirWs(const Vec3& dir_ws) -> Quat
{
  const Vec3 from = glm::normalize(oxygen::space::move::Forward);
  const Vec3 to = glm::normalize(dir_ws);
  const float d = glm::dot(from, to);
  if (d > 0.9999f) {
    return Quat { 1.0f, 0.0f, 0.0f, 0.0f };
  }
  if (d < -0.9999f) {
    // 180-degree flip; pick a stable axis not parallel to "from".
    Vec3 axis = glm::cross(from, oxygen::space::move::Up);
    if (glm::dot(axis, axis) < 1e-6f) {
      axis = glm::cross(from, oxygen::space::move::Right);
    }
    axis = glm::normalize(axis);
    return glm::angleAxis(glm::pi<float>(), axis);
  }

  Vec3 axis = glm::cross(from, to);
  const float axis_len2 = glm::dot(axis, axis);
  if (axis_len2 < 1e-8f) {
    return Quat { 1.0f, 0.0f, 0.0f, 0.0f };
  }
  axis = glm::normalize(axis);
  const float angle = std::acos(std::clamp(d, -1.0f, 1.0f));
  return glm::angleAxis(angle, axis);
}

auto TryBuildCubemapRgba8FromImageLayout(const std::vector<std::byte>& rgba8,
  const std::uint32_t width, const std::uint32_t height,
  std::vector<std::byte>& out_padded_faces, std::uint32_t& out_face_size,
  std::string& out_error) -> bool
{
  out_padded_faces.clear();
  out_face_size = 0U;
  out_error.clear();

  if (rgba8.empty() || width == 0U || height == 0U) {
    out_error = "Invalid image";
    return false;
  }

  constexpr std::size_t kBytesPerPixel = 4U;
  const std::size_t expected_bytes = static_cast<std::size_t>(width)
    * static_cast<std::size_t>(height) * kBytesPerPixel;
  if (rgba8.size() < expected_bytes) {
    out_error = "Decoded pixel buffer too small";
    return false;
  }

  enum class Layout : std::uint8_t {
    kStripHorizontal,
    kStripVertical,
    kCrossHorizontal,
    kCrossVertical,
  };

  std::optional<Layout> layout;
  std::uint32_t face_size = 0U;

  // Supported layouts:
  // - Strip: 6x1 or 1x6 faces
  // - Cross: 4x3 (horizontal cross) or 3x4 (vertical cross)
  if (width == height * 6U) {
    layout = Layout::kStripHorizontal;
    face_size = height;
  } else if (height == width * 6U) {
    layout = Layout::kStripVertical;
    face_size = width;
  } else if (width % 4U == 0U && height % 3U == 0U
    && (width / 4U) == (height / 3U)) {
    layout = Layout::kCrossHorizontal;
    face_size = width / 4U;
  } else if (width % 3U == 0U && height % 4U == 0U
    && (width / 3U) == (height / 4U)) {
    layout = Layout::kCrossVertical;
    face_size = width / 3U;
  } else {
    out_error = "Skybox image must be: 6x1 strip, 1x6 strip, 4x3 cross, or 3x4 "
                "cross (square faces)";
    return false;
  }

  if (face_size == 0U) {
    out_error = "Invalid skybox face size";
    return false;
  }

  constexpr std::size_t kRowPitchAlignment = 256U;
  const std::size_t face_bytes_per_row
    = static_cast<std::size_t>(face_size) * kBytesPerPixel;
  const std::size_t row_pitch
    = AlignUpSize(face_bytes_per_row, kRowPitchAlignment);
  const std::size_t slice_pitch
    = row_pitch * static_cast<std::size_t>(face_size);

  out_padded_faces.resize(slice_pitch * 6U);
  std::memset(out_padded_faces.data(), 0, out_padded_faces.size());

  const std::size_t src_stride
    = static_cast<std::size_t>(width) * kBytesPerPixel;

  auto CopyFace
    = [&](const std::uint32_t dst_face, const std::uint32_t src_face_x,
        const std::uint32_t src_face_y) -> void {
    const std::size_t dst_slice_offset
      = static_cast<std::size_t>(dst_face) * slice_pitch;
    const std::uint32_t base_x = src_face_x * face_size;
    const std::uint32_t base_y = src_face_y * face_size;

    for (std::uint32_t y = 0U; y < face_size; ++y) {
      const std::uint32_t src_x_px = base_x;
      const std::uint32_t src_y_px = base_y + y;
      const std::size_t src_offset
        = static_cast<std::size_t>(src_y_px) * src_stride
        + static_cast<std::size_t>(src_x_px) * kBytesPerPixel;
      const std::size_t dst_offset
        = dst_slice_offset + static_cast<std::size_t>(y) * row_pitch;
      std::memcpy(out_padded_faces.data() + dst_offset,
        rgba8.data() + src_offset, face_bytes_per_row);
    }
  };

  // D3D cube face order (array slice): +X, -X, +Y, -Y, +Z, -Z
  switch (*layout) {
  case Layout::kStripHorizontal:
    for (std::uint32_t face = 0U; face < 6U; ++face) {
      CopyFace(face, face, 0U);
    }
    break;
  case Layout::kStripVertical:
    for (std::uint32_t face = 0U; face < 6U; ++face) {
      CopyFace(face, 0U, face);
    }
    break;
  case Layout::kCrossHorizontal:
    // Cross layout (4x3):
    //         +Y
    //  -X  +Z  +X  -Z
    //         -Y
    CopyFace(0U, 2U, 1U); // +X
    CopyFace(1U, 0U, 1U); // -X
    CopyFace(2U, 1U, 0U); // +Y
    CopyFace(3U, 1U, 2U); // -Y
    CopyFace(4U, 1U, 1U); // +Z
    CopyFace(5U, 3U, 1U); // -Z
    break;
  case Layout::kCrossVertical:
    // Cross layout (3x4):
    //         +Y
    //  -X  +Z  +X
    //         -Y
    //         -Z
    CopyFace(0U, 2U, 1U); // +X
    CopyFace(1U, 0U, 1U); // -X
    CopyFace(2U, 1U, 0U); // +Y
    CopyFace(3U, 1U, 2U); // -Y
    CopyFace(4U, 1U, 1U); // +Z
    CopyFace(5U, 1U, 3U); // -Z
    break;
  }

  out_face_size = face_size;
  return true;
}

auto FlipRgba8Vertically(std::span<std::byte> rgba8, const std::uint32_t width,
  const std::uint32_t height) -> void
{
  if (width == 0U || height == 0U) {
    return;
  }

  constexpr std::size_t kBytesPerPixel = 4U;
  const std::size_t row_bytes
    = static_cast<std::size_t>(width) * kBytesPerPixel;
  const std::size_t expected_size
    = row_bytes * static_cast<std::size_t>(height);
  if (rgba8.size() < expected_size) {
    return;
  }

  std::vector<std::byte> tmp;
  tmp.resize(row_bytes);

  for (std::uint32_t y = 0U; y < height / 2U; ++y) {
    const std::size_t y0 = static_cast<std::size_t>(y);
    const std::size_t y1 = static_cast<std::size_t>(height - 1U - y);

    auto* row0 = rgba8.data() + (y0 * row_bytes);
    auto* row1 = rgba8.data() + (y1 * row_bytes);

    std::memcpy(tmp.data(), row0, row_bytes);
    std::memcpy(row0, row1, row_bytes);
    std::memcpy(row1, tmp.data(), row_bytes);
  }
}

auto ApplyUvOriginFix(const glm::vec2 scale, const glm::vec2 offset,
  const bool flip_u, const bool flip_v) -> std::pair<glm::vec2, glm::vec2>
{
  glm::vec2 out_scale = scale;
  glm::vec2 out_offset = offset;

  // Apply flips in "raw UV" space so UI scale/offset remains intuitive.
  // u' = (1 - u) * s + o  =>  u' = u * (-s) + (s + o)
  if (flip_u) {
    out_offset.x = out_scale.x + out_offset.x;
    out_scale.x = -out_scale.x;
  }
  if (flip_v) {
    out_offset.y = out_scale.y + out_offset.y;
    out_scale.y = -out_scale.y;
  }

  return { out_scale, out_offset };
}

auto MakeLookRotationFromPosition(const glm::vec3& position,
  const glm::vec3& target, const glm::vec3& up_direction = { 0.0F, 0.0F, 1.0F })
  -> glm::quat
{
  const auto forward_raw = target - position;
  const float forward_len2 = glm::dot(forward_raw, forward_raw);
  if (forward_len2 <= 1e-8F) {
    return glm::quat(1.0F, 0.0F, 0.0F, 0.0F);
  }

  const auto forward = glm::normalize(forward_raw);
  const auto right = glm::normalize(glm::cross(forward, up_direction));
  const auto up = glm::cross(right, forward);

  glm::mat4 look_matrix(1.0F);
  // NOLINTBEGIN(*-pro-bounds-avoid-unchecked-container-access)
  look_matrix[0] = glm::vec4(right, 0.0F);
  look_matrix[1] = glm::vec4(up, 0.0F);
  look_matrix[2] = glm::vec4(-forward, 0.0F);
  // NOLINTEND(*-pro-bounds-avoid-unchecked-container-access)

  return glm::quat_cast(look_matrix);
}

auto MakeLookRotationMinusYForwardFromPosition(const glm::vec3& position,
  const glm::vec3& target, const glm::vec3& up_direction = { 0.0F, 0.0F, 1.0F })
  -> glm::quat
{
  const auto to_target = target - position;
  const float len2 = glm::dot(to_target, to_target);
  if (len2 <= 1e-8F) {
    return glm::quat(1.0F, 0.0F, 0.0F, 0.0F);
  }

  // Oxygen world convention: RIGHT-HANDED, Z-UP, FORWARD = -Y.
  // We want the node's local forward (-Y) to point toward the target.
  const glm::vec3 forward = glm::normalize(to_target);

  glm::vec3 up = up_direction;
  const float up_len2 = glm::dot(up, up);
  if (up_len2 <= 1e-8F) {
    up = { 0.0F, 0.0F, 1.0F };
  } else {
    up = glm::normalize(up);
  }

  // Guard against degeneracy.
  if (std::abs(glm::dot(forward, up)) > 0.999F) {
    up = { 1.0F, 0.0F, 0.0F };
  }

  // Right-handed basis: right = up x forward.
  glm::vec3 right = glm::cross(up, forward);
  const float right_len2 = glm::dot(right, right);
  if (right_len2 <= 1e-8F) {
    return glm::quat(1.0F, 0.0F, 0.0F, 0.0F);
  }
  right = glm::normalize(right);

  // Re-orthogonalize up.
  up = glm::normalize(glm::cross(forward, right));

  // Local axes in world space (columns): +X=right, +Y=back, +Z=up.
  const glm::vec3 back = -forward;

  glm::mat4 m(1.0F);
  // NOLINTBEGIN(*-pro-bounds-avoid-unchecked-container-access)
  m[0] = glm::vec4(right, 0.0F);
  m[1] = glm::vec4(back, 0.0F);
  m[2] = glm::vec4(up, 0.0F);
  // NOLINTEND(*-pro-bounds-avoid-unchecked-container-access)

  return glm::quat_cast(m);
}

auto ResolveBaseColorTextureResourceIndex(
  oxygen::examples::textured_cube::MainModule::TextureIndexMode mode,
  std::uint32_t custom_resource_index) -> oxygen::data::pak::v2::ResourceIndexT
{
  using oxygen::data::pak::v2::ResourceIndexT;
  using enum oxygen::examples::textured_cube::MainModule::TextureIndexMode;

  switch (mode) {
  case kFallback:
    return oxygen::data::pak::v2::kFallbackResourceIndex;
  case kCustom:
    return static_cast<ResourceIndexT>(custom_resource_index);
  case kForcedError:
  default:
    return (std::numeric_limits<ResourceIndexT>::max)();
  }
}

auto MakeCubeMaterial(const char* name, const glm::vec4& rgba,
  oxygen::data::pak::v2::ResourceIndexT base_color_texture_resource_index,
  oxygen::content::ResourceKey base_color_texture_key,
  oxygen::data::MaterialDomain domain = oxygen::data::MaterialDomain::kOpaque)
  -> std::shared_ptr<const oxygen::data::MaterialAsset>
{
  using namespace oxygen::data;

  pak::MaterialAssetDesc desc {};
  desc.header.asset_type = 7; // MaterialAsset (for tooling/debug)

  constexpr std::size_t maxn = sizeof(desc.header.name) - 1;
  const std::size_t n = (std::min)(maxn, std::strlen(name));
  std::memcpy(desc.header.name, name, n);
  desc.header.name[n] = '\0';

  desc.header.version = 1;
  desc.header.streaming_priority = 255;
  desc.material_domain = static_cast<uint8_t>(domain);
  desc.flags = 0;
  desc.shader_stages = 0;

  desc.base_color[0] = rgba.r;
  desc.base_color[1] = rgba.g;
  desc.base_color[2] = rgba.b;
  desc.base_color[3] = rgba.a;

  desc.normal_scale = 1.0f;
  desc.metalness = Unorm16 { 0.0f };
  desc.roughness = Unorm16 { 0.75f };
  desc.ambient_occlusion = Unorm16 { 1.0f };

  desc.base_color_texture = base_color_texture_resource_index;

  const AssetKey asset_key { .guid = GenerateAssetGuid() };

  // Runtime: when a ResourceKey is provided, bind it to the material's base
  // color texture slot (opaque to the renderer).
  if (base_color_texture_key != static_cast<oxygen::content::ResourceKey>(0)) {
    std::vector<oxygen::content::ResourceKey> texture_keys;
    texture_keys.push_back(base_color_texture_key);
    return std::make_shared<const MaterialAsset>(asset_key, desc,
      std::vector<ShaderReference> {}, std::move(texture_keys));
  }

  // Default: no runtime texture keys (use fallback/placeholder behavior).
  return std::make_shared<const MaterialAsset>(
    asset_key, desc, std::vector<ShaderReference> {});
}

auto BuildCubeGeometry(
  const std::shared_ptr<const oxygen::data::MaterialAsset>& material,
  const glm::vec2 /*uv_scale*/, const glm::vec2 /*uv_offset*/)
  -> std::shared_ptr<oxygen::data::GeometryAsset>
{
  using oxygen::data::MeshBuilder;
  using oxygen::data::Vertex;
  using oxygen::data::pak::GeometryAssetDesc;
  using oxygen::data::pak::MeshViewDesc;

  auto cube_data = oxygen::data::MakeCubeMeshAsset();
  if (!cube_data) {
    return nullptr;
  }

  std::vector<Vertex> vertices = cube_data->first;

  auto mesh
    = MeshBuilder(0, "CubeLOD0")
        .WithVertices(vertices)
        .WithIndices(cube_data->second)
        .BeginSubMesh("full", material)
        .WithMeshView(MeshViewDesc {
          .first_index = 0,
          .index_count = static_cast<uint32_t>(cube_data->second.size()),
          .first_vertex = 0,
          .vertex_count = static_cast<uint32_t>(vertices.size()),
        })
        .EndSubMesh()
        .Build();

  GeometryAssetDesc geo_desc {};
  geo_desc.lod_count = 1;
  const auto bb_min = mesh->BoundingBoxMin();
  const auto bb_max = mesh->BoundingBoxMax();
  geo_desc.bounding_box_min[0] = bb_min.x;
  geo_desc.bounding_box_min[1] = bb_min.y;
  geo_desc.bounding_box_min[2] = bb_min.z;
  geo_desc.bounding_box_max[0] = bb_max.x;
  geo_desc.bounding_box_max[1] = bb_max.y;
  geo_desc.bounding_box_max[2] = bb_max.z;

  return std::make_shared<oxygen::data::GeometryAsset>(
    oxygen::data::AssetKey { .guid = oxygen::data::GenerateAssetGuid() },
    geo_desc,
    std::vector<std::shared_ptr<oxygen::data::Mesh>> { std::move(mesh) });
}

} // namespace

namespace oxygen::examples::textured_cube {

auto MainModule::GetEffectiveUvTransform() const
  -> std::pair<glm::vec2, glm::vec2>
{
  bool fix_u = extra_flip_u_;
  bool fix_v = extra_flip_v_;

  // If the mesh UV origin and the texture image origin differ, apply a V flip
  // either by normalizing the texture at upload time (preferred) or by
  // normalizing UVs via the material UV transform.
  if (orientation_fix_mode_ == OrientationFixMode::kNormalizeUvInTransform) {
    if (uv_origin_ != UvOrigin::kTopLeft
      && image_origin_ == ImageOrigin::kTopLeft) {
      fix_v = !fix_v;
    }
    if (uv_origin_ == UvOrigin::kTopLeft
      && image_origin_ != ImageOrigin::kTopLeft) {
      fix_v = !fix_v;
    }
  }

  return ApplyUvOriginFix(uv_scale_, uv_offset_, fix_u, fix_v);
}

MainModule::MainModule(const common::AsyncEngineApp& app)
  : Base(app)
{
  DCHECK_NOTNULL_F(app_.platform);
  DCHECK_F(!app_.gfx_weak.expired());
}

auto MainModule::BuildDefaultWindowProperties() const
  -> platform::window::Properties
{
  platform::window::Properties p("Oxygen Example");
  p.extent = { .width = 2560U, .height = 960U };
  p.flags = { .hidden = false,
    .always_on_top = false,
    .full_screen = app_.fullscreen,
    .maximized = false,
    .minimized = false,
    .resizable = true,
    .borderless = false };
  return p;
}

auto MainModule::OnAttached(observer_ptr<AsyncEngine> engine) noexcept -> bool
{
  if (!engine) {
    return false;
  }

  if (!Base::OnAttached(engine)) {
    return false;
  }

  if (!InitInputBindings()) {
    return false;
  }

  return true;
}

auto MainModule::OnShutdown() noexcept -> void { }

auto MainModule::OnFrameStart(engine::FrameContext& context) -> void
{
  Base::OnFrameStart(context);
}

auto MainModule::OnExampleFrameStart(engine::FrameContext& context) -> void
{
  if (!scene_) {
    scene_ = std::make_shared<scene::Scene>("TexturedCube-Scene");
  }
  context.SetScene(observer_ptr { scene_.get() });
}

auto MainModule::OnSceneMutation(engine::FrameContext& context) -> co::Co<>
{
  DCHECK_NOTNULL_F(app_window_);
  DCHECK_NOTNULL_F(scene_);

  UpdateFrameContext(context, [this](int w, int h) {
    EnsureMainCamera(w, h);
    RegisterViewForRendering(main_camera_);
  });

  if (!app_window_->GetWindow()) {
    co_return;
  }

  if (!cube_node_.IsAlive()) {
    cube_node_ = scene_->CreateNode("Cube");
    cube_node_.GetTransform().SetLocalPosition({ 0.0f, 0.0f, 0.0f });
    cube_needs_rebuild_ = true;
  }

  if (auto env = scene_->GetEnvironment(); !env) {
    auto new_env = std::make_unique<scene::SceneEnvironment>();
    auto& sky = new_env->AddSystem<scene::environment::SkySphere>();
    sky.SetSource(scene::environment::SkySphereSource::kSolidColor);
    sky.SetSolidColorRgb(Vec3 { 0.06F, 0.08F, 0.12F });
    sky.SetIntensity(1.0F);

    auto& sky_light = new_env->AddSystem<scene::environment::SkyLight>();
    sky_light.SetIntensity(sky_light_intensity_);
    sky_light.SetDiffuseIntensity(sky_light_diffuse_intensity_);
    sky_light.SetSpecularIntensity(sky_light_specular_intensity_);
    sky_light.SetTintRgb(Vec3 { 1.0F, 1.0F, 1.0F });
    sky_light.SetSource(scene::environment::SkyLightSource::kCapturedScene);

    scene_->SetEnvironment(std::move(new_env));
  } else if (env) {
    if (!env->TryGetSystem<scene::environment::SkySphere>()) {
      auto& sky = env->AddSystem<scene::environment::SkySphere>();
      sky.SetSource(scene::environment::SkySphereSource::kSolidColor);
      sky.SetSolidColorRgb(Vec3 { 0.06F, 0.08F, 0.12F });
      sky.SetIntensity(1.0F);
    }
    if (!env->TryGetSystem<scene::environment::SkyLight>()) {
      auto& sky_light = env->AddSystem<scene::environment::SkyLight>();
      sky_light.SetIntensity(sky_light_intensity_);
      sky_light.SetDiffuseIntensity(sky_light_diffuse_intensity_);
      sky_light.SetSpecularIntensity(sky_light_specular_intensity_);
      sky_light.SetTintRgb(Vec3 { 1.0F, 1.0F, 1.0F });
      sky_light.SetSource(scene::environment::SkyLightSource::kCapturedScene);
    }
  }

  if (!sun_node_.IsAlive()) {
    sun_node_ = scene_->CreateNode("Sun");
    sun_node_.GetTransform().SetLocalPosition({ 0.0f, -20.0f, 20.0f });

    auto sun_light = std::make_unique<scene::DirectionalLight>();
    sun_light->Common().intensity = sun_intensity_;
    sun_light->Common().color_rgb = sun_color_rgb_;
    sun_light->SetIsSunLight(true);
    sun_light->SetEnvironmentContribution(true);

    const bool attached = sun_node_.AttachLight(std::move(sun_light));
    CHECK_F(attached, "Failed to attach DirectionalLight to Sun");
  }

  if (sun_node_.IsAlive()) {
    auto tf = sun_node_.GetTransform();
    const Vec3 dir = sun_ray_dir_from_skybox_ ? glm::normalize(sun_ray_dir_ws_)
                                              : kDefaultSunRayDirWs;
    const Quat rot = MakeRotationFromForwardToDirWs(dir);
    tf.SetLocalRotation(rot);

    // Position the node along the sun's apparent direction (purely for debug).
    tf.SetLocalPosition(camera_target_ + dir * 50.0f);

    // Refresh sun light with UI-driven values each frame so tweaks stick.
    if (auto sun_light = sun_node_.GetLightAs<scene::DirectionalLight>()) {
      auto& light = sun_light->get();
      light.Common().intensity = sun_intensity_;
      light.Common().color_rgb = sun_color_rgb_;
      light.SetEnvironmentContribution(true);
      light.SetIsSunLight(true);
    }
  }

  if (!fill_light_node_.IsAlive()) {
    fill_light_node_ = scene_->CreateNode("Fill");
    fill_light_node_.GetTransform().SetLocalPosition({ -6.0f, 5.0f, 3.0f });

    auto fill_light = std::make_unique<scene::PointLight>();
    fill_light->Common().intensity = 80.0F;
    fill_light->Common().color_rgb = { 0.85F, 0.90F, 1.0F };
    fill_light->SetRange(45.0F);

    const bool attached = fill_light_node_.AttachLight(std::move(fill_light));
    CHECK_F(attached, "Failed to attach PointLight to Fill");
  }

  if (png_load_requested_) {
    png_load_requested_ = false;

    const std::filesystem::path png_path { std::string { png_path_.data() } };
    if (png_path.empty()) {
      png_status_message_ = "No PNG path provided";
    } else {
      auto asset_loader = app_.engine ? app_.engine->GetAssetLoader() : nullptr;
      if (!asset_loader) {
        png_status_message_ = "AssetLoader unavailable";
      } else {
#if !defined(OXYGEN_WINDOWS)
        png_status_message_ = "PNG loading only supported on Windows";
#else
        std::string decode_error;
        if (!DecodeImageRgba8Wic(
              png_path, png_rgba8_, png_width_, png_height_, decode_error)) {
          png_status_message_
            = decode_error.empty() ? "PNG decode failed" : decode_error;
        } else {
          png_reupload_requested_ = true;
        }
#endif
      }
    }
  }

  if (skybox_load_requested_) {
    skybox_load_requested_ = false;

    const std::filesystem::path img_path { std::string {
      skybox_path_.data() } };
    if (img_path.empty()) {
      skybox_status_message_ = "No skybox path provided";
    } else {
      auto asset_loader = app_.engine ? app_.engine->GetAssetLoader() : nullptr;
      if (!asset_loader) {
        skybox_status_message_ = "AssetLoader unavailable";
      } else {
#if !defined(OXYGEN_WINDOWS)
        skybox_status_message_ = "Skybox loading only supported on Windows";
#else
        std::string decode_error;
        if (!DecodeImageRgba8Wic(img_path, skybox_rgba8_, skybox_width_,
              skybox_height_, decode_error)) {
          skybox_status_message_
            = decode_error.empty() ? "Skybox decode failed" : decode_error;
        } else {
          skybox_reupload_requested_ = true;
        }
#endif
      }
    }
  }

  if (skybox_reupload_requested_) {
    skybox_reupload_requested_ = false;

#if !defined(OXYGEN_WINDOWS)
    skybox_status_message_ = "Skybox upload only supported on Windows";
#else
    if (skybox_rgba8_.empty() || skybox_width_ == 0U || skybox_height_ == 0U) {
      skybox_status_message_ = "No decoded skybox pixels";
    } else {
      auto asset_loader = app_.engine ? app_.engine->GetAssetLoader() : nullptr;
      if (!asset_loader) {
        skybox_status_message_ = "AssetLoader unavailable";
      } else {
        std::vector<std::byte> cubemap_padded;
        std::uint32_t face_size = 0U;
        std::string pack_error;
        if (!TryBuildCubemapRgba8FromImageLayout(skybox_rgba8_, skybox_width_,
              skybox_height_, cubemap_padded, face_size, pack_error)) {
          skybox_status_message_
            = pack_error.empty() ? "Skybox pack failed" : pack_error;
        } else {
          // Estimate sun direction from the +Z face (face index 4) to match
          // Oxygen's Z-up convention.
          Vec3 sun_ray_dir_ws { 0.0f, 0.0f, 1.0f };
          sun_ray_dir_from_skybox_ = TryEstimateSunRayDirFromCubemapFace(
            cubemap_padded, face_size, 4U, sun_ray_dir_ws);
          if (sun_ray_dir_from_skybox_) {
            sun_ray_dir_ws_ = sun_ray_dir_ws;
          }

          using oxygen::data::pak::v2::TextureResourceDesc;

          // Use a fresh key each upload so the renderer doesn't keep an older
          // bindless entry for the same key.
          skybox_texture_key_ = asset_loader->MintSyntheticTextureKey();

          TextureResourceDesc desc {};
          desc.data_offset = static_cast<oxygen::data::pak::v2::OffsetT>(
            sizeof(TextureResourceDesc));
          desc.size_bytes = static_cast<oxygen::data::pak::v2::DataBlobSizeT>(
            cubemap_padded.size());
          desc.texture_type
            = static_cast<std::uint8_t>(oxygen::TextureType::kTextureCube);
          desc.compression_type = 0;
          desc.width = face_size;
          desc.height = face_size;
          desc.depth = 1;
          desc.array_layers = 6;
          desc.mip_levels = 1;
          desc.format = static_cast<std::uint8_t>(oxygen::Format::kRGBA8UNorm);
          desc.alignment = 256;

          std::vector<std::uint8_t> packed;
          packed.resize(sizeof(TextureResourceDesc) + cubemap_padded.size());
          std::memcpy(packed.data(), &desc, sizeof(TextureResourceDesc));
          std::memcpy(packed.data() + sizeof(TextureResourceDesc),
            cubemap_padded.data(), cubemap_padded.size());

          auto tex = co_await asset_loader->LoadResourceAsync<
            oxygen::data::TextureResource>(
            oxygen::content::CookedResourceData<oxygen::data::TextureResource> {
              .key = skybox_texture_key_,
              .bytes
              = std::span<const std::uint8_t>(packed.data(), packed.size()),
            });
          if (!tex) {
            skybox_status_message_ = "Skybox texture decode failed";
          } else {
            skybox_last_face_size_ = static_cast<int>(face_size);
            skybox_status_message_ = "Loaded";

            // Apply as scene sky and enable sky lighting from the same cubemap.
            auto env = scene_->GetEnvironment();
            if (!env) {
              auto new_env = std::make_unique<scene::SceneEnvironment>();
              auto& sky = new_env->AddSystem<scene::environment::SkySphere>();
              sky.SetSource(scene::environment::SkySphereSource::kCubemap);
              sky.SetCubemapResource(skybox_texture_key_);

              auto& sky_light
                = new_env->AddSystem<scene::environment::SkyLight>();
              sky_light.SetSource(
                scene::environment::SkyLightSource::kSpecifiedCubemap);
              sky_light.SetCubemapResource(skybox_texture_key_);
              sky_light.SetIntensity(sky_light_intensity_);
              sky_light.SetDiffuseIntensity(sky_light_diffuse_intensity_);
              sky_light.SetSpecularIntensity(sky_light_specular_intensity_);
              sky_light.SetTintRgb(Vec3 { 1.0F, 1.0F, 1.0F });

              scene_->SetEnvironment(std::move(new_env));
            } else {
              auto sky = env->TryGetSystem<scene::environment::SkySphere>();
              if (!sky) {
                auto& sky_ref = env->AddSystem<scene::environment::SkySphere>();
                sky = observer_ptr<scene::environment::SkySphere>(&sky_ref);
              }
              sky->SetSource(scene::environment::SkySphereSource::kCubemap);
              sky->SetCubemapResource(skybox_texture_key_);

              auto sky_light
                = env->TryGetSystem<scene::environment::SkyLight>();
              if (!sky_light) {
                auto& sky_light_ref
                  = env->AddSystem<scene::environment::SkyLight>();
                sky_light
                  = observer_ptr<scene::environment::SkyLight>(&sky_light_ref);
              }
              sky_light->SetSource(
                scene::environment::SkyLightSource::kSpecifiedCubemap);
              sky_light->SetCubemapResource(skybox_texture_key_);
              sky_light->SetIntensity(sky_light_intensity_);
              sky_light->SetDiffuseIntensity(sky_light_diffuse_intensity_);
              sky_light->SetSpecularIntensity(sky_light_specular_intensity_);
              sky_light->SetTintRgb(Vec3 { 1.0F, 1.0F, 1.0F });
            }
          }
        }
      }
    }
#endif
  }

  if (png_reupload_requested_) {
    png_reupload_requested_ = false;

#if !defined(OXYGEN_WINDOWS)
    png_status_message_ = "PNG upload only supported on Windows";
#else
    if (png_rgba8_.empty() || png_width_ == 0U || png_height_ == 0U) {
      png_status_message_ = "No decoded PNG pixels";
    } else {
      auto asset_loader = app_.engine ? app_.engine->GetAssetLoader() : nullptr;
      if (!asset_loader) {
        png_status_message_ = "AssetLoader unavailable";
      } else {
        // Use a fresh key each upload so the renderer doesn't keep an older
        // bindless entry for the same key.
        custom_texture_key_ = asset_loader->MintSyntheticTextureKey();

        // Keep a non-zero resource index for the material-side demo path.
        if (custom_texture_resource_index_ == 0U) {
          custom_texture_resource_index_ = 1U;
        } else {
          ++custom_texture_resource_index_;
        }

        std::vector<std::byte> rgba8 = png_rgba8_;

        bool flip_on_upload = false;
        if (orientation_fix_mode_
          == OrientationFixMode::kNormalizeTextureOnUpload) {
          if (uv_origin_ != UvOrigin::kTopLeft
            && image_origin_ == ImageOrigin::kTopLeft) {
            flip_on_upload = true;
          }
          if (uv_origin_ == UvOrigin::kTopLeft
            && image_origin_ != ImageOrigin::kTopLeft) {
            flip_on_upload = true;
          }
        }
        if (flip_on_upload) {
          FlipRgba8Vertically(rgba8, png_width_, png_height_);
        }

        using oxygen::data::pak::v2::TextureResourceDesc;

        const auto AlignUp = [](const std::size_t value,
                               const std::size_t alignment) -> std::size_t {
          if (alignment == 0U) {
            return value;
          }
          const auto mask = alignment - 1U;
          return (value + mask) & ~mask;
        };

        // TextureBinder expects cooked texture data to be row-pitch aligned
        // to 256 bytes when the resource advertises alignment=256.
        constexpr std::size_t kRowPitchAlignment = 256U;
        constexpr std::size_t kBytesPerPixel = 4U; // RGBA8
        const std::size_t bytes_per_row
          = static_cast<std::size_t>(png_width_) * kBytesPerPixel;
        const std::size_t row_pitch
          = AlignUp(bytes_per_row, kRowPitchAlignment);
        const std::size_t padded_size
          = row_pitch * static_cast<std::size_t>(png_height_);

        std::vector<std::byte> rgba8_padded;
        rgba8_padded.resize(padded_size);
        for (std::uint32_t y = 0; y < png_height_; ++y) {
          const auto dst_offset = static_cast<std::size_t>(y) * row_pitch;
          const auto src_offset = static_cast<std::size_t>(y) * bytes_per_row;
          std::memcpy(rgba8_padded.data() + dst_offset,
            rgba8.data() + src_offset, bytes_per_row);
        }

        TextureResourceDesc desc {};
        desc.data_offset = static_cast<oxygen::data::pak::v2::OffsetT>(
          sizeof(TextureResourceDesc));
        desc.size_bytes = static_cast<oxygen::data::pak::v2::DataBlobSizeT>(
          rgba8_padded.size());
        desc.texture_type
          = static_cast<std::uint8_t>(oxygen::TextureType::kTexture2D);
        desc.compression_type = 0;
        desc.width = png_width_;
        desc.height = png_height_;
        desc.depth = 1;
        desc.array_layers = 1;
        desc.mip_levels = 1;
        desc.format = static_cast<std::uint8_t>(oxygen::Format::kRGBA8UNorm);
        desc.alignment = 256;

        std::vector<std::uint8_t> packed;
        packed.resize(sizeof(TextureResourceDesc) + rgba8_padded.size());
        std::memcpy(packed.data(), &desc, sizeof(TextureResourceDesc));
        std::memcpy(packed.data() + sizeof(TextureResourceDesc),
          rgba8_padded.data(), rgba8_padded.size());

        auto tex = co_await asset_loader->LoadResourceAsync<
          oxygen::data::TextureResource>(
          oxygen::content::CookedResourceData<oxygen::data::TextureResource> {
            .key = custom_texture_key_,
            .bytes
            = std::span<const std::uint8_t>(packed.data(), packed.size()),
          });
        if (!tex) {
          png_status_message_ = "Texture buffer decode failed";
        } else {
          png_last_width_ = static_cast<int>(tex->GetWidth());
          png_last_height_ = static_cast<int>(tex->GetHeight());
          png_status_message_ = "Loaded";
          texture_index_mode_ = TextureIndexMode::kCustom;
          cube_needs_rebuild_ = true;
        }
      }
    }
#endif
  }

  if (cube_needs_rebuild_) {
    const auto res_index = ResolveBaseColorTextureResourceIndex(
      texture_index_mode_, custom_texture_resource_index_);

    // Ensure we have a valid (type-encoded) key for forced-error mode without
    // relying on magic invalid key values.
    auto asset_loader = app_.engine ? app_.engine->GetAssetLoader() : nullptr;
    if (texture_index_mode_ == TextureIndexMode::kForcedError
      && forced_error_key_ == static_cast<oxygen::content::ResourceKey>(0)
      && asset_loader) {
      forced_error_key_ = asset_loader->MintSyntheticTextureKey();
    }

    const auto base_color_key = [&]() -> oxygen::content::ResourceKey {
      using enum TextureIndexMode;
      switch (texture_index_mode_) {
      case kCustom:
        return custom_texture_key_;
      case kForcedError:
        return forced_error_key_;
      case kFallback:
      default:
        return static_cast<oxygen::content::ResourceKey>(0);
      }
    }();

    cube_material_ = MakeCubeMaterial(
      "CubeMat", { 1.0f, 1.0f, 1.0f, 1.0f }, res_index, base_color_key);

    const auto [uv_scale, uv_offset] = GetEffectiveUvTransform();
    auto cube_geo = BuildCubeGeometry(cube_material_, uv_scale, uv_offset);
    if (cube_geo) {
      if (cube_geometry_) {
        retired_cube_geometries_.push_back(cube_geometry_);
      }

      cube_geometry_ = std::move(cube_geo);
      cube_node_.GetRenderable().SetGeometry(cube_geometry_);
      cube_needs_rebuild_ = false;

      if (auto* renderer = ResolveRenderer(); renderer && cube_material_) {
        (void)renderer->OverrideMaterialUvTransform(
          *cube_material_, uv_scale, uv_offset);
      }

      constexpr std::size_t kMaxRetired = 16;
      if (retired_cube_geometries_.size() > kMaxRetired) {
        retired_cube_geometries_.erase(retired_cube_geometries_.begin(),
          retired_cube_geometries_.end() - kMaxRetired);
      }
    }
  }

  // Keep the UV transform override sticky. Some renderer pipelines rebuild
  // material constants each frame; re-applying here ensures the authored
  // values remain active even after UI interaction ends.
  // TODO: Replace this with MaterialInstance authoring (or per-draw instance
  // constants) so the UV transform is an instance parameter rather than a
  // shared material mutation.
  if (cube_material_) {
    if (auto* renderer = ResolveRenderer(); renderer) {
      const auto [uv_scale, uv_offset] = GetEffectiveUvTransform();
      (void)renderer->OverrideMaterialUvTransform(
        *cube_material_, uv_scale, uv_offset);
    }
  }

  ApplyOrbitAndZoom();

  co_return;
}

auto MainModule::OnGameplay(engine::FrameContext& /*context*/) -> co::Co<>
{
  // Keep camera updates in scene mutation for immediate transform propagation.
  co_return;
}

auto MainModule::OnGuiUpdate(engine::FrameContext& context) -> co::Co<>
{
  DCHECK_NOTNULL_F(app_window_);

  if (!app_window_->GetWindow()) {
    co_return;
  }

  if (auto imgui_module_ref = app_.engine->GetModule<imgui::ImGuiModule>()) {
    auto& imgui_module = imgui_module_ref->get();
    if (auto* imgui_context = imgui_module.GetImGuiContext()) {
      ImGui::SetCurrentContext(imgui_context);
    }
  }

  DrawDebugOverlay(context);

  co_return;
}

auto MainModule::OnPreRender(engine::FrameContext& context) -> co::Co<>
{
  DCHECK_NOTNULL_F(app_window_);

  if (auto imgui_module_ref = app_.engine->GetModule<imgui::ImGuiModule>()) {
    auto& imgui_module = imgui_module_ref->get();
    if (auto* imgui_context = imgui_module.GetImGuiContext()) {
      ImGui::SetCurrentContext(imgui_context);
    }
  }

  if (auto rg = GetRenderGraph(); rg) {
    rg->SetupRenderPasses();

    if (auto shader_pass_config = rg->GetShaderPassConfig();
      shader_pass_config) {
      shader_pass_config->clear_color
        = graphics::Color { 0.08F, 0.08F, 0.10F, 1.0F };
      shader_pass_config->debug_name = "ShaderPass";
    }
  }

  co_return;
}

auto MainModule::OnCompositing(engine::FrameContext& context) -> co::Co<>
{
  MarkSurfacePresentable(context);
  co_return;
}

auto MainModule::OnFrameEnd(engine::FrameContext& /*context*/) -> void { }

auto MainModule::InitInputBindings() noexcept -> bool
{
  using oxygen::input::Action;
  using oxygen::input::ActionTriggerChain;
  using oxygen::input::ActionTriggerDown;
  using oxygen::input::ActionTriggerTap;
  using oxygen::input::ActionValueType;
  using oxygen::input::InputActionMapping;
  using oxygen::input::InputMappingContext;
  using oxygen::platform::InputSlots;

  if (!app_.input_system) {
    LOG_F(ERROR, "InputSystem not available; skipping input bindings");
    return false;
  }

  zoom_in_action_ = std::make_shared<Action>("zoom in", ActionValueType::kBool);
  zoom_out_action_
    = std::make_shared<Action>("zoom out", ActionValueType::kBool);
  rmb_action_ = std::make_shared<Action>("rmb", ActionValueType::kBool);
  orbit_action_
    = std::make_shared<Action>("camera orbit", ActionValueType::kAxis2D);

  app_.input_system->AddAction(zoom_in_action_);
  app_.input_system->AddAction(zoom_out_action_);
  app_.input_system->AddAction(rmb_action_);
  app_.input_system->AddAction(orbit_action_);

  camera_controls_ctx_ = std::make_shared<InputMappingContext>("camera");
  {
    // Zoom in: Mouse wheel up
    {
      const auto trigger = std::make_shared<ActionTriggerTap>();
      trigger->SetTapTimeThreshold(0.25F);
      trigger->MakeExplicit();
      const auto mapping = std::make_shared<InputActionMapping>(
        zoom_in_action_, InputSlots::MouseWheelUp);
      mapping->AddTrigger(trigger);
      camera_controls_ctx_->AddMapping(mapping);
    }

    // Zoom out: Mouse wheel down
    {
      const auto trigger = std::make_shared<ActionTriggerTap>();
      trigger->SetTapTimeThreshold(0.25F);
      trigger->MakeExplicit();
      const auto mapping = std::make_shared<InputActionMapping>(
        zoom_out_action_, InputSlots::MouseWheelDown);
      mapping->AddTrigger(trigger);
      camera_controls_ctx_->AddMapping(mapping);
    }

    // RMB helper mapping
    {
      const auto trig_down = std::make_shared<ActionTriggerDown>();
      trig_down->MakeExplicit();
      trig_down->SetActuationThreshold(0.1F);
      const auto mapping = std::make_shared<InputActionMapping>(
        rmb_action_, InputSlots::RightMouseButton);
      mapping->AddTrigger(trig_down);
      camera_controls_ctx_->AddMapping(mapping);
    }

    // Orbit mapping: MouseXY with an implicit chain requiring RMB.
    {
      const auto trig_move = std::make_shared<ActionTriggerDown>();
      trig_move->MakeExplicit();
      trig_move->SetActuationThreshold(0.0F);

      const auto rmb_chain = std::make_shared<ActionTriggerChain>();
      rmb_chain->SetLinkedAction(rmb_action_);
      rmb_chain->MakeImplicit();
      rmb_chain->RequirePrerequisiteHeld(true);

      const auto mapping = std::make_shared<InputActionMapping>(
        orbit_action_, InputSlots::MouseXY);
      mapping->AddTrigger(trig_move);
      mapping->AddTrigger(rmb_chain);
      camera_controls_ctx_->AddMapping(mapping);
    }

    app_.input_system->AddMappingContext(camera_controls_ctx_, 10);
    app_.input_system->ActivateMappingContext(camera_controls_ctx_);
  }

  return true;
}

auto MainModule::EnsureMainCamera(const int width, const int height) -> void
{
  using scene::PerspectiveCamera;

  if (!scene_) {
    return;
  }

  if (!main_camera_.IsAlive()) {
    main_camera_ = scene_->CreateNode("MainCamera");
  }

  if (!main_camera_.HasCamera()) {
    auto camera = std::make_unique<PerspectiveCamera>();
    const bool attached = main_camera_.AttachCamera(std::move(camera));
    CHECK_F(attached, "Failed to attach PerspectiveCamera to MainCamera");
  }

  const auto cam_ref = main_camera_.GetCameraAs<PerspectiveCamera>();
  if (cam_ref) {
    const float aspect = height > 0
      ? (static_cast<float>(width) / static_cast<float>(height))
      : 1.0F;
    auto& cam = cam_ref->get();
    cam.SetFieldOfView(glm::radians(60.0F));
    cam.SetAspectRatio(aspect);
    cam.SetNearPlane(0.05F);
    cam.SetFarPlane(500.0F);
    cam.SetViewport(oxygen::ViewPort { .top_left_x = 0.0F,
      .top_left_y = 0.0F,
      .width = static_cast<float>(width),
      .height = static_cast<float>(height),
      .min_depth = 0.0F,
      .max_depth = 1.0F });
  }

  ApplyOrbitAndZoom();
}

auto MainModule::ApplyOrbitAndZoom() -> void
{
  if (!main_camera_.IsAlive()) {
    return;
  }

  // Zoom via mouse wheel actions
  if (zoom_in_action_ && zoom_in_action_->WasTriggeredThisFrame()) {
    orbit_distance_
      = (std::max)(orbit_distance_ - zoom_step_, min_cam_distance_);
  }
  if (zoom_out_action_ && zoom_out_action_->WasTriggeredThisFrame()) {
    orbit_distance_
      = (std::min)(orbit_distance_ + zoom_step_, max_cam_distance_);
  }

  // Orbit via MouseXY deltas for this frame
  if (orbit_action_
    && orbit_action_->GetValueType()
      == oxygen::input::ActionValueType::kAxis2D) {
    glm::vec2 orbit_delta(0.0f);
    for (const auto& tr : orbit_action_->GetFrameTransitions()) {
      const auto& v = tr.value_at_transition.GetAs<oxygen::Axis2D>();
      orbit_delta.x += v.x;
      orbit_delta.y += v.y;
    }

    if (std::abs(orbit_delta.x) > 0.0f || std::abs(orbit_delta.y) > 0.0f) {
      orbit_yaw_rad_ += orbit_delta.x * orbit_sensitivity_;
      orbit_pitch_rad_ += orbit_delta.y * orbit_sensitivity_ * -1.0f;

      const float kMinPitch = -glm::half_pi<float>() + 0.05f;
      const float kMaxPitch = glm::half_pi<float>() - 0.05f;
      orbit_pitch_rad_ = std::clamp(orbit_pitch_rad_, kMinPitch, kMaxPitch);
    }
  }

  const float cp = std::cos(orbit_pitch_rad_);
  const float sp = std::sin(orbit_pitch_rad_);
  const float cy = std::cos(orbit_yaw_rad_);
  const float sy = std::sin(orbit_yaw_rad_);

  const glm::vec3 offset = orbit_distance_ * glm::vec3(cp * cy, cp * sy, sp);
  const glm::vec3 cam_pos = camera_target_ + offset;

  auto tf = main_camera_.GetTransform();
  tf.SetLocalPosition(cam_pos);
  tf.SetLocalRotation(MakeLookRotationFromPosition(cam_pos, camera_target_));
}

auto MainModule::DrawDebugOverlay(engine::FrameContext& /*context*/) -> void
{
  ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(420, 200), ImGuiCond_FirstUseEver);

  if (!ImGui::Begin(
        "Textured Cube Debug", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::End();
    return;
  }

  ImGui::TextUnformatted("Controls:");
  ImGui::BulletText("Mouse wheel: zoom");
  ImGui::BulletText("RMB + mouse drag: orbit");

  if (ImGui::BeginTabBar("DemoTabs")) {
    // Materials / UV tab
    if (ImGui::BeginTabItem("Materials/UV")) {
      ImGui::Separator();
      ImGui::TextUnformatted("Texture:");

      bool mat_changed = false;
      bool rebuild_requested = false;
      bool uv_transform_changed = false;

      {
        int mode = static_cast<int>(texture_index_mode_);
        const bool mode_changed = ImGui::RadioButton("Forced error", &mode,
          static_cast<int>(TextureIndexMode::kForcedError));
        ImGui::SameLine();
        const bool mode_changed_2 = ImGui::RadioButton(
          "Fallback (0)", &mode, static_cast<int>(TextureIndexMode::kFallback));
        ImGui::SameLine();
        const bool mode_changed_3 = ImGui::RadioButton(
          "Custom", &mode, static_cast<int>(TextureIndexMode::kCustom));

        mat_changed |= (mode_changed || mode_changed_2 || mode_changed_3);
        rebuild_requested |= (mode_changed || mode_changed_2 || mode_changed_3);
        texture_index_mode_ = static_cast<TextureIndexMode>(mode);
      }

      if (texture_index_mode_ == TextureIndexMode::kCustom) {
        int custom_idx = static_cast<int>(custom_texture_resource_index_);
        if (ImGui::InputInt("Resource index", &custom_idx)) {
          custom_idx = (std::max)(0, custom_idx);
          custom_texture_resource_index_
            = static_cast<std::uint32_t>(custom_idx);
          mat_changed = true;
          rebuild_requested = true;
        }

        ImGui::InputText("PNG path", png_path_.data(), png_path_.size());
        if (ImGui::Button("Browse...")) {
#if defined(OXYGEN_WINDOWS)
          std::string chosen;
          if (TryBrowseForPngFile(chosen)) {
            std::snprintf(
              png_path_.data(), png_path_.size(), "%s", chosen.c_str());
          }
#endif
        }
        ImGui::SameLine();
        if (ImGui::Button("Load PNG")) {
          png_load_requested_ = true;
          png_status_message_.clear();
        }

        if (!png_status_message_.empty()) {
          ImGui::Text("PNG: %s", png_status_message_.c_str());
        }
        if (png_last_width_ > 0 && png_last_height_ > 0) {
          ImGui::Text("Last PNG: %dx%d", png_last_width_, png_last_height_);
        }
      }

      ImGui::Separator();
      ImGui::TextUnformatted("UV:");

      constexpr float kUvScaleMin = 0.01f;
      constexpr float kUvScaleMax = 64.0f;
      constexpr float kUvOffsetMin = -64.0f;
      constexpr float kUvOffsetMax = 64.0f;

      auto SanitizeFinite = [](float v, float fallback) -> float {
        return std::isfinite(v) ? v : fallback;
      };

      float uv_scale[2] = { uv_scale_.x, uv_scale_.y };
      if (ImGui::DragFloat2("UV scale", uv_scale, 0.01f, kUvScaleMin,
            kUvScaleMax, "%.3f", ImGuiSliderFlags_AlwaysClamp)) {
        const glm::vec2 new_scale {
          std::clamp(
            SanitizeFinite(uv_scale[0], 1.0f), kUvScaleMin, kUvScaleMax),
          std::clamp(
            SanitizeFinite(uv_scale[1], 1.0f), kUvScaleMin, kUvScaleMax),
        };
        if (new_scale != uv_scale_) {
          uv_scale_ = new_scale;
          mat_changed = true;
          uv_transform_changed = true;
        }
      }

      float uv_offset[2] = { uv_offset_.x, uv_offset_.y };
      if (ImGui::DragFloat2("UV offset", uv_offset, 0.01f, kUvOffsetMin,
            kUvOffsetMax, "%.3f", ImGuiSliderFlags_AlwaysClamp)) {
        const glm::vec2 new_offset {
          std::clamp(
            SanitizeFinite(uv_offset[0], 0.0f), kUvOffsetMin, kUvOffsetMax),
          std::clamp(
            SanitizeFinite(uv_offset[1], 0.0f), kUvOffsetMin, kUvOffsetMax),
        };
        if (new_offset != uv_offset_) {
          uv_offset_ = new_offset;
          mat_changed = true;
          uv_transform_changed = true;
        }
      }

      if (ImGui::Button("Reset UV")) {
        uv_scale_ = { 1.0f, 1.0f };
        uv_offset_ = { 0.0f, 0.0f };
        mat_changed = true;
        uv_transform_changed = true;
      }

      ImGui::Separator();
      ImGui::TextUnformatted("Orientation:");

      if (ImGui::Button("Apply recommended settings")) {
        orientation_fix_mode_ = OrientationFixMode::kNormalizeTextureOnUpload;
        uv_origin_ = UvOrigin::kBottomLeft;
        image_origin_ = ImageOrigin::kTopLeft;
        extra_flip_u_ = false;
        extra_flip_v_ = false;
        uv_transform_changed = true;
        if (!png_rgba8_.empty()) {
          png_reupload_requested_ = true;
        }
      }

      if (ImGui::CollapsingHeader("Advanced", ImGuiTreeNodeFlags_None)) {
        ImGui::TextUnformatted(
          "These controls exist to understand and debug origin mismatches.");

        {
          int mode = static_cast<int>(orientation_fix_mode_);
          const bool m0
            = ImGui::RadioButton("Fix: normalize texture on upload", &mode,
              static_cast<int>(OrientationFixMode::kNormalizeTextureOnUpload));
          const bool m1
            = ImGui::RadioButton("Fix: normalize UV in transform", &mode,
              static_cast<int>(OrientationFixMode::kNormalizeUvInTransform));
          const bool m2 = ImGui::RadioButton(
            "Fix: none", &mode, static_cast<int>(OrientationFixMode::kNone));

          if (m0 || m1 || m2) {
            const auto prev = orientation_fix_mode_;
            orientation_fix_mode_ = static_cast<OrientationFixMode>(mode);
            uv_transform_changed = true;

            const bool prev_upload
              = (prev == OrientationFixMode::kNormalizeTextureOnUpload);
            const bool next_upload = (orientation_fix_mode_
              == OrientationFixMode::kNormalizeTextureOnUpload);
            if ((prev_upload || next_upload) && !png_rgba8_.empty()) {
              png_reupload_requested_ = next_upload;
            }
          }
        }

        {
          int uv_origin = static_cast<int>(uv_origin_);
          if (ImGui::RadioButton("UV origin: bottom-left (authoring)",
                &uv_origin, static_cast<int>(UvOrigin::kBottomLeft))) {
            uv_origin_ = static_cast<UvOrigin>(uv_origin);
            uv_transform_changed = true;
            if (!png_rgba8_.empty()
              && orientation_fix_mode_
                == OrientationFixMode::kNormalizeTextureOnUpload) {
              png_reupload_requested_ = true;
            }
          }
          if (ImGui::RadioButton("UV origin: top-left", &uv_origin,
                static_cast<int>(UvOrigin::kTopLeft))) {
            uv_origin_ = static_cast<UvOrigin>(uv_origin);
            uv_transform_changed = true;
            if (!png_rgba8_.empty()
              && orientation_fix_mode_
                == OrientationFixMode::kNormalizeTextureOnUpload) {
              png_reupload_requested_ = true;
            }
          }
        }

        {
          int img_origin = static_cast<int>(image_origin_);
          if (ImGui::RadioButton("Image origin: top-left (PNG/WIC)",
                &img_origin, static_cast<int>(ImageOrigin::kTopLeft))) {
            image_origin_ = static_cast<ImageOrigin>(img_origin);
            uv_transform_changed = true;
            if (!png_rgba8_.empty()
              && orientation_fix_mode_
                == OrientationFixMode::kNormalizeTextureOnUpload) {
              png_reupload_requested_ = true;
            }
          }
          if (ImGui::RadioButton("Image origin: bottom-left", &img_origin,
                static_cast<int>(ImageOrigin::kBottomLeft))) {
            image_origin_ = static_cast<ImageOrigin>(img_origin);
            uv_transform_changed = true;
            if (!png_rgba8_.empty()
              && orientation_fix_mode_
                == OrientationFixMode::kNormalizeTextureOnUpload) {
              png_reupload_requested_ = true;
            }
          }
        }

        {
          bool flip_u = extra_flip_u_;
          bool flip_v = extra_flip_v_;
          if (ImGui::Checkbox("Extra flip U", &flip_u)) {
            extra_flip_u_ = flip_u;
            uv_transform_changed = true;
          }
          ImGui::SameLine();
          if (ImGui::Checkbox("Extra flip V", &flip_v)) {
            extra_flip_v_ = flip_v;
            uv_transform_changed = true;
          }
        }

        if (!png_rgba8_.empty()
          && orientation_fix_mode_
            == OrientationFixMode::kNormalizeTextureOnUpload
          && ImGui::Button("Re-upload PNG")) {
          png_reupload_requested_ = true;
          png_status_message_.clear();
        }
      }

      if (uv_transform_changed && cube_material_) {
        if (auto* renderer = ResolveRenderer(); renderer) {
          const auto [effective_uv_scale, effective_uv_offset]
            = GetEffectiveUvTransform();
          (void)renderer->OverrideMaterialUvTransform(
            *cube_material_, effective_uv_scale, effective_uv_offset);
        }
      }

      if (rebuild_requested) {
        cube_needs_rebuild_ = true;
      }

      const auto res_index = ResolveBaseColorTextureResourceIndex(
        texture_index_mode_, custom_texture_resource_index_);
      ImGui::Text("BaseColorTexture resource index: %u",
        static_cast<std::uint32_t>(res_index));

      ImGui::EndTabItem();
    }

    // Lighting tab
    if (ImGui::BeginTabItem("Lighting")) {
      ImGui::Separator();
      ImGui::TextUnformatted("Skybox:");

      ImGui::InputText("Skybox path", skybox_path_.data(), skybox_path_.size());
      if (ImGui::Button("Browse skybox...")) {
#if defined(OXYGEN_WINDOWS)
        std::string chosen;
        if (TryBrowseForImageFile(chosen)) {
          std::snprintf(
            skybox_path_.data(), skybox_path_.size(), "%s", chosen.c_str());
        }
#endif
      }
      ImGui::SameLine();
      if (ImGui::Button("Load skybox")) {
        skybox_load_requested_ = true;
        skybox_status_message_.clear();
      }

      if (!skybox_status_message_.empty()) {
        ImGui::Text("Skybox: %s", skybox_status_message_.c_str());
      }
      if (skybox_last_face_size_ > 0) {
        ImGui::Text("Last skybox face: %dx%d", skybox_last_face_size_,
          skybox_last_face_size_);
      }

      ImGui::Separator();
      ImGui::TextUnformatted("Sky light:");

      bool skylight_changed = false;
      if (ImGui::SliderFloat("SkyLight intensity", &sky_light_intensity_, 0.0f,
            8.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp)) {
        skylight_changed = true;
      }
      if (ImGui::SliderFloat("SkyLight diffuse", &sky_light_diffuse_intensity_,
            0.0f, 4.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp)) {
        skylight_changed = true;
      }
      if (ImGui::SliderFloat("SkyLight specular",
            &sky_light_specular_intensity_, 0.0f, 4.0f, "%.2f",
            ImGuiSliderFlags_AlwaysClamp)) {
        skylight_changed = true;
      }

      if (skylight_changed) {
        if (auto env = scene_ ? scene_->GetEnvironment() : nullptr; env) {
          if (auto sky_light
            = env->TryGetSystem<scene::environment::SkyLight>()) {
            sky_light->SetIntensity(sky_light_intensity_);
            sky_light->SetDiffuseIntensity(sky_light_diffuse_intensity_);
            sky_light->SetSpecularIntensity(sky_light_specular_intensity_);
          }
        }
      }

      ImGui::Separator();
      ImGui::TextUnformatted("Sun (directional):");
      bool sun_changed = false;
      if (ImGui::SliderFloat("Sun intensity", &sun_intensity_, 0.0f, 30.0f,
            "%.2f", ImGuiSliderFlags_AlwaysClamp)) {
        sun_changed = true;
      }
      float sun_color[3]
        = { sun_color_rgb_.x, sun_color_rgb_.y, sun_color_rgb_.z };
      if (ImGui::ColorEdit3("Sun color", sun_color,
            ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR)) {
        sun_color_rgb_ = { sun_color[0], sun_color[1], sun_color[2] };
        sun_changed = true;
      }
      if (sun_changed) {
        if (sun_node_.IsAlive()) {
          if (auto light = sun_node_.GetLightAs<scene::DirectionalLight>()) {
            auto& light_ref = light->get();
            light_ref.Common().intensity = sun_intensity_;
            light_ref.Common().color_rgb = sun_color_rgb_;
          }
        }
      }

      ImGui::EndTabItem();
    }

    ImGui::EndTabBar();
  }

  ImGui::Separator();
  ImGui::Text("Orbit yaw:   %.3f rad", orbit_yaw_rad_);
  ImGui::Text("Orbit pitch: %.3f rad", orbit_pitch_rad_);
  ImGui::Text("Distance:    %.3f", orbit_distance_);

  ImGui::End();
}

} // namespace oxygen::examples::textured_cube
