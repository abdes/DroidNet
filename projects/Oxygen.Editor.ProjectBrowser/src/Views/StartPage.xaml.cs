// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ProjectBrowser.Views;

using System.ComponentModel;
using System.Diagnostics;
using CommunityToolkit.Mvvm.DependencyInjection;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Navigation;
using Oxygen.Editor.ProjectBrowser.ViewModels;

/// <summary>The project browser start page.</summary>
/// <para>
/// This is a container page that internally uses a simplified WinUI
/// <see cref="NavigationView" /> as its top level component. It is simplified in
/// the sense that we do not care about a navigation stack or about
/// collapsing/expanding the navigation panel.
/// </para>
/// <para>
/// Because the WinUI <see cref="NavigationView" /> is not really MVVM friendly,
/// specific adaptation logic need to happen to map the `page-oriented` navigation
/// model of WinUI to the `viewmodel-oriented` navigation of MVVM.
/// </para>
public sealed partial class StartPage
{
    private static readonly Dictionary<Type, Type> Mappings = new()
    {
        { typeof(StartHomeViewModel), typeof(StartHomePage) },
        { typeof(StartNewViewModel), typeof(StartNewPage) },
        { typeof(StartOpenViewModel), typeof(StartOpenPage) },
    };

    /// <summary>Initializes a new instance of the <see cref="StartPage" /> class.</summary>
    public StartPage()
    {
        this.InitializeComponent();

        this.ViewModel = Ioc.Default.GetRequiredService<StartViewModel>();
    }

    public StartViewModel ViewModel { get; private set; }

    protected override async void OnNavigatedTo(NavigationEventArgs e)
    {
        Debug.WriteLine("Navigation to StartPage.");

        this.ViewModel.PropertyChanged += this.OnCurrentNavigationChanged;

        // Trigger navigation to the ViewModel's current state
        this.NavigateToViewModel(this.ViewModel.CurrentNavigation);

        await Task.CompletedTask;
    }

    protected override async void OnNavigatedFrom(NavigationEventArgs e)
    {
        Debug.WriteLine("Navigation away from StartPage.");
        this.ViewModel.PropertyChanged -= this.OnCurrentNavigationChanged;
        await Task.CompletedTask;
    }

    private static Type PageForViewModel(Type viewModelType)
    {
        if (Mappings.TryGetValue(viewModelType, out var pageType))
        {
            return pageType;
        }

        throw new ArgumentException($"View type `{viewModelType}` is not mapped to any page", nameof(viewModelType));
    }

    // event handler
    private void OnCurrentNavigationChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (!string.Equals(args.PropertyName, nameof(this.ViewModel.CurrentNavigation), StringComparison.Ordinal))
        {
            return;
        }

        // Request the Frame to navigate the corresponding page.
        Debug.WriteLine("Start page CurrentNavigation has changed");
        this.NavigateToViewModel(this.ViewModel.CurrentNavigation);
    }

    private void NavigateToViewModel(Type? vmType)
    {
        if (vmType == null)
        {
            return;
        }

        Debug.WriteLine($"StartPage NavigateToViewModel {vmType.Name}");
        try
        {
            var pageType = PageForViewModel(vmType);
            _ = this.ContentFrame.Navigate(pageType);
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Navigation failed (and ignored): {ex.Message}");
        }
    }

    private void OnNavigationSelectionChanged(NavigationView sender, NavigationViewSelectionChangedEventArgs args)
    {
        Debug.Assert(
            !args.IsSettingsSelected,
            "Do not enable settings in this navigation view. Use the top level navigation for settings.");

        this.ViewModel.ShowPageForMenuItemCommand.Execute((NavigationItem)args.SelectedItem);
    }
}
