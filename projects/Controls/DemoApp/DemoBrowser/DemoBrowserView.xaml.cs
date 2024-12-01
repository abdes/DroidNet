// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using DroidNet.Converters;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls.Demo.DemoBrowser;

/// <summary>The view for the application's main window shell.</summary>
[ViewModel(typeof(DemoBrowserViewModel))]
public sealed partial class DemoBrowserView
{
    private const string IndexToNavigationItemConverterKey = "IndexToNavigationItemConverter";

    /// <summary>
    /// Initializes a new instance of the <see cref="DemoBrowserView"/> class.
    /// </summary>
    public DemoBrowserView()
    {
        this.InitializeComponent();

        this.Resources[IndexToNavigationItemConverterKey]
            = new IndexToNavigationItemConverter(
                this.NavigationView,
                () => this.ViewModel!.AllItems.Cast<object>().ToList());

        this.NavigationView.DisplayModeChanged += this.OnPaneDisplayModeChanged;
    }

    /// <summary>
    /// Gets the settings item from the navigation view.
    /// </summary>
    public object? SettingsItem => this.NavigationView.SettingsItem;

    /// <summary>
    /// Converts the NavigationViewPaneDisplayMode to Visibility.
    /// </summary>
    /// <param name="mode">The NavigationViewPaneDisplayMode.</param>
    /// <returns>Visibility.Visible if the mode is LeftMinimal, otherwise Visibility.Collapsed.</returns>
    private static Visibility GetHeaderSpacerVisibility(NavigationViewPaneDisplayMode mode) => mode == NavigationViewPaneDisplayMode.LeftMinimal ? Visibility.Visible : Visibility.Collapsed;

    private void OnPaneDisplayModeChanged(NavigationView sender, object args) => this.Header.Margin = new Thickness(
            sender.DisplayMode == NavigationViewDisplayMode.Minimal ? 50 : 8, 8, 8, 8);

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
}
