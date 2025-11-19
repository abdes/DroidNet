// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using DroidNet.Aura.Decoration;
using DroidNet.Controls.Menus;
using Microsoft.UI;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;

namespace DroidNet.Aura.Windowing;

/// <summary>
///     Defines the contract for a managed window that encapsulates metadata and state information.
/// </summary>
/// <remarks>
///     When decoration specifies a menu via <see cref="WindowDecorationOptions.Menu"/>,
///     the managed window will resolve the menu provider from the service provider during creation
///     and store the resulting <see cref="IMenuSource"/> for the lifetime of the window.
/// <para>
///     Menu sources are lightweight data structures that do not require explicit disposal. They
///     will be garbage collected when the managed window is no longer referenced.
/// </para>
/// </remarks>
public interface IManagedWindow : INotifyPropertyChanged, INotifyPropertyChanging
{
    /// <summary>
    ///     Gets the unique identifier for the window.
    /// </summary>
    public WindowId Id { get; }

    /// <summary>
    ///     Gets the dispatcher queue for executing UI thread operations.
    /// </summary>
    public DispatcherQueue DispatcherQueue { get; }

    /// <summary>
    ///     Gets the WinUI Window instance.
    /// </summary>
    public Window Window { get; }

    /// <summary>
    ///     Gets a value indicating whether the window is currently active.
    /// </summary>
    public bool IsActive { get; }

    /// <summary>
    ///     Gets the category of the window.
    /// </summary>
    public WindowCategory? Category { get; }

    /// <summary>
    ///     Gets or sets the optional decoration options for the window.
    /// </summary>
    public WindowDecorationOptions? Decorations { get; set; }

    /// <summary>
    ///     Gets the menu source for this window, if one was created from a menu provider.
    /// </summary>
    /// <remarks>
    ///     This property returns the menu source that was created during window initialization
    ///     based on the decoration's menu options. Returns null if no menu was specified or if the
    ///     menu provider could not be found.
    /// </remarks>
    public IMenuSource? MenuSource { get; }

    /// <summary>
    ///     Gets the optional metadata for custom window properties.
    /// </summary>
    public IReadOnlyDictionary<string, object>? Metadata { get; }

    /// <summary>
    ///     Gets the timestamp when the window was created.
    /// </summary>
    public DateTimeOffset CreatedAt { get; }

    /// <summary>
    ///     Gets the timestamp of the most recent activation.
    /// </summary>
    public DateTimeOffset? LastActivatedAt { get; }

    /// <summary>
    ///     Gets the current on-screen bounds of the window (derived from the underlying AppWindow position/size).
    /// </summary>
    public Windows.Graphics.RectInt32 CurrentBounds { get; }

    /// <summary>
    ///     Gets the bounds to restore to when the window is not maximized or minimized.
    ///     Used for persisting window position across application restarts.
    /// </summary>
    /// <remarks>
    ///     This value is captured:
    ///     <list type="bullet">
    ///         <item>Initially when the window is created</item>
    ///         <item>When the window is moved or resized while in Restored state</item>
    ///         <item>Before the window is maximized or minimized</item>
    ///     </list>
    ///     This ensures we always have a valid position to restore to, even if the application
    ///     is closed while maximized or minimized.
    /// </remarks>
    public Windows.Graphics.RectInt32 RestoredBounds { get; }

    /// <summary>
    ///     Gets the minimum width constraint for the window.
    /// </summary>
    /// <remarks>
    ///     This value constrains both user resizing and programmatic resize operations.
    ///     When set, the window cannot be resized smaller than this width.
    /// </remarks>
    public int? MinimumWidth { get; }

    /// <summary>
    ///     Gets the minimum height constraint for the window.
    /// </summary>
    /// <remarks>
    ///     This value constrains both user resizing and programmatic resize operations.
    ///     When set, the window cannot be resized smaller than this height.
    /// </remarks>
    public int? MinimumHeight { get; }

    /// <summary>
    ///     Gets a value indicating whether the window is currently minimized.
    /// </summary>
    /// <returns><c>true</c> if the window is minimized; otherwise, <c>false</c>.</returns>
    public bool IsMinimized();

    /// <summary>
    ///     Gets a value indicating whether the window is currently maximized.
    /// </summary>
    /// <returns><c>true</c> if the window is maximized; otherwise, <c>false</c>.</returns>
    public bool IsMaximized();

    /// <summary>
    ///     Gets a value indicating whether the window is currently in full-screen mode.
    /// </summary>
    /// <returns><c>true</c> if the window is in full-screen mode; otherwise, <c>false</c>.</returns>
    public bool IsFullScreen();

    /// <summary>
    ///     Gets a value indicating whether the window is currently in compact overlay mode.
    /// </summary>
    /// <returns><c>true</c> if the window is in compact overlay mode; otherwise, <c>false</c>.</returns>
    public bool IsCompactOverlay();

    /// <summary>
    ///     Minimizes the window asynchronously.
    /// </summary>
    /// <returns>A task representing the asynchronous operation.</returns>
    public Task MinimizeAsync();

    /// <summary>
    ///     Maximizes the window asynchronously.
    /// </summary>
    /// <returns>A task representing the asynchronous operation.</returns>
    public Task MaximizeAsync();

    /// <summary>
    ///     Restores the window asynchronously.
    /// </summary>
    /// <returns>A task representing the asynchronous operation.</returns>
    public Task RestoreAsync();

    /// <summary>
    ///     Moves the window to the specified position asynchronously.
    /// </summary>
    /// <param name="position">The target position.</param>
    /// <returns>A task representing the asynchronous operation.</returns>
    public Task MoveAsync(Windows.Graphics.PointInt32 position);

    /// <summary>
    ///     Resizes the window to the specified size asynchronously.
    /// </summary>
    /// <param name="size">The target size.</param>
    /// <returns>A task representing the asynchronous operation.</returns>
    public Task ResizeAsync(Windows.Graphics.SizeInt32 size);

    /// <summary>
    ///     Sets the window bounds (position and size) asynchronously.
    /// </summary>
    /// <param name="bounds">The target bounds.</param>
    /// <returns>A task representing the asynchronous operation.</returns>
    public Task SetBoundsAsync(Windows.Graphics.RectInt32 bounds);
}
