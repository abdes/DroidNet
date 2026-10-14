// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>Describes an interactive edit lifecycle event raised by <see cref="ColorBox"/>: a channel edit or a pick.</summary>
/// <param name="channel">The edited channel, or <see langword="null"/> for a pick, which can change every channel.</param>
/// <param name="interactionKind">The input interaction that owns the edit.</param>
/// <param name="completionKind">The completion kind when the event represents an edit completion.</param>
/// <param name="inputText">The committed text, or <see langword="null"/> for non-text interactions.</param>
public sealed class ColorBoxEditSessionEventArgs(
    Component? channel,
    NumberBoxEditInteractionKind interactionKind,
    NumberBoxEditCompletionKind? completionKind = null,
    string? inputText = null) : EventArgs
{
    /// <summary>Gets the edited channel, or <see langword="null"/> for a pick.</summary>
    public Component? Channel { get; } = channel;

    /// <summary>Gets the input interaction that owns the edit.</summary>
    public NumberBoxEditInteractionKind InteractionKind { get; } = interactionKind;

    /// <summary>Gets the completion kind when the event represents an edit completion.</summary>
    public NumberBoxEditCompletionKind? CompletionKind { get; } = completionKind;

    /// <summary>Gets the committed text, or <see langword="null"/> for non-text interactions.</summary>
    public string? InputText { get; } = inputText;
}
