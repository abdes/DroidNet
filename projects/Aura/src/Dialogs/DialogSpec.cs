// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Aura.Dialogs;

/// <summary>
///     Describes a simple WinUI 3 <see cref="ContentDialog"/> to be displayed by the <see cref="IDialogService"/>.
/// </summary>
/// <param name="Title">The dialog title.</param>
/// <param name="Content">The dialog content (typically a string or a XAML element).</param>
public sealed record DialogSpec(string Title, object? Content)
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
