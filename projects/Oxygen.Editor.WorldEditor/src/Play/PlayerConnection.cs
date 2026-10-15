// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Nodes;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Dispatching;
using Oxygen.Editor.ContentPipeline.Mounting;
using Oxygen.Editor.Runtime.Player;

namespace Oxygen.Editor.World.Play;

/// <summary>
/// One running player and the editor's hold on it: its control connection and events, the cooked roots it reads,
/// and the session preferences it follows.
/// </summary>
/// <remarks>
/// Created on the UI thread, which also receives every event. The player ends when the connection is disposed; the
/// connection ends when the player does.
/// </remarks>
internal sealed partial class PlayerConnection : IAsyncDisposable
{
    private static readonly string[] Topics =
        [PlayerEvent.LifecycleTopic, PlayerEvent.LogTopic, PlayerEvent.DiagnosticTopic, PlayerEvent.StatsTopic];

    private readonly PlayerProcess process;
    private readonly DispatcherQueue dispatcher;
    private readonly ILogger logger;
    private readonly TaskCompletionSource<string?> firstScene = new(TaskCreationOptions.RunContinuationsAsynchronously);
    private CookedContentMountSet mounts;
    private PlayerSettingsSync? settingsSync;
    private List<PlayerEvent>? replayQueue = [];
    private long lastSequence;
    private bool stopping;
    private bool ended;

    private PlayerConnection(PlayerProcess process, CookedContentMountSet mounts, DispatcherQueue dispatcher, ILogger logger)
    {
        this.process = process;
        this.mounts = mounts;
        this.dispatcher = dispatcher;
        this.logger = logger;
    }

    /// <summary>Occurs for each player event, in order and once.</summary>
    public event EventHandler<PlayerEvent>? EventReceived;

    /// <summary>Occurs once when the player ended; the argument says whether it was asked to, or quit itself.</summary>
    public event EventHandler<bool>? Ended;

    /// <summary>Occurs when the player did not apply a session preference; the argument says which and why.</summary>
    public event EventHandler<string>? SettingRefused;

    /// <summary>Gets the player's last lines of standard output and error.</summary>
    public IReadOnlyList<string> RecentOutput => this.process.RecentOutput;

    /// <summary>Starts the player on a launch file and connects to it.</summary>
    /// <param name="executable">The player executable.</param>
    /// <param name="launchFile">The launch file, which names the roots in <paramref name="mounts"/>.</param>
    /// <param name="mounts">The cooked roots the player reads; the connection holds them from now on.</param>
    /// <param name="settings">The preferences the player follows.</param>
    /// <param name="dispatcher">The UI thread.</param>
    /// <param name="loggerFactory">The logger factory.</param>
    /// <param name="cancellationToken">Cancels the start; a started player is ended.</param>
    /// <returns>The connection, listening to the player's events.</returns>
    /// <exception cref="PlayerLaunchException">The player did not start or refused the editor.</exception>
    public static async Task<PlayerConnection> LaunchAsync(
        string executable,
        string launchFile,
        CookedContentMountSet mounts,
        PlaySessionSettingsService settings,
        DispatcherQueue dispatcher,
        ILoggerFactory? loggerFactory,
        CancellationToken cancellationToken)
    {
        PlayerProcess process;
        try
        {
            process = await PlayerProcess.StartAsync(executable, launchFile, loggerFactory?.CreateLogger<PlayerProcess>(), cancellationToken).ConfigureAwait(true);
        }
        catch
        {
            mounts.Dispose();
            throw;
        }

        var logger = loggerFactory?.CreateLogger<PlayerConnection>() ?? NullLogger<PlayerConnection>.Instance;
        var connection = new PlayerConnection(process, mounts, dispatcher, logger);
        try
        {
            await connection.ListenAsync(cancellationToken).ConfigureAwait(true);
            connection.settingsSync = new PlayerSettingsSync(settings, connection.RequestAsync, connection.OnSettingRefused, logger);

            // The launch file has the preferences of its writing; any later change is sent now.
            connection.settingsSync.SendAll();
            return connection;
        }
        catch
        {
            await connection.DisposeAsync().ConfigureAwait(true);
            throw;
        }
    }

    /// <summary>Waits for the launch file's scene, which the player plays as it starts.</summary>
    /// <param name="cancellationToken">Cancels the wait.</param>
    /// <returns>Null once the scene loaded, otherwise why it failed.</returns>
    /// <exception cref="PlayerLaunchException">The player ended first.</exception>
    public async Task<string?> WaitForLaunchSceneAsync(CancellationToken cancellationToken)
    {
        try
        {
            return await this.firstScene.Task.WaitAsync(cancellationToken).ConfigureAwait(true);
        }
        catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
        {
            throw new PlayerLaunchException("The player ended before the scene loaded.");
        }
    }

    /// <summary>Ends the running session, then plays a scene from the roots that were just written to the launch
    /// file; the player reads the launch file again and remounts.</summary>
    /// <param name="scenePath">The cooked scene's virtual path.</param>
    /// <param name="newMounts">The roots the launch file names now; the connection holds them from now on and
    /// releases the previous ones.</param>
    /// <param name="cancellationToken">Cancels the wait for the scene.</param>
    /// <returns>Null once the scene loaded, otherwise why it failed.</returns>
    /// <exception cref="PlayerCommandException">The connection closed.</exception>
    public async Task<string?> PlaySceneAsync(string scenePath, CookedContentMountSet newMounts, CancellationToken cancellationToken)
    {
        try
        {
            await this.EndSessionAsync(cancellationToken).ConfigureAwait(true);
            _ = this.process.BringToFront();
            var loaded = await this.RequestAsync("ngin.session.begin", new JsonObject { ["scene"] = scenePath }, cancellationToken).ConfigureAwait(true);
            this.LogSessionBegan(scenePath, loaded?["data"]?["camera"]?.GetValue<string>() ?? string.Empty);
            return null;
        }
        catch (PlayerCommandException exception) when (exception.Code != PlayerCommandException.ConnectionClosedCode)
        {
            return exception.Message;
        }
        finally
        {
            // The player remounted the launch file's roots, even for a scene that then failed.
            (this.mounts, var previous) = (newMounts, this.mounts);
            previous.Dispose();
        }
    }

    /// <summary>Brings the player window to the front.</summary>
    /// <returns>Whether the window came forward.</returns>
    public bool BringToFront() => this.process.BringToFront();

    /// <inheritdoc />
    /// <remarks>Quits the player, then releases the roots it read.</remarks>
    public async ValueTask DisposeAsync()
    {
        this.stopping = true;
        this.settingsSync?.Dispose();
        try
        {
            await this.process.DisposeAsync().ConfigureAwait(true);
        }
        finally
        {
            this.mounts.Dispose();
            this.OnDisconnected();
        }
    }

    private async Task EndSessionAsync(CancellationToken cancellationToken)
    {
        try
        {
            _ = await this.RequestAsync("ngin.session.end", parameters: null, cancellationToken).ConfigureAwait(true);
        }
        catch (PlayerCommandException exception) when (exception.Code != PlayerCommandException.ConnectionClosedCode)
        {
            // No session runs.
            this.LogSessionEndRefused(exception.Message);
        }
    }

    private Task<JsonNode?> RequestAsync(string method, JsonObject? parameters, CancellationToken cancellationToken)
        => this.process.Client.RequestAsync(method, parameters, cancellationToken);

    private void OnSettingRefused(string message) => this.SettingRefused?.Invoke(this, message);

    // Subscribing starts live events at the newest one, so the events the starting player published before are read
    // back first; live events wait meanwhile, and the sequence numbers drop the overlap.
    private async Task ListenAsync(CancellationToken cancellationToken)
    {
        var client = this.process.Client;
        client.EventReceived += (_, received) => this.dispatcher.TryEnqueue(() => this.OnPlayerEvent(received));
        client.Disconnected += (_, _) => this.dispatcher.TryEnqueue(this.OnDisconnected);
        await client.SubscribeAsync(Topics, cancellationToken).ConfigureAwait(true);
        var held = await client.ReadEventsAsync(since: 0, Topics, cancellationToken).ConfigureAwait(true);
        var queued = this.replayQueue ?? [];
        this.replayQueue = null;
        foreach (var received in held.Concat(queued).OrderBy(static received => received.Sequence))
        {
            this.Handle(received);
        }
    }

    private void OnPlayerEvent(PlayerEvent received)
    {
        if (this.replayQueue is { } queue)
        {
            queue.Add(received);
            return;
        }

        this.Handle(received);
    }

    private void Handle(PlayerEvent received)
    {
        if (this.ended || received.Sequence <= this.lastSequence)
        {
            return;
        }

        this.lastSequence = received.Sequence;
        if (received.Topic == PlayerEvent.LifecycleTopic)
        {
            this.TrackLifecycle(received);
        }

        this.EventReceived?.Invoke(this, received);
    }

    private void TrackLifecycle(PlayerEvent received)
    {
        switch (received.GetString("state"))
        {
            case "scene_loaded":
                _ = this.firstScene.TrySetResult(null);
                break;
            case "scene_failed":
                _ = this.firstScene.TrySetResult(received.GetString("message") is { Length: > 0 } message ? message : "The scene failed to load.");
                break;
            case "stopping":
                this.stopping = true;
                break;
            default:
                break;
        }
    }

    private void OnDisconnected()
    {
        if (this.ended)
        {
            return;
        }

        this.ended = true;
        this.settingsSync?.Dispose();
        this.LogPlayerGone(this.stopping);
        _ = this.firstScene.TrySetCanceled();
        this.Ended?.Invoke(this, this.stopping);
    }
}
