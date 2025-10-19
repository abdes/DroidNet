// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Config;

/// <summary>
/// Exception thrown when settings validation fails.
/// </summary>
public sealed class SettingsValidationException : Exception
{
    /// <summary>
    /// Initializes a new instance of the <see cref="SettingsValidationException"/> class.
    /// </summary>
    public SettingsValidationException()
        : base("Settings validation failed.")
    {
        this.ValidationErrors = Array.Empty<SettingsValidationError>();
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="SettingsValidationException"/> class.
    /// </summary>
    /// <param name="message">The error message.</param>
    public SettingsValidationException(string message)
        : base(message)
    {
        this.ValidationErrors = Array.Empty<SettingsValidationError>();
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="SettingsValidationException"/> class.
    /// </summary>
    /// <param name="message">The error message.</param>
    /// <param name="innerException">The inner exception.</param>
    public SettingsValidationException(string message, Exception innerException)
        : base(message, innerException)
    {
        this.ValidationErrors = Array.Empty<SettingsValidationError>();
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="SettingsValidationException"/> class.
    /// </summary>
    /// <param name="message">The error message.</param>
    /// <param name="validationErrors">Collection of validation errors that occurred.</param>
    public SettingsValidationException(string message, IReadOnlyList<SettingsValidationError> validationErrors)
        : base(message)
    {
        this.ValidationErrors = validationErrors ?? throw new ArgumentNullException(nameof(validationErrors));
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="SettingsValidationException"/> class.
    /// </summary>
    /// <param name="message">The error message.</param>
    /// <param name="validationErrors">Collection of validation errors that occurred.</param>
    /// <param name="innerException">The inner exception.</param>
    public SettingsValidationException(
        string message,
        IReadOnlyList<SettingsValidationError> validationErrors,
        Exception innerException)
        : base(message, innerException)
    {
        this.ValidationErrors = validationErrors ?? throw new ArgumentNullException(nameof(validationErrors));
    }

    /// <summary>
    /// Gets the collection of validation errors that occurred.
    /// </summary>
    public IReadOnlyList<SettingsValidationError> ValidationErrors { get; }
}
