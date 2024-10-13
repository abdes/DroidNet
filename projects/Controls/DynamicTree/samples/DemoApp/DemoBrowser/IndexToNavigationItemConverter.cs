// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Demo.DemoBrowser;

using Microsoft.UI.Xaml.Data;

/// <summary>
/// A converter used to get a <see cref="NavigationItem" /> by its index in the items collection view.
/// </summary>
/// <param name="routedNavigationView">The view that contains the items collection.</param>
internal sealed partial class IndexToNavigationItemConverter(DemoBrowserView routedNavigationView) : IValueConverter
{
    public object? Convert(object? value, Type targetType, object? parameter, string language)
    {
        if (value is not int index || index == -1 || routedNavigationView.ViewModel is null)
        {
            return null;
        }

        if (index == routedNavigationView.ViewModel.SelectedItemIndex)
        {
            // Note that the SettingsItem may be null before the NavigationView is loaded.
            return routedNavigationView.SettingsItem;
        }

        return index < routedNavigationView.ViewModel.AllItems.Count
            ? routedNavigationView.ViewModel.AllItems[index]
            : null;
    }

    public object ConvertBack(object value, Type targetType, object? parameter, string language)
        => throw new InvalidOperationException(
            "Don't use IndexToNavigationItemConverter.ConvertBack; it's meaningless.");
}
