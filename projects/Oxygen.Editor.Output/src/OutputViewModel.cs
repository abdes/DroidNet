// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Controls.OutputConsole;
using DroidNet.Controls.OutputConsole.Model;
using DroidNet.Routing;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Data.Settings;

namespace Oxygen.Editor.Output;

/// <summary>
///     The Output panel's view model: the shared output buffer and how the panel shows it.
/// </summary>
/// <remarks>
///     <para>
///         The view preferences (levels, hidden subsystems, timestamps, subsystems, wrapping, folding, search mode,
///         the details pane and its message/properties split) are per user, kept across sessions and projects. They load once when the view
///         model is created; every change is written in order. Without a settings manager (tests, design time) they
///         live for the session only.
///     </para>
///     <para>
///         The search text, following and pausing are session state: a new session starts unfiltered, following
///         the newest line and not paused.
///     </para>
/// </remarks>
[RetainedViewModel]
public partial class OutputViewModel : ObservableObject
{
    /// <summary>The payload version this editor reads and writes.</summary>
    internal const int CurrentVersion = 2;

    /// <summary>The application-wide preference identity in the editor database.</summary>
    internal static readonly SettingKey<Preferences> Key = new("Output", "Console");

    private readonly IEditorSettingsManager? settings;
    private readonly ILogger logger;
    private Task persistence = Task.CompletedTask;
    private long revision;
    private bool applyingStored;

    /// <summary>
    ///     Initializes a new instance of the <see cref="OutputViewModel" /> class and starts loading the stored
    ///     preferences.
    /// </summary>
    /// <param name="buffer">The buffer that stores the output log entries.</param>
    /// <param name="settings">The editor settings manager; <see langword="null" /> keeps the preferences for the session only.</param>
    /// <param name="loggerFactory">The optional logger factory.</param>
    public OutputViewModel(OutputLogBuffer buffer, IEditorSettingsManager? settings = null, ILoggerFactory? loggerFactory = null)
    {
        ArgumentNullException.ThrowIfNull(buffer);
        this.Buffer = buffer;
        this.settings = settings;
        this.logger = loggerFactory?.CreateLogger<OutputViewModel>() ?? NullLogger<OutputViewModel>.Instance;
        this.Loading = this.LoadAsync();
    }

    /// <summary>
    ///     Gets the buffer that stores the output log entries.
    /// </summary>
    public OutputLogBuffer Buffer { get; }

    [ObservableProperty]
    public partial LevelMask LevelFilter { get; set; } = LevelMask.Information | LevelMask.Warning | LevelMask.Error | LevelMask.Fatal;

    [ObservableProperty]
    public partial string TextFilter { get; set; } = string.Empty;

    [ObservableProperty]
    public partial bool FollowTail { get; set; } = true;

    [ObservableProperty]
    public partial bool IsPaused { get; set; }

    [ObservableProperty]
    public partial bool ShowTimestamps { get; set; }

    [ObservableProperty]
    public partial bool WordWrap { get; set; }

    [ObservableProperty]
    public partial bool ShowSubsystem { get; set; } = true;

    [ObservableProperty]
    public partial IReadOnlyCollection<string> HiddenSubsystems { get; set; } = [];

    [ObservableProperty]
    public partial bool FoldDuplicates { get; set; }

    [ObservableProperty]
    public partial OutputSearchMode SearchMode { get; set; } = OutputSearchMode.Filter;

    [ObservableProperty]
    public partial bool IsDetailsPaneOpen { get; set; }

    [ObservableProperty]
    public partial double DetailsSplitRatio { get; set; } = OutputConsoleView.DefaultDetailsSplitRatio;

    /// <summary>Gets the load of the stored preferences; it never fails.</summary>
    internal Task Loading { get; }

    /// <summary>Waits until every change made so far is written.</summary>
    /// <returns>A task that completes when the writes are done.</returns>
    internal Task FlushAsync() => this.persistence;

    partial void OnLevelFilterChanged(LevelMask value) => this.Persist();

    partial void OnShowTimestampsChanged(bool value) => this.Persist();

    partial void OnWordWrapChanged(bool value) => this.Persist();

    partial void OnShowSubsystemChanged(bool value) => this.Persist();

    partial void OnHiddenSubsystemsChanged(IReadOnlyCollection<string> value) => this.Persist();

    partial void OnFoldDuplicatesChanged(bool value) => this.Persist();

    partial void OnSearchModeChanged(OutputSearchMode value) => this.Persist();

    partial void OnIsDetailsPaneOpenChanged(bool value) => this.Persist();

    partial void OnDetailsSplitRatioChanged(double value) => this.Persist();

    private Preferences Capture() => new(
        CurrentVersion,
        this.LevelFilter,
        [.. this.HiddenSubsystems],
        this.ShowTimestamps,
        this.ShowSubsystem,
        this.WordWrap,
        this.FoldDuplicates,
        this.SearchMode,
        this.IsDetailsPaneOpen,
        this.DetailsSplitRatio);

    private void Apply(Preferences stored)
    {
        this.applyingStored = true;
        try
        {
            this.LevelFilter = stored.Levels & LevelMask.All;
            this.HiddenSubsystems = [.. stored.HiddenSubsystems ?? []];
            this.ShowTimestamps = stored.ShowTimestamps;
            this.ShowSubsystem = stored.ShowSubsystem;
            this.WordWrap = stored.WordWrap;
            this.FoldDuplicates = stored.FoldDuplicates;
            this.SearchMode = Enum.IsDefined(stored.SearchMode) ? stored.SearchMode : OutputSearchMode.Filter;
            this.IsDetailsPaneOpen = stored.DetailsPaneOpen;
            this.DetailsSplitRatio = double.IsFinite(stored.DetailsSplitRatio)
                ? Math.Clamp(stored.DetailsSplitRatio, OutputConsoleView.MinDetailsSplitRatio, OutputConsoleView.MaxDetailsSplitRatio)
                : OutputConsoleView.DefaultDetailsSplitRatio;
        }
        finally
        {
            this.applyingStored = false;
        }
    }

    // Loaded values are shown without being written back.
    private void Persist()
    {
        if (this.applyingStored)
        {
            return;
        }

        ++this.revision;
        this.persistence = this.WriteAfterAsync(this.persistence, this.Capture());
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Unreadable preferences are discarded with a warning; they must never block the Output panel.")]
    private async Task LoadAsync()
    {
        if (this.settings is null)
        {
            return;
        }

        var before = this.revision;
        Preferences? stored;
        try
        {
            stored = await this.settings.LoadSettingAsync(Key, SettingContext.Application()).ConfigureAwait(true);
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

        this.Apply(stored);
    }

    [SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "A failed preferences write is logged; the in-memory preferences stay current.")]
    private async Task WriteAfterAsync(Task previous, Preferences preferences)
    {
        await previous.ConfigureAwait(true);
        if (this.settings is null)
        {
            return;
        }

        try
        {
            await this.settings.SaveSettingAsync(Key, preferences, SettingContext.Application()).ConfigureAwait(true);
        }
        catch (Exception failure)
        {
            this.LogPreferencesNotSaved(failure);
        }
    }

    /// <summary>The stored Output panel preferences.</summary>
    /// <param name="Version">The payload version.</param>
    /// <param name="Levels">The levels shown.</param>
    /// <param name="HiddenSubsystems">The subsystems hidden; an empty string stands for lines without one.</param>
    /// <param name="ShowTimestamps">Whether lines show their time.</param>
    /// <param name="ShowSubsystem">Whether lines show their subsystem.</param>
    /// <param name="WordWrap">Whether long lines wrap.</param>
    /// <param name="FoldDuplicates">Whether consecutive identical lines fold.</param>
    /// <param name="SearchMode">Whether the search text filters or finds.</param>
    /// <param name="DetailsPaneOpen">Whether the details pane is open.</param>
    /// <param name="DetailsSplitRatio">The details pane's share of width for the message; the properties take the rest.</param>
    internal sealed record Preferences(
        int Version,
        LevelMask Levels,
        IReadOnlyList<string> HiddenSubsystems,
        bool ShowTimestamps,
        bool ShowSubsystem,
        bool WordWrap,
        bool FoldDuplicates,
        OutputSearchMode SearchMode,
        bool DetailsPaneOpen,
        double DetailsSplitRatio);
}
