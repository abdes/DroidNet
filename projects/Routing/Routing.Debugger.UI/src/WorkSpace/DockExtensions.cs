// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using DroidNet.Docking;
using Microsoft.UI.Xaml;

namespace DroidNet.Routing.Debugger.UI.WorkSpace;

/// <summary>
/// Extensions methods for the <see cref="IDock" /> interface.
/// </summary>
public static class DockExtensions
{
    /// <summary>
    /// Parses a dock width from its string value to a <see cref="GridLength" /> value.
    /// </summary>
    /// <param name="dock">The dock for which the preferred width is to be converted.</param>
    /// <inheritdoc cref="GridLengthFromString" />
    public static GridLength GridWidth(this IDock dock)
        => GridLengthFromString(dock.Width);

    /// <summary>
    /// Parses a dock height from its string value to a <see cref="GridLength" /> value.
    /// </summary>
    /// <param name="dock">The dock for which the preferred height is to be converted.</param>
    /// <inheritdoc cref="GridLengthFromString" />
    public static GridLength GridHeight(this IDock dock)
        => GridLengthFromString(dock.Height);

    /// <summary>
    /// Parses a dock preferred length from its string value to a <see cref="GridLength" /> value.
    /// </summary>
    /// <param name="length">The length as a string.</param>
    /// <returns>If the preferred length is null or empty, this method returns <see cref="GridLength.Auto" />; otherwise if the
    /// parsing succeeds, it returns a valid GridLength from the string value.</returns>
    /// <exception cref="ArgumentException">if the string format is invalid.</exception>
    /// <remarks>
    /// The format of the string should be consistent with how GridLength sizing works:
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

        if (length.Equals("auto", StringComparison.OrdinalIgnoreCase))
        {
            return new GridLength(1, GridUnitType.Auto);
        }

        var isStar = length[^1] == '*';
        var numericPart = isStar ? length[..^1] : length;
        if (double.TryParse(numericPart, NumberStyles.Any, CultureInfo.InvariantCulture, out var numericValue))
        {
            var type = isStar ? GridUnitType.Star : GridUnitType.Pixel;
            return new GridLength(numericValue, type);
        }

        throw new ArgumentException($"The length `{length}` does contain a valid numeric value", nameof(length));
    }
}
