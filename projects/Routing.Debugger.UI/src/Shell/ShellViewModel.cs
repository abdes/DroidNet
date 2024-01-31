// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.Shell;

using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Routing.Debugger.UI;
using DroidNet.Routing.Debugger.UI.WorkSpace;
using DroidNet.Routing.UI.Contracts;

public partial class ShellViewModel : ObservableObject, IOutletContainer
{
    [ObservableProperty]
    private string? url;

    public WorkSpaceViewModel WorkSpace { get; } = new();

    public void LoadContent(object viewModel, string? outletName = null)
    {
        switch (outletName)
        {
            case null:
            case Router.Outlet.Primary:
            case DebuggerConstants.AppOutletName:
                this.WorkSpace.LoadApp(viewModel);
                break;

            default:
                this.WorkSpace.LoadDockable(viewModel, outletName);
                break;
        }
    }

    public object? GetViewModelForOutlet(string? outletName) => throw new NotImplementedException();

    public string GetPropertyNameForOutlet(string? outletName) => throw new NotImplementedException();
}
