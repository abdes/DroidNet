// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Aura.Dialogs;

/// <summary>
///     Describes a dialog whose content is produced from a ViewModel by the application-wide
///     <c>VmToViewConverter</c> resource.
/// </summary>
/// <param name="Title">The dialog title.</param>
/// <param name="ViewModel">The view model to convert to a view.</param>
public sealed record ViewModelDialogSpec(string Title, object ViewModel)
{
    /// <summary>
    ///     Gets the primary button text.
    /// </summary>
    public string PrimaryButtonText { get; init; } = string.Empty;

    /// <summary>
    ///     Gets the secondary button text.
    /// </summary>
    public string SecondaryButtonText { get; init; } = string.Empty;

    /// <summary>
    ///     Gets the close button text.
    /// </summary>
    public string CloseButtonText { get; init; } = "Close";

    /// <summary>
    ///     Gets the default button.
    /// </summary>
    public DialogButton DefaultButton { get; init; } = DialogButton.Close;
}
