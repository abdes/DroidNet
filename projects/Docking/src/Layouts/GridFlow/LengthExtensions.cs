// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using Microsoft.UI.Xaml;

namespace DroidNet.Docking.Layouts.GridFlow;

/// <summary>
/// Provides extension methods for converting <see cref="Width"/> and <see cref="Height"/> to <see cref="GridLength"/>.
/// </summary>
/// <remarks>
/// These extension methods facilitate the conversion of UI-independent length values to <see cref="GridLength"/> values,
/// which are used in grid-based layouts in XAML.
/// </remarks>
internal static class LengthExtensions
{
    /// <summary>
    /// Translates a UI-independent <see cref="Width"/> into a <see cref="GridLength"/> value.
    /// </summary>
    /// <param name="width">The width to be converted.</param>
    /// <returns>A <see cref="GridLength"/> representing the specified width.</returns>
    /// <remarks>
    /// This method converts a <see cref="Width"/> object to a <see cref="GridLength"/> that can be used in XAML grid layouts.
    /// <para>
    /// <strong>Example Usage:</strong>
    /// <code><![CDATA[
    /// Width width = new Width("2*");
    /// GridLength gridLength = width.ToGridLength();
    /// // gridLength is GridLength(2, GridUnitType.Star)
    /// ]]></code>
    /// </para>
    /// </remarks>
    public static GridLength ToGridLength(this Width width) => GridLengthFromString(width);

    /// <summary>
    /// Translates a UI-independent <see cref="Height"/> into a <see cref="GridLength"/> value.
    /// </summary>
    /// <param name="height">The height to be converted.</param>
    /// <returns>A <see cref="GridLength"/> representing the specified height.</returns>
    /// <remarks>
    /// This method converts a <see cref="Height"/> object to a <see cref="GridLength"/> that can be used in XAML grid layouts.
    /// <para>
    /// <strong>Example Usage:</strong>
    /// <code><![CDATA[
    /// Height height = new Height("auto");
    /// GridLength gridLength = height.ToGridLength();
    /// // gridLength is GridLength.Auto
    /// ]]></code>
    /// </para>
    /// </remarks>
    public static GridLength ToGridLength(this Height height) => GridLengthFromString(height);

    /// <summary>
    /// Parses a dock preferred length from its string value to a <see cref="GridLength"/> value.
    /// </summary>
    /// <param name="length">The length as a string.</param>
    /// <returns>
    /// If the length to be converted is null or empty, this method returns <see cref="GridLength.Auto"/>; otherwise, if
    /// it contains a properly formatted string value, it returns a valid <see cref="GridLength"/> corresponding to that value.
    /// </returns>
    /// <exception cref="ArgumentException">Thrown if the string format is invalid.</exception>
    /// <remarks>
    /// Valid formats for the string value should be consistent with how <see cref="GridLength"/> sizing works, and include:
    /// <para>
    /// <strong>Fixed Width:</strong> a numeric value that can be parsed as a <see langword="double"/>.
    /// </para>
    /// <para>
    /// <strong>Star Sizing:</strong> a numeric value that can be parsed as a <see langword="double"/>, followed by a <c>*</c>.
    /// </para>
    /// <para>
    /// <strong>Auto Sizing:</strong> the word "auto". Case does not matter.
    /// </para>
    /// <para>
    /// <strong>Example Usage:</strong>
    /// <code><![CDATA[
    /// GridLength length1 = GridLengthFromString("100");
    /// // length1 is GridLength(100, GridUnitType.Pixel)
    /// GridLength length2 = GridLengthFromString("2*");
    /// // length2 is GridLength(2, GridUnitType.Star)
    /// GridLength length3 = GridLengthFromString("auto");
    /// // length3 is GridLength.Auto
    /// ]]></code>
    /// </para>
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
                    $"The length `{length}` does not contain a valid numeric value",
                    nameof(length));
            }

            type = GridUnitType.Star;
        }
        else
        {
            if (!double.TryParse(length, NumberStyles.Any, CultureInfo.InvariantCulture, out numericValue))
            {
                throw new ArgumentException(
                    $"The length `{length}` is not a valid absolute length",
                    nameof(length));
            }

            type = GridUnitType.Pixel;
        }

        return new GridLength(numericValue, type);
    }
}
