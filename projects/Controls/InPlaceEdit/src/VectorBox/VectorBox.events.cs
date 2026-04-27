// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>
///     Represents a control that allows the user to input and display small fixed-size numeric vectors (Vector2/Vector3).
/// </summary>
public partial class VectorBox
{
    /// <summary>
    ///     Occurs when one vector component starts an interactive edit.
    /// </summary>
    public event EventHandler<VectorBoxEditSessionEventArgs>? EditSessionStarted;

    /// <summary>
    ///     Occurs when one vector component commits or cancels an interactive edit.
    /// </summary>
    public event EventHandler<VectorBoxEditSessionEventArgs>? EditSessionCompleted;

    /// <summary>
    ///     Occurs when a component value is being validated.
    /// </summary>
    public event EventHandler<ValidationEventArgs<float>>? Validate;

    /// <summary>
    ///     Raises the <see cref="Validate" /> event.
    /// </summary>
    /// <param name="e">The <see cref="ValidationEventArgs{T}" /> instance containing the event data.</param>
    protected virtual void OnValidate(ValidationEventArgs<float> e)
    {
        this.Validate?.Invoke(this, e);
    }

    private void OnNumberBoxEditSessionStarted(object? sender, NumberBoxEditSessionEventArgs e)
    {
        if (this.TryGetComponent(sender, out var component))
        {
            this.EditSessionStarted?.Invoke(this, new VectorBoxEditSessionEventArgs(component, e.InteractionKind, e.CompletionKind));
        }
    }

    private void OnNumberBoxEditSessionCompleted(object? sender, NumberBoxEditSessionEventArgs e)
    {
        if (this.TryGetComponent(sender, out var component))
        {
            this.EditSessionCompleted?.Invoke(this, new VectorBoxEditSessionEventArgs(component, e.InteractionKind, e.CompletionKind));
        }
    }

    private bool TryGetComponent(object? sender, out Component component)
    {
        if (ReferenceEquals(sender, this.numberBoxX))
        {
            component = Component.X;
            return true;
        }

        if (ReferenceEquals(sender, this.numberBoxY))
        {
            component = Component.Y;
            return true;
        }

        if (ReferenceEquals(sender, this.numberBoxZ))
        {
            component = Component.Z;
            return true;
        }

        component = default;
        return false;
    }
}
