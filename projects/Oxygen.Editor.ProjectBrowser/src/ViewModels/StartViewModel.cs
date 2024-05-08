// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.ViewModels;

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Microsoft.UI.Xaml.Controls;

/// <summary>
/// The view model for the start screen. Mainly responsible for the navigation
/// between the different views in the project browser start screen.
/// </summary>
public partial class StartViewModel : ObservableObject
{
    [ObservableProperty]
    private Type currentNavigation;

    [ObservableProperty]
    private List<NavigationItem> navigationItems = new()
    {
        new NavigationItem("Home", Symbol.Home, "H", typeof(StartHomeViewModel)),
        new NavigationItem("New", Symbol.Document, "N", typeof(StartNewViewModel)),
        new NavigationItem("Open", Symbol.Folder, "O", typeof(StartOpenViewModel)),
    };

    [ObservableProperty]
    private NavigationItem selectedItem;

    /// <summary>Initializes a new instance of the <see cref="StartViewModel" /> class.</summary>
    public StartViewModel()
    {
        this.SelectedItem = this.navigationItems[0];
        this.CurrentNavigation = this.navigationItems[0].TargetViewModel;
    }

    [RelayCommand]
    private void ShowHomePage() => this.ShowPageByName("Home");

    [RelayCommand]
    private void ShowNewPage() => this.ShowPageByName("New");

    [RelayCommand]
    private void ShowOpenPage() => this.ShowPageByName("Open");

    private void ShowPageByName(string name)
    {
        var menuItem = this.NavigationItems.Find(item => string.Equals(item.Text, name, StringComparison.Ordinal));
        if (menuItem != null)
        {
            this.ShowPageForMenuItem(menuItem);
        }
    }

    [RelayCommand]
    private void ShowPageForMenuItem(NavigationItem item)
    {
        this.SelectedItem = item;
        this.CurrentNavigation = item.TargetViewModel;
    }
}
