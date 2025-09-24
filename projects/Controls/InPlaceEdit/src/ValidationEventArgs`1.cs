// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>
///     Provides data for validation events, including the old and new values being validated.
/// </summary>
/// <typeparam name="T">The type of the values being validated.</typeparam>
public class ValidationEventArgs<T> : EventArgs
{
    /// <summary>
    ///     Initializes a new instance of the <see cref="ValidationEventArgs{T}" /> class.
    /// </summary>
    /// <param name="oldValue">The old value before the change.</param>
    /// <param name="newValue">The new value after the change.</param>
    public ValidationEventArgs(T? oldValue, T? newValue)
    {
        this.OldValue = oldValue;
        this.NewValue = newValue;
    }

    /// <summary>
    ///     Gets the old value before the change.
    /// </summary>
    public T? OldValue { get; }

    /// <summary>
    ///     Gets the new value after the change.
    /// </summary>
    public T? NewValue { get; }

    /// <summary>
    ///     Gets or sets a value indicating whether the new value is valid.
    /// </summary>
    public bool IsValid { get; set; } = true;
}
