// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Config;

/// <summary>
/// Exception thrown when a settings migration fails.
/// </summary>
public sealed class SettingsMigrationException : Exception
{
    /// <summary>
    /// Initializes a new instance of the <see cref="SettingsMigrationException"/> class.
    /// </summary>
    public SettingsMigrationException()
        : base("Settings migration failed.")
    {
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="SettingsMigrationException"/> class with a specified error message.
    /// </summary>
    /// <param name="message">The error message that explains the reason for the exception.</param>
    public SettingsMigrationException(string message)
        : base(message)
    {
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="SettingsMigrationException"/> class with a specified error message and a reference to the inner exception that is the cause of this exception.
    /// </summary>
    /// <param name="message">The error message that explains the reason for the exception.</param>
    /// <param name="innerException">The exception that is the cause of the current exception.</param>
    public SettingsMigrationException(string message, Exception innerException)
        : base(message, innerException)
    {
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="SettingsMigrationException"/> class with a specified error message, from and to versions, and a reference to the inner exception that is the cause of this exception.
    /// </summary>
    /// <param name="message">The error message that explains the reason for the exception.</param>
    /// <param name="fromVersion">The version from which migration was attempted.</param>
    /// <param name="toVersion">The version to which migration was attempted.</param>
    /// <param name="innerException">The exception that is the cause of the current exception, or <c>null</c>.</param>
    public SettingsMigrationException(string message, string fromVersion, string toVersion, Exception? innerException = null)
        : base(message, innerException)
    {
        this.FromVersion = fromVersion;
        this.ToVersion = toVersion;
    }

    /// <summary>
    /// Gets the version from which migration was attempted.
    /// </summary>
    public string? FromVersion { get; init; }

    /// <summary>
    /// Gets the version to which migration was attempted.
    /// </summary>
    public string? ToVersion { get; init; }
}
