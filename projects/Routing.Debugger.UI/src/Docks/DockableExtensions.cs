// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.Docks;

using System.Globalization;
using DroidNet.Docking;
using Microsoft.UI.Xaml;

public static class DockableExtensions
{
    /// <summary>
    /// Parses a dockable preferred width from its string value to a <see cref="GridLength" /> value.
    /// </summary>
    /// <param name="dockable">The dockable for which the preferred width is to
    /// be converted.</param>
    /// <inheritdoc cref="GridLengthFromString" />
    public static GridLength PreferredGridWidth(this IDockable dockable)
        => GridLengthFromString(dockable.PreferredWidth);

    /// <summary>
    /// Parses a dockable preferred height from its string value to a <see cref="GridLength" /> value.
    /// </summary>
    /// <param name="dockable">The dockable for which the preferred height is to
    /// be converted.</param>
    /// <inheritdoc cref="GridLengthFromString" />
    public static GridLength PreferredGridHeight(this IDockable dockable)
        => GridLengthFromString(dockable.PreferredHeight);

    /// <summary>
    /// Parses a dockable preferred length from its string value to a
    /// <see cref="GridLength" /> value.
    /// </summary>
    /// <param name="length">The length as a string.</param>
    /// <returns>If the preferred length is null or empty, this method returns
    /// <see cref="GridLength.Auto" />; otherwise if the parsing succeeds, it
    /// returns a valid GridLength from the string value.</returns>
    /// <exception cref="ArgumentException">if the string format is invalid.</exception>
    /// <remarks>
    /// The format of the string should be consistent with how GridLength sizing
    /// works:
    /// <list type="bullet">
    /// <item>
    /// <term>Fixed Width</term><description> a numeric value that can be parsed
    /// as a <see langword="double" /></description>
    /// </item>
    /// <item>
    /// <term>Star Sizing</term><description> a numeric value that can be parsed
    /// as a <see langword="double" />, followed by a <c>*</c></description>
    /// </item>
    /// <item>
    /// <term>Auto Sizing</term><description> the word "auto". Case does not
    /// matter.</description>
    /// </item>
    /// </list>
    /// </remarks>
    private static GridLength GridLengthFromString(string? length)
    {
        if (string.IsNullOrEmpty(length))
        {
            return GridLength.Auto;
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
            else
            {
                if (!double.TryParse(length, NumberStyles.Any, CultureInfo.InvariantCulture, out numericValue))
                {
                    throw new ArgumentException(
                        $"the dockable preferred length `{length}` does not contain a valid numeric value",
                        nameof(length));
                }
            }

            type = GridUnitType.Star;
        }
        else
        {
            if (!double.TryParse(length, NumberStyles.Any, CultureInfo.InvariantCulture, out numericValue))
            {
                throw new ArgumentException(
                    $"the dockable preferred length `{length}` is not a valid absolute length",
                    nameof(length));
            }

            type = GridUnitType.Pixel;
        }

        return new GridLength(numericValue, type);
    }
}
