// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;

namespace Oxygen.Editor.SceneInspector.Editing;

/// <summary>Converts the doubles inspector controls edit to the single-precision values the scene stores.</summary>
internal static class InspectorValues
{
    /// <summary>Builds a stored vector from edited components.</summary>
    /// <param name="x">The first component.</param>
    /// <param name="y">The second component.</param>
    /// <param name="z">The third component.</param>
    /// <returns>The single-precision vector.</returns>
    public static Vector3 ToVector3(double x, double y, double z) => new((float)x, (float)y, (float)z);

    /// <summary>Converts an edited number to the integer an integer property stores, rounded to the nearest one.</summary>
    /// <param name="value">The edited number.</param>
    /// <param name="integer">The integer; meaningful only when the method succeeds.</param>
    /// <returns>
    /// <see langword="false"/> when the number has no integer value: it is not finite, or it rounds beyond what an
    /// integer holds. Such a number is no edit; it never wraps around into another value.
    /// </returns>
    public static bool TryToInt32(double value, out int integer)
    {
        var rounded = Math.Round(value);
        if (!double.IsFinite(rounded) || rounded < int.MinValue || rounded > int.MaxValue)
        {
            integer = 0;
            return false;
        }

        integer = checked((int)rounded);
        return true;
    }
}
