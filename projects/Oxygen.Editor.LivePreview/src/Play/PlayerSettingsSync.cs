// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Text.Json.Nodes;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.Runtime.Player;

namespace Oxygen.Editor.LivePreview.Play;

/// <summary>
/// Keeps a running player on the launch settings: the display mode through <c>host.window.mode</c>, the window size
/// through <c>host.window.size</c>, VSync through <c>gfx.vsync</c> and the frame rate limit through
/// <c>ngin.target_fps</c>.
/// </summary>
/// <remarks>
/// Changes are sent in order, one request at a time; a value that changes again while its request is in flight is
/// sent once more with its latest value, so dragging a size sends what the player can follow, not every step. Used
/// on the UI thread only.
/// </remarks>
internal sealed partial class PlayerSettingsSync : IDisposable
{
    /// <summary>The player command that switches its window between windowed and borderless.</summary>
    internal const string WindowModeMethod = "host.window.mode";

    /// <summary>The player command that resizes its window, in physical pixels.</summary>
    internal const string WindowSizeMethod = "host.window.size";

    /// <summary>The player command that sets a console variable.</summary>
    internal const string CVarSetMethod = "cvar.set";

    /// <summary>The console variable that turns vertical sync on or off.</summary>
    internal const string VSyncCVar = "gfx.vsync";

    /// <summary>The console variable that limits the frame rate; 0 is no limit.</summary>
    internal const string TargetFrameRateCVar = "ngin.target_fps";

    private readonly PlaySettings settings;
    private readonly Func<string, JsonObject, CancellationToken, Task> request;
    private readonly Action<string> reportRefusal;
    private readonly ILogger logger;
    private readonly CancellationTokenSource lifetime = new();
    private Changes pending;
    private Task pump = Task.CompletedTask;
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="PlayerSettingsSync"/> class.</summary>
    /// <param name="settings">The preferences to follow.</param>
    /// <param name="request">Sends one request to the player and completes with its reply.</param>
    /// <param name="reportRefusal">Shows the user a setting the player did not apply, and why.</param>
    /// <param name="loggerFactory">The optional logger factory.</param>
    public PlayerSettingsSync(
        PlaySettings settings,
        Func<string, JsonObject, CancellationToken, Task> request,
        Action<string> reportRefusal,
        ILoggerFactory? loggerFactory = null)
    {
        this.settings = settings;
        this.request = request;
        this.reportRefusal = reportRefusal;
        this.logger = loggerFactory?.CreateLogger<PlayerSettingsSync>() ?? NullLogger<PlayerSettingsSync>.Instance;
        this.settings.PropertyChanged += this.OnSettingsChanged;
    }

    [Flags]
    private enum Changes
    {
        None = 0,
        WindowMode = 1,
        WindowSize = 2,
        VSync = 4,
        FrameRate = 8,
        All = WindowMode | WindowSize | VSync | FrameRate,
    }

    /// <summary>Gets completion of the requests sent or waiting so far.</summary>
    internal Task Sent => this.pump;

    /// <summary>Sends every setting, for a player that may have started before the latest changes.</summary>
    public void SendAll() => this.Request(Changes.All);

    /// <inheritdoc />
    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.disposed = true;
        this.settings.PropertyChanged -= this.OnSettingsChanged;
        this.lifetime.Cancel();
        this.lifetime.Dispose();
    }

    private static Changes ChangesOf(string? propertyName) => propertyName switch
    {
        nameof(PlaySettings.WindowMode) => Changes.WindowMode,
        nameof(PlaySettings.Width) or nameof(PlaySettings.Height) => Changes.WindowSize,
        nameof(PlaySettings.VSync) => Changes.VSync,
        nameof(PlaySettings.FrameCap) => Changes.FrameRate,
        _ => Changes.None,
    };

    private static JsonObject SetCVar(string name, JsonNode value) => new() { ["name"] = name, ["value"] = value };

    private void OnSettingsChanged(object? sender, PropertyChangedEventArgs args) => this.Request(ChangesOf(args.PropertyName));

    private void Request(Changes changes)
    {
        if (this.disposed || changes == Changes.None)
        {
            return;
        }

        this.pending |= changes;
        if (this.pump.IsCompleted)
        {
            this.pump = this.PumpAsync();
        }
    }

    private async Task PumpAsync()
    {
        while (this.pending != Changes.None && !this.disposed)
        {
            var changes = this.pending;
            this.pending = Changes.None;
            if (changes.HasFlag(Changes.WindowMode)
                && !await this.SendAsync(WindowModeMethod, new JsonObject { ["mode"] = PlayerLaunchFile.ModeName(this.settings.WindowMode) }, "display mode").ConfigureAwait(true))
            {
                return;
            }

            // A borderless window covers its display; its size is sent when it is framed again.
            if ((changes & (Changes.WindowMode | Changes.WindowSize)) != Changes.None && this.settings.IsWindowed
                && !await this.SendAsync(WindowSizeMethod, new JsonObject { ["width"] = this.settings.Width, ["height"] = this.settings.Height }, "window size").ConfigureAwait(true))
            {
                return;
            }

            if (changes.HasFlag(Changes.VSync)
                && !await this.SendAsync(CVarSetMethod, SetCVar(VSyncCVar, this.settings.VSync), "VSync setting").ConfigureAwait(true))
            {
                return;
            }

            if (changes.HasFlag(Changes.FrameRate)
                && !await this.SendAsync(CVarSetMethod, SetCVar(TargetFrameRateCVar, this.settings.FrameCap), "frame rate limit").ConfigureAwait(true))
            {
                return;
            }
        }
    }

    // False once the connection is gone, which ends the sync.
    private async Task<bool> SendAsync(string method, JsonObject parameters, string setting)
    {
        if (this.disposed)
        {
            return false;
        }

        try
        {
            await this.request(method, parameters, this.lifetime.Token).ConfigureAwait(true);
            return true;
        }
        catch (PlayerCommandException exception) when (exception.Code != PlayerCommandException.ConnectionClosedCode)
        {
            this.LogSettingRefused(setting, exception.Message);
            this.reportRefusal($"The player did not apply the {setting}: {exception.Message}");
            return true;
        }
        catch (PlayerCommandException)
        {
            return false;
        }
        catch (OperationCanceledException) when (this.disposed)
        {
            return false;
        }
    }
}
