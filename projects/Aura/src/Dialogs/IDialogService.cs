// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI;

namespace DroidNet.Aura.Dialogs;

/// <summary>
///     Defines a WinUI 3 dialog and picker service that can be used from view models without exposing WinUI XamlRoot
///     details.
/// </summary>
public interface IDialogService
{
    /// <summary>
    ///     Shows a dialog owned by the currently active Aura window. Dialogs of one window are shown one at a time, in
    ///     request order; dialogs of different windows are independent.
    /// </summary>
    /// <param name="dialog">The dialog specification.</param>
    /// <param name="cancellationToken">
    ///     A token that cancels the dialog: a dialog that has not closed yet closes, after its running primary action
    ///     returns, and the call throws <see cref="OperationCanceledException"/>. A dialog that already closed with a
    ///     button, or whose primary action returned <see langword="true"/>, reports that button.
    /// </param>
    /// <returns>
    ///     The button that dismissed the dialog: <see cref="DialogButton.Primary"/>, <see cref="DialogButton.Secondary"/>,
    ///     or <see cref="DialogButton.None"/> for the close button, Escape or a programmatic hide.
    /// </returns>
    /// <exception cref="DialogServiceException">No Aura window can own the dialog, or its view cannot be resolved.</exception>
    public Task<DialogButton> ShowAsync(DialogSpec dialog, CancellationToken cancellationToken = default);

    /// <summary>
    ///     Shows a dialog owned by a specific Aura window. Dialogs of one window are shown one at a time, in request
    ///     order; dialogs of different windows are independent.
    /// </summary>
    /// <param name="dialog">The dialog specification.</param>
    /// <param name="ownerWindowId">The owner window id.</param>
    /// <param name="cancellationToken">
    ///     A token that cancels the dialog, as described for <see cref="ShowAsync(DialogSpec, CancellationToken)"/>.
    /// </param>
    /// <returns>The button that dismissed the dialog.</returns>
    /// <exception cref="DialogServiceException">The window cannot own the dialog, or its view cannot be resolved.</exception>
    public Task<DialogButton> ShowAsync(DialogSpec dialog, WindowId ownerWindowId, CancellationToken cancellationToken = default);

    /// <summary>
    ///     Shows an informational dialog with a single OK button, owned by the currently active Aura window.
    /// </summary>
    /// <param name="title">The title of the dialog.</param>
    /// <param name="message">The message to display in the dialog.</param>
    /// <param name="cancellationToken">A token that cancels the dialog operation.</param>
    /// <returns>A task that completes when the dialog closes.</returns>
    public Task ShowMessageAsync(string title, string message, CancellationToken cancellationToken = default);

    /// <summary>
    ///     Shows an informational dialog with a single OK button, owned by a specific Aura window.
    /// </summary>
    /// <param name="title">The title of the dialog.</param>
    /// <param name="message">The message to display in the dialog.</param>
    /// <param name="ownerWindowId">The owner window id.</param>
    /// <param name="cancellationToken">A token that cancels the dialog operation.</param>
    /// <returns>A task that completes when the dialog closes.</returns>
    public Task ShowMessageAsync(string title, string message, WindowId ownerWindowId, CancellationToken cancellationToken = default);

    /// <summary>
    ///     Shows a confirmation dialog owned by the currently active Aura window.
    /// </summary>
    /// <param name="title">The title of the dialog.</param>
    /// <param name="message">The message to display in the dialog.</param>
    /// <param name="cancellationToken">A token that cancels the dialog operation.</param>
    /// <returns>A task that returns <see langword="true"/> if the user selects the primary button; otherwise, <see langword="false"/>.</returns>
    public Task<bool> ConfirmAsync(string title, string message, CancellationToken cancellationToken = default);

    /// <summary>
    ///     Shows a confirmation dialog owned by a specific Aura window.
    /// </summary>
    /// <param name="title">The title of the dialog.</param>
    /// <param name="message">The message to display in the dialog.</param>
    /// <param name="ownerWindowId">The owner window id.</param>
    /// <param name="cancellationToken">A token that cancels the dialog operation.</param>
    /// <returns>A task that returns <see langword="true"/> if the user selects the primary button; otherwise, <see langword="false"/>.</returns>
    public Task<bool> ConfirmAsync(string title, string message, WindowId ownerWindowId, CancellationToken cancellationToken = default);

    /// <summary>
    ///     Shows a configured folder picker owned by the currently active Aura window.
    /// </summary>
    /// <param name="picker">The title and location preferences; these do not restrict folder selection.</param>
    /// <param name="cancellationToken">A token checked before and after the native picker operation.</param>
    /// <returns>The selected folder path, or <see langword="null"/> when the user cancels.</returns>
    public Task<string?> PickFolderAsync(FolderPickerSpec picker, CancellationToken cancellationToken = default);

    /// <summary>Shows a configured folder picker owned by a specific Aura window.</summary>
    /// <param name="picker">The title and location preferences; these do not restrict folder selection.</param>
    /// <param name="ownerWindowId">The owner window id.</param>
    /// <param name="cancellationToken">A token checked before and after the native picker operation.</param>
    /// <returns>The selected folder path, or <see langword="null"/> when the user cancels.</returns>
    public Task<string?> PickFolderAsync(FolderPickerSpec picker, WindowId ownerWindowId, CancellationToken cancellationToken = default);

    /// <summary>Shows a configured file-open picker owned by the currently active Aura window.</summary>
    /// <param name="picker">The title, location preferences and labeled file types.</param>
    /// <param name="cancellationToken">A token checked before and after the native picker operation.</param>
    /// <returns>The selected file path, or <see langword="null"/> when the user cancels.</returns>
    public Task<string?> PickFileAsync(FilePickerSpec picker, CancellationToken cancellationToken = default);

    /// <summary>Shows a configured file-open picker owned by a specific Aura window.</summary>
    /// <param name="picker">The title, location preferences and labeled file types.</param>
    /// <param name="ownerWindowId">The owner window id.</param>
    /// <param name="cancellationToken">A token checked before and after the native picker operation.</param>
    /// <returns>The selected file path, or <see langword="null"/> when the user cancels.</returns>
    public Task<string?> PickFileAsync(FilePickerSpec picker, WindowId ownerWindowId, CancellationToken cancellationToken = default);

    /// <summary>Shows a configured file-open picker that selects several files, owned by the currently active Aura window.</summary>
    /// <param name="picker">The title, location preferences and labeled file types.</param>
    /// <param name="cancellationToken">A token checked before and after the native picker operation.</param>
    /// <returns>The selected file paths, in the picker's order; empty when the user cancels.</returns>
    public Task<IReadOnlyList<string>> PickFilesAsync(FilePickerSpec picker, CancellationToken cancellationToken = default);

    /// <summary>Shows a configured file-open picker that selects several files, owned by a specific Aura window.</summary>
    /// <param name="picker">The title, location preferences and labeled file types.</param>
    /// <param name="ownerWindowId">The owner window id.</param>
    /// <param name="cancellationToken">A token checked before and after the native picker operation.</param>
    /// <returns>The selected file paths, in the picker's order; empty when the user cancels.</returns>
    public Task<IReadOnlyList<string>> PickFilesAsync(FilePickerSpec picker, WindowId ownerWindowId, CancellationToken cancellationToken = default);

    /// <summary>Shows a configured file-save picker owned by the currently active Aura window.</summary>
    /// <param name="picker">The title, location preferences, suggested name and labeled file types.</param>
    /// <param name="cancellationToken">A token checked before and after the native picker operation.</param>
    /// <returns>The chosen file path, or <see langword="null"/> when the user cancels.</returns>
    public Task<string?> PickSaveFileAsync(FileSavePickerSpec picker, CancellationToken cancellationToken = default);

    /// <summary>Shows a configured file-save picker owned by a specific Aura window.</summary>
    /// <param name="picker">The title, location preferences, suggested name and labeled file types.</param>
    /// <param name="ownerWindowId">The owner window id.</param>
    /// <param name="cancellationToken">A token checked before and after the native picker operation.</param>
    /// <returns>The chosen file path, or <see langword="null"/> when the user cancels.</returns>
    public Task<string?> PickSaveFileAsync(FileSavePickerSpec picker, WindowId ownerWindowId, CancellationToken cancellationToken = default);
}
