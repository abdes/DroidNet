//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed

#include <EditorModule/EditorView.h>

namespace Oxygen::Interop {

  /// <summary>
  /// What an editor viewport renders: the lit scene or one of its diagnostic
  /// views. Editor view state, never authored data.
  /// </summary>
  public enum class ViewModeManaged : System::Int32 {
    /// <summary>The fully lit scene.</summary>
    Lit = 0,

    /// <summary>Material base colour without lighting.</summary>
    Unlit = 1,

    /// <summary>Geometry edges only.</summary>
    Wireframe = 2,

    /// <summary>The lit scene with geometry edges over it.</summary>
    LitWireframe = 3,

    /// <summary>Direct light contribution only.</summary>
    DirectLighting = 4,

    /// <summary>Image-based (sky) light contribution only.</summary>
    IndirectLighting = 5,

    /// <summary>World-space surface normals.</summary>
    WorldNormals = 6,

    /// <summary>Material roughness.</summary>
    Roughness = 7,

    /// <summary>Material metalness.</summary>
    Metalness = 8,

    /// <summary>Linear scene depth.</summary>
    LinearDepth = 9,

    /// <summary>Directional light shadowing.</summary>
    ShadowMask = 10,
  };

  [[nodiscard]] inline auto ToNativeViewMode(ViewModeManaged mode)
    -> ::oxygen::interop::module::EditorViewMode {
    using NativeMode = ::oxygen::interop::module::EditorViewMode;
    switch (mode) {
    case ViewModeManaged::Unlit:
      return NativeMode::kUnlit;
    case ViewModeManaged::Wireframe:
      return NativeMode::kWireframe;
    case ViewModeManaged::LitWireframe:
      return NativeMode::kLitWireframe;
    case ViewModeManaged::DirectLighting:
      return NativeMode::kDirectLighting;
    case ViewModeManaged::IndirectLighting:
      return NativeMode::kIndirectLighting;
    case ViewModeManaged::WorldNormals:
      return NativeMode::kWorldNormals;
    case ViewModeManaged::Roughness:
      return NativeMode::kRoughness;
    case ViewModeManaged::Metalness:
      return NativeMode::kMetalness;
    case ViewModeManaged::LinearDepth:
      return NativeMode::kLinearDepth;
    case ViewModeManaged::ShadowMask:
      return NativeMode::kShadowMask;
    case ViewModeManaged::Lit:
    default:
      return NativeMode::kLit;
    }
  }

  /// <summary>The rate and duration of the last completed engine frame.</summary>
  public value struct FrameStatisticsManaged {
    /// <summary>Frames per second, as measured by the engine clock.</summary>
    float FramesPerSecond;

    /// <summary>Duration of the last completed frame, in milliseconds.</summary>
    float FrameTimeMilliseconds;
  };

} // namespace Oxygen::Interop
