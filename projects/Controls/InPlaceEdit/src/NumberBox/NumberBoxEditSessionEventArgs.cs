// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>
///     Describes an interactive edit lifecycle event raised by <see cref="NumberBox" />.
/// </summary>
public sealed class NumberBoxEditSessionEventArgs : EventArgs
{
    /// <summary>
    ///     Initializes a new instance of the <see cref="NumberBoxEditSessionEventArgs"/> class.
    /// </summary>
    /// <param name="interactionKind">The input interaction that owns the edit.</param>
    /// <param name="completionKind">The completion kind when the event represents an edit completion.</param>
    /// <param name="inputText">The committed text, or <see langword="null"/> for non-text interactions.</param>
    public NumberBoxEditSessionEventArgs(
        NumberBoxEditInteractionKind interactionKind,
        NumberBoxEditCompletionKind? completionKind = null,
        string? inputText = null)
    {
        this.InteractionKind = interactionKind;
        this.CompletionKind = completionKind;
        this.InputText = inputText;
    }

    /// <summary>
    ///     Gets the input interaction that owns the edit.
    /// </summary>
    public NumberBoxEditInteractionKind InteractionKind { get; }

    /// <summary>
    ///     Gets the completion kind when the event represents an edit completion.
    /// </summary>
    public NumberBoxEditCompletionKind? CompletionKind { get; }

    /// <summary>Gets the committed text, or <see langword="null"/> for non-text interactions.</summary>
    public string? InputText { get; }
}
