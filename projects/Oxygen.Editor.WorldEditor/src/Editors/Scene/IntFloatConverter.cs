// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Data;

namespace Oxygen.Editor.WorldEditor.Editors.Scene;

/// <summary>
/// Converter to allow binding an integer (ViewModel) to a float-valued NumberBox.NumberValue.
/// Converts int -> float (Convert) and float -> int (ConvertBack), clamping integer results.
/// </summary>
public sealed class IntFloatConverter : IValueConverter
{
    public object Convert(object value, Type targetType, object parameter, string language)
    {
        if (value is int i) return (float)i;
        if (value is long l) return (float)l;
        if (value is double d) return (float)d;
        if (value is float f) return f;
        return 0f;
    }

    public object ConvertBack(object value, Type targetType, object parameter, string language)
    {
        if (value is float f)
        {
            var rounded = (int)Math.Round(f);
            return rounded;
        }

        if (value is double d)
        {
            return (int)Math.Round(d);
        }

        if (value is string s && int.TryParse(s, out var parsed))
        {
            return parsed;
        }

        return 0;
    }
}
