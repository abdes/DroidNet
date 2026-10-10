//===----------------------------------------------------------------------===//
// Managed wrapper for oxygen::vortex::GroundGridConfig
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed

#include <Oxygen/Vortex/Types/GroundGridConfig.h>

#include "Views/ColorManaged.h"

namespace Oxygen::Interop {

  namespace native = ::oxygen;

  /// <summary>
  /// Managed mirror of <c>oxygen::vortex::GroundGridConfig</c>: how every
  /// view draws the ground grid. A new instance holds the engine defaults.
  /// </summary>
  public
  ref class GroundGridConfigManaged sealed {
  public:
    GroundGridConfigManaged() {
      const native::vortex::GroundGridConfig defaults {};
      Enabled = defaults.enabled;
      Spacing = defaults.spacing;
      MajorEvery = defaults.major_every;
      LineThickness = defaults.line_thickness;
      MajorThickness = defaults.major_thickness;
      AxisThickness = defaults.axis_thickness;
      FadeStart = defaults.fade_start;
      FadePower = defaults.fade_power;
      HorizonBoost = defaults.horizon_boost;
      OriginX = defaults.origin.x;
      OriginY = defaults.origin.y;
      SmoothMotion = defaults.smooth_motion;
      SmoothTime = defaults.smooth_time;
      MinorColor = ColorManaged::FromNative(defaults.minor_color);
      MajorColor = ColorManaged::FromNative(defaults.major_color);
      AxisColorX = ColorManaged::FromNative(defaults.axis_color_x);
      AxisColorY = ColorManaged::FromNative(defaults.axis_color_y);
      OriginColor = ColorManaged::FromNative(defaults.origin_color);
    }

    property bool Enabled;
    property float Spacing;
    property System::UInt32 MajorEvery;
    property float LineThickness;
    property float MajorThickness;
    property float AxisThickness;
    property float FadeStart;
    property float FadePower;
    property float HorizonBoost;
    property float OriginX;
    property float OriginY;
    property bool SmoothMotion;
    property float SmoothTime;
    property ColorManaged MinorColor;
    property ColorManaged MajorColor;
    property ColorManaged AxisColorX;
    property ColorManaged AxisColorY;
    property ColorManaged OriginColor;

    native::vortex::GroundGridConfig ToNative() {
      native::vortex::GroundGridConfig n;
      n.enabled = Enabled;
      n.spacing = Spacing;
      n.major_every = MajorEvery;
      n.line_thickness = LineThickness;
      n.major_thickness = MajorThickness;
      n.axis_thickness = AxisThickness;
      n.fade_start = FadeStart;
      n.fade_power = FadePower;
      n.horizon_boost = HorizonBoost;
      n.origin = native::Vec2 { OriginX, OriginY };
      n.smooth_motion = SmoothMotion;
      n.smooth_time = SmoothTime;
      n.minor_color = MinorColor.ToNative();
      n.major_color = MajorColor.ToNative();
      n.axis_color_x = AxisColorX.ToNative();
      n.axis_color_y = AxisColorY.ToNative();
      n.origin_color = OriginColor.ToNative();
      return n;
    }
  };

} // namespace Oxygen::Interop
