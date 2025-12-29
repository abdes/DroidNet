//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "MainModule.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
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

#if defined(OXYGEN_WINDOWS)
#  include <shobjidl_core.h>
#  include <wincodec.h>
#  include <windows.h>
#  include <wrl/client.h>
#endif

namespace {

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

auto DecodePngRgba8Wic(const std::filesystem::path& file_path,
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

auto ResolveBaseColorTextureResourceIndex(
  oxygen::examples::textured_cube::MainModule::TextureIndexMode mode,
  std::uint32_t custom_resource_index) -> oxygen::data::pak::v1::ResourceIndexT
{
  using oxygen::data::pak::v1::ResourceIndexT;
  using enum oxygen::examples::textured_cube::MainModule::TextureIndexMode;

  switch (mode) {
  case kFallback:
    return oxygen::data::pak::v1::kFallbackResourceIndex;
  case kCustom:
    return static_cast<ResourceIndexT>(custom_resource_index);
  case kForcedError:
  default:
    return (std::numeric_limits<ResourceIndexT>::max)();
  }
}

auto MakeCubeMaterial(const char* name, const glm::vec4& rgba,
  oxygen::data::pak::v1::ResourceIndexT base_color_texture_resource_index,
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
  desc.metalness = 0.0f;
  desc.roughness = 0.6f;
  desc.ambient_occlusion = 1.0f;

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
        std::vector<std::byte> rgba8;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::string decode_error;
        if (!DecodePngRgba8Wic(png_path, rgba8, width, height, decode_error)) {
          png_status_message_
            = decode_error.empty() ? "PNG decode failed" : decode_error;
        } else {
          // Use a fresh key each time so the renderer doesn't keep an older
          // bindless entry for the same key.
          custom_texture_key_ = asset_loader->MintSyntheticTextureKey();

          // Keep a non-zero resource index for the material-side demo path.
          if (custom_texture_resource_index_ == 0U) {
            custom_texture_resource_index_ = 1U;
          } else {
            ++custom_texture_resource_index_;
          }

          using oxygen::data::pak::v1::TextureResourceDesc;

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
            = static_cast<std::size_t>(width) * kBytesPerPixel;
          const std::size_t row_pitch
            = AlignUp(bytes_per_row, kRowPitchAlignment);
          const std::size_t padded_size
            = row_pitch * static_cast<std::size_t>(height);

          std::vector<std::byte> rgba8_padded;
          rgba8_padded.resize(padded_size);
          for (std::uint32_t y = 0; y < height; ++y) {
            const auto dst_offset = static_cast<std::size_t>(y) * row_pitch;
            const auto src_offset = static_cast<std::size_t>(y) * bytes_per_row;
            std::memcpy(rgba8_padded.data() + dst_offset,
              rgba8.data() + src_offset, bytes_per_row);
          }

          TextureResourceDesc desc {};
          desc.data_offset = static_cast<oxygen::data::pak::v1::OffsetT>(
            sizeof(TextureResourceDesc));
          desc.size_bytes = static_cast<oxygen::data::pak::v1::DataBlobSizeT>(
            rgba8_padded.size());
          desc.texture_type
            = static_cast<std::uint8_t>(oxygen::TextureType::kTexture2D);
          desc.compression_type = 0;
          desc.width = width;
          desc.height = height;
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

          auto tex = co_await asset_loader->LoadTextureFromBufferAsync(
            custom_texture_key_,
            std::span<const std::uint8_t>(packed.data(), packed.size()));
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
#endif
      }
    }
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

    auto cube_geo = BuildCubeGeometry(cube_material_, uv_scale_, uv_offset_);
    if (cube_geo) {
      if (cube_geometry_) {
        retired_cube_geometries_.push_back(cube_geometry_);
      }

      cube_geometry_ = std::move(cube_geo);
      cube_node_.GetRenderable().SetGeometry(cube_geometry_);
      cube_needs_rebuild_ = false;

      if (auto* renderer = ResolveRenderer(); renderer && cube_material_) {
        (void)renderer->OverrideMaterialUvTransform(
          *cube_material_, uv_scale_, uv_offset_);
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
      (void)renderer->OverrideMaterialUvTransform(
        *cube_material_, uv_scale_, uv_offset_);
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

  ImGui::Separator();
  ImGui::TextUnformatted("Texture Playground:");

  bool changed = false;
  bool rebuild_requested = false;
  bool uv_transform_changed = false;
  {
    int mode = static_cast<int>(texture_index_mode_);
    const bool mode_changed = ImGui::RadioButton(
      "Forced error", &mode, static_cast<int>(TextureIndexMode::kForcedError));
    ImGui::SameLine();
    const bool mode_changed_2 = ImGui::RadioButton(
      "Fallback (0)", &mode, static_cast<int>(TextureIndexMode::kFallback));
    ImGui::SameLine();
    const bool mode_changed_3 = ImGui::RadioButton(
      "Custom", &mode, static_cast<int>(TextureIndexMode::kCustom));

    changed |= (mode_changed || mode_changed_2 || mode_changed_3);
    rebuild_requested |= (mode_changed || mode_changed_2 || mode_changed_3);
    texture_index_mode_ = static_cast<TextureIndexMode>(mode);
  }

  if (texture_index_mode_ == TextureIndexMode::kCustom) {
    int custom_idx = static_cast<int>(custom_texture_resource_index_);
    if (ImGui::InputInt("Resource index", &custom_idx)) {
      custom_idx = (std::max)(0, custom_idx);
      custom_texture_resource_index_ = static_cast<std::uint32_t>(custom_idx);
      changed = true;
      rebuild_requested = true;
    }

    ImGui::InputText("PNG path", png_path_.data(), png_path_.size());
    if (ImGui::Button("Browse...")) {
#if defined(OXYGEN_WINDOWS)
      std::string chosen;
      if (TryBrowseForPngFile(chosen)) {
        std::snprintf(png_path_.data(), png_path_.size(), "%s", chosen.c_str());
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

  constexpr float kUvScaleMin = 0.01f;
  constexpr float kUvScaleMax = 64.0f;
  constexpr float kUvOffsetMin = -64.0f;
  constexpr float kUvOffsetMax = 64.0f;

  auto SanitizeFinite = [](float v, float fallback) -> float {
    return std::isfinite(v) ? v : fallback;
  };

  float uv_scale[2] = { uv_scale_.x, uv_scale_.y };
  if (ImGui::DragFloat2("UV scale", uv_scale, 0.01f, kUvScaleMin, kUvScaleMax,
        "%.3f", ImGuiSliderFlags_AlwaysClamp)) {
    const glm::vec2 new_scale {
      std::clamp(SanitizeFinite(uv_scale[0], 1.0f), kUvScaleMin, kUvScaleMax),
      std::clamp(SanitizeFinite(uv_scale[1], 1.0f), kUvScaleMin, kUvScaleMax),
    };
    if (new_scale != uv_scale_) {
      uv_scale_ = new_scale;
      changed = true;
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
      changed = true;
      uv_transform_changed = true;
    }
  }

  if (ImGui::Button("Reset UV")) {
    uv_scale_ = { 1.0f, 1.0f };
    uv_offset_ = { 0.0f, 0.0f };
    changed = true;
    uv_transform_changed = true;
  }

  if (uv_transform_changed && cube_material_) {
    if (auto* renderer = ResolveRenderer(); renderer) {
      (void)renderer->OverrideMaterialUvTransform(
        *cube_material_, uv_scale_, uv_offset_);
    }
  }

  if (rebuild_requested) {
    cube_needs_rebuild_ = true;
  }

  const auto res_index = ResolveBaseColorTextureResourceIndex(
    texture_index_mode_, custom_texture_resource_index_);
  ImGui::Text("BaseColorTexture resource index: %u",
    static_cast<std::uint32_t>(res_index));

  ImGui::Separator();
  ImGui::Text("Orbit yaw:   %.3f rad", orbit_yaw_rad_);
  ImGui::Text("Orbit pitch: %.3f rad", orbit_pitch_rad_);
  ImGui::Text("Distance:    %.3f", orbit_distance_);

  ImGui::End();
}

} // namespace oxygen::examples::textured_cube
