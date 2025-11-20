// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;
using System.Text.Json;
using CommunityToolkit.WinUI;
using Microsoft.UI;
using Microsoft.UI.Windowing;

namespace DroidNet.Aura.Windowing;

/// <summary>
///     Window placement persistence helpers for <see cref="WindowManagerService"/>.
/// </summary>
/// <remarks>
///     When Aura records your window layout, it looks at the window’s last “normal” size and
///     position (even if you currently have it minimized or maximized). It figures out which
///     monitor that rectangle belongs to so it can keep the right work area in mind, then notes
///     whether you left the window restored, maximized, minimized, or in full screen. All of that
///     gets written into a small JSON blob whenever the window manager asks for placement data, so
///     whatever persistence store you use (settings, profile sync, etc.) can remember both the
///     geometry and the presenter state.
///     <para>
///     When Aura restores a window, it reads that blob back in. It first verifies the saved bounds,
///     clamps or centers them so they stay on-screen relative to the monitor that was active
///     before, and applies those bounds as the new “restore” rectangle. Only after the bounds are
///     locked in does Aura reapply the presenter: full screen if that’s what you used last time,
///     otherwise maximized, minimized, or simply restored. The result is that your windows come
///     back exactly how you left them, without flashing through the wrong size or losing their
///     preferred position.
///     </para><para>
///     NOTE: The actual persistence mechanism (where you store the JSON blob) is up to you. This
///     class just provides the means to serialize and deserialize/apply the placement data.
///     </para>
/// </remarks>
public sealed partial class WindowManagerService
{
    private enum WindowPlacementPresenterState
    {
        Restored,
        Maximized,
        Minimized,
        FullScreen,
    }

    /// <inheritdoc />
    public string? GetWindowPlacementString(WindowId windowId)
    {
        if (!this.windows.TryGetValue(windowId, out var window))
        {
            return null;
        }

        var bounds = window.RestoredBounds;
        var presenterState = GetPersistedPresenterState(window);

        // Use the window center to resolve the monitor work area
        var centerX = bounds.X + (bounds.Width / 2);
        var centerY = bounds.Y + (bounds.Height / 2);

        var hMon = MonitorFromPoint(new POINT(centerX, centerY), MonitorDefaultToNearest);
        var mi = default(MONITORINFO);
        mi.cbSize = (uint)Marshal.SizeOf<MONITORINFO>();
        if (!GetMonitorInfo(hMon, ref mi))
        {
            // Fallback to primary monitor
            hMon = MonitorFromPoint(new POINT(0, 0), MonitorDefaultToPrimary);
            mi = default;
            mi.cbSize = (uint)Marshal.SizeOf<MONITORINFO>();
            _ = GetMonitorInfo(hMon, ref mi);
        }

        var placement = new
        {
            Bounds = new { bounds.X, bounds.Y, bounds.Width, bounds.Height },
            MonitorWorkArea = new { mi.rcWork.Left, mi.rcWork.Top, mi.rcWork.Right, mi.rcWork.Bottom },
            PresenterState = presenterState.ToString(),
            SavedAt = DateTimeOffset.UtcNow,
        };

        return JsonSerializer.Serialize(placement);
    }

    /// <inheritdoc />
    public async Task RestoreWindowPlacementAsync(WindowId windowId, string placement)
    {
        if (string.IsNullOrWhiteSpace(placement))
        {
            return;
        }

        if (!this.windows.TryGetValue(windowId, out var window))
        {
            return;
        }

        try
        {
            using var doc = JsonDocument.Parse(placement);
            var root = doc.RootElement;

            if (!TryGetSavedBounds(root, out var saved))
            {
                return;
            }

            var presenterState = TryGetPresenterState(root, out var parsedState)
                ? parsedState
                : WindowPlacementPresenterState.Restored;

            // Resolve monitor by saved rect center and get monitor info
            var centerX = saved.X + (saved.Width / 2);
            var centerY = saved.Y + (saved.Height / 2);

            var mi = GetMonitorInfoForPoint(centerX, centerY);

            // If saved rect does not intersect the monitor work area, use primary monitor as fallback
            var work = mi.rcWork;
            var intersects = RectsIntersect(saved.X, saved.Y, saved.Width, saved.Height, work.Left, work.Top, work.Width, work.Height);
            Windows.Graphics.RectInt32 finalRect;
            if (intersects)
            {
                // Clamp to work area to ensure visible
                finalRect = ClampToWorkArea(saved, work);
            }
            else
            {
                // Fallback: center on the chosen monitor's work area and preserve size (clamped)
                var width = Math.Min(saved.Width, work.Width);
                var height = Math.Min(saved.Height, work.Height);
                var x = work.Left + ((work.Width - width) / 2);
                var y = work.Top + ((work.Height - height) / 2);
                finalRect = new Windows.Graphics.RectInt32 { X = x, Y = y, Width = width, Height = height };
            }

            window.RestoredBounds = finalRect;
            await this.SetWindowBoundsAsync(windowId, finalRect).ConfigureAwait(false);
            await ApplyPresenterStateAsync(window, presenterState).ConfigureAwait(false);
        }
        catch (JsonException)
        {
            // Ignore invalid placement strings
        }
    }

    private static bool TryGetSavedBounds(JsonElement root, out Windows.Graphics.RectInt32 saved)
    {
        saved = default;
        if (!root.TryGetProperty("Bounds", out var boundsEl))
        {
            return false;
        }

        try
        {
            saved = new Windows.Graphics.RectInt32
            {
                X = boundsEl.GetProperty("X").GetInt32(),
                Y = boundsEl.GetProperty("Y").GetInt32(),
                Width = boundsEl.GetProperty("Width").GetInt32(),
                Height = boundsEl.GetProperty("Height").GetInt32(),
            };

            return true;
        }
        catch (JsonException)
        {
            return false;
        }
    }

    private static MONITORINFO GetMonitorInfoForPoint(int centerX, int centerY)
    {
        var hMon = MonitorFromPoint(new POINT(centerX, centerY), MonitorDefaultToNearest);
        var mi = default(MONITORINFO);
        mi.cbSize = (uint)Marshal.SizeOf<MONITORINFO>();
        if (!GetMonitorInfo(hMon, ref mi))
        {
            hMon = MonitorFromPoint(new POINT(0, 0), MonitorDefaultToPrimary);
            mi = default;
            mi.cbSize = (uint)Marshal.SizeOf<MONITORINFO>();
            _ = GetMonitorInfo(hMon, ref mi);
        }

        return mi;
    }

    private static bool TryGetPresenterState(JsonElement root, out WindowPlacementPresenterState state)
    {
        state = WindowPlacementPresenterState.Restored;
        if (!root.TryGetProperty("PresenterState", out var stateElement) || stateElement.ValueKind != JsonValueKind.String)
        {
            return false;
        }

        var value = stateElement.GetString();
        return !string.IsNullOrWhiteSpace(value) && Enum.TryParse(value, ignoreCase: true, out state);
    }

    private static WindowPlacementPresenterState GetPersistedPresenterState(ManagedWindow window)
        => window.IsFullScreen()
            ? WindowPlacementPresenterState.FullScreen
            : window.IsMinimized()
                ? WindowPlacementPresenterState.Minimized
                : window.IsMaximized()
                    ? WindowPlacementPresenterState.Maximized
                    : WindowPlacementPresenterState.Restored;

    private static async Task ApplyPresenterStateAsync(ManagedWindow window, WindowPlacementPresenterState presenterState)
    {
        switch (presenterState)
        {
            case WindowPlacementPresenterState.FullScreen:
                await SetFullScreenPresenterAsync(window).ConfigureAwait(false);
                break;
            case WindowPlacementPresenterState.Maximized:
                await window.MaximizeAsync().ConfigureAwait(false);
                break;
            case WindowPlacementPresenterState.Minimized:
                await window.MinimizeAsync().ConfigureAwait(false);
                break;
            default:
                await window.RestoreAsync().ConfigureAwait(false);
                break;
        }
    }

    private static async Task SetFullScreenPresenterAsync(ManagedWindow window)
        => await window.DispatcherQueue.EnqueueAsync(() =>
        {
            var appWindow = window.Window.AppWindow;
            try
            {
                appWindow.SetPresenter(AppWindowPresenterKind.FullScreen);
            }
            catch (Exception ex) when (ex is NotSupportedException or ArgumentException)
            {
                if (appWindow.Presenter is OverlappedPresenter presenter)
                {
                    presenter.Maximize();
                }
            }
        }).ConfigureAwait(false);
}
