// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;

namespace DroidNet.Controls;

/// <summary>
///     Formatting and mask handling for <see cref="VectorBox" /> components.
/// </summary>
public partial class VectorBox
{
    /// <summary>
    ///     Validates a numeric value against the current mask for a given component.
    /// </summary>
    /// <param name="componentMask">The mask to apply.</param>
    /// <param name="value">The value to validate.</param>
    /// <returns>True if the value is valid according to the mask; otherwise, false.</returns>
    public static bool IsValidMaskValue(string? componentMask, double value)
    {
        if (string.IsNullOrEmpty(componentMask))
        {
            return true;
        }

        try
        {
            var parser = new MaskParser(componentMask);
            return parser.IsValidValue(value);
        }
        catch (ArgumentException)
        {
            return true;
        }
    }

    /// <summary>
    ///     Formats a numeric value according to the specified mask.
    /// </summary>
    /// <param name="componentMask">The mask to apply.</param>
    /// <param name="value">The value to format.</param>
    /// <param name="withPadding">Whether to apply padding spaces per the mask.</param>
    /// <returns>The formatted value, or the value as string if mask is not applicable.</returns>
    public static string FormatMaskValue(string? componentMask, double value, bool withPadding = false)
    {
        if (string.IsNullOrEmpty(componentMask))
        {
            return value.ToString(CultureInfo.InvariantCulture);
        }

        try
        {
            var parser = new MaskParser(componentMask);
            return parser.FormatValue(value, withPadding);
        }
        catch (ArgumentException)
        {
            return value.ToString(CultureInfo.InvariantCulture);
        }
    }
}
