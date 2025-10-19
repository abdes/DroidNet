// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Config;

/// <summary>
/// General result for settings source operations.
/// </summary>
public sealed record SettingsSourceResult
{
    /// <summary>
    /// Gets a value indicating whether the operation was successful.
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
    /// Gets additional details about the operation, if available.
    /// </summary>
    public string? Details { get; init; }

    /// <summary>
    /// Creates a successful result.
    /// </summary>
    /// <param name="details">Optional details about the operation.</param>
    /// <returns>A successful SettingsSourceResult.</returns>
    public static SettingsSourceResult CreateSuccess(string? details = null)
    {
        return new SettingsSourceResult
        {
            Success = true,
            Details = details,
        };
    }

    /// <summary>
    /// Creates a failed result.
    /// </summary>
    /// <param name="errorMessage">The error message describing the failure.</param>
    /// <param name="exception">Optional exception that caused the failure.</param>
    /// <returns>A failed SettingsSourceResult.</returns>
    public static SettingsSourceResult CreateFailure(string errorMessage, Exception? exception = null)
    {
        return new SettingsSourceResult
        {
            Success = false,
            ErrorMessage = errorMessage,
            Exception = exception,
        };
    }
}
