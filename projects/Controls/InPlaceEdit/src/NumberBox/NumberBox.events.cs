// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>
///     Represents a control that allows the user to input and display numeric values.
/// </summary>
public partial class NumberBox
{
    /// <summary>
    ///     Occurs when the user starts an interactive numeric edit.
    /// </summary>
    public event EventHandler<NumberBoxEditSessionEventArgs>? EditSessionStarted;

    /// <summary>
    ///     Occurs when the user commits or cancels an interactive numeric edit.
    /// </summary>
    public event EventHandler<NumberBoxEditSessionEventArgs>? EditSessionCompleted;

    /// <summary>
    ///     Occurs when the value is being validated.
    /// </summary>
    public event EventHandler<ValidationEventArgs<float>>? Validate;

    /// <summary>Internal so UI test hosts can drive edit-session transitions.</summary>
    /// <param name="interactionKind">The kind of interaction that started the edit session.</param>
    internal void OnEditSessionStarted(NumberBoxEditInteractionKind interactionKind)
        => this.EditSessionStarted?.Invoke(this, new NumberBoxEditSessionEventArgs(interactionKind));

    /// <summary>Internal so UI test hosts can drive edit-session transitions.</summary>
    /// <param name="interactionKind">The kind of interaction that started the edit session.</param>
    /// <param name="completionKind">Indicates how the edit session completed (committed or cancelled).</param>
    /// <param name="inputText">Optional input text provided during the edit session, if any.</param>
    internal void OnEditSessionCompleted(
        NumberBoxEditInteractionKind interactionKind,
        NumberBoxEditCompletionKind completionKind,
        string? inputText = null)
        => this.EditSessionCompleted?.Invoke(this, new NumberBoxEditSessionEventArgs(interactionKind, completionKind, inputText));

    /// <summary>
    ///     Raises the <see cref="Validate" /> event.
    /// </summary>
    /// <param name="e">The <see cref="ValidationEventArgs{T}" /> instance containing the event data.</param>
    protected virtual void OnValidate(ValidationEventArgs<float> e)
    {
        this.Validate?.Invoke(this, e);
        this.valueIsValid = e.IsValid;
    }
}
