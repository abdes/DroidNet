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
    /// <summary>
    ///     Gets an observable stream of window lifecycle events emitted for every registered window.
    /// </summary>
    public IObservable<WindowLifecycleEvent> WindowEvents { get; }

    /// <summary>
    ///     Gets the context of the window that is currently active, if one has focus.
    /// </summary>
    public WindowContext? ActiveWindow { get; }

    /// <summary>
    ///     Gets a snapshot of all windows currently tracked by the manager.
    /// </summary>
    public IReadOnlyCollection<WindowContext> OpenWindows { get; }

    /// <summary>
    ///     Registers an already-created window using the default <see cref="WindowCategory.System"/> classification.
    /// </summary>
    /// <param name="window">The window instance to register.</param>
    /// <param name="metadata">Optional metadata to associate with the window.</param>
    /// <returns>The <see cref="WindowContext"/> created for the registered window.</returns>
    /// <exception cref="InvalidOperationException">
    ///     Thrown when the window is already registered or when registration fails.
    /// </exception>
    public Task<WindowContext> RegisterWindowAsync(
        Window window,
        IReadOnlyDictionary<string, object>? metadata = null);

    /// <summary>
    ///     Registers an already-created window with explicit classification and optional decoration metadata.
    /// </summary>
    /// <param name="window">The window instance to register.</param>
    /// <param name="category">Window category identifier. Use constants from <see cref="WindowCategory"/>.</param>
    /// <param name="metadata">Optional metadata to associate with the window.</param>
    /// <returns>The <see cref="WindowContext"/> created for the registered window.</returns>
    /// <exception cref="InvalidOperationException">
    ///     Thrown when the window is already registered or when registration fails.
    /// </exception>
    public Task<WindowContext> RegisterDecoratedWindowAsync(
        Window window,
        WindowCategory category,
        IReadOnlyDictionary<string, object>? metadata = null);

    /// <summary>
    ///     Closes a specific window by its context.
    /// </summary>
    /// <param name="context">The window context to close.</param>
    /// <returns>True if the window was successfully closed; otherwise, false.</returns>
    public Task<bool> CloseWindowAsync(WindowContext context);

    /// <summary>
    ///     Closes a window by its unique identifier.
    /// </summary>
    /// <param name="windowId">The unique identifier of the window to close.</param>
    /// <returns>True if the window was found and closed; otherwise, false.</returns>
    public Task<bool> CloseWindowAsync(WindowId windowId);

    /// <summary>
    ///     Requests activation for a specific window context.
    /// </summary>
    /// <param name="context">The window context to activate.</param>
    public void ActivateWindow(WindowContext context);

    /// <summary>
    ///     Requests activation for a window by its unique identifier.
    /// </summary>
    /// <param name="windowId">The unique identifier of the window to activate.</param>
    public void ActivateWindow(WindowId windowId);

    /// <summary>
    ///     Attempts to find a window context by its identifier.
    /// </summary>
    /// <param name="windowId">The unique identifier of the window.</param>
    /// <returns>The <see cref="WindowContext"/> if found; otherwise, null.</returns>
    public WindowContext? GetWindow(WindowId windowId);

    /// <summary>
    ///     Returns the subset of tracked windows that match a given category.
    /// </summary>
    /// <param name="category">The window category to filter by. Use constants from <see cref="WindowCategory"/>.</param>
    /// <returns>A collection of matching window contexts.</returns>
    public IReadOnlyCollection<WindowContext> GetWindowsByCategory(WindowCategory category);

    /// <summary>
    ///     Initiates closure of every window currently tracked by the manager.
    /// </summary>
    /// <returns>A task representing the asynchronous operation.</returns>
    public Task CloseAllWindowsAsync();
}
