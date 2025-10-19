// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Config;

/// <summary>
/// Result of loading from a settings source.
/// </summary>
public sealed record SettingsSourceReadResult
{
    /// <summary>
    /// Gets a value indicating whether the read operation was successful.
    /// </summary>
    public required bool Success { get; init; }

    /// <summary>
    /// Gets the metadata associated with the settings content, if available.
    /// </summary>
    public SettingsMetadata? Metadata { get; init; }

    /// <summary>
    /// Gets the settings content organized by section names, if successful.
    /// </summary>
    public IReadOnlyDictionary<string, object>? SectionsData { get; init; }

    /// <summary>
    /// Gets the error message if the operation failed.
    /// </summary>
    public string? ErrorMessage { get; init; }

    /// <summary>
    /// Gets the exception that caused the failure, if available.
    /// </summary>
    public Exception? Exception { get; init; }

    /// <summary>
    /// Creates a successful read result.
    /// </summary>
    /// <param name="sectionsData">The settings content organized by section names.</param>
    /// <param name="metadata">The metadata associated with the settings content.</param>
    /// <returns>A successful SettingsSourceReadResult.</returns>
    public static SettingsSourceReadResult CreateSuccess(
        IReadOnlyDictionary<string, object> sectionsData,
        SettingsMetadata? metadata = null)
    {
        return new SettingsSourceReadResult
        {
            Success = true,
            SectionsData = sectionsData,
            Metadata = metadata,
        };
    }

    /// <summary>
    /// Creates a failed read result.
    /// </summary>
    /// <param name="errorMessage">The error message describing the failure.</param>
    /// <param name="exception">Optional exception that caused the failure.</param>
    /// <returns>A failed SettingsSourceReadResult.</returns>
    public static SettingsSourceReadResult CreateFailure(string errorMessage, Exception? exception = null)
    {
        return new SettingsSourceReadResult
        {
            Success = false,
            ErrorMessage = errorMessage,
            Exception = exception,
        };
    }
}
