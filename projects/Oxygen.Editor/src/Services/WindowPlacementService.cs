// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Reactive.Linq;
using DroidNet.Aura.Windowing;
using DroidNet.Routing;
using DroidNet.Routing.Events;
using Microsoft.EntityFrameworkCore;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Oxygen.Editor.Data;
using Oxygen.Editor.Data.Models;

namespace Oxygen.Editor.Services;

/// <summary>
///     Responsible for manging the persistence and retrieval of window placement information, and
///     its application to windows.
/// </summary>
/// <remarks>
///     This is a perliminary implementation that currently handle placement of the `ProjectBrowser`
///     and `WroldEditor` windows. Identification of the windows relies on the fact that we use
///     routed navigation, and wach window will be associated with a navigation context. The
///     Navigation URL is used to identify the window type, and the placement information is stored
///     in the editor persistent settings.
///     <para>
///     All placement settings will be associated with the `WindowPlacementService` module, keyed
///     by the URL of the window's navigation context. The placement is obtained from the
///     `WindowManagerService` and provided to it when it needs to be restored.
///     </para>
/// </remarks>
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "injected into public Application class")]
public sealed partial class WindowPlacementService : IDisposable
{
    private readonly ILogger logger;
    private readonly IWindowManagerService windowManager;
    private readonly IRouter router;
    private readonly IDisposable wmSubscription;
    private readonly IDisposable routerSubscription;
    private readonly Func<PersistentState> contextFactory;

    private readonly Dictionary<WindowId, WindowPlacementInfo> windows = [];
    private bool isDisposed;

    /// <summary>
    ///     Initializes a new instance of the <see cref="WindowPlacementService"/> class.
    /// </summary>
    /// <param name="windowManager">The window manager service used to manage window lifecycles and placement.</param>
    /// <param name="router">The router used for navigation context and events.</param>
    /// <param name="contextFactory">A factory function to create instances of <see cref="PersistentState"/> for data persistence.</param>
    /// <param name="loggerFactory">An optional logger factory for logging.</param>
    public WindowPlacementService(IWindowManagerService windowManager, IRouter router, Func<PersistentState> contextFactory, ILoggerFactory? loggerFactory = null)
    {
        this.logger = loggerFactory?.CreateLogger<WindowPlacementService>() ?? NullLoggerFactory.Instance.CreateLogger<WindowPlacementService>();

        this.windowManager = windowManager;
        this.router = router;
        this.contextFactory = contextFactory;

        this.windowManager.WindowClosed += this.OnWindowClosed;
        this.windowManager.WindowClosing += this.OnWindowClosing;
        this.wmSubscription = this.windowManager.WindowEvents
            .Where(e => e.EventType == WindowLifecycleEventType.Created)
            .Subscribe(e => this.OnWindowCreated(e.Context));

        this.routerSubscription = this.router.Events
            .OfType<ActivationStarted>() // At this stage we have the window, the URL, and no outlets have been activated yet
            .Subscribe(e =>
            {
                // We only care about contexts that have a navigation target that is not null
                // (typically a window).
                if (e.Context is not { NavigationTarget: { } })
                {
                    return;
                }

                this.OnNavigationContextCreated(e.Context);
            });
    }

    /// <inheritdoc/>
    public void Dispose()
    {
        // Do not change this code. Put cleanup code in 'Dispose(bool disposing)' method
        this.Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    /// <summary>
    ///     Releases the unmanaged resources used by the <see cref="WindowPlacementService"/> and
    ///     optionally releases the managed resources.
    /// </summary>
    /// <param name="disposing">
    ///     True to release both managed and unmanaged resources; false to release only unmanaged
    ///     resources.
    /// </param>
    private void Dispose(bool disposing)
    {
        if (this.isDisposed)
        {
            return;
        }

        if (disposing)
        {
            // No managed state (managed objects) to dispose of.
        }

        this.windowManager.WindowClosed -= this.OnWindowClosed;
        this.windowManager.WindowClosing -= this.OnWindowClosing;

        this.wmSubscription.Dispose();
        this.routerSubscription.Dispose();

        this.isDisposed = true;
    }

    private async Task OnWindowClosing(object? sender, WindowClosingEventArgs args)
    {
        // Check if we are tracking placement for such window (we should be).
        _ = this.windows.TryGetValue(args.WindowId, out var placementInfo);
        Debug.Assert(placementInfo is not null, message: $"WindowPlacementService: unexpected window (Id={args.WindowId.Value}) being closed that is not tracked.");

        // Get the current placement information for the window being closed, and save it.
        var placement = this.windowManager.GetWindowPlacementString(args.WindowId);
        if (placement is not null)
        {
            Debug.Assert(placementInfo.Key is not null, message: $"WindowPlacementService: unexpected null placement key for window (Id={args.WindowId.Value}).");
            await this.TrySaveWindowPlacementAsync(placementInfo.Key, placement).ConfigureAwait(false);
        }
    }

    private async Task OnWindowClosed(object? sender, WindowClosedEventArgs args)
        => _ = this.windows.Remove(args.WindowId);

    private async void OnWindowCreated(IManagedWindow managed)
    {
        // Check if the window has already been placed (unlikely, but to be sure).
        _ = this.windows.TryGetValue(managed.Id, out var placementInfo);
        if (placementInfo?.IsPLaced == true)
        {
            return;
        }

        if (placementInfo is null)
        {
            // Look for a placement key in the window's metadata.
            var placementKey = managed.Metadata is not null && managed.Metadata.TryGetValue("WindowPlacementKey", out var keyObj) && keyObj is string keyStr
                ? keyStr
                : null;
            if (placementKey is null)
            {
                // No placement key found, we cannot restore placement.
                return;
            }

            placementInfo = new WindowPlacementInfo
            {
                Key = placementKey,
                IsPLaced = false,
            };
            this.windows[managed.Id] = placementInfo;
        }

        // at this point we have a placement info, but it is not placed yet.
        await this.TryRestoreWindowPlacementAsync(managed.Id, placementInfo).ConfigureAwait(false);
    }

    // Try to find a placement record for this context, and if found and not applied, set its
    // placement key to the navigation URL. If not found (unlikley), add a new record.
    private async void OnNavigationContextCreated(INavigationContext context)
    {
        if (context.NavigationTarget is not Window window || context.State is null)
        {
            return;
        }

        var windowId = window.AppWindow.Id;
        _ = this.windows.TryGetValue(windowId, out var placementInfo);
        if (placementInfo?.IsPLaced == true)
        {
            return;
        }

        if (placementInfo is null)
        {
            placementInfo = new WindowPlacementInfo
            {
                Key = context.State.Url,
                IsPLaced = false,
            };
            this.windows[windowId] = placementInfo;
        }

        // at this point we have a placement info, but it is not placed yet.
        await this.TryRestoreWindowPlacementAsync(windowId, placementInfo).ConfigureAwait(false);
    }

    private async Task TryRestoreWindowPlacementAsync(WindowId windowId, WindowPlacementInfo placement)
    {
        if (placement?.Key is null)
        {
            return;
        }

        var context = this.contextFactory();
        try
        {
            var record = await context.Set<WindowPlacements>()
                .AsNoTracking()
                .FirstOrDefaultAsync(p => p.PlacementKey == placement.Key).ConfigureAwait(false);

            if (record is null || string.IsNullOrEmpty(record.PlacementData))
            {
                return;
            }

            await this.windowManager.RestoreWindowPlacementAsync(windowId, record.PlacementData).ConfigureAwait(false);
            placement.IsPLaced = true;
        }
        finally
        {
            await context.DisposeAsync().ConfigureAwait(false);
        }
    }

    private async Task TrySaveWindowPlacementAsync(string key, string placement)
    {
        var context = this.contextFactory();
        try
        {
            var tracked = await context.Set<WindowPlacements>()
                .FirstOrDefaultAsync(p => p.PlacementKey == key).ConfigureAwait(false);

            if (tracked is not null)
            {
                tracked.PlacementData = placement;
            }
            else
            {
                tracked = new WindowPlacements
                {
                    PlacementKey = key,
                    PlacementData = placement,
                };
                _ = context.Set<WindowPlacements>().Add(tracked);
            }

            _ = await context.SaveChangesAsync().ConfigureAwait(false);
        }
        finally
        {
            await context.DisposeAsync().ConfigureAwait(false);
        }
    }

    private class WindowPlacementInfo
    {
        public string? Key { get; set; }

        public bool IsPLaced { get; set; }
    }
}
