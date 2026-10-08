//===----------------------------------------------------------------------===//
// Managed wrapper for native EditorView::Config
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed

#include <msclr/marshal_cppstd.h>
#include <optional>

#define WIN32_LEAN_AND_MEAN
#include <EditorModule/EditorView.h>
#include <Oxygen/Graphics/Common/Surface.h>

#include "Views/CameraViewPresetManaged.h"
#include "Views/ColorManaged.h"
#include "Views/EditorCameraStateManaged.h"
#include "Views/ViewIdManaged.h"
#include "Views/ViewModeManaged.h"

namespace Oxygen::Interop {

  namespace native = ::oxygen;

  using System::Guid;

  namespace detail {
    //! A 16-byte native key with the managed GUID's byte layout.
    template <typename Key>
    auto ToNativeKey(Guid id) -> Key {
      auto bytes = id.ToByteArray();
      Key key {};
      for (int i = 0; i < 16; ++i) {
        key[static_cast<std::size_t>(i)] = bytes[i];
      }
      return key;
    }
  } // namespace detail

  /// <summary>
  /// Managed mirror of <c>oxygen::interop::module::EditorView::Config</c>.
  /// Represents the configuration used to create views from managed callers.
  /// The compositing target is the optional surface id, carried natively as
  /// the surface registry key.
  /// </summary>
  public
  ref class ViewConfigManaged sealed {
  public:
    ViewConfigManaged() {
      Name = gcnew System::String("");
      Purpose = gcnew System::String("");
      Width = 1u;
      Height = 1u;
      ClearColor = ColorManaged{ 0.1f, 0.2f, 0.38f, 1.0f };
      CompositingTarget = System::Nullable<Guid>();
      CameraPreset = CameraViewPresetManaged::Perspective;
      ViewMode = ViewModeManaged::Lit;
      ShowGrid = true;
      ShowSelectionOutline = true;
    }

    // Human readable name for the view
    property System::String^ Name;

    // Purpose description (debugging, grouping, etc.)
    property System::String^ Purpose;

    // Optional GUID of the surface to attach as compositing target. If not
    // specified the view will use the fallback width/height and render offscreen.
    property System::Nullable<Guid> CompositingTarget;

    property System::UInt32 Width;
    property System::UInt32 Height;

    // Background clear color used when building the offscreen color texture.
    property ColorManaged ClearColor;

    /// <summary>The preset the editor camera starts with.</summary>
    property CameraViewPresetManaged CameraPreset;

    /// <summary>
    /// Editor camera state to start from instead of framing the scene, or
    /// null for a new pane.
    /// </summary>
    property EditorCameraStateManaged^ EditorCamera;

    /// <summary>The authored camera node the view looks through, if any.</summary>
    property System::Nullable<Guid> SceneCamera;

    /// <summary>
    /// The host view a camera preview inset is composed over, or no value for
    /// a view that presents to its own surface.
    /// </summary>
    property System::Nullable<ViewIdManaged> InsetHost;

    property ViewModeManaged ViewMode;

    property bool ShowGrid;

    /// <summary>Whether the view outlines the selected nodes.</summary>
    property bool ShowSelectionOutline;

    native::interop::module::EditorView::Config ToNative() {
      native::interop::module::EditorView::Config n;
      n.name = msclr::interop::marshal_as<std::string>(Name);
      n.purpose = msclr::interop::marshal_as<std::string>(Purpose);
      n.width = Width;
      n.height = Height;
      n.clear_color = ClearColor.ToNative();
      n.camera_preset = ToNativeCameraViewPreset(CameraPreset);

      // The view names its surface by key; the engine thread resolves it
      // every frame, so a released surface is never reached through a stale
      // address.
      if (CompositingTarget.HasValue) {
        n.compositing_target = detail::ToNativeKey<native::interop::module::SurfaceRegistry::GuidKey>(
          CompositingTarget.Value);
      }
      if (EditorCamera != nullptr) {
        n.editor_camera = EditorCamera->ToNative();
      }
      if (SceneCamera.HasValue) {
        n.scene_camera
          = detail::ToNativeKey<native::interop::module::UuidKey>(SceneCamera.Value);
      }
      if (InsetHost.HasValue) {
        n.inset_host = InsetHost.Value.ToNative();
      }
      n.render_options.view_mode = ToNativeViewMode(ViewMode);
      n.render_options.show_grid = ShowGrid;
      n.render_options.show_selection_outline = ShowSelectionOutline;

      return n;
    }
  };

} // namespace Oxygen::Interop
