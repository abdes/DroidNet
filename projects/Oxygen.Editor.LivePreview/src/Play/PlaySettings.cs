// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using CommunityToolkit.Mvvm.ComponentModel;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Data.Settings;
using Oxygen.Editor.Runtime.Player;

namespace Oxygen.Editor.LivePreview.Play;

/// <summary>
/// Owns the player launch settings: the display mode, the window size, VSync and the frame rate limit. They are per
/// user, kept across sessions and projects, and never scene data. A new player starts with them and a running player
/// follows every change.
/// </summary>
/// <remarks>
/// The settings load once and are then served from memory; every change is written in order. Without a settings
/// manager (tests, design time) they live for the session only.
/// </remarks>
/// <param name="settings">The editor settings manager, accessed serially by this service.</param>
/// <param name="loggerFactory">The optional logger factory.</param>
public sealed partial class PlaySettings(
    IEditorSettingsManager? settings,
    ILoggerFactory? loggerFactory = null) : ObservableObject
{
    /// <summary>The smallest framebuffer width or height the player accepts.</summary>
    public const int MinimumSize = 64;

    /// <summary>The largest framebuffer width or height the player accepts.</summary>
    public const int MaximumSize = 16384;

    /// <summary>The highest frame rate limit the player accepts.</summary>
    public const int MaximumFrameRateLimit = 240;

    /// <summary>The payload version this editor reads and writes.</summary>
    internal const int CurrentVersion = 3;

    /// <summary>The application-wide preference identity in the editor database.</summary>
    internal static readonly SettingKey<Preferences> Key = new("WorldEditor", "PlaySession");

    /// <summary>The settings a new user starts with, and Reset returns to.</summary>
    internal static readonly Preferences Defaults = new(
        CurrentVersion,
        PlayerWindowMode.Windowed,
        Width: 1280,
        Height: 720,
        CustomSize: false,
        VSync: true,
        FrameRateLimited: false,
        FrameRateLimit: 60);

    private readonly ILogger logger = loggerFactory?.CreateLogger<PlaySettings>()
        ?? NullLogger<PlaySettings>.Instance;

    private Task persistence = Task.CompletedTask;
    private Task? load;
    private long revision;
    private Preferences current = Defaults;

    /// <summary>Gets the entries of the window size list: the named sizes, then Custom.</summary>
    public static IReadOnlyList<PlayerWindowSizeOption> SizeOptions { get; } =
    [
        PlayerWindowSizeOption.Named(1920, 1080, "Full HD"),
        PlayerWindowSizeOption.Named(2560, 1440, "QHD"),
        PlayerWindowSizeOption.Named(3840, 2160, "4K UHD"),
        PlayerWindowSizeOption.Named(1280, 720, "HD"),
        PlayerWindowSizeOption.Custom,
    ];

    /// <summary>Gets or sets whether the player window is framed or borderless over its display.</summary>
    public PlayerWindowMode WindowMode
    {
        get => this.current.Mode;
        set => this.Update(this.current with { Mode = value });
    }

    /// <summary>Gets a value indicating whether the window is framed, so its size applies.</summary>
    public bool IsWindowed => this.current.Mode == PlayerWindowMode.Windowed;

    /// <summary>Gets or sets the window size list's entry: a named size, or Custom for the width and height typed
    /// in. A size no entry names reads as Custom. Setting null, which a list sends when it clears its selection,
    /// changes nothing.</summary>
    [AllowNull]
    public PlayerWindowSizeOption SizeOption
    {
        get => this.current.CustomSize
            ? PlayerWindowSizeOption.Custom
            : SizeOptions.FirstOrDefault(option => option.Width == this.current.Width && option.Height == this.current.Height)
                ?? PlayerWindowSizeOption.Custom;
        set
        {
            if (value is null)
            {
                return;
            }

            this.Update(value.IsCustom
                ? this.current with { CustomSize = true }
                : this.current with { Width = value.Width, Height = value.Height, CustomSize = false });
        }
    }

    /// <summary>Gets a value indicating whether the size is custom, so its width and height are edited.</summary>
    public bool HasCustomSize => this.SizeOption.IsCustom;

    /// <summary>Gets or sets the framebuffer width in physical pixels; values outside the player's range are
    /// ignored.</summary>
    public int Width
    {
        get => this.current.Width;
        set => this.Update(IsValidSize(value) ? this.current with { Width = value } : this.current);
    }

    /// <summary>Gets or sets the framebuffer height in physical pixels; values outside the player's range are
    /// ignored.</summary>
    public int Height
    {
        get => this.current.Height;
        set => this.Update(IsValidSize(value) ? this.current with { Height = value } : this.current);
    }

    /// <summary>Gets or sets a value indicating whether the player presents with vertical sync.</summary>
    public bool VSync
    {
        get => this.current.VSync;
        set => this.Update(this.current with { VSync = value });
    }

    /// <summary>Gets or sets a value indicating whether the frame rate is limited to
    /// <see cref="FrameRateLimit"/>.</summary>
    public bool FrameRateLimited
    {
        get => this.current.FrameRateLimited;
        set => this.Update(this.current with { FrameRateLimited = value });
    }

    /// <summary>Gets or sets the frame rate limit in frames per second; it applies while
    /// <see cref="FrameRateLimited"/> is set. Values outside the player's range are ignored.</summary>
    public int FrameRateLimit
    {
        get => this.current.FrameRateLimit;
        set => this.Update(IsValidFrameRateLimit(value) ? this.current with { FrameRateLimit = value } : this.current);
    }

    /// <summary>Gets the frame cap the player takes: the limit when the frame rate is limited, otherwise 0.</summary>
    public int FrameCap => this.current.FrameRateLimited ? this.current.FrameRateLimit : 0;

    /// <summary>Determines whether a value is an acceptable framebuffer width or height.</summary>
    /// <param name="value">The candidate size, in physical pixels.</param>
    /// <returns><see langword="true"/> from <see cref="MinimumSize"/> to <see cref="MaximumSize"/>.</returns>
    public static bool IsValidSize(int value) => value is >= MinimumSize and <= MaximumSize;

    /// <summary>Determines whether a value is an acceptable frame rate limit.</summary>
    /// <param name="value">The candidate limit, in frames per second.</param>
    /// <returns><see langword="true"/> from 1 to <see cref="MaximumFrameRateLimit"/>.</returns>
    public static bool IsValidFrameRateLimit(int value) => value is >= 1 and <= MaximumFrameRateLimit;

    /// <summary>Returns every setting to its default.</summary>
    public void Reset() => this.Update(Defaults);

    /// <summary>Loads the stored settings once; later calls return the same load.</summary>
    /// <returns>The load, which never fails: unreadable settings keep the defaults.</returns>
    public Task EnsureLoadedAsync() => this.load ??= this.LoadAsync();

    /// <summary>Waits until every change made so far is written.</summary>
    /// <returns>A task that completes when the writes are done.</returns>
    public Task FlushAsync() => this.persistence;

    // A stored value outside its range keeps its default.
    private static Preferences Sanitize(Preferences stored) => stored with
    {
        Width = IsValidSize(stored.Width) ? stored.Width : Defaults.Width,
        Height = IsValidSize(stored.Height) ? stored.Height : Defaults.Height,
        FrameRateLimit = IsValidFrameRateLimit(stored.FrameRateLimit) ? stored.FrameRateLimit : Defaults.FrameRateLimit,
        Mode = Enum.IsDefined(stored.Mode) ? stored.Mode : Defaults.Mode,
    };

    // Changes the settings, raises a change for every property that reads differently, and writes them.
    private void Update(Preferences next, bool persist = true)
    {
        if (next == this.current)
        {
            return;
        }

        var before = this.Snapshot();
        this.current = next;
        foreach (var (name, value) in this.Snapshot())
        {
            if (!Equals(before[name], value))
            {
                this.OnPropertyChanged(name);
            }
        }

        if (persist)
        {
            ++this.revision;
            this.persistence = this.WriteAfterAsync(this.persistence, next);
        }
    }

    // What every property reads now, so a change raises exactly the properties it changed.
    private Dictionary<string, object> Snapshot() => new(StringComparer.Ordinal)
    {
        [nameof(this.WindowMode)] = this.WindowMode,
        [nameof(this.IsWindowed)] = this.IsWindowed,
        [nameof(this.SizeOption)] = this.SizeOption,
        [nameof(this.HasCustomSize)] = this.HasCustomSize,
        [nameof(this.Width)] = this.Width,
        [nameof(this.Height)] = this.Height,
        [nameof(this.VSync)] = this.VSync,
        [nameof(this.FrameRateLimited)] = this.FrameRateLimited,
        [nameof(this.FrameRateLimit)] = this.FrameRateLimit,
        [nameof(this.FrameCap)] = this.FrameCap,
    };

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Unreadable settings are discarded with a warning; they must never block Play.")]
    private async Task LoadAsync()
    {
        if (settings is null)
        {
            return;
        }

        var before = this.revision;
        Preferences? stored;
        try
        {
            stored = await settings.LoadSettingAsync(Key, SettingContext.Application()).ConfigureAwait(true);
        }
        catch (Exception failure)
        {
            this.LogPreferencesUnreadable(failure);
            return;
        }

        // A change made while loading is newer than the stored value.
        if (stored is null || this.revision != before)
        {
            return;
        }

        if (stored.Version != CurrentVersion)
        {
            this.LogPreferencesDiscarded(stored.Version);
            return;
        }

        // Loaded values are shown without being written back.
        this.Update(Sanitize(stored), persist: false);
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "A failed settings write is logged; the in-memory settings stay current.")]
    private async Task WriteAfterAsync(Task previous, Preferences preferences)
    {
        await previous.ConfigureAwait(true);
        if (settings is null)
        {
            return;
        }

        try
        {
            await settings.SaveSettingAsync(Key, preferences, SettingContext.Application()).ConfigureAwait(true);
        }
        catch (Exception failure)
        {
            this.LogPreferencesNotSaved(failure);
        }
    }

    /// <summary>The stored player launch settings.</summary>
    /// <param name="Version">The payload version.</param>
    /// <param name="Mode">Whether the window is framed or borderless.</param>
    /// <param name="Width">The framebuffer width in physical pixels.</param>
    /// <param name="Height">The framebuffer height in physical pixels.</param>
    /// <param name="CustomSize">Whether the user chose Custom, keeping the typed size even when it has a name.</param>
    /// <param name="VSync">Whether the player presents with vertical sync.</param>
    /// <param name="FrameRateLimited">Whether the frame rate is limited.</param>
    /// <param name="FrameRateLimit">The frame rate limit, in frames per second.</param>
    internal sealed record Preferences(
        int Version,
        PlayerWindowMode Mode,
        int Width,
        int Height,
        bool CustomSize,
        bool VSync,
        bool FrameRateLimited,
        int FrameRateLimit);
}
