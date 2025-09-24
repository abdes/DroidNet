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
    ///     Occurs when the value is being validated.
    /// </summary>
    public event EventHandler<ValidationEventArgs<float>>? Validate;

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
