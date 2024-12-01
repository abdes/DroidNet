// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using DroidNet.Converters;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.ProjectBrowser.ViewModels;

namespace Oxygen.Editor.ProjectBrowser.Views;

/// <summary>
/// The project browser main view; which uses a <see cref="NavigationView" />, but instead of relying on a <see cref="Frame" />
/// for page navigation, it uses the application router.
/// </summary>
[ViewModel(typeof(MainViewModel))]
public sealed partial class MainView
{
    private const string IndexToNavigationItemConverterKey = "IndexToNavigationItemConverter";

    /// <summary>
    /// Initializes a new instance of the <see cref="MainView"/> class.
    /// </summary>
    public MainView()
    {
        this.InitializeComponent();

        this.Resources[IndexToNavigationItemConverterKey] = new IndexToNavigationItemConverter(
            this.NavigationView,
            () => this.ViewModel!.AllItems.Cast<object>().ToList());
    }

    private async void OnSelectionChanged(NavigationView sender, NavigationViewSelectionChangedEventArgs args)
    {
        _ = sender; // unused

        Debug.Assert(
            this.ViewModel is not null,
            "Expecting the ViewModel to have been already set before selection changed events are fired from a navigation view.");

        if (args.IsSettingsSelected)
        {
            await this.ViewModel.NavigateToSettingsAsync().ConfigureAwait(true);
        }
        else if (args.SelectedItem is not null)
        {
            await this.ViewModel.NavigateToItemAsync((NavigationItem)args.SelectedItem).ConfigureAwait(true);
        }
    }
}
