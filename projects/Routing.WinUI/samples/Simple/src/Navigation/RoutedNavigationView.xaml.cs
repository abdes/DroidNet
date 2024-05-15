// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Samples.Simple.Navigation;

using System;
using System.Diagnostics;
using DroidNet.Hosting.Generators;
using DroidNet.Mvvm.Generators;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Data;

/// <summary>A replacement for the <see cref="NavigationView" /> control that does not require a <see cref="Frame" /> for better
/// MVVM compatibility and fewer dependencies on parent and siblings.</summary>
[ViewModel(typeof(RoutedNavigationViewModel))]
[InjectAs(ServiceLifetime.Singleton)]
public sealed partial class RoutedNavigationView
{
    private const string IndexToNavigationItemConverterKey = "IndexToNavigationItemConverter";

    public RoutedNavigationView()
    {
        this.InitializeComponent();

        this.Resources[IndexToNavigationItemConverterKey] = new IndexToNavigationItemConverter(this);
    }

    private void OnSelectionChanged(NavigationView sender, NavigationViewSelectionChangedEventArgs args)
    {
        _ = sender; // unused

        Debug.Assert(
            this.ViewModel is not null,
            "Expecting the ViewModel to have been already set before selection changed events are fired from a navigation view.");

        if (args.IsSettingsSelected)
        {
            this.ViewModel.NavigateToSettings();
        }
        else if (args.SelectedItem is not null)
        {
            this.ViewModel.NavigateToItem((NavigationItem)args.SelectedItem);
        }
    }

    private void OnNavigationViewLoaded(object sender, RoutedEventArgs args)
    {
        _ = sender; // unused
        _ = args; // unused

        Debug.Assert(
            this.ViewModel is not null,
            "Expecting the ViewModel to have been already set before selection changed events are fired from a navigation view.");

        // Check if the selected item should be the SettingsItem and update it after the NavigationView is loaded.
        if (this.ViewModel.IsSettingsSelected)
        {
            this.NavigationView.SelectedItem = this.NavigationView.SettingsItem;
        }
    }

    private sealed class IndexToNavigationItemConverter(RoutedNavigationView routedNavigationView) : IValueConverter
    {
        public object? Convert(object? value, Type targetType, object? parameter, string language)
        {
            if (value is not int index || index == -1 || routedNavigationView.ViewModel is null)
            {
                return null;
            }

            if (index == int.MaxValue)
            {
                // Note that the SettingsItem may be null before the NavigationView is loaded.
                return routedNavigationView.NavigationView.SettingsItem;
            }

            return index < routedNavigationView.ViewModel.AllItems.Count
                ? routedNavigationView.ViewModel.AllItems[index]
                : null;
        }

        public object ConvertBack(object value, Type targetType, object? parameter, string language)
            => throw new InvalidOperationException(
                "Don't use IndexToNavigationItemConverter.ConvertBack; it's meaningless.");
    }
}
