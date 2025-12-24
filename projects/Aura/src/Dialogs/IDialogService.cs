// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI;

namespace DroidNet.Aura.Dialogs;

/// <summary>
///     Defines a WinUI 3 dialog service that can be used from ViewModels without exposing WinUI XamlRoot details.
/// </summary>
public interface IDialogService
{
    /// <summary>
    ///     Shows a simple dialog owned by the currently active Aura window.
    /// </summary>
    /// <param name="dialog">The dialog specification.</param>
    /// <param name="cancellationToken">A token that cancels the dialog operation.</param>
    /// <returns>The button that dismissed the dialog.</returns>
    public Task<DialogButton> ShowAsync(DialogSpec dialog, CancellationToken cancellationToken = default);

    /// <summary>
    ///     Shows a simple dialog owned by a specific Aura window.
    /// </summary>
    /// <param name="dialog">The dialog specification.</param>
    /// <param name="ownerWindowId">The owner window id.</param>
    /// <param name="cancellationToken">A token that cancels the dialog operation.</param>
    /// <returns>The button that dismissed the dialog.</returns>
    public Task<DialogButton> ShowAsync(DialogSpec dialog, WindowId ownerWindowId, CancellationToken cancellationToken = default);

    /// <summary>
    ///     Shows a dialog whose content is resolved from a ViewModel using the application-wide <c>VmToViewConverter</c>.
    ///     The dialog is owned by the currently active Aura window.
    /// </summary>
    /// <param name="dialog">The dialog specification.</param>
    /// <param name="cancellationToken">A token that cancels the dialog operation.</param>
    /// <returns>The button that dismissed the dialog.</returns>
    public Task<DialogButton> ShowAsync(ViewModelDialogSpec dialog, CancellationToken cancellationToken = default);

    /// <summary>
    ///     Shows a dialog whose content is resolved from a ViewModel using the application-wide <c>VmToViewConverter</c>.
    ///     The dialog is owned by a specific Aura window.
    /// </summary>
    /// <param name="dialog">The dialog specification.</param>
    /// <param name="ownerWindowId">The owner window id.</param>
    /// <param name="cancellationToken">A token that cancels the dialog operation.</param>
    /// <returns>The button that dismissed the dialog.</returns>
    public Task<DialogButton> ShowAsync(ViewModelDialogSpec dialog, WindowId ownerWindowId, CancellationToken cancellationToken = default);

    /// <summary>
    ///     Shows an informational dialog with a single close button.
    /// </summary>
    /// <param name="title">The title of the dialog.</param>
    /// <param name="message">The message to display in the dialog.</param>
    /// <param name="cancellationToken">A token that cancels the dialog operation.</param>
    /// <returns>A task that represents the asynchronous operation.</returns>
    public Task ShowMessageAsync(string title, string message, CancellationToken cancellationToken = default);

    /// <summary>
    ///     Shows a confirmation dialog and returns <see langword="true"/> when the user selects the primary button.
    /// </summary>
    /// <param name="title">The title of the dialog.</param>
    /// <param name="message">The message to display in the dialog.</param>
    /// <param name="cancellationToken">A token that cancels the dialog operation.</param>
    /// <returns>A task that returns <see langword="true"/> if the user selects the primary button; otherwise, <see langword="false"/>.</returns>
    public Task<bool> ConfirmAsync(string title, string message, CancellationToken cancellationToken = default);
}
