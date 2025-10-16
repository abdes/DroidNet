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
    IObservable<WindowLifecycleEvent> WindowEvents { get; }

    /// <summary>
    /// Gets the currently active window context, if any.
    /// </summary>
    WindowContext? ActiveWindow { get; }

    /// <summary>
    /// Gets all currently open windows.
    /// </summary>
    IReadOnlyCollection<WindowContext> OpenWindows { get; }

    /// <summary>
    /// Creates and displays a new window using the specified factory.
    /// </summary>
    /// <typeparam name="TWindow">The type of window to create.</typeparam>
    /// <param name="windowType">Semantic type identifier (e.g., "Main", "Tool", "Document").</param>
    /// <param name="title">Optional window title. If null, uses the window's default title.</param>
    /// <param name="metadata">Optional metadata to associate with the window.</param>
    /// <param name="activateWindow">Whether to activate the window after creation. Default is true.</param>
    /// <returns>The created <see cref="WindowContext"/>.</returns>
    /// <exception cref="InvalidOperationException">Thrown when window creation fails.</exception>
    Task<WindowContext> CreateWindowAsync<TWindow>(
        string windowType = "Main",
        string? title = null,
        IReadOnlyDictionary<string, object>? metadata = null,
        bool activateWindow = true)
        where TWindow : Window;

    /// <summary>
    /// Creates and displays a new window by type name.
    /// </summary>
    /// <param name="windowTypeName">The fully qualified type name of the window.</param>
    /// <param name="windowType">Semantic type identifier (e.g., "Main", "Tool", "Document").</param>
    /// <param name="title">Optional window title.</param>
    /// <param name="metadata">Optional metadata to associate with the window.</param>
    /// <param name="activateWindow">Whether to activate the window after creation.</param>
    /// <returns>The created <see cref="WindowContext"/>.</returns>
    Task<WindowContext> CreateWindowAsync(
        string windowTypeName,
        string windowType = "Main",
        string? title = null,
        IReadOnlyDictionary<string, object>? metadata = null,
        bool activateWindow = true);

    /// <summary>
    /// Closes a specific window by its context.
    /// </summary>
    /// <param name="context">The window context to close.</param>
    /// <returns>True if the window was successfully closed; otherwise, false.</returns>
    Task<bool> CloseWindowAsync(WindowContext context);

    /// <summary>
    /// Closes a window by its unique identifier.
    /// </summary>
    /// <param name="windowId">The unique identifier of the window to close.</param>
    /// <returns>True if the window was found and closed; otherwise, false.</returns>
    Task<bool> CloseWindowAsync(Guid windowId);

    /// <summary>
    /// Activates a specific window, bringing it to the foreground.
    /// </summary>
    /// <param name="context">The window context to activate.</param>
    void ActivateWindow(WindowContext context);

    /// <summary>
    /// Activates a window by its unique identifier.
    /// </summary>
    /// <param name="windowId">The unique identifier of the window to activate.</param>
    void ActivateWindow(Guid windowId);

    /// <summary>
    /// Gets a window context by its unique identifier.
    /// </summary>
    /// <param name="windowId">The unique identifier of the window.</param>
    /// <returns>The <see cref="WindowContext"/> if found; otherwise, null.</returns>
    WindowContext? GetWindow(Guid windowId);

    /// <summary>
    /// Gets all windows of a specific semantic type.
    /// </summary>
    /// <param name="windowType">The semantic window type to filter by.</param>
    /// <returns>A collection of matching window contexts.</returns>
    IReadOnlyCollection<WindowContext> GetWindowsByType(string windowType);

    /// <summary>
    /// Closes all open windows.
    /// </summary>
    /// <returns>A task representing the asynchronous operation.</returns>
    Task CloseAllWindowsAsync();
}
