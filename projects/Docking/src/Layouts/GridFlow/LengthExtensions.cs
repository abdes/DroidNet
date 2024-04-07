// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Layouts.GridFlow;

using System.Globalization;
using DroidNet.Docking;
using Microsoft.UI.Xaml;

internal static class LengthExtensions
{
    /// <summary>Translates a UI independent <see cref="Width" /> into a <see cref="GridLength" /> value.</summary>
    /// <param name="width">The width to be converted.</param>
    /// <inheritdoc cref="GridLengthFromString" />
    public static GridLength ToGridLength(this Width width) => GridLengthFromString(width);

    /// <summary>Translates a UI independent <see cref="Height" /> into a <see cref="GridLength" /> value.</summary>
    /// <param name="height">The height to be converted.</param>
    /// <inheritdoc cref="GridLengthFromString" />
    public static GridLength ToGridLength(this Height height) => GridLengthFromString(height);

    /// <summary>Parses a dock preferred length from its string value to a <see cref="GridLength" /> value.</summary>
    /// <param name="length">The length as a string.</param>
    /// <returns>If the length to be converted is null or empty, this method returns <see cref="GridLength.Auto" />; otherwise if
    /// is contains a properly formatted string value, it returns a valid GridLength corresponding to that value.</returns>
    /// <exception cref="ArgumentException">if the string format is invalid.</exception>
    /// <remarks>
    /// Valid formats for the string value should be consistent with how GridLength sizing works, and include:
    /// <list type="bullet">
    /// <item>
    /// <term>Fixed Width</term><description> a numeric value that can be parsed as a <see langword="double" /></description>
    /// </item>
    /// <item>
    /// <term>Star Sizing</term><description> a numeric value that can be parsed as a <see langword="double" />, followed by a
    /// <c>*</c></description>
    /// </item>
    /// <item>
    /// <term>Auto Sizing</term><description> the word "auto". Case does not matter.</description>
    /// </item>
    /// </list>
    /// </remarks>
    private static GridLength GridLengthFromString(string? length)
    {
        if (string.IsNullOrEmpty(length))
        {
            return new GridLength(1, GridUnitType.Star);
        }

        double numericValue;
        GridUnitType type;

        if (length.Equals("auto", StringComparison.OrdinalIgnoreCase))
        {
            numericValue = 1;
            type = GridUnitType.Auto;
        }
        else if (length[^1] == '*')
        {
            length = length.Remove(length.Length - 1);
            if (string.IsNullOrEmpty(length))
            {
                numericValue = 1;
            }
            else if (!double.TryParse(length, NumberStyles.Any, CultureInfo.InvariantCulture, out numericValue))
            {
                throw new ArgumentException(
                    $"the length `{length}` does not contain a valid numeric value",
                    nameof(length));
            }

            type = GridUnitType.Star;
        }
        else
        {
            if (!double.TryParse(length, NumberStyles.Any, CultureInfo.InvariantCulture, out numericValue))
            {
                throw new ArgumentException(
                    $"the length `{length}` is not a valid absolute length",
                    nameof(length));
            }

            type = GridUnitType.Pixel;
        }

        return new GridLength(numericValue, type);
    }
}
