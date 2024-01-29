// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.Shell;

using DroidNet.Docking;
using Microsoft.UI.Xaml.Data;

public class DockGroupOrientationConverter : IValueConverter
{
    public object Convert(object value, Type targetType, object parameter, string language)
    {
        if (value is Orientation orientation)
        {
            return orientation == Orientation.Horizontal
                ? Microsoft.UI.Xaml.Controls.Orientation.Horizontal
                : Microsoft.UI.Xaml.Controls.Orientation.Vertical;
        }

        throw new ArgumentException($"value to convert must be a {typeof(Orientation).FullName}", nameof(value));
    }

    public object ConvertBack(object value, Type targetType, object parameter, string language)
        => throw new NotImplementedException();
}
