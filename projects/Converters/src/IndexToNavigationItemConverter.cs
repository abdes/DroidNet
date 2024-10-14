// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Converters;

using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Data;
using static DroidNet.Converters.IndexToNavigationItemConverter;

/// <summary>
/// A converter to get the navigation item corresponding to the given index in the containing <see cref="NavigationView" />.
/// </summary>
/// <param name="routedNavigationView">The <see cref="NavigationView" /> containing the navigation items.</param>
/// <param name="navigationItems">The list of all navigation items.</param>
public sealed partial class IndexToNavigationItemConverter(
    NavigationView routedNavigationView,
    GetNavigationItems navigationItems)
    : IValueConverter
{
    public delegate IList<object> GetNavigationItems();

    public object? Convert(object? value, Type targetType, object? parameter, string language)
    {
        if (value is not int index || index == -1)
        {
            return null;
        }

        if (index == int.MaxValue)
        {
            // Note that the SettingsItem may be null before the NavigationView is loaded.
            return routedNavigationView.SettingsItem;
        }

        var items = navigationItems();
        return index < items.Count ? items[index] : null;
    }

    public object ConvertBack(object value, Type targetType, object? parameter, string language)
        => throw new InvalidOperationException(
            "Don't use IndexToNavigationItemConverter.ConvertBack; it's meaningless.");
}
