// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Routing.WinUI;

namespace DroidNet.Routing.Demo.Navigation;

/// <summary>
/// A ViewModel for a page with a NavigationView control, which relies on a <see cref="IRouter">router</see> to navigate between
/// pages. A view for this view model does not need to use (and should not use) a Frame for the page navigation. Frame can only
/// work if it creates the Page instances itself, breaking the MVVM model.
/// </summary>
/// <param name="router">The router to use for navigation.</param>
[System.Diagnostics.CodeAnalysis.SuppressMessage(
    "Maintainability",
    "CA1515:Consider making public types internal",
    Justification = "ViewModel classes must be public because the ViewModel property in the generated code for the view is public")]
public partial class RoutedNavigationViewModel(IRouter router) : ObservableObject, IOutletContainer, IRoutingAware
{
    private const int InvalidItemIndex = -1;
    private const int SettingsItemIndex = int.MaxValue;
    private const string SettingsItemPath = "settings";

    private IActiveRoute? activeRoute;

    [ObservableProperty]
    private object? currentNavigation;

    [ObservableProperty]
    private int selectedItemIndex = InvalidItemIndex;

    /// <summary>
    /// Gets the list of navigation items.
    /// </summary>
    public IList<NavigationItem> NavigationItems { get; } =
    [
        new("1", "One", "\uf146", "1", typeof(PageOneViewModel)),
        new("2", "Two", "\uf147", "2", typeof(PageTwoViewModel)),
    ];

    /// <summary>
    /// Gets the list of footer items.
    /// </summary>
    public IList<NavigationItem> FooterItems { get; } =
    [
        new("3", "Three", "\uf148", "3", typeof(PageThreeViewModel)),
    ];

    /// <summary>
    /// Gets the list of all navigation items, including footer items.
    /// </summary>
    public IList<NavigationItem> AllItems => [.. this.NavigationItems, .. this.FooterItems];

    /// <summary>
    /// Gets a value indicating whether the settings item is selected.
    /// </summary>
    /// <value>
    /// <see langword="true"/> if the settings item is selected; otherwise, <see langword="false"/>.
    /// </value>
    public bool IsSettingsSelected => this.SelectedItemIndex == SettingsItemIndex;

    /// <inheritdoc/>
    public Task OnNavigatedToAsync(IActiveRoute route, INavigationContext navigationContext)
    {
        this.activeRoute = route;
        return Task.CompletedTask;
    }

    /// <inheritdoc/>
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

    /// <summary>
    /// Navigates to the specified navigation item.
    /// </summary>
    /// <param name="requestedItem">The navigation item to navigate to.</param>
    /// <remarks>
    /// This method finds the index of the requested navigation item and navigates to it if it is
    /// not already selected. If the item is unknown, a debug message is logged.
    /// </remarks>
    /// <returns>A <see cref="Task"/> representing the asynchronous operation.</returns>
    [RelayCommand]
    internal async Task NavigateToItemAsync(NavigationItem requestedItem)
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
            await router.NavigateAsync(navItem.Path, new PartialNavigation() { RelativeTo = this.activeRoute }).ConfigureAwait(true);
        }
    }

    /// <summary>
    /// Navigates to the settings page.
    /// </summary>
    /// <remarks>
    /// This method checks if the settings item is already selected. If not, it navigates to the
    /// settings page using the router. This avoids unnecessary navigation if the settings item is
    /// already selected.
    /// </remarks>
    /// <returns>A <see cref="Task"/> representing the asynchronous operation.</returns>
    [RelayCommand]
    internal async Task NavigateToSettingsAsync()
    {
        if (!this.IsSettingsSelected)
        {
            // Avoid navigation if the selected item is same than before
            await router.NavigateAsync(SettingsItemPath, new PartialNavigation() { RelativeTo = this.activeRoute }).ConfigureAwait(true);
        }
    }

    /// <summary>
    /// Finds a navigation item that matches the specified predicate.
    /// </summary>
    /// <param name="match">The predicate to match against navigation items.</param>
    /// <returns>
    /// A tuple containing the index of the matched navigation item and the navigation item itself.
    /// If no item is found, returns a tuple with an index of -1 and a null item.
    /// </returns>
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
