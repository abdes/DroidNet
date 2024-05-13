// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Converters;

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Data;
using Oxygen.Editor.ProjectBrowser.Projects;

public class SelectedLocationConverter : IValueConverter
{
    public object Convert(object? value, Type targetType, object? parameter, string language)
    {
        if (value == null || parameter == null || value is not KnownLocation location)
        {
            return false;
        }

        if (!Enum.TryParse(typeof(KnownLocations), parameter.ToString(), ignoreCase: true, out var locationKey))
        {
            return false;
        }

        return (KnownLocations)locationKey == location.Key;
    }

    public object ConvertBack(object value, Type targetType, object parameter, string language)
        => DependencyProperty.UnsetValue;
}
