// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT).
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ViewModels;

using System.Windows.Input;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Navigation;
using Oxygen.Editor.Pages.Settings.ViewModels;
using Oxygen.Editor.ProjectBrowser.ViewModels;
using Oxygen.Editor.Services;

public partial class ShellViewModel : ObservableRecipient
{
    [ObservableProperty]
    private bool isBackEnabled;

    public ShellViewModel(INavigationService navigationService)
    {
        this.NavigationService = navigationService;
        this.NavigationService.Navigated += this.OnNavigated;

        this.MenuFileExitCommand = new RelayCommand(OnMenuFileExit);
        this.MenuSettingsCommand = new RelayCommand(this.OnMenuSettings);
        this.MenuViewsMainCommand = new RelayCommand(this.OnMenuViewsMain);
        this.MenuViewsStartCommand = new RelayCommand(this.OnMenuViewsStart);
    }

    public ICommand MenuFileExitCommand
    {
        get;
    }

    public ICommand MenuSettingsCommand
    {
        get;
    }

    public ICommand MenuViewsMainCommand
    {
        get;
    }

    public ICommand MenuViewsStartCommand
    {
        get;
    }

    public INavigationService NavigationService
    {
        get;
    }

    private static void OnMenuFileExit() => Application.Current.Exit();

    private void OnNavigated(object sender, NavigationEventArgs e)
        => this.IsBackEnabled = this.NavigationService.CanGoBack;

    private void OnMenuSettings()
        => this.NavigationService.NavigateTo(typeof(SettingsViewModel).FullName!);

    private void OnMenuViewsMain()
        => this.NavigationService.NavigateTo(typeof(MainViewModel).FullName!);

    private void OnMenuViewsStart() => this.NavigationService.NavigateTo(typeof(StartViewModel).FullName!);
}
