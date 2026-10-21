// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Globalization;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Documents;
using Oxygen.Editor.LivePreview.Play;
using Oxygen.Editor.Shell.Windows;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.SceneEditor.Play;

/// <summary>
/// The Play transport of one scene's toolbar: Play (Restart while this scene plays) with the player launch settings,
/// Stop (Cancel while Play is on its way), and the status of the workspace's Play.
/// </summary>
/// <remarks>
/// Play saves the open documents first, then plays the scene. Clicking the status shows what it is about: the player
/// window while one runs, otherwise the panel that explains the step on its way or the step that failed.
/// </remarks>
public sealed partial class ScenePlayToolbarViewModel : ObservableObject, IDisposable
{
    private const string PlayGlyph = "";
    private const string RestartGlyph = "";

    // Everything this view model derives from the session status.
    private static readonly string[] StatusProperties =
    [
        nameof(PrimaryLabel),
        nameof(PrimaryGlyph),
        nameof(PrimaryAccent),
        nameof(PrimaryToolTip),
        nameof(CanPrimary),
        nameof(CanStop),
        nameof(StopToolTip),
        nameof(IsBusy),
        nameof(StatusText),
        nameof(StatusDetail),
        nameof(HasStatusDetail),
        nameof(StatusTone),
        nameof(StatusToolTip),
        nameof(StatusRevealsPlayer),
        nameof(CanShowStatus),
    ];

    private readonly PlaySession session;
    private readonly DocumentId documentId;
    private readonly Func<Task<bool>> saveInputs;
    private readonly IPlacementService? placement;
    private bool hasCamera;

    /// <summary>Initializes a new instance of the <see cref="ScenePlayToolbarViewModel"/> class.</summary>
    /// <param name="session">The workspace's play sessions.</param>
    /// <param name="documentId">The scene document of this toolbar.</param>
    /// <param name="saveInputs">Saves the unsaved documents Play reads, after asking; false when Play must not run.</param>
    /// <param name="placement">Brings the panels with a failure's details forward, if any.</param>
    public ScenePlayToolbarViewModel(PlaySession session, DocumentId documentId, Func<Task<bool>> saveInputs, IPlacementService? placement)
    {
        this.session = session;
        this.documentId = documentId;
        this.saveInputs = saveInputs;
        this.placement = placement;
        this.session.PropertyChanged += this.OnSessionChanged;
        _ = this.session.Settings.EnsureLoadedAsync();
    }

    /// <summary>Gets the player launch settings the flyout edits.</summary>
    public PlaySettings Settings => this.session.Settings;

    /// <summary>Gets the entries of the window size list.</summary>
    public IReadOnlyList<PlayerWindowSizeOption> SizeOptions => PlaySettings.SizeOptions;

    /// <summary>Gets the Play button's text: Restart while this scene plays, otherwise Play.</summary>
    public string PrimaryLabel => this.IsPlayingThisScene ? "Restart" : "Play";

    /// <summary>Gets the Play button's icon.</summary>
    public string PrimaryGlyph => this.IsPlayingThisScene ? RestartGlyph : PlayGlyph;

    /// <summary>Gets how the Play button shows.</summary>
    public PlayPrimaryAccent PrimaryAccent => !this.CanPrimary
        ? PlayPrimaryAccent.Unavailable
        : this.IsPlayingThisScene
            ? this.Status.IsStale ? PlayPrimaryAccent.RestartChanged : PlayPrimaryAccent.Restart
            : PlayPrimaryAccent.Play;

    /// <summary>Gets what the Play button does, or why it is unavailable.</summary>
    public string PrimaryToolTip => this.Status switch
    {
        { IsBusy: true } => "Play is on its way. Press Cancel to stop it (Shift+F5).",
        _ when !this.session.IsPlayerInstalled => $"The installed SDK has no player at '{this.session.PlayerExecutable}'.",
        _ when !this.hasCamera => "Add a camera to play this scene.",
        { IsRunning: true, IsStale: true } when this.IsThisScene => "Restart to play your changes (F5)",
        { IsRunning: true } when this.IsThisScene => "Play the scene again from the start (F5)",
        { IsRunning: true } => $"Stop '{this.Status.SceneName}' and play this scene (F5)",
        _ => "Save, cook and play (F5)",
    };

    /// <summary>Gets a value indicating whether the Play button can be pressed.</summary>
    public bool CanPrimary => !this.Status.IsBusy && this.session.IsPlayerInstalled && this.hasCamera;

    /// <summary>Gets a value indicating whether there is a Play to cancel or a player to close.</summary>
    public bool CanStop => this.Status.IsBusy || this.Status.IsRunning || this.Status.Activity == PlayActivity.PlayerIdle;

    /// <summary>Gets what Stop does: Cancel while Play is on its way, otherwise Stop.</summary>
    public string StopToolTip => this.Status.IsBusy ? "Cancel (Shift+F5)" : "Stop and close the player (Shift+F5)";

    /// <summary>Gets a value indicating whether Play is on its way: saving, cooking or starting the player.</summary>
    public bool IsBusy => this.Status.IsBusy;

    /// <summary>Gets the status text, which names the scene when another scene plays.</summary>
    public string StatusText
    {
        get
        {
            var text = this.Status switch
            {
                { Activity: PlayActivity.Idle } => "Ready · Standalone",
                { Activity: PlayActivity.Cooking } => "Cooking scene…",
                { Activity: PlayActivity.StartingPlayer } => "Starting player…",
                { Activity: PlayActivity.LoadingScene } => "Loading scene…",
                { IsRunning: true, IsStale: true } => "Scene modified",
                { Activity: PlayActivity.Paused } => "Paused",
                { Activity: PlayActivity.Playing, FramesPerSecond: { } fps } => string.Create(CultureInfo.CurrentCulture, $"Playing · {fps} FPS"),
                { Activity: PlayActivity.Playing } => "Playing",
                { Activity: PlayActivity.PlayerIdle } => "Player open",
                { Failure: { } failure } => failure.Summary,
                _ => string.Empty,
            };
            var other = (this.Status.IsBusy || this.Status.IsRunning) && !this.IsThisScene && this.Status.SceneName.Length > 0;
            return other ? $"{this.Status.SceneName} · {text}" : text;
        }
    }

    /// <summary>Gets what follows the status text in the secondary color: a failure's error count, for example
    /// "· 2 errors".</summary>
    public string StatusDetail => this.Status.Failure is { ErrorCount: > 0 } failure
        ? string.Create(CultureInfo.CurrentCulture, $"· {failure.ErrorCount} {(failure.ErrorCount == 1 ? "error" : "errors")}")
        : string.Empty;

    /// <summary>Gets a value indicating whether the status has a detail.</summary>
    public bool HasStatusDetail => this.StatusDetail.Length > 0;

    /// <summary>Gets how the status shows.</summary>
    public PlayStatusTone StatusTone => this.Status switch
    {
        { IsBusy: true } => PlayStatusTone.Busy,
        { Activity: PlayActivity.Failed } => PlayStatusTone.Critical,
        { IsRunning: true, IsStale: true } => PlayStatusTone.Caution,
        { Activity: PlayActivity.Playing } => PlayStatusTone.Success,
        _ => PlayStatusTone.Neutral,
    };

    /// <summary>Gets what the status means and what clicking it shows.</summary>
    public string StatusToolTip => this.Status switch
    {
        { Activity: PlayActivity.Idle } => "Play opens the scene in the standalone player.",
        { Activity: PlayActivity.Cooking } => "Cooking the scene. Click to show the Cooking panel.",
        { Activity: PlayActivity.StartingPlayer or PlayActivity.LoadingScene } => "The player is loading the scene. Click to show its output.",
        { IsRunning: true, IsStale: true } => "A document changed since the session started; Restart (F5) plays your changes. Click to bring the player to the front.",
        { IsRunning: true } or { Activity: PlayActivity.PlayerIdle } => "Click to bring the player to the front.",
        { Failure: { } failure } => PanelOf(failure) switch
        {
            PlayPanel.Problems => $"{failure.Message} Click to show Problems.",
            PlayPanel.Cooking => $"{failure.Message} Click to show the Cooking panel.",
            PlayPanel.Output => $"{failure.Message} Click to show the player's output.",
            _ => failure.Message,
        },
        _ => string.Empty,
    };

    /// <summary>Gets a value indicating whether clicking the status brings the player window to the front.</summary>
    public bool StatusRevealsPlayer => this.Status.IsRunning || this.Status.Activity == PlayActivity.PlayerIdle;

    /// <summary>Gets a value indicating whether clicking the status shows something.</summary>
    public bool CanShowStatus => TargetOf(this.Status) != PlayStatusTarget.None;

    private PlayStatus Status => this.session.Status;

    private bool IsThisScene => this.Status.DocumentId == this.documentId;

    private bool IsPlayingThisScene => this.Status.IsRunning && this.IsThisScene;

    /// <summary>Gets what clicking a status shows.</summary>
    /// <param name="status">The status.</param>
    /// <returns>The player window while one runs, otherwise the panel that explains the step.</returns>
    internal static PlayStatusTarget TargetOf(PlayStatus status) => status switch
    {
        { Activity: PlayActivity.Cooking } => PlayStatusTarget.Cooking,
        { Activity: PlayActivity.StartingPlayer or PlayActivity.LoadingScene } => PlayStatusTarget.Output,
        { IsRunning: true } or { Activity: PlayActivity.PlayerIdle } => PlayStatusTarget.Player,
        { Failure: { } failure } => PanelOf(failure) switch
        {
            PlayPanel.Problems => PlayStatusTarget.Problems,
            PlayPanel.Cooking => PlayStatusTarget.Cooking,
            PlayPanel.Output => PlayStatusTarget.Output,
            _ => PlayStatusTarget.None,
        },
        _ => PlayStatusTarget.None,
    };

    /// <summary>Gets the identity of a panel the play bar brings forward.</summary>
    /// <param name="panel">The panel.</param>
    /// <returns>The panel's identity; <see langword="null"/> for <see cref="PlayPanel.None"/>.</returns>
    internal static string? PanelIdOf(PlayPanel panel) => panel switch
    {
        PlayPanel.Problems => "problems",
        PlayPanel.Cooking => "cook",
        PlayPanel.Output => "log",
        _ => null,
    };

    /// <summary>Gets the panel with the details of a failure.</summary>
    /// <param name="failure">The failure.</param>
    /// <returns>The panel; <see cref="PlayPanel.None"/> when no panel explains it.</returns>
    internal static PlayPanel PanelOf(PlayFailure failure) => failure.Stage switch
    {
        PlayStage.Scene => PlayPanel.Problems,
        PlayStage.Cook => PlayPanel.Cooking,
        PlayStage.Launch when failure.Code != PlayDiagnosticCodes.PlayerMissing => PlayPanel.Output,
        PlayStage.Run => PlayPanel.Output,
        _ => PlayPanel.None,
    };

    /// <summary>Reads the scene again: whether it has a camera to play through.</summary>
    /// <param name="sceneHasCamera">Whether the scene has a camera.</param>
    public void UpdateScene(bool sceneHasCamera)
    {
        if (this.hasCamera == sceneHasCamera)
        {
            return;
        }

        this.hasCamera = sceneHasCamera;
        this.OnPropertyChanged(nameof(this.CanPrimary));
        this.OnPropertyChanged(nameof(this.PrimaryAccent));
        this.OnPropertyChanged(nameof(this.PrimaryToolTip));
        this.PrimaryCommand.NotifyCanExecuteChanged();
    }

    /// <summary>
    /// Saves the unsaved documents Play reads, after asking, then plays or restarts the scene, when the Play button can be
    /// pressed. Play does not run when the user keeps them unsaved or a save fails.
    /// </summary>
    /// <returns>A task that completes when the session began or Play stopped.</returns>
    public async Task PlayAsync()
    {
        if (!this.CanPrimary)
        {
            return;
        }

        if (await this.saveInputs().ConfigureAwait(true))
        {
            await this.session.PlayAsync(this.documentId).ConfigureAwait(true);
        }
    }

    /// <summary>Cancels Play or closes the player, when there is one.</summary>
    /// <returns>A task that completes when the player ended.</returns>
    public Task StopAsync() => this.CanStop ? this.session.StopAsync() : Task.CompletedTask;

    /// <inheritdoc />
    public void Dispose() => this.session.PropertyChanged -= this.OnSessionChanged;

    [RelayCommand(CanExecute = nameof(CanPrimary))]
    private Task PrimaryAsync() => this.PlayAsync();

    [RelayCommand(CanExecute = nameof(CanStop))]
    private Task StopPlayAsync() => this.StopAsync();

    // Always executable: a disabled button would grey the status out. A status with nothing to show is not hit-test
    // visible instead.
    [RelayCommand]
    private void ShowStatus()
    {
        var target = TargetOf(this.Status);
        if (target == PlayStatusTarget.Player)
        {
            _ = this.session.BringPlayerToFront();
            return;
        }

        var panel = target switch
        {
            PlayStatusTarget.Problems => PlayPanel.Problems,
            PlayStatusTarget.Cooking => PlayPanel.Cooking,
            PlayStatusTarget.Output => PlayPanel.Output,
            _ => PlayPanel.None,
        };
        if (PanelIdOf(panel) is { } panelId && this.placement is { } placement)
        {
            _ = placement.ShowPanelAsync(panelId);
        }
    }

    [RelayCommand]
    private void ResetSettings() => this.Settings.Reset();

    private void OnSessionChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (!string.Equals(args.PropertyName, nameof(PlaySession.Status), StringComparison.Ordinal))
        {
            return;
        }

        foreach (var property in StatusProperties)
        {
            this.OnPropertyChanged(property);
        }

        this.PrimaryCommand.NotifyCanExecuteChanged();
        this.StopPlayCommand.NotifyCanExecuteChanged();
    }
}
