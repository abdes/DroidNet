// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Config;

/// <summary>
/// Types of changes that can occur to settings sources.
/// </summary>
public enum SettingsSourceChangeType
{
    /// <summary>A new source was added to the service.</summary>
    Added,

    /// <summary>An existing source was updated with new content.</summary>
    Updated,

    /// <summary>A source was removed from the service.</summary>
    Removed,

    /// <summary>A source failed to load or operation failed.</summary>
    Failed,
}

/// <summary>
/// Event arguments for settings source lifecycle changes.
/// </summary>
public sealed class SettingsSourceChangedEventArgs : EventArgs
{
    /// <summary>
    /// Initializes a new instance of the <see cref="SettingsSourceChangedEventArgs"/> class.
    /// </summary>
    /// <param name="sourceId">The identifier of the settings source that changed.</param>
    /// <param name="changeType">The type of change that occurred.</param>
    public SettingsSourceChangedEventArgs(string sourceId, SettingsSourceChangeType changeType)
    {
        this.SourceId = sourceId ?? throw new ArgumentNullException(nameof(sourceId));
        this.ChangeType = changeType;
        this.Timestamp = DateTimeOffset.UtcNow;
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="SettingsSourceChangedEventArgs"/> class.
    /// </summary>
    /// <param name="sourceId">The identifier of the settings source that changed.</param>
    /// <param name="changeType">The type of change that occurred.</param>
    /// <param name="errorMessage">Optional error message if the change represents a failure.</param>
    public SettingsSourceChangedEventArgs(string sourceId, SettingsSourceChangeType changeType, string? errorMessage)
    {
        this.SourceId = sourceId ?? throw new ArgumentNullException(nameof(sourceId));
        this.ChangeType = changeType;
        this.ErrorMessage = errorMessage;
        this.Timestamp = DateTimeOffset.UtcNow;
    }

    /// <summary>
    /// Gets the identifier of the settings source that changed.
    /// </summary>
    public string SourceId { get; }

    /// <summary>
    /// Gets the type of change that occurred.
    /// </summary>
    public SettingsSourceChangeType ChangeType { get; }

    /// <summary>
    /// Gets an optional error message if the change represents a failure.
    /// </summary>
    public string? ErrorMessage { get; }

    /// <summary>
    /// Gets the timestamp when the change occurred.
    /// </summary>
    public DateTimeOffset Timestamp { get; }
}
