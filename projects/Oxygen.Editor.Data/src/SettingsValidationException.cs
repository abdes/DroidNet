// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel.DataAnnotations;

namespace Oxygen.Editor.Data;

/// <summary>
/// Represents one or more validation failures when trying to save a setting that does not meet configured validators.
/// </summary>
public sealed class SettingsValidationException : ValidationException
{
    /// <summary>
    /// Initializes a new instance of the <see cref="SettingsValidationException"/> class.
    /// </summary>
    public SettingsValidationException()
        : base("One or more validation results indicate the value is invalid.")
    {
        this.Results = [];
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="SettingsValidationException"/> class with a message.
    /// </summary>
    /// <param name="message">The validation error message.</param>
    public SettingsValidationException(string message)
        : base(message)
    {
        this.Results = [];
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="SettingsValidationException"/> class with a message and inner
    /// exception.
    /// </summary>
    /// <param name="message">The validation error message.</param>
    /// <param name="innerException">The inner exception.</param>
    public SettingsValidationException(string message, Exception innerException)
        : base(message, innerException)
    {
        this.Results = [];
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="SettingsValidationException"/> class.
    /// </summary>
    /// <param name="message">The error message.</param>
    /// <param name="results">Validation results that failed.</param>
    /// <exception cref="ArgumentNullException">Thrown when <paramref name="results"/> is null.</exception>
    public SettingsValidationException(string message, IEnumerable<ValidationResult> results)
        : base(message ?? throw new ArgumentNullException(nameof(message)))
    {
        this.Results = results?.ToList().AsReadOnly() ?? throw new ArgumentNullException(nameof(results));
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="SettingsValidationException"/> class with an inner exception.
    /// </summary>
    /// <param name="message">The error message.</param>
    /// <param name="results">Validation results that failed.</param>
    /// <param name="innerException">The optional inner exception.</param>
    public SettingsValidationException(string message, IEnumerable<ValidationResult> results, Exception? innerException)
        : base(message ?? throw new ArgumentNullException(nameof(message)), innerException)
    {
        this.Results = results?.ToList().AsReadOnly() ?? throw new ArgumentNullException(nameof(results));
    }

    /// <summary>
    /// Gets the collection of individual validation results.
    /// </summary>
    public IReadOnlyList<ValidationResult> Results { get; }
}
