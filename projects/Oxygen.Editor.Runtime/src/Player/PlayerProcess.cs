// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;

namespace Oxygen.Editor.Runtime.Player;

/// <summary>
/// A running <c>Oxygen.Player</c> that this editor started and owns over its control connection. The player stays
/// up between play sessions; it ends when it is quit, when its window closes, or when the editor's connection goes
/// away.
/// </summary>
public sealed partial class PlayerProcess : IAsyncDisposable
{
    /// <summary>How long a starting player may take to accept the editor.</summary>
    public static readonly TimeSpan StartTimeout = TimeSpan.FromSeconds(30);

    /// <summary>How long a player may take to end after it was asked to.</summary>
    public static readonly TimeSpan QuitTimeout = TimeSpan.FromSeconds(3);

    private const int OutputLines = 20;
    private const string ClientName = "Oxygen Editor";

    private readonly Process process;
    private readonly ILogger logger;
    private readonly Queue<string> output = new();
    private PlayerControlClient? client;
    private bool started;

    private PlayerProcess(Process process, ILogger logger)
    {
        this.process = process;
        this.logger = logger;
    }

    /// <summary>Gets the control connection.</summary>
    public PlayerControlClient Client => this.client ?? throw new InvalidOperationException("The player is not connected.");

    /// <summary>Gets a value indicating whether the process has ended.</summary>
    public bool HasExited => this.process.HasExited;

    /// <summary>Gets the last lines the process wrote to its console, which explain a failed start.</summary>
    public IReadOnlyList<string> RecentOutput
    {
        get
        {
            lock (this.output)
            {
                return [.. this.output];
            }
        }
    }

    /// <summary>Starts a player on a launch file and connects to it as its owner.</summary>
    /// <param name="executable">The SDK's <c>Oxygen.Player.exe</c>.</param>
    /// <param name="launchFile">The launch file.</param>
    /// <param name="logger">The logger.</param>
    /// <param name="cancellationToken">Cancels the start; a started process is ended.</param>
    /// <returns>The running, connected player.</returns>
    /// <exception cref="PlayerLaunchException">The player is missing, failed to start or refused the editor.</exception>
    public static async Task<PlayerProcess> StartAsync(string executable, string launchFile, ILogger? logger, CancellationToken cancellationToken)
    {
        ArgumentException.ThrowIfNullOrEmpty(executable);
        ArgumentException.ThrowIfNullOrEmpty(launchFile);
        if (!File.Exists(executable))
        {
            throw new PlayerLaunchException($"The installed SDK has no player at '{executable}'.");
        }

        var start = new ProcessStartInfo(executable)
        {
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            WorkingDirectory = Path.GetDirectoryName(executable)!,
        };
        start.ArgumentList.Add("--launch");
        start.ArgumentList.Add(launchFile);
        var process = new Process { StartInfo = start, EnableRaisingEvents = true };
        var player = new PlayerProcess(process, logger ?? NullLogger.Instance);
        process.OutputDataReceived += player.OnOutput;
        process.ErrorDataReceived += player.OnOutput;
        try
        {
            if (!process.Start())
            {
                throw new PlayerLaunchException($"'{executable}' did not start.");
            }

            player.started = true;
            process.BeginOutputReadLine();
            process.BeginErrorReadLine();
            player.LogStarted(process.Id, executable, launchFile);
            var endpoint = await PlayerControlEndpoint.WaitForAsync(process, StartTimeout, cancellationToken).ConfigureAwait(false);
            player.client = await PlayerControlClient.ConnectAsync(endpoint, ClientName, owner: true, logger, cancellationToken).ConfigureAwait(false);
            player.LogConnected(process.Id, endpoint.Port);
            return player;
        }
        catch (Exception exception) when (exception is System.ComponentModel.Win32Exception)
        {
            await player.DisposeAsync().ConfigureAwait(false);
            throw new PlayerLaunchException($"'{executable}' could not be started: {exception.Message}", exception);
        }
        catch (PlayerLaunchException exception) when (player.started)
        {
            // A player that ended explains why on its output; the wait reads the output to its end.
            await player.DisposeAsync().ConfigureAwait(false);
            throw new PlayerLaunchException(exception.Message, exception) { PlayerOutput = player.RecentOutput };
        }
        catch
        {
            await player.DisposeAsync().ConfigureAwait(false);
            throw;
        }
    }

    /// <summary>Brings the player window to the front with input focus.</summary>
    /// <remarks>Windows lets the foreground process give the foreground away, so this works while the editor is
    /// the window the user just used.</remarks>
    /// <returns>Whether the window came forward.</returns>
    public bool BringToFront()
    {
        if (this.process.HasExited)
        {
            return false;
        }

        this.process.Refresh();
        var window = this.process.MainWindowHandle;
        if (window == nint.Zero)
        {
            return false;
        }

        if (PlayerWindowNative.IsIconic(window))
        {
            _ = PlayerWindowNative.ShowWindow(window, PlayerWindowNative.ShowRestore);
        }

        return PlayerWindowNative.SetForegroundWindow(window);
    }

    /// <summary>Asks the player to end and waits for it; a player that does not end in time is killed.</summary>
    /// <returns>A task that completes when the process has ended.</returns>
    public async Task QuitAsync()
    {
        if (!this.started)
        {
            return;
        }

        if (this.process.HasExited)
        {
            // Waiting on an ended process reads its redirected output to the end, so the last lines are kept.
            await this.process.WaitForExitAsync(CancellationToken.None).ConfigureAwait(false);
            return;
        }

        using var timeout = new CancellationTokenSource(QuitTimeout);
        try
        {
            if (this.client is { IsClosed: false } connected)
            {
                try
                {
                    // A notification: the player closes the connection as it ends, so no reply would arrive.
                    await connected.NotifyAsync("ngin.quit", parameters: null, timeout.Token).ConfigureAwait(false);
                }
                catch (PlayerCommandException)
                {
                    // The connection closed, so the player is ending already.
                }
            }

            await this.process.WaitForExitAsync(timeout.Token).ConfigureAwait(false);
            this.LogQuit(this.process.Id);
        }
        catch (OperationCanceledException)
        {
            this.LogKilled(this.process.Id);
            this.process.Kill();
            await this.process.WaitForExitAsync(CancellationToken.None).ConfigureAwait(false);
        }
    }

    /// <inheritdoc />
    public async ValueTask DisposeAsync()
    {
        await this.QuitAsync().ConfigureAwait(false);
        if (this.client is { } connected)
        {
            await connected.DisposeAsync().ConfigureAwait(false);
        }

        this.process.Dispose();
    }

    private void OnOutput(object sender, DataReceivedEventArgs args)
    {
        if (args.Data is not { } line)
        {
            return;
        }

        lock (this.output)
        {
            this.output.Enqueue(line);
            while (this.output.Count > OutputLines)
            {
                _ = this.output.Dequeue();
            }
        }
    }
}
