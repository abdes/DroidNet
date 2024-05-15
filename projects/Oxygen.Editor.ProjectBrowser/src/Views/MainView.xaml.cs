// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Views;

using System.Diagnostics;
using DroidNet.Hosting.Generators;
using DroidNet.Mvvm.Generators;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Data;
using Oxygen.Editor.ProjectBrowser.ViewModels;

/// <summary>
/// The project browser main view; which uses a <see cref="NavigationView" />, but instead of relying on a <see cref="Frame" />
/// for page navigation, it uses the application router.
/// </summary>
[ViewModel(typeof(MainViewModel))]
[InjectAs(ServiceLifetime.Singleton)]
public sealed partial class MainView
{
    private const string IndexToNavigationItemConverterKey = "IndexToNavigationItemConverter";

    public MainView()
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

    private sealed class IndexToNavigationItemConverter(MainView routedNavigationView) : IValueConverter
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
