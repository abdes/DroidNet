// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Data;

namespace DroidNet.Samples.Tests;

public partial class NullToVisibilityConverter : IValueConverter
{
    public object Convert(object? value, Type targetType, object? parameter, string language)
    {
        var invisibility = Visibility.Collapsed;

        if (parameter is string preferredInvisibility && Enum.TryParse(
                typeof(Visibility),
                preferredInvisibility,
                ignoreCase: true,
                out var result))
        {
            invisibility = (Visibility)result;
        }

        return value == null ? invisibility : Visibility.Visible;
    }

    public object ConvertBack(object value, Type targetType, object? parameter, string language)
        => throw new InvalidOperationException("Don't use NullToVisibilityConverter.ConvertBack; it's meaningless.");
}
