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

#include "Views/ColorManaged.h"

namespace Oxygen::Interop {

  namespace native = ::oxygen;

  using System::Guid;

  /// <summary>
  /// Managed mirror of <c>oxygen::interop::module::EditorView::Config</c>.
  /// Represents the configuration used to create views from managed callers.
  /// Note: the managed compositing target is represented as an optional
  /// <see cref="System::Guid"/> (surface id). Mapping a Guid to a native
  /// surface pointer is performed by the caller (EngineRunner / surface
  /// registry) and is intentionally not done inside this DTO.
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

    static ViewConfigManaged^
      FromNative(const native::interop::module::EditorView::Config& n) {
      auto m = gcnew ViewConfigManaged();
      m->Name = gcnew System::String(n.name.c_str());
      m->Purpose = gcnew System::String(n.purpose.c_str());
      m->Width = n.width;
      m->Height = n.height;
      m->ClearColor = ColorManaged::FromNative(n.clear_color);

      // compositing_target is a native pointer -> we cannot directly convert
      // it to a GUID here. Leave CompositingTarget empty.
      m->CompositingTarget = System::Nullable<Guid>();

      return m;
    }

    native::interop::module::EditorView::Config ToNative() {
      native::interop::module::EditorView::Config n;
      n.name = msclr::interop::marshal_as<std::string>(Name);
      n.purpose = msclr::interop::marshal_as<std::string>(Purpose);
      n.width = Width;
      n.height = Height;
      n.clear_color = ClearColor.ToNative();

      // NOTE: CompositingTarget is represented on managed side as a GUID. The
      // mapping of GUID -> native surface pointer is performed by the
      // EngineRunner / SurfaceRegistry when creating the view. For now we
      // leave compositing_target unset here (it will be handled by the caller).
      n.compositing_target = std::optional<native::graphics::Surface*>();

      return n;
    }
  };

} // namespace Oxygen::Interop
