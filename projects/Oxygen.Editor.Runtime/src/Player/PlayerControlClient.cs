// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Text.Json.Nodes;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;

namespace Oxygen.Editor.Runtime.Player;

/// <summary>
/// A JSON-RPC 2.0 connection to an Oxygen host application's control server: one JSON object per line over
/// loopback TCP. Requests complete with their reply; events arrive for the subscribed topics.
/// </summary>
/// <remarks>
/// <see cref="EventReceived"/> and <see cref="Disconnected"/> are raised on the connection's reader task, in the
/// order the server sent them.
/// </remarks>
public sealed partial class PlayerControlClient : IAsyncDisposable
{
    private const string EventMethod = "rpc.event";

    private readonly TcpClient connection;
    private readonly NetworkStream stream;
    private readonly ILogger logger;
    private readonly SemaphoreSlim writeGate = new(1, 1);
    private readonly ConcurrentDictionary<long, TaskCompletionSource<JsonNode?>> pending = new();
    private readonly CancellationTokenSource lifetime = new();
    private Task reading = Task.CompletedTask;
    private long nextId;
    private int closed;

    private PlayerControlClient(TcpClient connection, ILogger logger)
    {
        this.connection = connection;
        this.stream = connection.GetStream();
        this.logger = logger;
    }

    /// <summary>Occurs when the server publishes an event to a subscribed topic.</summary>
    public event EventHandler<PlayerEventReceivedEventArgs>? EventReceived;

    /// <summary>Occurs once when the connection closes, from either side.</summary>
    public event EventHandler? Disconnected;

    /// <summary>Gets a value indicating whether the connection is closed.</summary>
    public bool IsClosed => Volatile.Read(ref this.closed) != 0;

    /// <summary>Connects and introduces the client.</summary>
    /// <param name="endpoint">The server's endpoint.</param>
    /// <param name="clientName">The name the server records for this client.</param>
    /// <param name="owner">Whether to claim ownership; the owner's departure ends an application started with
    /// <c>exit_when_owner_leaves</c>.</param>
    /// <param name="logger">The logger.</param>
    /// <param name="cancellationToken">Cancels the connection.</param>
    /// <returns>The connected client.</returns>
    /// <exception cref="PlayerLaunchException">The server refused the client or did not grant ownership.</exception>
    public static async Task<PlayerControlClient> ConnectAsync(
        PlayerControlEndpoint endpoint,
        string clientName,
        bool owner,
        ILogger? logger,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(endpoint);
        var tcp = new TcpClient();
        PlayerControlClient? client = null;
        try
        {
            await tcp.ConnectAsync(IPAddress.Loopback, endpoint.Port, cancellationToken).ConfigureAwait(false);
            client = new PlayerControlClient(tcp, logger ?? NullLogger.Instance);
            client.reading = Task.Run(client.ReadAsync, CancellationToken.None);
            var hello = await client.RequestAsync(
                "rpc.hello",
                new JsonObject { ["token"] = endpoint.Token, ["client"] = clientName, ["owner"] = owner },
                cancellationToken).ConfigureAwait(false);
            if (owner && hello?["owner"]?.GetValue<bool>() != true)
            {
                throw new PlayerLaunchException("Another client already owns the player.");
            }

            return client;
        }
        catch (Exception exception) when (exception is SocketException or IOException or PlayerCommandException)
        {
            await DisposeAsync(client, tcp).ConfigureAwait(false);
            throw new PlayerLaunchException("The editor could not connect to the player: " + exception.Message, exception);
        }
        catch
        {
            await DisposeAsync(client, tcp).ConfigureAwait(false);
            throw;
        }

        static async Task DisposeAsync(PlayerControlClient? client, TcpClient tcp)
        {
            if (client is not null)
            {
                await client.DisposeAsync().ConfigureAwait(false);
            }
            else
            {
                tcp.Dispose();
            }
        }
    }

    /// <summary>Runs a command and waits for its reply.</summary>
    /// <param name="method">The command name, such as <c>ngin.session.begin</c>.</param>
    /// <param name="parameters">The command's arguments, or <see langword="null"/>.</param>
    /// <param name="cancellationToken">Stops waiting; the command keeps running in the player.</param>
    /// <returns>The command's result.</returns>
    /// <exception cref="PlayerCommandException">The command failed, or the connection closed first.</exception>
    public async Task<JsonNode?> RequestAsync(string method, JsonObject? parameters, CancellationToken cancellationToken)
    {
        ArgumentException.ThrowIfNullOrEmpty(method);
        var id = Interlocked.Increment(ref this.nextId);
        var reply = new TaskCompletionSource<JsonNode?>(TaskCreationOptions.RunContinuationsAsynchronously);
        this.pending[id] = reply;
        try
        {
            var request = new JsonObject { ["jsonrpc"] = "2.0", ["id"] = id, ["method"] = method };
            if (parameters is not null)
            {
                request["params"] = parameters;
            }

            this.LogRequest(method, id);
            await this.WriteAsync(request, cancellationToken).ConfigureAwait(false);
            if (this.IsClosed)
            {
                throw ConnectionClosed();
            }

            return await reply.Task.WaitAsync(cancellationToken).ConfigureAwait(false);
        }
        finally
        {
            _ = this.pending.TryRemove(id, out _);
        }
    }

    /// <summary>Runs a command without waiting for it: the server sends no reply to a notification.</summary>
    /// <param name="method">The command name, such as <c>ngin.quit</c>.</param>
    /// <param name="parameters">The command's arguments, or <see langword="null"/>.</param>
    /// <param name="cancellationToken">Cancels the send.</param>
    /// <returns>A task that completes when the notification is sent.</returns>
    /// <exception cref="PlayerCommandException">The connection is closed.</exception>
    public Task NotifyAsync(string method, JsonObject? parameters, CancellationToken cancellationToken)
    {
        ArgumentException.ThrowIfNullOrEmpty(method);
        var notification = new JsonObject { ["jsonrpc"] = "2.0", ["method"] = method };
        if (parameters is not null)
        {
            notification["params"] = parameters;
        }

        return this.WriteAsync(notification, cancellationToken);
    }

    /// <summary>Subscribes to event topics.</summary>
    /// <param name="topics">The topics.</param>
    /// <param name="cancellationToken">Cancels the request.</param>
    /// <returns>A task that completes when the server confirmed the subscription.</returns>
    public Task SubscribeAsync(IEnumerable<string> topics, CancellationToken cancellationToken)
        => this.RequestAsync("rpc.subscribe", new JsonObject { ["topics"] = new JsonArray([.. topics.Select(static topic => (JsonNode)topic)]) }, cancellationToken);

    /// <summary>Reads the events the server still holds, including those published before this client subscribed.</summary>
    /// <param name="since">The last sequence already seen; 0 reads from the oldest held event.</param>
    /// <param name="topics">The topics to read.</param>
    /// <param name="cancellationToken">Cancels the request.</param>
    /// <returns>The events in sequence order.</returns>
    public async Task<IReadOnlyList<PlayerEvent>> ReadEventsAsync(long since, IEnumerable<string> topics, CancellationToken cancellationToken)
    {
        var result = await this.RequestAsync(
            "rpc.events",
            new JsonObject { ["since"] = since, ["topics"] = new JsonArray([.. topics.Select(static topic => (JsonNode)topic)]) },
            cancellationToken).ConfigureAwait(false);
        return result?["events"] is JsonArray events
            ? [.. events.OfType<JsonObject>().Select(ToEvent).OfType<PlayerEvent>()]
            : [];
    }

    /// <inheritdoc />
    public async ValueTask DisposeAsync()
    {
        this.Close();
        await this.lifetime.CancelAsync().ConfigureAwait(false);
        try
        {
            await this.reading.ConfigureAwait(false);
        }
        catch (OperationCanceledException)
        {
            // The reader stops when the lifetime ends.
        }

        await this.stream.DisposeAsync().ConfigureAwait(false);
        this.lifetime.Dispose();
        this.writeGate.Dispose();
    }

    private static PlayerEvent? ToEvent(JsonObject envelope)
        => envelope["topic"]?.GetValue<string>() is { } topic && envelope["data"] is JsonObject data
            ? new PlayerEvent(envelope["seq"]?.GetValue<long>() ?? 0, topic, (JsonObject)data.DeepClone())
            : null;

    private static PlayerCommandException ConnectionClosed()
        => new(PlayerCommandException.ConnectionClosedCode, "The player connection closed.", data: null);

    private async Task WriteAsync(JsonObject message, CancellationToken cancellationToken)
    {
        var bytes = Encoding.UTF8.GetBytes(message.ToJsonString() + "\n");
        await this.writeGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            await this.stream.WriteAsync(bytes, cancellationToken).ConfigureAwait(false);
        }
        catch (Exception exception) when (exception is IOException or ObjectDisposedException)
        {
            this.Close();
            throw ConnectionClosed();
        }
        finally
        {
            _ = this.writeGate.Release();
        }
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "A handler failure is logged; the connection keeps delivering the server's messages.")]
    private async Task ReadAsync()
    {
        try
        {
            using var reader = new StreamReader(this.stream, Encoding.UTF8, detectEncodingFromByteOrderMarks: false, leaveOpen: true);
            while (await reader.ReadLineAsync(this.lifetime.Token).ConfigureAwait(false) is { } line)
            {
                try
                {
                    this.Dispatch(line);
                }
                catch (Exception exception)
                {
                    this.LogMessageFailed(exception);
                }
            }
        }
        catch (Exception exception) when (exception is IOException or ObjectDisposedException or OperationCanceledException)
        {
            // The connection closed.
        }
        finally
        {
            this.Close();
        }
    }

    private void Dispatch(string line)
    {
        if (JsonNode.Parse(line) is not JsonObject message)
        {
            return;
        }

        if (message["id"] is JsonValue idValue && idValue.TryGetValue<long>(out var id))
        {
            if (this.pending.TryGetValue(id, out var reply))
            {
                if (message["error"] is JsonObject error)
                {
                    var code = error["code"]?.GetValue<int>() ?? 0;
                    var text = error["message"]?.GetValue<string>() ?? "The command failed.";
                    this.LogRequestFailed(id, code, text);
                    _ = reply.TrySetException(new PlayerCommandException(code, text, error["data"]?.DeepClone()));
                }
                else
                {
                    _ = reply.TrySetResult(message["result"]?.DeepClone());
                }
            }

            return;
        }

        if (string.Equals(message["method"]?.GetValue<string>(), EventMethod, StringComparison.Ordinal)
            && message["params"] is JsonObject parameters
            && ToEvent(parameters) is { } received)
        {
            this.EventReceived?.Invoke(this, new PlayerEventReceivedEventArgs(received));
        }
    }

    private void Close()
    {
        if (Interlocked.Exchange(ref this.closed, 1) != 0)
        {
            return;
        }

        this.connection.Dispose();
        foreach (var reply in this.pending.Values)
        {
            _ = reply.TrySetException(ConnectionClosed());
        }

        this.LogDisconnected();
        this.Disconnected?.Invoke(this, EventArgs.Empty);
    }
}
