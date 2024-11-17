// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Routing;
using DroidNet.Routing.WinUI;

namespace Oxygen.Editor.ProjectBrowser.ViewModels;

/// <summary>
/// The view model for the start screen. Mainly responsible for the navigation
/// between the different views in the project browser start screen.
/// </summary>
/// <param name="router">The application router to use when navigating.</param>
public partial class MainViewModel(IRouter router) : ObservableObject, IOutletContainer, IRoutingAware
{
    private const int InvalidItemIndex = -1;
    private const int SettingsItemIndex = int.MaxValue;
    private const string SettingsItemPath = "settings";

    [ObservableProperty]
    private object? currentNavigation;

    [ObservableProperty]
    private int selectedItemIndex = InvalidItemIndex;

    public IList<NavigationItem> NavigationItems { get; } =
    [
        new("home", "Home", "\uE80F", "H", typeof(HomeViewModel)),
        new("new", "New", "\uE8A5", "N", typeof(NewProjectViewModel)),
        new("open", "Open", "\uE8B7", "O", typeof(OpenProjectViewModel)),
    ];

    public IList<NavigationItem> AllItems => [.. this.NavigationItems];

    /// <inheritdoc/>
    public IActiveRoute? ActiveRoute { get; set; }

    public bool IsSettingsSelected => this.SelectedItemIndex == SettingsItemIndex;

    /// <inheritdoc/>
    public void LoadContent(object viewModel, OutletName? outletName = null)
    {
        var viewModelType = viewModel.GetType();

        Debug.WriteLine($"Navigate to Page: {viewModelType.FullName}");

        /* TODO: uncomment after the settings page is added
        if (viewModelType == typeof(SettingsViewModel))
        {
            this.SelectedItemIndex = SettingsItemIndex;
            this.CurrentNavigation = viewModel;
            return;
        }
        */

        var (index, navItem) = this.FindNavigationItem(item => item.TargetViewModel == viewModelType);
        if (navItem != null)
        {
            this.SelectedItemIndex = index;
            this.CurrentNavigation = viewModel;
        }
        else
        {
            Debug.WriteLine($"No navigation item was found for view model type: {viewModelType}");
        }
    }

    [RelayCommand]
    internal void NavigateToItem(NavigationItem requestedItem)
    {
        var (index, navItem) = this.FindNavigationItem(item => item == requestedItem);
        if (navItem is null)
        {
            Debug.WriteLine($"Navigation item is unknown to me: {requestedItem.Path}");
            return;
        }

        if (index != this.SelectedItemIndex)
        {
            // Avoid navigation if the selected item is same than before
            router.Navigate(navItem.Path, new PartialNavigation() { RelativeTo = this.ActiveRoute });
        }
    }

    [RelayCommand]
    internal void NavigateToSettings()
    {
        if (!this.IsSettingsSelected)
        {
            // Avoid navigation if the selected item is same than before
            router.Navigate(SettingsItemPath, new PartialNavigation() { RelativeTo = this.ActiveRoute });
        }
    }

    private (int index, NavigationItem? item) FindNavigationItem(Predicate<NavigationItem> match)
    {
        for (var index = 0; index < this.AllItems.Count; ++index)
        {
            var item = this.AllItems[index];
            if (match.Invoke(item))
            {
                return (index, item);
            }
        }

        return (InvalidItemIndex, default);
    }
}
