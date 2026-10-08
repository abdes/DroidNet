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

      if (n.compositing_target.has_value()) {
        auto bytes = gcnew cli::array<System::Byte>(16);
        for (int i = 0; i < 16; ++i) {
          bytes[i] = (*n.compositing_target)[static_cast<std::size_t>(i)];
        }
        m->CompositingTarget = Guid(bytes);
      }

      return m;
    }

    native::interop::module::EditorView::Config ToNative() {
      native::interop::module::EditorView::Config n;
      n.name = msclr::interop::marshal_as<std::string>(Name);
      n.purpose = msclr::interop::marshal_as<std::string>(Purpose);
      n.width = Width;
      n.height = Height;
      n.clear_color = ClearColor.ToNative();

      if (CompositingTarget.HasValue) {
        auto bytes = CompositingTarget.Value.ToByteArray();
        native::interop::module::SurfaceRegistry::GuidKey key {};
        for (int i = 0; i < 16; ++i) {
          key[static_cast<std::size_t>(i)] = bytes[i];
        }
        n.compositing_target = key;
      }

      return n;
    }
  };

} // namespace Oxygen::Interop
