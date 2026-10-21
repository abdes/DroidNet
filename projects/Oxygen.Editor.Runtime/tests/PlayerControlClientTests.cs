// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Text.Json.Nodes;
using AwesomeAssertions;
using Oxygen.Editor.Runtime.Player;

namespace Oxygen.Editor.Runtime.Tests;

/// <summary>
/// Checks the JSON-RPC client against an in-process server that speaks the control protocol: one JSON object per
/// line over loopback TCP.
/// </summary>
[TestClass]
public sealed class PlayerControlClientTests
{
    private const string Token = "secret";

    public TestContext TestContext { get; set; } = null!;

    [TestMethod]
    public async Task ConnectAsync_IntroducesTheClientAsOwner()
    {
        await using var server = await FakeControlServer.StartAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var connecting = PlayerControlClient.ConnectAsync(new PlayerControlEndpoint(server.Port, Token), "Editor", owner: true, logger: null, this.TestContext.CancellationToken);

        var hello = await server.ReadAsync().ConfigureAwait(false);
        await server.ReplyAsync(hello, new JsonObject { ["owner"] = true }).ConfigureAwait(false);
        await using var client = await connecting.ConfigureAwait(false);

        _ = hello["method"]!.GetValue<string>().Should().Be("rpc.hello");
        _ = hello["params"]!["token"]!.GetValue<string>().Should().Be(Token);
        _ = hello["params"]!["owner"]!.GetValue<bool>().Should().BeTrue();
        _ = client.IsClosed.Should().BeFalse();
    }

    [TestMethod]
    public async Task ConnectAsync_FailsWhenOwnershipIsRefused()
    {
        await using var server = await FakeControlServer.StartAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var connecting = PlayerControlClient.ConnectAsync(new PlayerControlEndpoint(server.Port, Token), "Editor", owner: true, logger: null, this.TestContext.CancellationToken);

        await server.ReplyAsync(await server.ReadAsync().ConfigureAwait(false), new JsonObject { ["owner"] = false }).ConfigureAwait(false);

        var connect = async () => await connecting.ConfigureAwait(false);
        _ = await connect.Should().ThrowAsync<PlayerLaunchException>().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task RequestAsync_CompletesWithItsOwnReplyWhenRepliesArriveOutOfOrder()
    {
        await using var server = await FakeControlServer.StartAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        await using var client = await this.ConnectAsync(server).ConfigureAwait(false);

        var first = client.RequestAsync("ngin.session.begin", new JsonObject { ["scene"] = "/Content/Scenes/A.oscene" }, this.TestContext.CancellationToken);
        var second = client.RequestAsync("ngin.pause", parameters: null, this.TestContext.CancellationToken);
        var firstRequest = await server.ReadAsync().ConfigureAwait(false);
        var secondRequest = await server.ReadAsync().ConfigureAwait(false);
        await server.ReplyAsync(secondRequest, new JsonObject { ["output"] = "paused" }).ConfigureAwait(false);
        await server.ReplyAsync(firstRequest, new JsonObject { ["output"] = "loaded" }).ConfigureAwait(false);

        _ = (await second.ConfigureAwait(false))!["output"]!.GetValue<string>().Should().Be("paused");
        _ = (await first.ConfigureAwait(false))!["output"]!.GetValue<string>().Should().Be("loaded");
        _ = firstRequest["params"]!["scene"]!.GetValue<string>().Should().Be("/Content/Scenes/A.oscene");
    }

    [TestMethod]
    public async Task RequestAsync_ThrowsTheCommandFailureWithItsData()
    {
        await using var server = await FakeControlServer.StartAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        await using var client = await this.ConnectAsync(server).ConfigureAwait(false);

        var request = client.RequestAsync("ngin.session.begin", parameters: null, this.TestContext.CancellationToken);
        var sent = await server.ReadAsync().ConfigureAwait(false);
        await server.SendAsync(new JsonObject
        {
            ["jsonrpc"] = "2.0",
            ["id"] = sent["id"]!.DeepClone(),
            ["error"] = new JsonObject { ["code"] = -32000, ["message"] = "the scene failed", ["data"] = new JsonObject { ["output"] = "no camera" } },
        }).ConfigureAwait(false);

        var run = async () => await request.ConfigureAwait(false);
        var failure = (await run.Should().ThrowAsync<PlayerCommandException>().ConfigureAwait(false)).Which;
        _ = failure.Code.Should().Be(-32000);
        _ = failure.Message.Should().Be("the scene failed");
        _ = failure.ErrorData!["output"]!.GetValue<string>().Should().Be("no camera");
    }

    [TestMethod]
    public async Task NotifyAsync_SendsNoIdAndExpectsNoReply()
    {
        await using var server = await FakeControlServer.StartAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        await using var client = await this.ConnectAsync(server).ConfigureAwait(false);

        await client.NotifyAsync("ngin.quit", parameters: null, this.TestContext.CancellationToken).ConfigureAwait(false);

        var sent = await server.ReadAsync().ConfigureAwait(false);
        _ = sent["method"]!.GetValue<string>().Should().Be("ngin.quit");
        _ = sent.ContainsKey("id").Should().BeFalse();
    }

    [TestMethod]
    public async Task Events_ArriveInOrderAndHeldEventsCanBeReadBack()
    {
        await using var server = await FakeControlServer.StartAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        await using var client = await this.ConnectAsync(server).ConfigureAwait(false);
        var received = new List<PlayerEvent>();
        var two = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        client.EventReceived += (_, args) =>
        {
            received.Add(args.Event);
            if (received.Count == 2)
            {
                two.SetResult();
            }
        };

        await server.SendEventAsync(7, PlayerEvent.LifecycleTopic, new JsonObject { ["state"] = "session_started", ["scene"] = "/S.oscene" }).ConfigureAwait(false);
        await server.SendEventAsync(8, PlayerEvent.LogTopic, new JsonObject { ["severity"] = "info", ["channel"] = "script", ["text"] = "hello" }).ConfigureAwait(false);
        await two.Task.WaitAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = received.Select(static value => (value.Sequence, value.Topic)).Should().Equal((7L, PlayerEvent.LifecycleTopic), (8L, PlayerEvent.LogTopic));
        _ = received[0].GetString("state").Should().Be("session_started");
        _ = received[1].GetString("missing").Should().BeEmpty();

        var reading = client.ReadEventsAsync(since: 0, [PlayerEvent.LifecycleTopic], this.TestContext.CancellationToken);
        var request = await server.ReadAsync().ConfigureAwait(false);
        await server.ReplyAsync(request, new JsonObject
        {
            ["events"] = new JsonArray(new JsonObject { ["seq"] = 3, ["frame"] = 1, ["topic"] = PlayerEvent.LifecycleTopic, ["data"] = new JsonObject { ["state"] = "scene_loaded" } }),
            ["next_seq"] = 9,
            ["dropped"] = 0,
        }).ConfigureAwait(false);

        var held = await reading.ConfigureAwait(false);
        _ = request["method"]!.GetValue<string>().Should().Be("rpc.events");
        _ = held.Should().ContainSingle().Which.GetString("state").Should().Be("scene_loaded");
    }

    [TestMethod]
    public async Task ServerClosing_FailsPendingRequestsAndRaisesDisconnectedOnce()
    {
        var server = await FakeControlServer.StartAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        await using var client = await this.ConnectAsync(server).ConfigureAwait(false);
        var disconnects = 0;
        var disconnected = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        client.Disconnected += (_, _) =>
        {
            _ = Interlocked.Increment(ref disconnects);
            disconnected.TrySetResult();
        };
        var pending = client.RequestAsync("ngin.session.end", parameters: null, this.TestContext.CancellationToken);
        _ = await server.ReadAsync().ConfigureAwait(false);

        await server.DisposeAsync().ConfigureAwait(false);
        await disconnected.Task.WaitAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        var wait = async () => await pending.ConfigureAwait(false);
        _ = (await wait.Should().ThrowAsync<PlayerCommandException>().ConfigureAwait(false)).Which.Code
            .Should().Be(PlayerCommandException.ConnectionClosedCode);
        _ = client.IsClosed.Should().BeTrue();
        _ = disconnects.Should().Be(1);
    }

    private async Task<PlayerControlClient> ConnectAsync(FakeControlServer server)
    {
        var connecting = PlayerControlClient.ConnectAsync(new PlayerControlEndpoint(server.Port, Token), "Editor", owner: false, logger: null, this.TestContext.CancellationToken);
        await server.ReplyAsync(await server.ReadAsync().ConfigureAwait(false), new JsonObject { ["owner"] = false }).ConfigureAwait(false);
        return await connecting.ConfigureAwait(false);
    }

    /// <summary>A loopback server that accepts one client and lets a test read and write its lines.</summary>
    private sealed class FakeControlServer : IAsyncDisposable
    {
        private readonly TcpListener listener;
        private readonly Task<TcpClient> accepting;
        private StreamReader? reader;
        private StreamWriter? writer;
        private TcpClient? connection;

        private FakeControlServer(TcpListener listener, CancellationToken cancellationToken)
        {
            this.listener = listener;
            this.accepting = listener.AcceptTcpClientAsync(cancellationToken).AsTask();
        }

        public int Port => ((IPEndPoint)this.listener.LocalEndpoint).Port;

        public static Task<FakeControlServer> StartAsync(CancellationToken cancellationToken)
        {
            var listener = new TcpListener(IPAddress.Loopback, 0);
            listener.Start();
            return Task.FromResult(new FakeControlServer(listener, cancellationToken));
        }

        public async Task<JsonObject> ReadAsync()
        {
            await this.EnsureConnectedAsync().ConfigureAwait(false);
            var line = await this.reader!.ReadLineAsync().ConfigureAwait(false) ?? throw new InvalidOperationException("The client closed the connection.");
            return JsonNode.Parse(line)!.AsObject();
        }

        public Task ReplyAsync(JsonObject request, JsonObject result)
            => this.SendAsync(new JsonObject { ["jsonrpc"] = "2.0", ["id"] = request["id"]!.DeepClone(), ["result"] = result });

        public Task SendEventAsync(long sequence, string topic, JsonObject data)
            => this.SendAsync(new JsonObject
            {
                ["jsonrpc"] = "2.0",
                ["method"] = "rpc.event",
                ["params"] = new JsonObject { ["seq"] = sequence, ["frame"] = 1, ["topic"] = topic, ["data"] = data },
            });

        public async Task SendAsync(JsonObject message)
        {
            await this.EnsureConnectedAsync().ConfigureAwait(false);
            await this.writer!.WriteAsync(message.ToJsonString() + "\n").ConfigureAwait(false);
            await this.writer.FlushAsync().ConfigureAwait(false);
        }

        public async ValueTask DisposeAsync()
        {
            this.listener.Stop();
            this.reader?.Dispose();
            if (this.writer is not null)
            {
                await this.writer.DisposeAsync().ConfigureAwait(false);
            }

            if (this.connection is { } accepted)
            {
                accepted.Dispose();
            }
            else
            {
                try
                {
                    (await this.accepting.ConfigureAwait(false)).Dispose();
                }
                catch (Exception exception) when (exception is SocketException or OperationCanceledException or ObjectDisposedException)
                {
                    // No client connected.
                }
            }
        }

        private async Task EnsureConnectedAsync()
        {
            if (this.connection is not null)
            {
                return;
            }

            this.connection = await this.accepting.ConfigureAwait(false);
            var stream = this.connection.GetStream();
            this.reader = new StreamReader(stream, Encoding.UTF8, leaveOpen: true);
            this.writer = new StreamWriter(stream, new UTF8Encoding(encoderShouldEmitUTF8Identifier: false), leaveOpen: true);
        }
    }
}
