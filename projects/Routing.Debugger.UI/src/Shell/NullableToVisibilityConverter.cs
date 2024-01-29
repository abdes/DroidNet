// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.Shell;

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Data;

public class NullableToVisibilityConverter : IValueConverter
{
    private Visibility NullValue { get; set; } = Visibility.Collapsed;

    private Visibility NotNullValue { get; set; } = Visibility.Visible;

    public object Convert(object? value, Type targetType, object parameter, string language)
        => value == null ? this.NullValue : this.NotNullValue;

    public object ConvertBack(object value, Type targetType, object parameter, string language)
        => throw new NotImplementedException();
}
