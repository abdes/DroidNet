// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Aura.WindowManagement;

/// <summary>
/// Represents the type of window lifecycle event.
/// </summary>
public enum WindowLifecycleEventType
{
    /// <summary>
    /// Window was created and is about to be shown.
    /// </summary>
    Created,

    /// <summary>
    /// Window was activated (brought to foreground).
    /// </summary>
    Activated,

    /// <summary>
    /// Window was deactivated (lost focus).
    /// </summary>
    Deactivated,

    /// <summary>
    /// Window was closed.
    /// </summary>
    Closed,
}

/// <summary>
/// Represents an event in a window's lifecycle.
/// </summary>
/// <param name="EventType">The type of lifecycle event.</param>
/// <param name="Context">The window context associated with the event.</param>
/// <param name="Timestamp">When the event occurred.</param>
public sealed record WindowLifecycleEvent(
    WindowLifecycleEventType EventType,
    WindowContext Context,
    DateTimeOffset Timestamp)
{
    /// <summary>
    /// Creates a new lifecycle event for the current time.
    /// </summary>
    /// <param name="eventType">The type of event.</param>
    /// <param name="context">The window context.</param>
    /// <returns>A new <see cref="WindowLifecycleEvent"/>.</returns>
    public static WindowLifecycleEvent Create(WindowLifecycleEventType eventType, WindowContext context)
        => new(eventType, context, DateTimeOffset.UtcNow);
}
