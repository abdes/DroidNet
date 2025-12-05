//===----------------------------------------------------------------------===//
// Managed wrapper for oxygen::graphics::Color
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed

#include <Oxygen/Graphics/Common/Types/Color.h>

namespace Oxygen::Interop {

  namespace native = ::oxygen;

  /// <summary>
  /// Simple managed value type representing an RGBA color compatible with
  /// the native <c>oxygen::graphics::Color</c> type.
  /// </summary>
  public value struct ColorManaged {
  public:
    float R;
    float G;
    float B;
    float A;

    ColorManaged(float r, float g, float b, float a) : R(r), G(g), B(b), A(a) {}

    static ColorManaged FromNative(const native::graphics::Color& n) {
      return ColorManaged{ n.r, n.g, n.b, n.a };
    }

    native::graphics::Color ToNative() {
      native::graphics::Color c;
      c.r = R;
      c.g = G;
      c.b = B;
      c.a = A;
      return c;
    }
  };

} // namespace Oxygen::Interop
