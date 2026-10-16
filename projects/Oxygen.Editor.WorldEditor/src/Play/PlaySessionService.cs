// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Controls.OutputConsole.Model;
using DroidNet.Hosting.WinUI;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Dispatching;
using Oxygen.Editor.ContentBrowser.Scripts;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Mounting;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Runtime.Player;
using Oxygen.Managed.Core.Compatibility;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.Play;

/// <summary>
/// Runs scenes in the SDK player. Play saves, cooks, then starts the player on the scene, or plays the scene in the
/// running player; Stop ends the player. The player window lives as long as the session: closing it ends the
/// session too. One player runs for the workspace, following the session preferences as they change.
/// </summary>
/// <remarks>
/// Every public member and every event is used on the UI thread; the player's events are marshalled there.
/// </remarks>
public sealed partial class PlaySessionService : ObservableObject, IRuntimeProblemSource, IDisposable
{
    private readonly IProjectContextService projects;
    private readonly IContentPipelineService pipeline;
    private readonly CookPublicationService publication;
    private readonly CookedContentMountService mountService;
    private readonly OpenDocuments documents;
    private readonly IOperationResultPublisher operationResults;
    private readonly OutputLogBuffer output;
    private readonly DispatcherQueue dispatcher;
    private readonly ILogger logger;
    private readonly ILoggerFactory? loggerFactory;
    private readonly List<ScriptProblem> runtimeProblems = [];
    private IPlayWorkspace? workspace;
    private PlayerConnection? player;
    private CancellationTokenSource? play;
    // The scene whose session runs or ran last; its problems name its nodes.
    private PlayRequest? scene;
    private RuntimeProblemReader? reader;
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="PlaySessionService"/> class.</summary>
    /// <param name="projects">The active project.</param>
    /// <param name="pipeline">The content pipeline that cooks the scene.</param>
    /// <param name="publication">The cooked content publication the player mounts.</param>
    /// <param name="mountService">Validates and holds the cooked roots while the player reads them.</param>
    /// <param name="settings">The play session preferences.</param>
    /// <param name="documents">The open documents, whose edits make a running session stale.</param>
    /// <param name="operationResults">Publishes Play failures.</param>
    /// <param name="output">The Output panel's buffer, which receives the player's log.</param>
    /// <param name="hosting">The UI thread that player events are marshalled to.</param>
    /// <param name="loggerFactory">The logger factory.</param>
    public PlaySessionService(
        IProjectContextService projects,
        IContentPipelineService pipeline,
        CookPublicationService publication,
        CookedContentMountService mountService,
        PlaySessionSettingsService settings,
        OpenDocuments documents,
        IOperationResultPublisher operationResults,
        OutputLogBuffer output,
        HostingContext hosting,
        ILoggerFactory? loggerFactory = null)
    {
        ArgumentNullException.ThrowIfNull(hosting);
        this.projects = projects;
        this.pipeline = pipeline;
        this.publication = publication;
        this.mountService = mountService;
        this.Settings = settings;
        this.documents = documents;
        this.operationResults = operationResults;
        this.output = output;
        this.dispatcher = hosting.Dispatcher;
        this.loggerFactory = loggerFactory;
        this.logger = loggerFactory?.CreateLogger<PlaySessionService>() ?? NullLogger<PlaySessionService>.Instance;
        this.PlayerExecutable = EditorNativeInstallation.Discover(
            AppContext.BaseDirectory,
#if DEBUG
            "Debug").PlayerPath;
#else
            "Release").PlayerPath;
#endif
        this.documents.Opened += this.OnDocumentOpened;
        this.documents.Closed += this.OnDocumentClosed;
        foreach (var document in this.documents.Documents)
        {
            document.StateChanged += this.OnDocumentStateChanged;
        }
    }

    /// <inheritdoc />
    public event EventHandler? RuntimeProblemsChanged;

    /// <summary>Gets the play session preferences.</summary>
    public PlaySessionSettingsService Settings { get; }

    /// <summary>Gets the SDK's player executable.</summary>
    public string PlayerExecutable { get; }

    /// <summary>Gets a value indicating whether the installed SDK has a player.</summary>
    public bool IsPlayerInstalled => File.Exists(this.PlayerExecutable);

    /// <summary>Gets what Play is doing.</summary>
    [ObservableProperty]
    public partial PlayStatus Status { get; private set; } = PlayStatus.Idle;

    /// <inheritdoc />
    public IReadOnlyList<ScriptProblem> RuntimeProblems => this.runtimeProblems;

    /// <summary>Gets what clicking the status shows: the player window while one runs, otherwise the panel that
    /// explains the step on its way or the failure.</summary>
    public PlayStatusTarget StatusTarget => this.Status switch
    {
        { Activity: PlayActivity.Cooking } => PlayStatusTarget.Cooking,
        { Activity: PlayActivity.StartingPlayer or PlayActivity.LoadingScene } => PlayStatusTarget.Output,
        { IsRunning: true } or { Activity: PlayActivity.PlayerIdle } => PlayStatusTarget.Player,
        { Failure.Panel: PlayPanel.Problems } => PlayStatusTarget.Problems,
        { Failure.Panel: PlayPanel.Cooking } => PlayStatusTarget.Cooking,
        { Failure.Panel: PlayPanel.Output } => PlayStatusTarget.Output,
        _ => PlayStatusTarget.None,
    };

    /// <summary>Connects the workspace that saves documents and shows panels.</summary>
    /// <param name="value">The workspace.</param>
    public void AttachWorkspace(IPlayWorkspace value) => this.workspace = value;

    /// <summary>Plays a scene: saves, cooks, then starts the player on it or plays it in the running player.</summary>
    /// <remarks>A running session keeps running until the new scene is cooked. A Play that fails ends the
    /// player.</remarks>
    /// <param name="request">The scene to play.</param>
    /// <returns>A task that completes when the session began or Play stopped; failures are published.</returns>
    public async Task PlayAsync(PlayRequest request)
    {
        ArgumentNullException.ThrowIfNull(request);
        if (this.disposed || this.Status.IsBusy || this.projects.ActiveProject is not { } project)
        {
            return;
        }

        using var cancellation = new CancellationTokenSource();
        this.play = cancellation;
        this.LogPlayRequested(request.Scene.Name);
        try
        {
            await this.RunPlayAsync(project, request, cancellation.Token).ConfigureAwait(true);
        }
        catch (OperationCanceledException) when (cancellation.IsCancellationRequested)
        {
            // A player that started as Play was stopped ends too.
            this.LogPlayCancelled(request.Scene.Name);
            await this.ClosePlayerAsync().ConfigureAwait(true);
            this.Status = PlayStatus.Idle;
        }
        catch (PlayerLaunchException exception)
        {
            await this.FailAsync(PlayDiagnosticCodes.LaunchFailed, "Launch failed", exception.Message, PlayPanel.Output, exception).ConfigureAwait(true);
        }
        catch (PlayerCommandException exception) when (exception.Code == PlayerCommandException.ConnectionClosedCode)
        {
            await this.FailAsync(PlayDiagnosticCodes.LaunchFailed, "Launch failed", "The player ended before the session began.", PlayPanel.Output, exception).ConfigureAwait(true);
        }
        finally
        {
            this.play = null;
        }
    }

    /// <summary>Stops Play: cancels a Play on its way and ends the player.</summary>
    /// <returns>A task that completes when the player ended.</returns>
    public async Task StopAsync()
    {
        this.LogStopRequested(this.Status.SceneName);
        await this.EndAsync().ConfigureAwait(true);
        this.Status = PlayStatus.Idle;
    }

    /// <summary>Shows what the status is about: <see cref="StatusTarget"/>.</summary>
    public void RevealStatus()
    {
        switch (this.StatusTarget)
        {
            case PlayStatusTarget.Player:
                _ = this.player?.BringToFront();
                break;
            case PlayStatusTarget.Problems:
                this.workspace?.Reveal(PlayPanel.Problems);
                break;
            case PlayStatusTarget.Cooking:
                this.workspace?.Reveal(PlayPanel.Cooking);
                break;
            case PlayStatusTarget.Output:
                this.workspace?.Reveal(PlayPanel.Output);
                break;
            default:
                break;
        }
    }

    /// <inheritdoc />
    /// <remarks>The player names nodes, not identities, so only a name that one node has is selected.</remarks>
    public Task<bool> SelectProblemNodeAsync(ScriptProblem problem)
    {
        ArgumentNullException.ThrowIfNull(problem);
        if (problem is not { IsRuntime: true, SlotNode: { } name } || this.scene is not { } played || this.workspace is not { } host)
        {
            return Task.FromResult(false);
        }

        var named = played.Scene.AllNodes.Where(node => string.Equals(node.Name, name, StringComparison.Ordinal)).Take(2).ToArray();
        return named.Length == 1 ? host.SelectNodeAsync(played.DocumentId, named[0]) : Task.FromResult(false);
    }

    /// <summary>Ends the player for a closing project or editor.</summary>
    /// <returns>A task that completes when the player ended.</returns>
    public Task ShutdownAsync() => this.EndAsync();

    /// <inheritdoc />
    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.disposed = true;
        this.documents.Opened -= this.OnDocumentOpened;
        this.documents.Closed -= this.OnDocumentClosed;
        foreach (var document in this.documents.Documents)
        {
            document.StateChanged -= this.OnDocumentStateChanged;
        }

        // The player ends when its owner's connection closes, so releasing the connection ends it too.
        _ = this.EndAsync();
    }

    private static string LaunchFilePath(ProjectContext project) => Path.Combine(project.ProjectRoot, ".build", "play", "launch.json");

    private static IReadOnlyList<string> ScriptSourceRoots(ProjectContext project)
        => [.. project.AuthoringMounts.Select(mount => Path.GetFullPath(Path.Combine(project.ProjectRoot, mount.RelativePath))).Where(Directory.Exists)];

    private async Task RunPlayAsync(ProjectContext project, PlayRequest request, CancellationToken cancellationToken)
    {
        this.Status = new PlayStatus(PlayActivity.Saving, request.DocumentId, request.Scene.Name);
        if (!this.IsPlayerInstalled)
        {
            await this.FailAsync(PlayDiagnosticCodes.PlayerMissing, "Player missing", $"The installed SDK has no player at '{this.PlayerExecutable}'. Install the engine SDK, then press Play again.", PlayPanel.None).ConfigureAwait(true);
            return;
        }

        if (this.workspace is { } host && !await host.SaveAllAsync().ConfigureAwait(true))
        {
            await this.FailAsync(PlayDiagnosticCodes.SaveFailed, "Save failed", "A document could not be saved, so the scene was not played. Its save result says why.", PlayPanel.None).ConfigureAwait(true);
            return;
        }

        cancellationToken.ThrowIfCancellationRequested();
        this.Status = this.Status with { Activity = PlayActivity.Cooking };
        var cook = await this.pipeline.CookCurrentSceneAsync(request.SceneAssetUri, cancellationToken).ConfigureAwait(true);
        if (cook.Status == OperationStatus.Failed)
        {
            await this.FailAsync(PlayDiagnosticCodes.CookFailed, "Cook failed", $"'{request.Scene.Name}' did not cook. The Cooking panel shows why.", PlayPanel.Cooking, diagnostics: cook.Diagnostics).ConfigureAwait(true);
            return;
        }

        var scenePath = ContentPipelinePaths.ToCookedScenePath(request.SceneAssetUri);
        var mounts = await this.WriteLaunchFileAsync(project, request, scenePath, cancellationToken).ConfigureAwait(true);
        this.scene = request;
        this.reader = new RuntimeProblemReader(project);
        var failure = this.player is { } running
            ? await this.PlayInRunningPlayerAsync(running, scenePath, mounts, cancellationToken).ConfigureAwait(true)
            : await this.LaunchPlayerAsync(project, mounts, cancellationToken).ConfigureAwait(true);
        if (failure is not null)
        {
            await this.FailAsync(PlayDiagnosticCodes.SceneFailed, "Scene failed", failure, PlayPanel.Problems).ConfigureAwait(true);
            return;
        }

        this.Status = new PlayStatus(PlayActivity.Playing, request.DocumentId, request.Scene.Name);
        this.LogPlaying(request.Scene.Name);
    }

    // The launch file names the roots that were just published; they are held until the player reads others.
    private async Task<CookedContentMountSet> WriteLaunchFileAsync(ProjectContext project, PlayRequest request, string scenePath, CancellationToken cancellationToken)
    {
        using var publishedReader = await this.publication.AcquireForMountAsync(project, cancellationToken).ConfigureAwait(true);
        var mounts = await this.mountService.PrepareAsync(project, publishedReader, cancellationToken).ConfigureAwait(true);
        try
        {
            var window = new PlayerLaunchWindow(
                $"{request.Scene.Name} - Oxygen Player",
                this.Settings.WindowMode,
                this.Settings.Width,
                this.Settings.Height,
                this.Settings.VSync,
                this.Settings.FrameCap);
            var launch = new PlayerLaunchFile(mounts.Roots, scenePath, ScriptSourceRoots(project), window);
            var path = LaunchFilePath(project);
            await launch.WriteAsync(path, cancellationToken).ConfigureAwait(true);
            this.LogLaunchFileWritten(path, mounts.Roots.Count, launch.Scene);
            return mounts;
        }
        catch
        {
            mounts.Dispose();
            throw;
        }
    }

    private Task<string?> PlayInRunningPlayerAsync(PlayerConnection running, string scenePath, CookedContentMountSet mounts, CancellationToken cancellationToken)
    {
        this.Status = this.Status with { Activity = PlayActivity.LoadingScene };
        return running.PlaySceneAsync(scenePath, mounts, cancellationToken);
    }

    private async Task<string?> LaunchPlayerAsync(ProjectContext project, CookedContentMountSet mounts, CancellationToken cancellationToken)
    {
        this.Status = this.Status with { Activity = PlayActivity.StartingPlayer };
        var started = await PlayerConnection.LaunchAsync(this.PlayerExecutable, LaunchFilePath(project), mounts, this.Settings, this.dispatcher, this.loggerFactory, cancellationToken).ConfigureAwait(true);
        this.player = started;
        started.EventReceived += this.OnPlayerEvent;
        started.SettingRefused += this.OnSettingRefused;
        started.Ended += this.OnPlayerEnded;
        _ = started.BringToFront();
        this.Status = this.Status with { Activity = PlayActivity.LoadingScene };
        return await started.WaitForLaunchSceneAsync(cancellationToken).ConfigureAwait(true);
    }

    // Cancels a Play on its way and ends the player.
    private async Task EndAsync()
    {
        if (this.play is { } pending)
        {
            await pending.CancelAsync().ConfigureAwait(true);
        }

        await this.ClosePlayerAsync().ConfigureAwait(true);
    }

    private async Task ClosePlayerAsync()
    {
        if (this.player is { } running)
        {
            this.player = null;
            await running.DisposeAsync().ConfigureAwait(true);
        }
    }

    private void OnDocumentOpened(object? sender, DocumentEventArgs args) => args.Document.StateChanged += this.OnDocumentStateChanged;

    private void OnDocumentClosed(object? sender, DocumentEventArgs args) => args.Document.StateChanged -= this.OnDocumentStateChanged;

    private void OnDocumentStateChanged(object? sender, EventArgs args)
    {
        // Play saved every document, so any document with unsaved changes was edited after the session started.
        if (this.Status is { IsRunning: true, IsStale: false } && sender is Document { IsDirty: true })
        {
            this.Status = this.Status with { IsStale = true };
        }
    }

    // Ends the player and shows why Play failed, in the status and as an operation result.
    private async Task FailAsync(
        string code,
        string summary,
        string message,
        PlayPanel panel,
        Exception? exception = null,
        IReadOnlyList<DiagnosticRecord>? diagnostics = null)
    {
        // A player that never connected left its output on the launch failure.
        var playerOutput = exception is PlayerLaunchException { PlayerOutput.Count: > 0 } launch
            ? launch.PlayerOutput
            : this.player?.RecentOutput;
        await this.ClosePlayerAsync().ConfigureAwait(true);
        if (panel == PlayPanel.Output && playerOutput is not null)
        {
            this.AppendPlayerOutput(playerOutput);
        }

        var errors = panel switch
        {
            PlayPanel.Problems => this.runtimeProblems.Count(static problem => problem.Severity == ScriptProblemSeverity.Error),
            PlayPanel.Cooking => diagnostics?.Count(static record => record.Severity == DiagnosticSeverity.Error) ?? 0,
            _ => 0,
        };
        this.Status = this.Status with
        {
            Activity = PlayActivity.Failed,
            IsStale = false,
            FramesPerSecond = null,
            Failure = new PlayFailure(summary, message, panel, errors),
        };
        this.PublishFailure(PlayOperationKinds.Play, code, summary, message, exception?.Message, playerOutput, diagnostics, exception);
    }

    private void PublishFailure(
        string operationKind,
        string code,
        string title,
        string message,
        string? technicalMessage,
        IReadOnlyList<string>? playerOutput = null,
        IReadOnlyList<DiagnosticRecord>? diagnostics = null,
        Exception? exception = null)
    {
        var operationId = Guid.NewGuid();
        var technical = playerOutput is { Count: > 0 } lines ? string.Join(Environment.NewLine, lines) : technicalMessage;
        this.LogPlayFailed(code, message);
        this.operationResults.Publish(new OperationResult
        {
            OperationId = operationId,
            OperationKind = operationKind,
            Status = OperationStatus.Failed,
            Severity = DiagnosticSeverity.Error,
            Title = title,
            Message = message,
            CompletedAt = DateTimeOffset.UtcNow,
            AffectedScope = new AffectedScope { ProjectId = this.projects.ActiveProject?.ProjectId },
            Diagnostics = diagnostics is { Count: > 0 }
                ? diagnostics
                :
                [
                    new DiagnosticRecord
                    {
                        OperationId = operationId,
                        Domain = FailureDomain.PlaySession,
                        Severity = DiagnosticSeverity.Error,
                        Code = code,
                        Message = message,
                        TechnicalMessage = technical,
                        ExceptionType = exception?.GetType().FullName,
                    },
                ],
        });
    }
}
