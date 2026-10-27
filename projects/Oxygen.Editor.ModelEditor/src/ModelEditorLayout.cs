// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using CommunityToolkit.Mvvm.ComponentModel;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Data.Settings;

namespace Oxygen.Editor.ModelEditor;

/// <summary>
/// The Model Editor's layout: the import settings panel's width and whether it is collapsed, per user and shared by
/// every model document (model editor 5).
/// </summary>
/// <remarks>
/// The layout loads once and is then served from memory; every change is written in order. Without a settings manager
/// (tests, design time) it lives for the session only.
/// </remarks>
/// <param name="settings">The editor settings manager, accessed serially by this service.</param>
/// <param name="loggerFactory">The optional logger factory.</param>
public sealed partial class ModelEditorLayout(IEditorSettingsManager? settings, ILoggerFactory? loggerFactory = null) : ObservableObject
{
    /// <summary>The panel's width when the user never set one.</summary>
    public const double DefaultPanelWidth = 340;

    /// <summary>The narrowest the panel can be.</summary>
    public const double MinimumPanelWidth = 240;

    /// <summary>The payload version this editor reads and writes.</summary>
    internal const int CurrentVersion = 1;

    /// <summary>The application-wide layout identity in the editor database.</summary>
    internal static readonly SettingKey<Preferences> Key = new("ModelEditor", "Layout");

    private readonly ILogger logger = loggerFactory?.CreateLogger<ModelEditorLayout>() ?? NullLogger<ModelEditorLayout>.Instance;
    private Task persistence = Task.CompletedTask;
    private Task? load;
    private long revision;
    private double panelWidth = DefaultPanelWidth;
    private bool isPanelCollapsed;

    /// <summary>Gets or sets the import settings panel's width; a width below <see cref="MinimumPanelWidth"/> takes it.</summary>
    public double PanelWidth
    {
        get => this.panelWidth;
        set => this.Change(ref this.panelWidth, double.IsFinite(value) ? Math.Max(value, MinimumPanelWidth) : this.panelWidth);
    }

    /// <summary>Gets or sets a value indicating whether the import settings panel is collapsed.</summary>
    public bool IsPanelCollapsed
    {
        get => this.isPanelCollapsed;
        set => this.Change(ref this.isPanelCollapsed, value);
    }

    /// <summary>Loads the stored layout once; later calls return the same load.</summary>
    /// <returns>The load, which never fails: an unreadable layout keeps the defaults.</returns>
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
        ++this.revision;
        this.persistence = this.WriteAfterAsync(this.persistence, new Preferences(CurrentVersion, this.PanelWidth, this.IsPanelCollapsed));
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "An unreadable layout is discarded with a warning; it must never block opening a model.")]
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
            this.LogUnreadable(failure);
            return;
        }

        // A change made while the load ran is newer than the stored layout.
        if (stored is not { Version: CurrentVersion } || this.revision != before)
        {
            return;
        }

        // The stored layout is shown, not written back.
        this.panelWidth = double.IsFinite(stored.PanelWidth) ? Math.Max(stored.PanelWidth, MinimumPanelWidth) : DefaultPanelWidth;
        this.isPanelCollapsed = stored.IsPanelCollapsed;
        this.OnPropertyChanged(nameof(this.PanelWidth));
        this.OnPropertyChanged(nameof(this.IsPanelCollapsed));
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "The layout is a convenience: a failed write is logged and the next change writes again.")]
    private async Task WriteAfterAsync(Task previous, Preferences value)
    {
        await previous.ConfigureAwait(true);
        if (settings is null)
        {
            return;
        }

        try
        {
            await settings.SaveSettingAsync(Key, value, SettingContext.Application()).ConfigureAwait(true);
        }
        catch (Exception failure)
        {
            this.LogWriteFailed(failure);
        }
    }

    /// <summary>The stored layout.</summary>
    /// <param name="Version">The payload version.</param>
    /// <param name="PanelWidth">The import settings panel's width.</param>
    /// <param name="IsPanelCollapsed">Whether the panel is collapsed.</param>
    internal sealed record Preferences(int Version, double PanelWidth, bool IsPanelCollapsed);
}
