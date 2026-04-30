// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Services;

/// <summary>
/// Component-local field ids for <see cref="EngineComponentId.PerspectiveCamera"/>.
/// Mirrors <c>oxygen::interop::module::PerspectiveCameraField</c>.
/// </summary>
public enum PerspectiveCameraField
{
    /// <summary>Vertical field of view, in radians.</summary>
    FieldOfViewYRadians = 0,

    /// <summary>Viewport aspect ratio.</summary>
    AspectRatio = 1,

    /// <summary>Near clipping plane, in meters.</summary>
    NearPlane = 2,

    /// <summary>Far clipping plane, in meters.</summary>
    FarPlane = 3,
}
