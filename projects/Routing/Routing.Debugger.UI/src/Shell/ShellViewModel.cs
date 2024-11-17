// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Routing.WinUI;

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

    /// <summary>
    /// Gets the application router.
    /// </summary>
    /// <remarks>
    /// The router is responsible for managing navigation within the application.
    /// It is typically injected via dependency injection and used to navigate
    /// between different view models and views.
    /// </remarks>
    public IRouter Router { get; }

    /// <summary>
    /// Gets the view model for the dock outlet.
    /// </summary>
    /// <remarks>
    /// The dock outlet is used to host the dock view model, which is typically
    /// responsible for managing the docked panels or tool windows within the
    /// debugger shell. This property retrieves the current view model loaded
    /// in the dock outlet, if any.
    /// </remarks>
    public object? DockViewModel => this.Outlets["dock"].viewModel;
}
