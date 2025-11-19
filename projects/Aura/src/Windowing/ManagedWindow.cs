// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.WinUI;
using DroidNet.Aura.Decoration;
using DroidNet.Controls.Menus;
using Microsoft.UI;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;

namespace DroidNet.Aura.Windowing;

/// <summary>
///     Encapsulates metadata and state information for a managed window.
/// </summary>
/// <remarks>
///     When decoration specifies a menu via <see cref="WindowDecorationOptions.Menu"/>,
///     the ManagedWindow will resolve the menu provider from the service provider during creation
///     and store the resulting <see cref="IMenuSource"/> for the lifetime of the window.
/// <para>
///     Menu sources are lightweight data structures that do not require explicit disposal. They
///     will be garbage collected when the ManagedWindow is no longer referenced.
/// </para>
/// </remarks>
internal sealed partial class ManagedWindow : ObservableObject, IManagedWindow
{
    private IMenuSource? menuSource;

    /// <inheritdoc/>
    public required WindowId Id { get; init; }

    /// <inheritdoc/>
    public required DispatcherQueue DispatcherQueue { get; init; }

    /// <inheritdoc/>
    public required Window Window { get; init; }

    /// <inheritdoc/>
    public bool IsActive { get; private set; }

    /// <inheritdoc/>
    public required WindowCategory? Category { get; init; }

    /// <inheritdoc/>
    [ObservableProperty]
    public partial WindowDecorationOptions? Decorations { get; set; }

    /// <inheritdoc/>
    public IMenuSource? MenuSource => this.menuSource;

    /// <inheritdoc/>
    IReadOnlyDictionary<string, object>? IManagedWindow.Metadata => this.Metadata;

    /// <inheritdoc/>
    public required DateTimeOffset CreatedAt { get; init; }

    /// <inheritdoc/>
    public DateTimeOffset? LastActivatedAt { get; private set; }

    /// <inheritdoc/>
    public Windows.Graphics.RectInt32 CurrentBounds
        => new(
            this.Window.AppWindow.Position.X,
            this.Window.AppWindow.Position.Y,
            this.Window.AppWindow.Size.Width,
            this.Window.AppWindow.Size.Height);

    /// <inheritdoc/>
    public Windows.Graphics.RectInt32 RestoredBounds { get; internal set; }

    /// <inheritdoc/>
    public int? MinimumWidth { get; internal set; }

    /// <inheritdoc/>
    public int? MinimumHeight { get; internal set; }

    /// <summary>
    ///     Gets or sets the optional metadata for custom window properties.
    /// </summary>
    internal IReadOnlyDictionary<string, object>? Metadata { get; set; }

    /// <summary>
    ///     Gets or sets an action to execute when the window is closed, used for cleanup.
    /// </summary>
    internal Action? Cleanup { get; set; }

    /// <inheritdoc/>
    public bool IsMinimized()
        => this.Window.AppWindow.Presenter is OverlappedPresenter presenter
            && presenter.State == OverlappedPresenterState.Minimized;

    /// <inheritdoc/>
    public bool IsMaximized()
        => this.Window.AppWindow.Presenter is OverlappedPresenter presenter
            && presenter.State == OverlappedPresenterState.Maximized;

    /// <inheritdoc/>
    public bool IsFullScreen()
        => this.Window.AppWindow.Presenter is FullScreenPresenter;

    /// <inheritdoc/>
    public bool IsCompactOverlay()
        => this.Window.AppWindow.Presenter is CompactOverlayPresenter;

    /// <inheritdoc/>
    public async Task MinimizeAsync()
    {
        await this.DispatcherQueue.EnqueueAsync(() =>
        {
            if (this.Window.AppWindow.Presenter is OverlappedPresenter presenter)
            {
                // Capture current bounds before minimizing if in Restored state
                if (presenter.State == OverlappedPresenterState.Restored)
                {
                    this.RestoredBounds = this.CurrentBounds;
                }

                presenter.Minimize();
            }
        }).ConfigureAwait(false);
    }

    /// <inheritdoc/>
    public async Task MaximizeAsync()
    {
        await this.DispatcherQueue.EnqueueAsync(() =>
        {
            if (this.Window.AppWindow.Presenter is OverlappedPresenter presenter)
            {
                // Capture current bounds before maximizing if in Restored state
                if (presenter.State == OverlappedPresenterState.Restored)
                {
                    this.RestoredBounds = this.CurrentBounds;
                }

                presenter.Maximize();
            }
        }).ConfigureAwait(false);
    }

    /// <inheritdoc/>
    public async Task RestoreAsync()
    {
        await this.DispatcherQueue.EnqueueAsync(() =>
        {
            if (this.Window.AppWindow.Presenter is OverlappedPresenter presenter)
            {
                presenter.Restore();

                // After restoring, apply saved RestoredBounds if they differ from current bounds
                var restoredBounds = this.RestoredBounds;
                if (restoredBounds != this.CurrentBounds)
                {
                    this.Window.AppWindow.MoveAndResize(restoredBounds);
                }
            }
        }).ConfigureAwait(false);
    }

    /// <inheritdoc/>
    public async Task MoveAsync(Windows.Graphics.PointInt32 position)
    {
        await this.DispatcherQueue.EnqueueAsync(() => this.Window.AppWindow.Move(position)).ConfigureAwait(false);
    }

    /// <inheritdoc/>
    public async Task ResizeAsync(Windows.Graphics.SizeInt32 size)
    {
        await this.DispatcherQueue.EnqueueAsync(() => this.Window.AppWindow.Resize(size)).ConfigureAwait(false);
    }

    /// <inheritdoc/>
    public async Task SetBoundsAsync(Windows.Graphics.RectInt32 bounds)
    {
        await this.DispatcherQueue.EnqueueAsync(() =>
        {
            this.Window.AppWindow.Move(new Windows.Graphics.PointInt32 { X = bounds.X, Y = bounds.Y });
            this.Window.AppWindow.Resize(new Windows.Graphics.SizeInt32 { Width = bounds.Width, Height = bounds.Height });
        }).ConfigureAwait(false);
    }

    /// <summary>
    ///     Updates the activation state of this window context.
    /// </summary>
    /// <param name="isActive">Whether the window is active.</param>
    /// <returns>A new <see cref="ManagedWindow"/> with updated activation state.</returns>
    internal ManagedWindow WithActivationState(bool isActive)
    {
        this.IsActive = isActive;
        this.LastActivatedAt = isActive ? DateTimeOffset.UtcNow : this.LastActivatedAt;
        return this;
    }

    /// <summary>
    ///     Sets the menu source for this window context.
    /// </summary>
    /// <param name="menuSource">The menu source to set.</param>
    /// <remarks>
    ///     This method is intended to be called by <see cref="IWindowFactory"/>
    ///     implementations during context initialization.
    /// </remarks>
    internal void SetMenuSource(IMenuSource menuSource)
    {
        ArgumentNullException.ThrowIfNull(menuSource);
        this.menuSource = menuSource;
    }
}
