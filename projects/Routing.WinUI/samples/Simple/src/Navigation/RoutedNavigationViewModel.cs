// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Samples.Simple.Navigation;

using System;
using System.Diagnostics;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Hosting.Generators;
using DroidNet.Routing;
using Microsoft.Extensions.DependencyInjection;

/// <summary>
/// A ViewModel for a page with a NavigationView control, which relies on a <see cref="IRouter">router</see> to navigate between
/// pages. A view for this view model does not need to use (and should not use) a Frame for the page navigation. Frame can only
/// work if it creates the Page instances itself, breaking the MVVM model.
/// </summary>
/// <param name="router">The router to use for navigation.</param>
[InjectAs(ServiceLifetime.Singleton)]
public partial class RoutedNavigationViewModel(IRouter router) : ObservableObject, IOutletContainer, IRoutingAware
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
        new NavigationItem("1", "One", "\uf146", "1", typeof(PageOneViewModel)),
        new NavigationItem("2", "Two", "\uf147", "2", typeof(PageTwoViewModel)),
    ];

    public IList<NavigationItem> FooterItems { get; } =
    [
        new NavigationItem("3", "Three", "\uf148", "3", typeof(PageThreeViewModel)),
    ];

    public IList<NavigationItem> AllItems => [.. this.NavigationItems, .. this.FooterItems];

    public IActiveRoute? ActiveRoute { get; set; }

    public bool IsSettingsSelected => this.SelectedItemIndex == SettingsItemIndex;

    public void LoadContent(object viewModel, OutletName? outletName = null)
    {
        var viewModelType = viewModel.GetType();

        Debug.WriteLine($"Navigate to Page: {viewModelType.FullName}");

        if (viewModelType == typeof(SettingsViewModel))
        {
            this.SelectedItemIndex = SettingsItemIndex;
            this.CurrentNavigation = viewModel;
        }
        else
        {
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
