// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Routing.WinUI;

namespace DroidNet.Routing.Demo.Shell;

/// <summary>
/// The ViewModel for the application main window shell.
/// </summary>
[System.Diagnostics.CodeAnalysis.SuppressMessage(
    "Maintainability",
    "CA1515:Consider making public types internal",
    Justification = "ViewModel classes must be public because the ViewModel property in the generated code for the view is public")]
public partial class ShellViewModel : AbstractOutletContainer
{
    /// <summary>
    /// Initializes a new instance of the <see cref="ShellViewModel"/> class.
    /// </summary>
    public ShellViewModel()
    {
        this.Outlets.Add(OutletName.Primary, (nameof(this.ContentViewModel), null));
    }

    /// <summary>
    /// Gets the content view model for the primary outlet.
    /// </summary>
    /// <value>
    /// The content view model for the primary outlet, or <c>null</c> if no view model is loaded.
    /// </value>
    public object? ContentViewModel => this.Outlets[OutletName.Primary].viewModel;
}
