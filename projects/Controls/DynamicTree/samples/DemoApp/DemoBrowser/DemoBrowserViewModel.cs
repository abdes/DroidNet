// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Demo.DemoBrowser;

using System.Diagnostics;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Controls.Demo.DynamicTree;
using DroidNet.Hosting.Generators;
using DroidNet.Routing;
using Microsoft.Extensions.DependencyInjection;

/// <summary>
/// The view model for the <see cref="DemoBrowserView" /> view. Mainly responsible for the navigation between the different demos
/// in the project.
/// </summary>
/// <param name="router">The application router to use when navigating.</param>
[InjectAs(ServiceLifetime.Singleton)]
public partial class DemoBrowserViewModel(IRouter router) : ObservableObject, IOutletContainer, IRoutingAware
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
        new("dynamic-tree", "Dynamic Tree", typeof(ProjectLayoutViewModel)),
    ];

    public IList<NavigationItem> AllItems => [.. this.NavigationItems];

    public IActiveRoute? ActiveRoute { get; set; }

    public bool IsSettingsSelected => this.SelectedItemIndex == SettingsItemIndex;

    public void LoadContent(object viewModel, OutletName? outletName = null)
    {
        var viewModelType = viewModel.GetType();

        Debug.WriteLine($"Navigate to Page: {viewModelType.FullName}");

        /* TODO: uncomment if  the settings page is added
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
