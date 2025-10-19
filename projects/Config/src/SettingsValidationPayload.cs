// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;

namespace DroidNet.Config;

/// <summary>
/// Represents the outcome of a successful validation operation.
/// </summary>
public sealed record SettingsValidationPayload
{
    /// <summary>
    /// Initializes a new instance of the <see cref="SettingsValidationPayload"/> class.
    /// </summary>
    /// <param name="sectionsValidated">The number of sections that participated in validation.</param>
    /// <param name="message">A human-readable description of the validation result.</param>
    public SettingsValidationPayload(int sectionsValidated, string message)
    {
        if (string.IsNullOrWhiteSpace(message))
        {
            throw new ArgumentException("Message cannot be null or whitespace.", nameof(message));
        }

        this.SectionsValidated = sectionsValidated;
        this.Message = message;
    }

    /// <summary>
    /// Gets the number of sections that participated in validation.
    /// </summary>
    public int SectionsValidated { get; }

    /// <summary>
    /// Gets a human-readable description of the validation result.
    /// </summary>
    public string Message { get; }
}
