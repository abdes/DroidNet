// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Samples.Simple.Shell;

using DroidNet.Routing;

/// <summary>
/// The ViewModel for the application main window shell.
/// </summary>
public partial class ShellViewModel : AbstractOutletContainer
{
    public ShellViewModel() => this.Outlets.Add(OutletName.Primary, (nameof(this.ContentViewModel), null));

    public object? ContentViewModel => this.Outlets[OutletName.Primary].viewModel;
}
