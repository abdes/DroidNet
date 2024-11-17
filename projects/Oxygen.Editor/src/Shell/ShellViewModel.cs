// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Input;
using DroidNet.Routing;
using DroidNet.Routing.WinUI;
using Microsoft.UI.Xaml;

namespace Oxygen.Editor.Shell;

/// <summary>The ViewModel for the application main window's shell.</summary>
public partial class ShellViewModel : AbstractOutletContainer
{
    private readonly IRouter router;

    /// <summary>
    /// Initializes a new instance of the <see cref="ShellViewModel"/> class.
    /// </summary>
    /// <param name="router"></param>
    public ShellViewModel(IRouter router)
    {
        this.router = router;
        this.Outlets.Add(OutletName.Primary, (nameof(this.ContentViewModel), null));
    }

    public object? ContentViewModel => this.Outlets[OutletName.Primary].viewModel;

    [RelayCommand]
    private static void OnMenuFileExit() => Application.Current.Exit();

    [RelayCommand]
    private void MenuViewsProjectBrowser() => this.router.Navigate("/pb/home");
}
