// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI;
using Microsoft.UI.Xaml;

namespace DroidNet.Aura.Windowing;

/// <summary>
///     Defines the contract for coordinating Aura-managed windows in a WinUI 3 application.
/// </summary>
/// <remarks>
///     The window manager tracks windows that have been constructed by the host application,
///     exposing registration, activation, enumeration, and teardown helpers while keeping the
///     actual showing or placement of the window under application control. It also raises
///     lifecycle events so higher levels can react to creation, activation changes, and closure.
/// </remarks>
public interface IWindowManagerService : IDisposable
{
#pragma warning disable CA1003 // Use generic event handler instances
#pragma warning disable MA0046 // Use EventHandler<T> to declare events
    /// <summary>
    ///     Occurs when a window's presenter state has changed.
    /// </summary>
    public event AsyncEventHandler<PresenterStateChangeEventArgs>? PresenterStateChanged;

    /// <summary>
    ///     Occurs when a window is closing. Can be cancelled.
    /// </summary>
    public event AsyncEventHandler<WindowClosingEventArgs>? WindowClosing;

    /// <summary>
    ///     Occurs when a window has been closed.
    /// </summary>
    public event AsyncEventHandler<WindowClosedEventArgs>? WindowClosed;

    /// <summary>
    ///     Occurs when a window's bounds have changed.
    /// </summary>
    public event AsyncEventHandler<WindowBoundsChangedEventArgs>? WindowBoundsChanged;
#pragma warning restore CA1003 // Use generic event handler instances
#pragma warning restore MA0046 // Use EventHandler<T> to declare events

    /// <summary>
    ///     Gets occurs when a window's metadata has changed.
    /// </summary>
    public IObservable<WindowMetadataChange> MetadataChanged { get; }

    /// <summary>
    ///     Gets an observable stream of window lifecycle events.
    /// </summary>
    public IObservable<WindowLifecycleEvent> WindowEvents { get; }

    /// <summary>
    ///     Gets the currently active window, if any.
    /// </summary>
    public IManagedWindow? ActiveWindow { get; }

    /// <summary>
    ///     Gets a collection of all currently open windows.
    /// </summary>
    public IReadOnlyCollection<IManagedWindow> OpenWindows { get; }

    /// <summary>
    ///     Registers a new window with the manager.
    /// </summary>
    /// <param name="window">The window instance to register.</param>
    /// <param name="metadata">Optional initial metadata.</param>
    /// <returns>
    ///     A <see cref="Task{TResult}"/> that represents the asynchronous operation.
    ///     The task result contains the <see cref="IManagedWindow"/> created for the registered window.
    /// </returns>
    public Task<IManagedWindow> RegisterWindowAsync(Window window, IReadOnlyDictionary<string, object>? metadata = null);

    /// <summary>
    ///     Registers a new window with the manager and applies category-based decoration.
    /// </summary>
    /// <param name="window">The window instance to register.</param>
    /// <param name="category">The category to assign to the window.</param>
    /// <param name="metadata">Optional initial metadata.</param>
    /// <returns>
    ///     A <see cref="Task{TResult}"/> that represents the asynchronous operation.
    ///     The task result contains the <see cref="IManagedWindow"/> created for the registered window.
    /// </returns>
    public Task<IManagedWindow> RegisterDecoratedWindowAsync(Window window, WindowCategory category, IReadOnlyDictionary<string, object>? metadata = null);

    /// <summary>
    ///     Closes a managed window.
    /// </summary>
    /// <param name="windowId">The ID of the window to close.</param>
    /// <returns>
    ///     A <see cref="Task{TResult}"/> that represents the asynchronous operation.
    ///     The task result contains <see langword="true"/> if the window was closed; otherwise,
    ///     <see langword="false"/>.
    /// </returns>
    public Task<bool> CloseWindowAsync(WindowId windowId);

    /// <summary>
    ///     Closes all managed windows.
    /// </summary>
    /// <returns>A task representing the asynchronous operation.</returns>
    public Task CloseAllWindowsAsync();

    /// <summary>
    ///     Activates (brings to foreground) a managed window.
    /// </summary>
    /// <param name="windowId">The ID of the window to activate.</param>
    public void ActivateWindow(WindowId windowId);

    /// <summary>
    ///     Retrieves the managed window context for a given ID.
    /// </summary>
    /// <param name="windowId">The ID of the window to retrieve.</param>
    /// <returns>The <see cref="IManagedWindow"/> if found; otherwise, null.</returns>
    public IManagedWindow? GetWindow(WindowId windowId);

    /// <summary>
    ///     Minimizes the specified window.
    /// </summary>
    /// <param name="windowId">The ID of the window to minimize.</param>
    /// <returns>A task representing the asynchronous operation.</returns>
    public Task MinimizeWindowAsync(WindowId windowId);

    /// <summary>
    ///     Maximizes the specified window.
    /// </summary>
    /// <param name="windowId">The ID of the window to maximize.</param>
    /// <returns>A task representing the asynchronous operation.</returns>
    public Task MaximizeWindowAsync(WindowId windowId);

    /// <summary>
    ///     Restores the specified window to its previous state (from Minimized or Maximized).
    /// </summary>
    /// <param name="windowId">The ID of the window to restore.</param>
    /// <returns>A task representing the asynchronous operation.</returns>
    public Task RestoreWindowAsync(WindowId windowId);

    /// <summary>
    ///     Moves the specified window to a new screen position.
    /// </summary>
    /// <param name="windowId">The ID of the window to move.</param>
    /// <param name="position">The new screen position.</param>
    /// <returns>A <see cref="Task"/> representing the asynchronous operation.</returns>
    public Task MoveWindowAsync(WindowId windowId, Windows.Graphics.PointInt32 position);

    /// <summary>
    ///     Resizes the specified window.
    /// </summary>
    /// <param name="windowId">The ID of the window to resize.</param>
    /// <param name="size">The new window size.</param>
    /// <returns>A <see cref="Task"/> representing the asynchronous operation.</returns>
    public Task ResizeWindowAsync(WindowId windowId, Windows.Graphics.SizeInt32 size);

    /// <summary>
    ///     Sets the bounds (position and size) of the specified window.
    /// </summary>
    /// <param name="windowId">The ID of the window to manipulate.</param>
    /// <param name="bounds">The desired bounds for the window.</param>
    /// <returns>A <see cref="Task"/> representing the asynchronous operation.</returns>
    public Task SetWindowBoundsAsync(WindowId windowId, Windows.Graphics.RectInt32 bounds);

    /// <summary>
    ///     Sets the minimum size constraints for the specified window.
    /// </summary>
    /// <param name="windowId">The ID of the window.</param>
    /// <param name="minimumWidth">The minimum width constraint, or <see langword="null"/> to remove the constraint.</param>
    /// <param name="minimumHeight">The minimum height constraint, or <see langword="null"/> to remove the constraint.</param>
    /// <returns>A <see cref="Task"/> representing the asynchronous operation.</returns>
    /// <remarks>
    ///     These constraints affect both user resizing and programmatic resize operations.
    ///     Only works for windows with an <see cref="Microsoft.UI.Windowing.OverlappedPresenter"/>.
    /// </remarks>
    public Task SetWindowMinimumSizeAsync(WindowId windowId, int? minimumWidth, int? minimumHeight);

    /// <summary>
    ///     Returns a JSON placement string describing the window's current virtual desktop bounds
    ///     and the monitor work area at the time of capture. The string is suitable for persisting
    ///     and later restoring via <see cref="RestoreWindowPlacementAsync"/>.
    /// </summary>
    /// <param name="windowId">The ID of the window.</param>
    /// <returns>A JSON string describing the placement, or <see langword="null"/> if the window is not found.</returns>
    public string? GetWindowPlacementString(WindowId windowId);

    /// <summary>
    ///     Restores a window placement previously obtained from <see cref="GetWindowPlacementString"/>.
    ///     The implementation will attempt to place the window on the same monitor if available,
    ///     or fall back to a visible monitor if the original monitor is no longer connected.
    /// </summary>
    /// <param name="windowId">The ID of the window to restore.</param>
    /// <param name="placement">The placement JSON string previously returned by <see cref="GetWindowPlacementString"/>.</param>
    /// <returns>A <see cref="Task"/> representing the asynchronous operation.</returns>
    public Task RestoreWindowPlacementAsync(WindowId windowId, string placement);

    /// <summary>
    ///     Sets a metadata value for a window.
    /// </summary>
    /// <param name="windowId">The ID of the window.</param>
    /// <param name="key">The metadata key.</param>
    /// <param name="value">The metadata value.</param>
    public void SetMetadata(WindowId windowId, string key, object? value);

    /// <summary>
    ///     Removes a metadata value from a window.
    /// </summary>
    /// <param name="windowId">The ID of the window.</param>
    /// <param name="key">The metadata key.</param>
    public void RemoveMetadata(WindowId windowId, string key);

    /// <summary>
    ///     Tries to get a metadata value for a window.
    /// </summary>
    /// <param name="windowId">The ID of the window.</param>
    /// <param name="key">The metadata key.</param>
    /// <returns>The metadata value if found; otherwise, <see langword="null"/>.</returns>
    public object? TryGetMetadataValue(WindowId windowId, string key);
}
