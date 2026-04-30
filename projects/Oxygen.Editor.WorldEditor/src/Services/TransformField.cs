// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Services;

/// <summary>
/// Component-local field ids for <see cref="EngineComponentId.Transform"/>.
/// Mirrors <c>oxygen::interop::module::TransformField</c>.
/// </summary>
public enum TransformField
{
    /// <summary>Local position X.</summary>
    PositionX = 0,

    /// <summary>Local position Y.</summary>
    PositionY = 1,

    /// <summary>Local position Z.</summary>
    PositionZ = 2,

    /// <summary>Local rotation X (Euler degrees).</summary>
    RotationXDegrees = 3,

    /// <summary>Local rotation Y (Euler degrees).</summary>
    RotationYDegrees = 4,

    /// <summary>Local rotation Z (Euler degrees).</summary>
    RotationZDegrees = 5,

    /// <summary>Local scale X.</summary>
    ScaleX = 6,

    /// <summary>Local scale Y.</summary>
    ScaleY = 7,

    /// <summary>Local scale Z.</summary>
    ScaleZ = 8,
}
