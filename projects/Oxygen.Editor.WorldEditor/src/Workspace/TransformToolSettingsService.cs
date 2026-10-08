// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using CommunityToolkit.Mvvm.ComponentModel;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Data.Settings;
using Oxygen.Editor.Runtime.Engine;

namespace Oxygen.Editor.World.Workspace;

/// <summary>
/// Owns the transform gizmo's snapping and space preferences: per user, kept across sessions and
/// projects, never scene data.
/// </summary>
/// <remarks>
/// The preferences load once and are then served from memory; every change is written in order.
/// Without a settings manager (tests, design time) they live for the session only.
/// </remarks>
/// <param name="settings">The editor settings manager, accessed serially by this service.</param>
/// <param name="loggerFactory">The optional logger factory.</param>
public sealed partial class TransformToolSettingsService(
    IEditorSettingsManager? settings,
    ILoggerFactory? loggerFactory = null) : ObservableObject
{
    /// <summary>The default translation increment, in metres.</summary>
    public const float DefaultTranslationIncrement = 0.25F;

    /// <summary>The default rotation increment, in degrees.</summary>
    public const float DefaultRotationIncrement = 15.0F;

    /// <summary>The default scale increment.</summary>
    public const float DefaultScaleIncrement = 0.1F;

    /// <summary>The largest accepted increment of any kind.</summary>
    public const float MaximumIncrement = 1000.0F;

    /// <summary>The payload version this editor reads and writes.</summary>
    internal const int CurrentVersion = 1;

    /// <summary>The application-wide preference identity in the editor database.</summary>
    internal static readonly SettingKey<Preferences> Key = new("WorldEditor", "TransformTools");

    private readonly ILogger logger = loggerFactory?.CreateLogger<TransformToolSettingsService>()
        ?? NullLogger<TransformToolSettingsService>.Instance;

    private Task persistence = Task.CompletedTask;
    private Task? load;
    private long revision;
    private bool snapEnabled;
    private RuntimeTransformSpace space;
    private float translationIncrement = DefaultTranslationIncrement;
    private float rotationIncrement = DefaultRotationIncrement;
    private float scaleIncrement = DefaultScaleIncrement;

    /// <summary>Gets the translation increment presets, in metres.</summary>
    public static IReadOnlyList<float> TranslationPresets { get; } = [0.01F, 0.05F, 0.1F, 0.25F, 0.5F, 1.0F, 2.5F, 5.0F, 10.0F];

    /// <summary>Gets the rotation increment presets, in degrees.</summary>
    public static IReadOnlyList<float> RotationPresets { get; } = [1.0F, 5.0F, 10.0F, 15.0F, 22.5F, 30.0F, 45.0F, 90.0F];

    /// <summary>Gets the scale increment presets.</summary>
    public static IReadOnlyList<float> ScalePresets { get; } = [0.01F, 0.05F, 0.1F, 0.25F, 0.5F, 1.0F];

    /// <summary>Gets or sets a value indicating whether gizmo drags snap; Ctrl inverts it for a drag.</summary>
    public bool SnapEnabled
    {
        get => this.snapEnabled;
        set => this.Change(ref this.snapEnabled, value);
    }

    /// <summary>Gets or sets the axes translate and rotate follow.</summary>
    public RuntimeTransformSpace Space
    {
        get => this.space;
        set => this.Change(ref this.space, Enum.IsDefined(value) ? value : RuntimeTransformSpace.World);
    }

    /// <summary>Gets or sets the translation increment, in metres; invalid values are ignored.</summary>
    public float TranslationIncrement
    {
        get => this.translationIncrement;
        set => this.Change(ref this.translationIncrement, IsValidIncrement(value) ? value : this.translationIncrement);
    }

    /// <summary>Gets or sets the rotation increment, in degrees; invalid values are ignored.</summary>
    public float RotationIncrement
    {
        get => this.rotationIncrement;
        set => this.Change(ref this.rotationIncrement, IsValidIncrement(value) ? value : this.rotationIncrement);
    }

    /// <summary>Gets or sets the scale increment; invalid values are ignored.</summary>
    public float ScaleIncrement
    {
        get => this.scaleIncrement;
        set => this.Change(ref this.scaleIncrement, IsValidIncrement(value) ? value : this.scaleIncrement);
    }

    /// <summary>Gets the snapping increments in the runtime's form.</summary>
    public RuntimeTransformSnap Snap => new(this.SnapEnabled, this.TranslationIncrement, this.RotationIncrement, this.ScaleIncrement);

    /// <summary>Determines whether a value is an acceptable increment.</summary>
    /// <param name="value">The candidate increment.</param>
    /// <returns><see langword="true"/> for a finite value above zero and at most <see cref="MaximumIncrement"/>.</returns>
    public static bool IsValidIncrement(float value) => float.IsFinite(value) && value > 0.0F && value <= MaximumIncrement;

    /// <summary>Loads the stored preferences once; later calls return the same load.</summary>
    /// <returns>The load, which never fails: unreadable preferences keep the defaults.</returns>
    public Task EnsureLoadedAsync() => this.load ??= this.LoadAsync();

    /// <summary>Waits until every change made so far is written.</summary>
    /// <returns>A task that completes when the writes are done.</returns>
    public Task FlushAsync() => this.persistence;

    private void Change<T>(ref T storage, T value, [System.Runtime.CompilerServices.CallerMemberName] string? propertyName = null)
    {
        if (EqualityComparer<T>.Default.Equals(storage, value))
        {
            return;
        }

        storage = value;
        this.OnPropertyChanged(propertyName);
        this.OnPropertyChanged(nameof(this.Snap));
        ++this.revision;
        this.persistence = this.WriteAfterAsync(this.persistence, this.Capture());
    }

    private Preferences Capture()
        => new(CurrentVersion, this.SnapEnabled, this.TranslationIncrement, this.RotationIncrement, this.ScaleIncrement, this.Space);

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Unreadable preferences are discarded with a warning; they must never block the viewport.")]
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

        if (stored.Version != CurrentVersion || !IsValidIncrement(stored.TranslationIncrement)
            || !IsValidIncrement(stored.RotationIncrement) || !IsValidIncrement(stored.ScaleIncrement)
            || !Enum.IsDefined(stored.Space))
        {
            this.LogPreferencesDiscarded(stored.Version);
            return;
        }

        this.ApplyStored(stored);
    }

    private void ApplyStored(Preferences stored)
    {
        this.SnapEnabled = stored.SnapEnabled;
        this.Space = stored.Space;
        this.TranslationIncrement = stored.TranslationIncrement;
        this.RotationIncrement = stored.RotationIncrement;
        this.ScaleIncrement = stored.ScaleIncrement;
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "A failed preference write is logged; the in-memory preferences stay current.")]
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

    [LoggerMessage(Level = LogLevel.Warning, Message = "The transform tool preferences could not be read; the defaults apply.")]
    private partial void LogPreferencesUnreadable(Exception exception);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Discarded transform tool preferences of version {Version}; the defaults apply.")]
    private partial void LogPreferencesDiscarded(int version);

    [LoggerMessage(Level = LogLevel.Warning, Message = "The transform tool preferences could not be saved.")]
    private partial void LogPreferencesNotSaved(Exception exception);

    /// <summary>The stored transform tool preferences.</summary>
    /// <param name="Version">The payload version.</param>
    /// <param name="SnapEnabled">Whether drags snap.</param>
    /// <param name="TranslationIncrement">Metres.</param>
    /// <param name="RotationIncrement">Degrees.</param>
    /// <param name="ScaleIncrement">Scale factor step.</param>
    /// <param name="Space">The axes translate and rotate follow.</param>
    public sealed record Preferences(
        int Version,
        bool SnapEnabled,
        float TranslationIncrement,
        float RotationIncrement,
        float ScaleIncrement,
        RuntimeTransformSpace Space);
}
