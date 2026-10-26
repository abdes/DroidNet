// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Aura.Dialogs;

/// <summary>
///     Describes a dialog to be displayed by the <see cref="IDialogService"/>, in terms a view model can provide.
/// </summary>
/// <remarks>
///     The dialog's content is <see cref="Content"/> when it is set; otherwise, it is the view of
///     <see cref="ViewModel"/>, which the service resolves with its view locator. The predicates
///     <see cref="IsPrimaryButtonEnabled"/> and <see cref="HasModifiedPrimaryButtonOptions"/> are evaluated when the
///     dialog opens, after the primary action, and whenever <see cref="ViewModel"/> or
///     <see cref="PrimaryButtonOptions"/> raises <see cref="System.ComponentModel.INotifyPropertyChanged.PropertyChanged"/>.
/// </remarks>
/// <param name="Title">The dialog title.</param>
/// <param name="Content">
///     The dialog content: a string or a UI element; <see langword="null"/> to show the view of <see cref="ViewModel"/>.
/// </param>
public sealed record DialogSpec(string Title, object? Content)
{
    /// <summary>
    ///     Gets the primary button text. The primary button is hidden when the text is empty.
    /// </summary>
    public string PrimaryButtonText { get; init; } = string.Empty;

    /// <summary>
    ///     Gets the secondary button text. The secondary button is hidden when the text is empty.
    /// </summary>
    public string SecondaryButtonText { get; init; } = string.Empty;

    /// <summary>
    ///     Gets the close button text. The close button is hidden when the text is empty.
    /// </summary>
    public string CloseButtonText { get; init; } = "Close";

    /// <summary>
    ///     Gets the button that Enter invokes and that is drawn as the accent button; <see cref="DialogButton.None"/>
    ///     for none.
    /// </summary>
    public DialogButton DefaultButton { get; init; } = DialogButton.Close;

    /// <summary>
    ///     Gets the dialog's view model: the data context of its content and footer, and, when <see cref="Content"/> is
    ///     <see langword="null"/>, the view model whose view is the dialog's content.
    /// </summary>
    public object? ViewModel { get; init; }

    /// <summary>
    ///     Gets an optional asynchronous primary action. Returning <see langword="true"/> closes the dialog with
    ///     <see cref="DialogButton.Primary"/>; returning <see langword="false"/> keeps it open.
    /// </summary>
    /// <remarks>
    ///     While the action runs, the primary and secondary buttons are disabled and the user cannot dismiss the dialog.
    ///     The action receives the token passed to <see cref="IDialogService.ShowAsync(DialogSpec, CancellationToken)"/>;
    ///     when that token is cancelled, the dialog closes once the action returns, and the service reports the
    ///     cancellation unless the action returned <see langword="true"/>. An exception the action throws closes the
    ///     dialog and is rethrown by the service.
    /// </remarks>
    public Func<CancellationToken, Task<bool>>? PrimaryAction { get; init; }

    /// <summary>
    ///     Gets an optional predicate that enables the primary button, such as a view model's can-accept property.
    ///     Without it, the primary button is enabled.
    /// </summary>
    public Func<bool>? IsPrimaryButtonEnabled { get; init; }

    /// <summary>
    ///     Gets optional options of the primary button: a UI element, or a view model whose view the service resolves.
    ///     The dialog then shows a chevron beside the primary button that opens them in a flyout. The chevron stays
    ///     enabled while the primary button is disabled, so the options can still be changed.
    /// </summary>
    public object? PrimaryButtonOptions { get; init; }

    /// <summary>
    ///     Gets an optional predicate that marks the chevron of <see cref="PrimaryButtonOptions"/> with a dot, such as
    ///     when the options differ from their defaults.
    /// </summary>
    public Func<bool>? HasModifiedPrimaryButtonOptions { get; init; }

    /// <summary>
    ///     Gets the commands shown on the left of the button bar, in reading order, such as commands that act on the
    ///     dialog's content rather than close it.
    /// </summary>
    public IReadOnlyList<DialogCommand> FooterCommands { get; init; } = [];

    /// <summary>
    ///     Gets an optional maximum dialog width, in DIPs, for content wider than the default dialog. The dialog still
    ///     never exceeds its window.
    /// </summary>
    public double? MaxWidth { get; init; }

    /// <summary>
    ///     Gets an optional maximum dialog height, in DIPs, for content taller than the default dialog. The dialog still
    ///     never exceeds its window.
    /// </summary>
    public double? MaxHeight { get; init; }
}
