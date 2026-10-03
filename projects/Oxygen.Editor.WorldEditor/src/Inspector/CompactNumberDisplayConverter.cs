// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Data;

namespace Oxygen.Editor.World.Inspector;

/// <summary>Compacts mask-formatted inspector values without changing their precision or units.</summary>
public sealed partial class CompactNumberDisplayConverter : IValueConverter
{
    /// <inheritdoc/>
    public object Convert(object value, Type targetType, object parameter, string language)
    {
        if (value is not string text)
        {
            return value;
        }

        var decimalIndex = text.IndexOf('.', StringComparison.Ordinal);
        if (decimalIndex < 0)
        {
            return text;
        }

        var fractionEnd = decimalIndex + 1;
        while (fractionEnd < text.Length && char.IsAsciiDigit(text[fractionEnd]))
        {
            fractionEnd++;
        }

        if (fractionEnd == decimalIndex + 1)
        {
            return text;
        }

        var compactEnd = fractionEnd;
        while (compactEnd > decimalIndex + 1 && text[compactEnd - 1] == '0')
        {
            compactEnd--;
        }

        if (compactEnd == decimalIndex + 1)
        {
            compactEnd = decimalIndex;
        }

        var number = text[..compactEnd];
        if (decimalIndex == 0)
        {
            number = "0" + number;
        }
        else if (decimalIndex == 1 && text[0] is '-' or '+')
        {
            number = text[..1] + "0" + number[1..];
        }

        return number + text[fractionEnd..];
    }

    /// <inheritdoc/>
    public object ConvertBack(object value, Type targetType, object parameter, string language)
        => throw new NotSupportedException("Compact number display is a one-way presentation conversion.");
}
