// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Services;

/// <summary>
/// Component-local field ids for <see cref="EngineComponentId.OrthographicCamera"/>.
/// Mirrors <c>oxygen::interop::module::OrthographicCameraField</c>.
/// </summary>
public enum OrthographicCameraField
{
    /// <summary>Half of the visible height, in meters.</summary>
    OrthographicSize = 0,

    /// <summary>Retained Fixed aspect ratio (width / height).</summary>
    AspectRatio = 1,

    /// <summary>Near clipping plane, in meters.</summary>
    NearPlane = 2,

    /// <summary>Far clipping plane, in meters.</summary>
    FarPlane = 3,

    /// <summary>Physical aperture as an f-number.</summary>
    ApertureF = 4,

    /// <summary>Shutter rate in reciprocal seconds.</summary>
    ShutterRate = 5,

    /// <summary>ISO sensor sensitivity.</summary>
    Iso = 6,

    /// <summary>Auto or Fixed authored aspect policy.</summary>
    AspectMode = 7,
}
