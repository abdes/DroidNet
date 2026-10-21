// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;

namespace Oxygen.Editor.SceneInspector;

/// <summary>Converts the doubles inspector controls edit to the single-precision values the scene stores.</summary>
internal static class InspectorValues
{
    /// <summary>Builds a stored vector from edited components.</summary>
    /// <param name="x">The first component.</param>
    /// <param name="y">The second component.</param>
    /// <param name="z">The third component.</param>
    /// <returns>The single-precision vector.</returns>
    public static Vector3 ToVector3(double x, double y, double z) => new((float)x, (float)y, (float)z);
}
