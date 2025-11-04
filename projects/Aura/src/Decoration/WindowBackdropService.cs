// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Aura.Windowing;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Composition.SystemBackdrops;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Media;

namespace DroidNet.Aura.Decoration;

/// <summary>
///     Service that manages backdrop application for windows based on their decoration settings.
/// </summary>
/// <remarks>
///     The service observes window lifecycle events and automatically applies backdrops when windows are created,
///     using the backdrop specified in each window's <see cref="WindowDecorationOptions"/>.
///     <para>
///     Backdrops are applied using WinUI 3's <see cref="Window.SystemBackdrop"/> property with:
///     </para>
///     <list type="bullet">
///     <item><see cref="MicaBackdrop"/> for <see cref="BackdropKind.Mica"/></item>
///     <item><see cref="MicaBackdrop"/> with <see cref="MicaKind.BaseAlt"/> for <see cref="BackdropKind.MicaAlt"/></item>
///     <item><see cref="DesktopAcrylicBackdrop"/> for <see cref="BackdropKind.Acrylic"/></item>
///     <item>null for <see cref="BackdropKind.None"/> or when no backdrop is specified</item>
///     </list>
///     <para>
///     All backdrop applications are wrapped in exception handling. Errors are logged at Warning level
///     without throwing, allowing windows to function normally without backdrops.
///     </para>
/// </remarks>
public sealed partial class WindowBackdropService : IDisposable
{
    private readonly IWindowManagerService windowManager;
    private readonly ILogger<WindowBackdropService> logger;
    private readonly IDisposable? windowEventsSubscription;
    private bool disposed;

    /// <summary>
    ///     Initializes a new instance of the <see cref="WindowBackdropService"/> class.
    /// </summary>
    /// <param name="windowManager">The window manager service for observing window lifecycle events.</param>
    /// <param name="loggerFactory">
    ///     Optional logger factory used to create a service logger. If <see langword="null"/>,
    ///     a <see cref="NullLogger{T}"/> is used.
    /// </param>
    public WindowBackdropService(
        IWindowManagerService windowManager,
        ILoggerFactory? loggerFactory = null)
    {
        ArgumentNullException.ThrowIfNull(windowManager);

        this.windowManager = windowManager;
        this.logger = loggerFactory?.CreateLogger<WindowBackdropService>() ?? NullLogger<WindowBackdropService>.Instance;

        // Subscribe to window lifecycle events
        this.windowEventsSubscription = windowManager.WindowEvents.Subscribe(this.OnWindowEvent);
    }

    /// <summary>
    ///     Releases resources used by the backdrop service.
    /// </summary>
    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.windowEventsSubscription?.Dispose();
        this.disposed = true;
    }

    /// <summary>
    ///     Applies backdrop to a single window context.
    /// </summary>
    /// <param name="context">The window context to apply backdrop to.</param>
    public void ApplyBackdrop(WindowContext context)
    {
        ArgumentNullException.ThrowIfNull(context);

        var window = context.Window;
        var backdrop = context.Decorations?.Backdrop;

        // If no backdrop is specified, don't apply anything
        if (!backdrop.HasValue)
        {
            return;
        }

        this.LogApplyingBackdrop(backdrop.Value);

        if (backdrop.Value == BackdropKind.None)
        {
            this.LogSkippingNoneBackdrop();
            window.SystemBackdrop = null;
            return;
        }

        try
        {
            window.SystemBackdrop = CreateSystemBackdrop(backdrop.Value);
            this.LogBackdropApplied(backdrop.Value);
        }
#pragma warning disable CA1031 // Do not catch general exception types - intentional for graceful degradation
        catch (Exception ex)
#pragma warning restore CA1031
        {
            this.LogBackdropApplicationFailed(ex, backdrop.Value);
        }
    }

    /// <summary>
    ///     Applies backdrops to all open windows.
    /// </summary>
    public void ApplyBackdrop() => this.ApplyBackdrop(_ => true);

    /// <summary>
    ///     Applies backdrops to windows matching a predicate.
    /// </summary>
    /// <param name="predicate">Predicate to filter which windows should have backdrops applied.</param>
    public void ApplyBackdrop(Func<WindowContext, bool> predicate)
    {
        ArgumentNullException.ThrowIfNull(predicate);

        this.LogApplyingBackdropsToWindows();

        foreach (var context in this.windowManager.OpenWindows.Where(predicate))
        {
            this.ApplyBackdrop(context);
        }
    }

    /// <summary>
    ///     Creates a WinUI 3 SystemBackdrop instance for the specified backdrop kind.
    /// </summary>
    /// <param name="backdropKind">The backdrop kind to create.</param>
    /// <returns>A SystemBackdrop instance, or null for BackdropKind.None.</returns>
    private static SystemBackdrop? CreateSystemBackdrop(BackdropKind backdropKind)
        => backdropKind switch
        {
            BackdropKind.None => null,
            BackdropKind.Mica => new MicaBackdrop { Kind = MicaKind.Base },
            BackdropKind.MicaAlt => new MicaBackdrop { Kind = MicaKind.BaseAlt },
            BackdropKind.Acrylic => new DesktopAcrylicBackdrop(),
            _ => throw new ArgumentOutOfRangeException(
                nameof(backdropKind),
                backdropKind,
                $"Unsupported backdrop kind: {backdropKind}"),
        };

    /// <summary>
    ///     Applies backdrops to windows of a specific category.
    /// </summary>
    /// <param name="category">The window category.</param>
    private void ApplyBackdropToCategory(string category)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(category);

        this.ApplyBackdrop(ctx
            => category.Equals(ctx.Decorations?.Category)
                && ctx.Decorations?.Backdrop is null);
    }

    /// <summary>
    ///     Handles window lifecycle events to automatically apply backdrops to newly created windows.
    /// </summary>
    /// <param name="evt">The window lifecycle event.</param>
    private void OnWindowEvent(WindowLifecycleEvent evt)
    {
        if (evt.EventType == WindowLifecycleEventType.Created)
        {
            this.ApplyBackdrop(evt.Context);
        }
    }
}
