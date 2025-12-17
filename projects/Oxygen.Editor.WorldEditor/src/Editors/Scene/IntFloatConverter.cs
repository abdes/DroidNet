// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using Microsoft.UI.Xaml.Data;

namespace Oxygen.Editor.WorldEditor.Editors.Scene;

/// <summary>
/// Converter to allow binding an integer (ViewModel) to a float-valued NumberBox.NumberValue.
/// Converts int -> float (Convert) and float -> int (ConvertBack), clamping integer results.
/// </summary>
public sealed partial class IntFloatConverter : IValueConverter
{
    /// <inheritdoc/>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Style", "IDE0046:Convert to conditional expression", Justification = "code clarity")]
    public object Convert(object value, Type targetType, object parameter, string language)
    {
        if (value is int i)
        {
            return (float)i;
        }

        if (value is long l)
        {
            return (float)l;
        }

        if (value is double d)
        {
            return (float)d;
        }

        if (value is float f)
        {
            return f;
        }

        return 0f;
    }

    /// <inheritdoc/>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Style", "IDE0046:Convert to conditional expression", Justification = "code clarity")]
    public object ConvertBack(object value, Type targetType, object parameter, string language)
    {
        if (value is float f)
        {
            return (int)Math.Round(f);
        }

        if (value is double d)
        {
            return (int)Math.Round(d);
        }

        if (value is string s && int.TryParse(s, NumberStyles.Integer, CultureInfo.CurrentCulture, out var parsed))
        {
            return parsed;
        }

        return 0;
    }
}
