// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Config;

/// <summary>
/// Exception thrown when settings persistence operations fail.
/// </summary>
public sealed class SettingsPersistenceException : Exception
{
    /// <summary>
    /// Initializes a new instance of the <see cref="SettingsPersistenceException"/> class.
    /// </summary>
    public SettingsPersistenceException()
        : base("Settings persistence operation failed.")
    {
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="SettingsPersistenceException"/> class.
    /// </summary>
    /// <param name="message">The error message.</param>
    public SettingsPersistenceException(string message)
        : base(message)
    {
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="SettingsPersistenceException"/> class.
    /// </summary>
    /// <param name="message">The error message.</param>
    /// <param name="innerException">The inner exception.</param>
    public SettingsPersistenceException(string message, Exception innerException)
        : base(message, innerException)
    {
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="SettingsPersistenceException"/> class.
    /// </summary>
    /// <param name="message">The error message.</param>
    /// <param name="sourceId">The identifier of the settings source that failed.</param>
    public SettingsPersistenceException(string message, string sourceId)
        : base(message)
    {
        this.SourceId = sourceId;
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="SettingsPersistenceException"/> class.
    /// </summary>
    /// <param name="message">The error message.</param>
    /// <param name="sourceId">The identifier of the settings source that failed.</param>
    /// <param name="innerException">The inner exception.</param>
    public SettingsPersistenceException(string message, string sourceId, Exception innerException)
        : base(message, innerException)
    {
        this.SourceId = sourceId;
    }

    /// <summary>
    /// Gets the identifier of the settings source that failed, if available.
    /// </summary>
    public string? SourceId { get; }
}
