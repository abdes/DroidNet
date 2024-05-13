// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Shell;

using CommunityToolkit.Mvvm.Input;
using DroidNet.Hosting.Generators;
using DroidNet.Routing;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.UI.Xaml;

/// <summary>The ViewModel for the application main window's shell.</summary>
[InjectAs(ServiceLifetime.Singleton)]
public partial class ShellViewModel : AbstractOutletContainer
{
    private readonly IRouter router;

    public ShellViewModel(IRouter router)
    {
        this.router = router;
        this.Outlets.Add(OutletName.Primary, (nameof(this.ContentViewModel), null));
    }

    public object? ContentViewModel => this.Outlets[OutletName.Primary].viewModel;

    [RelayCommand]
    private static void OnMenuFileExit() => Application.Current.Exit();

    [RelayCommand]
    private void OnMenuSettings()
        => this.router.Navigate("/settings");

    [RelayCommand]
    private void MenuViewsProjectBrowser()
        => this.router.Navigate("/pb/home");
}
