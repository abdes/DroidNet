// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;

namespace DroidNet.Aura.WindowManagement;

/// <summary>
/// Defines the contract for managing multiple windows in a WinUI 3 application.
/// </summary>
/// <remarks>
/// This service provides centralized management of window lifecycle, activation state,
/// and window enumeration. It supports reactive event streams for window state changes
/// and integrates with the Aura theme system for consistent appearance across all windows.
/// </remarks>
public interface IWindowManagerService : IDisposable
{
    /// <summary>
    /// Gets an observable stream of window lifecycle events (Created, Closed, Activated).
    /// </summary>
    public IObservable<WindowLifecycleEvent> WindowEvents { get; }

    /// <summary>
    /// Gets the currently active window context, if any.
    /// </summary>
    public WindowContext? ActiveWindow { get; }

    /// <summary>
    /// Gets all currently open windows.
    /// </summary>
    public IReadOnlyCollection<WindowContext> OpenWindows { get; }

    /// <summary>
    /// Creates and displays a new window using the specified factory.
    /// </summary>
    /// <typeparam name="TWindow">The type of window to create.</typeparam>
    /// <param name="windowType">Window category identifier. Use constants from <see cref="WindowCategory"/>.</param>
    /// <param name="title">Optional window title. If null, uses the window's default title.</param>
    /// <param name="metadata">Optional metadata to associate with the window.</param>
    /// <param name="activateWindow">Whether to activate the window after creation. Default is true.</param>
    /// <returns>The created <see cref="WindowContext"/>.</returns>
    /// <exception cref="InvalidOperationException">Thrown when window creation fails.</exception>
    public Task<WindowContext> CreateWindowAsync<TWindow>(
        string windowType = WindowCategory.Main,
        string? title = null,
        IReadOnlyDictionary<string, object>? metadata = null,
        bool activateWindow = true)
        where TWindow : Window;

    /// <summary>
    /// Creates and displays a new window by type name.
    /// </summary>
    /// <param name="windowTypeName">The fully qualified type name of the window.</param>
    /// <param name="windowType">Window category identifier. Use constants from <see cref="WindowCategory"/>.</param>
    /// <param name="title">Optional window title.</param>
    /// <param name="metadata">Optional metadata to associate with the window.</param>
    /// <param name="activateWindow">Whether to activate the window after creation.</param>
    /// <returns>The created <see cref="WindowContext"/>.</returns>
    public Task<WindowContext> CreateWindowAsync(
        string windowTypeName,
        string windowType = WindowCategory.Main,
        string? title = null,
        IReadOnlyDictionary<string, object>? metadata = null,
        bool activateWindow = true);

    /// <summary>
    /// Closes a specific window by its context.
    /// </summary>
    /// <param name="context">The window context to close.</param>
    /// <returns>True if the window was successfully closed; otherwise, false.</returns>
    public Task<bool> CloseWindowAsync(WindowContext context);

    /// <summary>
    /// Closes a window by its unique identifier.
    /// </summary>
    /// <param name="windowId">The unique identifier of the window to close.</param>
    /// <returns>True if the window was found and closed; otherwise, false.</returns>
    public Task<bool> CloseWindowAsync(Guid windowId);

    /// <summary>
    /// Activates a specific window, bringing it to the foreground.
    /// </summary>
    /// <param name="context">The window context to activate.</param>
    public void ActivateWindow(WindowContext context);

    /// <summary>
    /// Activates a window by its unique identifier.
    /// </summary>
    /// <param name="windowId">The unique identifier of the window to activate.</param>
    public void ActivateWindow(Guid windowId);

    /// <summary>
    /// Gets a window context by its unique identifier.
    /// </summary>
    /// <param name="windowId">The unique identifier of the window.</param>
    /// <returns>The <see cref="WindowContext"/> if found; otherwise, null.</returns>
    public WindowContext? GetWindow(Guid windowId);

    /// <summary>
    /// Gets all windows of a specific category.
    /// </summary>
    /// <param name="windowType">The window category to filter by. Use constants from <see cref="WindowCategory"/>.</param>
    /// <returns>A collection of matching window contexts.</returns>
    public IReadOnlyCollection<WindowContext> GetWindowsByType(string windowType);

    /// <summary>
    /// Closes all open windows.
    /// </summary>
    /// <returns>A task representing the asynchronous operation.</returns>
    public Task CloseAllWindowsAsync();

    /// <summary>
    /// Registers an already-created window with the window manager.
    /// </summary>
    /// <param name="window">The window instance to register.</param>
    /// <param name="windowType">Window category identifier. Use constants from <see cref="WindowCategory"/>.</param>
    /// <param name="title">Optional window title. If null, uses the window's current title.</param>
    /// <param name="metadata">Optional metadata to associate with the window.</param>
    /// <returns>The created <see cref="WindowContext"/> for the registered window.</returns>
    /// <exception cref="InvalidOperationException">Thrown when the window is already registered.</exception>
    /// <remarks>
    /// This method is primarily used for integrating windows created outside the window manager,
    /// such as windows created by the routing system. The window must not already be tracked.
    /// </remarks>
    public Task<WindowContext> RegisterWindowAsync(
        Window window,
        string windowType = WindowCategory.Main,
        string? title = null,
        IReadOnlyDictionary<string, object>? metadata = null);
}
