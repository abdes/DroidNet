// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Config;

/// <summary>
/// Result of saving to a settings source.
/// </summary>
public sealed record SettingsSourceWriteResult
{
    /// <summary>
    /// Gets a value indicating whether the write operation was successful.
    /// </summary>
    public required bool Success { get; init; }

    /// <summary>
    /// Gets the error message if the operation failed.
    /// </summary>
    public string? ErrorMessage { get; init; }

    /// <summary>
    /// Gets the exception that caused the failure, if available.
    /// </summary>
    public Exception? Exception { get; init; }

    /// <summary>
    /// Gets additional details about the write operation, if available.
    /// </summary>
    public string? Details { get; init; }

    /// <summary>
    /// Creates a successful write result.
    /// </summary>
    /// <param name="details">Optional details about the write operation.</param>
    /// <returns>A successful SettingsSourceWriteResult.</returns>
    public static SettingsSourceWriteResult CreateSuccess(string? details = null)
    {
        return new SettingsSourceWriteResult
        {
            Success = true,
            Details = details,
        };
    }

    /// <summary>
    /// Creates a failed write result.
    /// </summary>
    /// <param name="errorMessage">The error message describing the failure.</param>
    /// <param name="exception">Optional exception that caused the failure.</param>
    /// <returns>A failed SettingsSourceWriteResult.</returns>
    public static SettingsSourceWriteResult CreateFailure(string errorMessage, Exception? exception = null)
    {
        return new SettingsSourceWriteResult
        {
            Success = false,
            ErrorMessage = errorMessage,
            Exception = exception,
        };
    }
}
