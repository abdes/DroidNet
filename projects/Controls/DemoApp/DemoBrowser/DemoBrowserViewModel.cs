// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Controls.Demo.DynamicTree;
using DroidNet.Controls.Demo.InPlaceEdit;
using DroidNet.Controls.Demo.Menus;
using DroidNet.Controls.Demo.OutputConsole;
using DroidNet.Controls.Demo.OutputLog;
using DroidNet.Controls.Demo.TabStrip;
using DroidNet.Controls.Demo.ToolBar;
using DroidNet.Controls.OutputLog;
using DroidNet.Routing;
using DroidNet.Routing.WinUI;

namespace DroidNet.Controls.Demo.DemoBrowser;

/// <summary>
/// The view model for the <see cref="DemoBrowserView"/> view. Mainly responsible for the navigation between the different demos
/// in the project.
/// </summary>
/// <param name="router">The application router to use when navigating.</param>
/// <param name="outputLogSink">A <see cref="DelegatingSink{T}"/> sink to be used for logs targeting the output log view.</param>
public partial class DemoBrowserViewModel(IRouter router, DelegatingSink<RichTextBlockSink> outputLogSink)
    : ObservableObject, IOutletContainer, IRoutingAware
{
    private const int InvalidItemIndex = -1;
    private const int SettingsItemIndex = int.MaxValue;
    private const string SettingsItemPath = "settings";

    private IActiveRoute? activeRoute;

    /// <summary>
    /// Gets or sets the current navigation object.
    /// </summary>
    [ObservableProperty]
    public partial object? CurrentNavigation { get; set; }

    /// <summary>
    /// Gets or sets the index of the selected navigation item.
    /// </summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(SelectedItem), nameof(IsSettingsSelected))]
    public partial int SelectedItemIndex { get; set; } = InvalidItemIndex;

    /// <summary>
    /// Gets the selected navigation item.
    /// </summary>
    /// <value>The selected navigation item, or <see langword="null"/> if no item is selected.</value>
    public NavigationItem? SelectedItem => this.SelectedItemIndex == InvalidItemIndex ? default : this.AllItems[this.SelectedItemIndex];

    /// <summary>
    /// Gets the list of navigation items.
    /// </summary>
    public IList<NavigationItem> NavigationItems { get; } =
    [
        new("in-place", "In-Place Edit", typeof(InPlaceEditDemoViewModel)),
        new("numberbox", "Number Box", typeof(NumberBoxDemoViewModel)),
        new("vectorbox", "VectorBox", typeof(VectorBoxDemoViewModel)),
        new("output-log", "Output Log", typeof(OutputLogDemoViewModel)),
        new("output-console", "Output Console", typeof(OutputConsoleDemoViewModel)),
        new("dynamic-tree", "Dynamic Tree", typeof(ProjectLayoutViewModel)),
        new("menubar", "MenuBar Demo", typeof(MenuBarDemoViewModel)),
        new("menuflyout", "MenuFlyout Demo", typeof(MenuFlyoutDemoViewModel)),
        new("menuitem", "MenuItem Demo", typeof(MenuItemDemoViewModel)),
        new("tabstrip", "TabStrip Demo", typeof(TabStripDemoViewModel)),
        new("toolbar", "ToolBar Demo", typeof(ToolBarDemoViewModel)),
    ];

    /// <summary>
    /// Gets the list of all navigation items.
    /// </summary>
    public IList<NavigationItem> AllItems => [.. this.NavigationItems];

    /// <summary>
    /// Gets a value indicating whether the settings item is selected.
    /// </summary>
    public bool IsSettingsSelected => this.SelectedItemIndex == SettingsItemIndex;

    /// <summary>
    /// Gets the output log sink.
    /// </summary>
    public DelegatingSink<RichTextBlockSink> OutputLogSink => outputLogSink;

    /// <inheritdoc/>
    public void LoadContent(object viewModel, OutletName? outletName = null)
    {
        var viewModelType = viewModel.GetType();

        Debug.WriteLine($"Navigate to Page: {viewModelType.FullName}");

        /* TODO: uncomment if the settings page is added
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

    /// <inheritdoc/>
    public Task OnNavigatedToAsync(IActiveRoute route, INavigationContext navigationContext)
    {
        this.activeRoute = route;
        return Task.CompletedTask;
    }

    /// <summary>
    /// Navigates to the specified navigation item.
    /// </summary>
    /// <param name="requestedItem">The navigation item to navigate to.</param>
    /// <returns>A <see cref="Task"/> representing the asynchronous operation.</returns>
    [RelayCommand]
    private async Task NavigateToItemAsync(NavigationItem requestedItem)
    {
        var (index, navItem) = this.FindNavigationItem(item => item == requestedItem);
        if (navItem is null)
        {
            Debug.WriteLine($"Navigation item is unknown to me: {requestedItem.Path}");
            return;
        }

        if (index != this.SelectedItemIndex)
        {
            // Avoid navigation if the selected item is the same as before
            await router.NavigateAsync(navItem.Path, new PartialNavigation() { RelativeTo = this.activeRoute }).ConfigureAwait(true);
        }
    }

    /// <summary>
    /// Navigates to the settings page.
    /// </summary>
    [RelayCommand]
    private async Task NavigateToSettingsAsync()
    {
        if (!this.IsSettingsSelected)
        {
            // Avoid navigation if the selected item is the same as before
            await router.NavigateAsync(SettingsItemPath, new PartialNavigation() { RelativeTo = this.activeRoute }).ConfigureAwait(true);
        }
    }

    /// <summary>
    /// Finds a navigation item that matches the specified predicate.
    /// </summary>
    /// <param name="match">The predicate to match the navigation item.</param>
    /// <returns>A tuple containing the index and the matched navigation item, or <see langword="null"/> if no match is found.</returns>
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
