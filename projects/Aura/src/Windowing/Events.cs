// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

#pragma warning disable SA1649 // File name should match first type name
#pragma warning disable SA1402 // File may only contain a single type

using System.ComponentModel;
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
///     Provides data for the <see cref="IWindowManagerService.PresenterStateChanging"/> and
///     <see cref="IWindowManagerService.PresenterStateChanged"/> events. This type conveys the
///     current and next overlapped presenter states and both the old and new bounds where applicable.
/// </summary>
/// <remarks>
///     This type is used both for pre-transition (<c>PresenterStateChanging</c>) and
///     post-transition (<c>PresenterStateChanged</c>) notifications. Handlers listening to the
///     pre-transition event can inspect <see cref="ProposedRestoredBounds"/> to determine the
///     intended restored location and size.
/// </remarks>
/// <param name="oldState">The current or previous presenter state for the window (e.g., Normal, Minimized, Maximized).</param>
/// <param name="newState">The presenter state that the window is transitioning to.</param>
/// <param name="oldBounds">The current window bounds before the transition, if available.</param>
/// <param name="newBounds">The window bounds after the transition, if available.</param>
/// <param name="proposedRestoredBounds">
///     For pre-transition events, the proposed restored bounds (nullable) describing the intended
///     restored window rectangle.
/// </param>
public sealed class PresenterStateChangeEventArgs(
    OverlappedPresenterState oldState,
    OverlappedPresenterState newState,
    Windows.Graphics.RectInt32? oldBounds = null,
    Windows.Graphics.RectInt32? newBounds = null,
    Windows.Graphics.RectInt32? proposedRestoredBounds = null) : EventArgs
{
    /// <summary>
    ///     Gets the overlapped presenter state that the window was in prior to the transition.
    /// </summary>
    public OverlappedPresenterState OldState { get; } = oldState;

    /// <summary>
    ///     Gets the overlapped presenter state that the window is entering as part of the transition.
    /// </summary>
    public OverlappedPresenterState NewState { get; } = newState;

    /// <summary>
    ///     Gets the bounds of the window before the transition occurred, if available; otherwise <see langword="null"/>.
    /// </summary>
    public Windows.Graphics.RectInt32? OldBounds { get; } = oldBounds;

    /// <summary>
    ///     Gets the bounds of the window after the transition occurred, if available; otherwise <see langword="null"/>.
    /// </summary>
    public Windows.Graphics.RectInt32? NewBounds { get; } = newBounds;

    /// <summary>
    ///     Gets, for pre-transition notifications, the proposed restored bounds. This value can be
    ///     used by handlers that need to learn the intended restored rectangle without requiring
    ///     access to the <see cref="OldBounds"/> or <see cref="NewBounds"/> values.
    /// </summary>
    /// <remarks>
    ///     This value is nullable and provided for consumers that need the intended restored
    ///     rectangle without relying on <see cref="OldBounds"/> or <see cref="NewBounds"/>.
    /// </remarks>
    public Windows.Graphics.RectInt32? ProposedRestoredBounds { get; } = proposedRestoredBounds;
}

/// <summary>
///     Event data for the <see cref="IWindowManagerService.WindowClosing"/> event.
/// </summary>
/// <remarks>
///     Inherits from <see cref="CancelEventArgs"/>, allowing handlers to cancel the close operation
///     by setting <see cref="CancelEventArgs.Cancel"/> to <see langword="true"/>.
/// </remarks>
public sealed class WindowClosingEventArgs : CancelEventArgs;

/// <summary>
///     Event data for the <see cref="IWindowManagerService.WindowClosed"/> event.
/// </summary>
/// <remarks>
///     Fired after a window has completed closing. This type contains no additional information;
///     use <see cref="IWindowManagerService"/> if you need to query window metadata after close.
/// </remarks>
public sealed class WindowClosedEventArgs : EventArgs;

/// <summary>
///     Event data for the <see cref="IWindowManagerService.WindowBoundsChanged"/> event.
/// </summary>
/// <param name="oldBounds">The window bounds before the change.</param>
/// <param name="newBounds">The window bounds after the change.</param>
public sealed class WindowBoundsChangedEventArgs(Windows.Graphics.RectInt32 oldBounds, Windows.Graphics.RectInt32 newBounds) : EventArgs
{
    /// <summary>
    ///     Gets the bounds of the window before the change occurred.
    /// </summary>
    public Windows.Graphics.RectInt32 OldBounds { get; } = oldBounds;

    /// <summary>
    ///     Gets the bounds of the window after the change occurred.
    /// </summary>
    public Windows.Graphics.RectInt32 NewBounds { get; } = newBounds;
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
    public Microsoft.UI.WindowId WindowId { get; } = windowId;

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
