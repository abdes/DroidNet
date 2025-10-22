// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Config;

/// <summary>
///     Types of changes that can occur to settings sources.
/// </summary>
public enum SourceChangeType
{
    /// <summary>A new source was added to the service.</summary>
    Added,

    /// <summary>An existing source was updated with new content.</summary>
    Updated,

    /// <summary>A source was removed from the service.</summary>
    Removed,

    /// <summary>A source was renamed.</summary>
    Renamed,
}

/// <summary>
///     Event arguments for settings source lifecycle changes.
/// </summary>
public sealed class SourceChangedEventArgs : EventArgs
{
    /// <summary>
    ///     Initializes a new instance of the <see cref="SourceChangedEventArgs"/> class.
    /// </summary>
    /// <param name="sourceId">The identifier of the settings source that changed.</param>
    /// <param name="changeType">The type of change that occurred.</param>
    /// <exception cref="ArgumentNullException">Thrown when <paramref name="sourceId"/> is <see langword="null"/>.</exception>
    public SourceChangedEventArgs(string sourceId, SourceChangeType changeType)
    {
        this.SourceId = sourceId ?? throw new ArgumentNullException(nameof(sourceId));
        this.ChangeType = changeType;
        this.Timestamp = DateTimeOffset.UtcNow;
    }

    /// <summary>
    ///     Initializes a new instance of the <see cref="SourceChangedEventArgs"/> class.
    /// </summary>
    /// <param name="sourceId">The identifier of the settings source that changed.</param>
    /// <param name="changeType">The type of change that occurred.</param>
    /// <param name="errorMessage">Optional error message if the change represents a failure.</param>
    /// <exception cref="ArgumentNullException">Thrown when <paramref name="sourceId"/> is <see langword="null"/>.</exception>
    public SourceChangedEventArgs(string sourceId, SourceChangeType changeType, string? errorMessage)
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
    public SourceChangeType ChangeType { get; }

    /// <summary>
    /// Gets an optional error message if the change represents a failure.
    /// </summary>
    public string? ErrorMessage { get; }

    /// <summary>
    /// Gets the timestamp when the change occurred.
    /// </summary>
    public DateTimeOffset Timestamp { get; }
}
