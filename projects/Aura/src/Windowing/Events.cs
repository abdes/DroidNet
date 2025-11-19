// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

#pragma warning disable SA1649 // File name should match first type name
#pragma warning disable SA1402 // File may only contain a single type

using System.ComponentModel;
using Microsoft.UI;
using Microsoft.UI.Windowing;

namespace DroidNet.Aura.Windowing;

/// <summary>
///     Represents an asynchronous event handler delegate.
/// </summary>
/// <remarks>
///     Use this delegate for events that support async handlers and should return a <see
///     cref="Task"/> so callers can await completion of handler work. Prefer returning a completed
///     <see cref="Task"/> instance when no asynchronous work is required.
/// </remarks>
/// <typeparam name="TEventArgs">The type of the event data passed to handlers.</typeparam>
/// <param name="sender">The source of the event; typically the object that raised the
/// event.</param>
/// <param name="e">The event data associated with this event invocation.</param>
/// <returns>A <see cref="Task"/> that completes when the handler's work is finished.</returns>
[System.Diagnostics.CodeAnalysis.SuppressMessage("Naming", "CA1711:Identifiers should not have incorrect suffix", Justification = "Async event handler pattern returning Task is intentional.")]
public delegate Task AsyncEventHandler<TEventArgs>(object? sender, TEventArgs e);

/// <summary>
///     Provides data for the <see cref="IWindowManagerService.PresenterStateChanged"/> event.
///     Conveys the current overlapped presenter state and associated bounds after a state transition.
/// </summary>
/// <param name="windowId">The identifier of the window whose presenter state changed.</param>
/// <param name="state">The current presenter state (e.g., Restored, Minimized, Maximized).</param>
/// <param name="bounds">The current window bounds, if available.</param>
public sealed class PresenterStateChangeEventArgs(
    WindowId windowId,
    OverlappedPresenterState state,
    Windows.Graphics.RectInt32? bounds = null) : EventArgs
{
    /// <summary>
    ///     Gets the identifier of the window whose presenter state changed.
    /// </summary>
    public WindowId WindowId { get; } = windowId;

    /// <summary>
    ///     Gets the current overlapped presenter state of the window.
    /// </summary>
    public OverlappedPresenterState State { get; } = state;

    /// <summary>
    ///     Gets the current bounds of the window, if available; otherwise <see langword="null"/>.
    /// </summary>
    public Windows.Graphics.RectInt32? Bounds { get; } = bounds;
}

/// <summary>
///     Event data for the <see cref="IWindowManagerService.WindowClosing"/> event.
/// </summary>
/// <remarks>
///     Inherits from <see cref="CancelEventArgs"/>, allowing handlers to cancel the close operation
///     by setting <see cref="CancelEventArgs.Cancel"/> to <see langword="true"/>.
/// </remarks>
public sealed class WindowClosingEventArgs : CancelEventArgs
{
    /// <summary>
    ///     Gets the ID of the window being closed.
    /// </summary>
    public required WindowId WindowId { get; init; }
}

/// <summary>
///     Event data for the <see cref="IWindowManagerService.WindowClosed"/> event.
/// </summary>
/// <remarks>
///     Fired after a window has completed closing.
/// </remarks>
public sealed class WindowClosedEventArgs : EventArgs
{
    /// <summary>
    ///     Gets the ID of the window that was closed.
    /// </summary>
    public required WindowId WindowId { get; init; }
}

/// <summary>
///     Event data for the <see cref="IWindowManagerService.WindowBoundsChanged"/> event.
/// </summary>
/// <param name="windowId">The identifier of the window whose bounds changed.</param>
/// <param name="bounds">The current window bounds after the change.</param>
public sealed class WindowBoundsChangedEventArgs(WindowId windowId, Windows.Graphics.RectInt32 bounds) : EventArgs
{
    /// <summary>
    ///     Gets the identifier of the window whose bounds changed.
    /// </summary>
    public WindowId WindowId { get; } = windowId;

    /// <summary>
    ///     Gets the current bounds of the window.
    /// </summary>
    public Windows.Graphics.RectInt32 Bounds { get; } = bounds;
}

/// <summary>
///     Represents a single change to a window's metadata (key / value pair).
/// </summary>
/// <param name="windowId">The identifier of the window whose metadata changed.</param>
/// <param name="key">The metadata key that changed (case-sensitive).</param>
/// <param name="oldValue">The previous value for the metadata key; <see langword="null"/> if none.</param>
/// <param name="newValue">The new value for the metadata key; <see langword="null"/> if the key was removed.</param>
public sealed class WindowMetadataChange(Microsoft.UI.WindowId windowId, string key, object? oldValue, object? newValue)
{
    /// <summary>
    ///     Gets the identifier of the window whose metadata changed.
    /// </summary>
    public WindowId WindowId { get; } = windowId;

    /// <summary>
    ///     Gets the metadata key that changed.
    /// </summary>
    public string Key { get; } = key;

    /// <summary>
    ///     Gets the previous value for the metadata key; <see langword="null"/> if no value was set.
    /// </summary>
    public object? OldValue { get; } = oldValue;

    /// <summary>
    ///     Gets the new value for the metadata key; <see langword="null"/> if the key was removed.
    /// </summary>
    public object? NewValue { get; } = newValue;
}
