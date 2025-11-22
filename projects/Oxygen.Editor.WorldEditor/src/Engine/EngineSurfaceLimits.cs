// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.WorldEditor.Engine;

/// <summary>
/// Defines the hard limits enforced by the engine when allocating Composition surfaces.
/// </summary>
public static class EngineSurfaceLimits
{
    /// <summary>
    /// Gets the maximum number of simultaneously active composition surfaces supported by the editor runtime.
    /// </summary>
    public const int MaxTotalSurfaces = 8;

    /// <summary>
    /// Gets the maximum number of composition surfaces a single document may keep alive.
    /// </summary>
    public const int MaxSurfacesPerDocument = 4;
}
