// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Config;

/// <summary>
///     Exception thrown when a settings persistence operation fails.
/// </summary>
public sealed class SettingsPersistenceException : Exception
{
    /// <summary>
    ///     Initializes a new instance of the <see cref="SettingsPersistenceException"/> class with a default error message.
    /// </summary>
    public SettingsPersistenceException()
        : base("Settings persistence operation failed.")
    {
    }

    /// <summary>
    ///     Initializes a new instance of the <see cref="SettingsPersistenceException"/> class with a specified error message.
    /// </summary>
    /// <param name="message">The error message that describes the error.</param>
    public SettingsPersistenceException(string message)
        : base(message)
    {
    }

    /// <summary>
    ///     Initializes a new instance of the <see cref="SettingsPersistenceException"/> class with a specified error
    ///     message and a reference to the inner exception that is the cause of this exception.
    /// </summary>
    /// <param name="message">The error message that describes the error.</param>
    /// <param name="innerException">
    ///     The exception that is the cause of the current exception, or <see langword="null"/> if no inner exception is specified.
    /// </param>
    /// <exception cref="ArgumentNullException">
    ///     Thrown when <paramref name="message"/> is <see langword="null"/>.
    /// </exception>
    public SettingsPersistenceException(string message, Exception innerException)
        : base(message, innerException)
    {
    }

    /// <summary>
    ///     Initializes a new instance of the <see cref="SettingsPersistenceException"/> class with a specified error
    ///     message and the identifier of the settings source that failed.
    /// </summary>
    /// <param name="message">The error message that describes the error.</param>
    /// <param name="sourceId">The identifier of the settings source that failed.</param>
    /// <exception cref="ArgumentNullException">
    ///     Thrown when <paramref name="message"/> is <see langword="null"/>.
    /// </exception>
    public SettingsPersistenceException(string message, string sourceId)
        : base(message)
    {
        this.SourceId = sourceId;
    }

    /// <summary>
    ///     Initializes a new instance of the <see cref="SettingsPersistenceException"/> class with a specified error
    ///     message, the identifier of the settings source that failed, and a reference to the inner exception that is
    ///     the cause of this exception.
    /// </summary>
    /// <param name="message">The error message that describes the error.</param>
    /// <param name="sourceId">The identifier of the settings source that failed.</param>
    /// <param name="innerException">
    ///     The exception that is the cause of the current exception, or <see langword="null"/> if no inner exception is
    ///     specified.
    /// </param>
    /// <exception cref="ArgumentNullException">
    ///     Thrown when <paramref name="message"/> is <see langword="null"/>.
    /// </exception>
    public SettingsPersistenceException(string message, string sourceId, Exception innerException)
        : base(message, innerException)
    {
        this.SourceId = sourceId;
    }

    /// <summary>
    ///     Gets the identifier of the settings source that failed, if available.
    /// </summary>
    /// <remarks>
    ///     This property is <see langword="null"/> if the exception was constructed without a source identifier.
    /// </remarks>
    public string? SourceId { get; }
}
