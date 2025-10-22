// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Config;

/// <summary>
///     Represents a validation error for a specific settings property.
/// </summary>
public sealed class SettingsValidationError
{
    /// <summary>
    ///     Initializes a new instance of the <see cref="SettingsValidationError"/> class.
    /// </summary>
    /// <param name="propertyName">The name of the property that failed validation.</param>
    /// <param name="message">The error message describing the validation failure.</param>
    /// <exception cref="ArgumentNullException">
    ///     Thrown when <paramref name="propertyName"/> or <paramref name="message"/> is <see langword="null"/>.
    /// </exception>
    public SettingsValidationError(string propertyName, string message)
    {
        this.PropertyName = propertyName ?? throw new ArgumentNullException(nameof(propertyName));
        this.Message = message ?? throw new ArgumentNullException(nameof(message));
    }

    /// <summary>
    ///     Initializes a new instance of the <see cref="SettingsValidationError"/> class.
    /// </summary>
    /// <param name="propertyName">The name of the property that failed validation.</param>
    /// <param name="message">The error message describing the validation failure.</param>
    /// <param name="attemptedValue">The value that was attempted to be set.</param>
    /// <param name="errorCode">Optional error code for categorizing the validation failure.</param>
    /// <exception cref="ArgumentNullException">
    ///     Thrown when <paramref name="propertyName"/> or <paramref name="message"/> is <see langword="null"/>.
    /// </exception>
    public SettingsValidationError(string propertyName, string message, object? attemptedValue, string? errorCode = null)
    {
        this.PropertyName = propertyName ?? throw new ArgumentNullException(nameof(propertyName));
        this.Message = message ?? throw new ArgumentNullException(nameof(message));
        this.AttemptedValue = attemptedValue;
        this.ErrorCode = errorCode;
    }

    /// <summary>
    ///     Gets the name of the property that failed validation.
    /// </summary>
    public string PropertyName { get; }

    /// <summary>
    ///     Gets the error message describing the validation failure.
    /// </summary>
    public string Message { get; }

    /// <summary>
    ///     Gets the value that was attempted to be set, if available.
    /// </summary>
    public object? AttemptedValue { get; }

    /// <summary>
    ///     Gets an optional error code for categorizing the validation failure.
    /// </summary>
    public string? ErrorCode { get; }

    /// <summary>
    ///     Returns a string representation of the validation error.
    /// </summary>
    /// <returns>A string containing the property name and error message.</returns>
    public override string ToString() => $"{this.PropertyName}: {this.Message}";
}
