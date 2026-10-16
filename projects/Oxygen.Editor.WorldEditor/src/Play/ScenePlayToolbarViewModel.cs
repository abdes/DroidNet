// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Globalization;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Documents;
using Oxygen.Editor.Documents;

namespace Oxygen.Editor.World.Play;

/// <summary>
/// The Play transport of one scene's toolbar: Play (Restart while this scene plays) with the player launch settings,
/// Stop (Cancel while Play is on its way), and the status of the workspace's Play.
/// </summary>
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

    private readonly PlaySessionService session;
    private readonly DocumentId documentId;
    private readonly Func<PlayRequest?> createRequest;
    private bool hasCamera;

    /// <summary>Initializes a new instance of the <see cref="ScenePlayToolbarViewModel"/> class.</summary>
    /// <param name="session">The workspace's play sessions.</param>
    /// <param name="documentId">The scene document of this toolbar.</param>
    /// <param name="createRequest">Describes the scene to play; null while the scene is not loaded.</param>
    public ScenePlayToolbarViewModel(PlaySessionService session, DocumentId documentId, Func<PlayRequest?> createRequest)
    {
        this.session = session;
        this.documentId = documentId;
        this.createRequest = createRequest;
        this.session.PropertyChanged += this.OnSessionChanged;
        _ = this.session.Settings.EnsureLoadedAsync();
    }

    /// <summary>Gets the player launch settings the flyout edits.</summary>
    public PlaySessionSettingsService Settings => this.session.Settings;

    /// <summary>Gets the entries of the window size list.</summary>
    public IReadOnlyList<PlayerWindowSizeOption> SizeOptions => PlaySessionSettingsService.SizeOptions;

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
                { Activity: PlayActivity.Saving } => "Saving…",
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
        { Activity: PlayActivity.Saving } => "Saving the open documents.",
        { Activity: PlayActivity.Cooking } => "Cooking the scene. Click to show the Cooking panel.",
        { Activity: PlayActivity.StartingPlayer or PlayActivity.LoadingScene } => "The player is loading the scene. Click to show its output.",
        { IsRunning: true, IsStale: true } => "A document changed since the session started; Restart (F5) plays your changes. Click to bring the player to the front.",
        { IsRunning: true } or { Activity: PlayActivity.PlayerIdle } => "Click to bring the player to the front.",
        { Failure: { } failure } => failure.Panel switch
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
    public bool CanShowStatus => this.session.StatusTarget != PlayStatusTarget.None;

    private PlayStatus Status => this.session.Status;

    private bool IsThisScene => this.Status.DocumentId == this.documentId;

    private bool IsPlayingThisScene => this.Status.IsRunning && this.IsThisScene;

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

    /// <summary>Plays or restarts the scene, when the Play button can be pressed.</summary>
    /// <returns>A task that completes when the session began or Play stopped.</returns>
    public Task PlayAsync()
        => this.CanPrimary && this.createRequest() is { } request ? this.session.PlayAsync(request) : Task.CompletedTask;

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
    private void ShowStatus() => this.session.RevealStatus();

    [RelayCommand]
    private void ResetSettings() => this.Settings.Reset();

    private void OnSessionChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (!string.Equals(args.PropertyName, nameof(PlaySessionService.Status), StringComparison.Ordinal))
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
