// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.Shell;

/// <summary>
/// A ViewModel fpr the debugger shell.
/// </summary>
public partial class ShellViewModel : AbstractOutletContainer
{
    /// <summary>
    /// Initializes a new instance of the <see cref="ShellViewModel" /> class.
    /// </summary>
    /// <param name="router">The application router; usually injected via DI.</param>
    public ShellViewModel(IRouter router)
    {
        this.Router = router;
        this.Outlets.Add("dock", (nameof(this.DockViewModel), null));
    }

    public IRouter Router { get; }

    public object? DockViewModel => this.Outlets["dock"].viewModel;
}
