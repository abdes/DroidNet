// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;
using System.Text.Json;
using Microsoft.UI;

namespace DroidNet.Aura.Windowing;

/// <summary>
///     Window placement persistence helpers for <see cref="WindowManagerService"/>.
/// </summary>
public sealed partial class WindowManagerService
{
    /// <inheritdoc />
    public string? GetWindowPlacementString(WindowId windowId)
    {
        if (!this.windows.TryGetValue(windowId, out var window))
        {
            return null;
        }

        var bounds = window.RestoredBounds;

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

        if (!this.windows.ContainsKey(windowId))
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

            await this.SetWindowBoundsAsync(windowId, finalRect).ConfigureAwait(false);
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
}
