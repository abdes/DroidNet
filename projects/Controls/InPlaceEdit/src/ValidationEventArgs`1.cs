// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>
///     Provides data for validation events, including the old and new values being validated.
/// </summary>
/// <typeparam name="T">The type of the values being validated.</typeparam>
/// <param name="oldValue">The old value before the change.</param>
/// <param name="newValue">The new value after the change.</param>
/// <param name="inputText">The raw text being validated, or <see langword="null"/> for programmatic changes.</param>
public class ValidationEventArgs<T>(T? oldValue, T? newValue, string? inputText = null) : EventArgs
{
    /// <summary>
    ///     Gets the old value before the change.
    /// </summary>
    public T? OldValue { get; } = oldValue;

    /// <summary>
    ///     Gets the new value after the change.
    /// </summary>
    public T? NewValue { get; } = newValue;

    /// <summary>Gets the raw text being validated, or <see langword="null"/> for programmatic changes.</summary>
    public string? InputText { get; } = inputText;

    /// <summary>
    ///     Gets or sets a value indicating whether the new value is valid.
    /// </summary>
    public bool IsValid { get; set; } = true;

    /// <summary>
    ///     Gets or sets an optional target object for discrimination (e.g., a <see cref="Component" /> enum value).
    /// </summary>
    public object? Target { get; set; }
}
