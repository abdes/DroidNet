// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Nodes;
using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.Runtime.Player;
using Oxygen.Editor.World.Play;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Play;

/// <summary>A running player follows the play session preferences through its control commands.</summary>
[TestClass]
[TestCategory("Play")]
public sealed class PlayerSettingsSyncTests
{
    private readonly PlaySessionSettingsService settings = new(settings: null);
    private readonly List<string> sent = [];
    private readonly List<string> refusals = [];
    private Func<string, JsonObject, Task> reply = static (_, _) => Task.CompletedTask;

    [TestMethod]
    public async Task SendAll_SetsTheDisplayModeWindowSizeVSyncAndFrameRate()
    {
        this.settings.FrameRateLimited = true;
        using var sut = this.CreateSync();

        sut.SendAll();
        await sut.Sent.ConfigureAwait(false);

        _ = this.sent.Should().Equal(
            """host.window.mode {"mode":"windowed"}""",
            """host.window.size {"width":1280,"height":720}""",
            """cvar.set {"name":"gfx.vsync","value":true}""",
            """cvar.set {"name":"ngin.target_fps","value":60}""");
    }

    [TestMethod]
    public async Task ABorderlessWindowGetsItsSizeOnlyWhenItIsFramedAgain()
    {
        using var sut = this.CreateSync();

        this.settings.WindowMode = PlayerWindowMode.Borderless;
        this.settings.Width = 1600;
        await sut.Sent.ConfigureAwait(false);
        var whileBorderless = this.sent.ToList();
        this.settings.WindowMode = PlayerWindowMode.Windowed;
        await sut.Sent.ConfigureAwait(false);

        _ = whileBorderless.Should().Equal("""host.window.mode {"mode":"borderless"}""");
        _ = this.sent.Skip(1).Should().Equal(
            """host.window.mode {"mode":"windowed"}""",
            """host.window.size {"width":1600,"height":720}""");
    }

    [TestMethod]
    public async Task Changes_MadeWhileARequestIsInFlightAreSentOnceWithTheirLatestValues()
    {
        var held = new TaskCompletionSource();
        this.reply = (_, _) => this.sent.Count == 1 ? held.Task : Task.CompletedTask;
        using var sut = this.CreateSync();

        this.settings.Width = 1000;
        this.settings.Width = 1100;
        this.settings.Height = 600;
        this.settings.FrameRateLimited = true;
        this.settings.FrameRateLimited = false;
        held.SetResult();
        await sut.Sent.ConfigureAwait(false);

        _ = this.sent.Should().Equal(
            """host.window.size {"width":1000,"height":720}""",
            """host.window.size {"width":1100,"height":600}""",
            """cvar.set {"name":"ngin.target_fps","value":0}""");
    }

    [TestMethod]
    public async Task ARefusedSettingIsReportedAndTheOthersStillApply()
    {
        this.reply = (method, _) => method == PlayerSettingsSync.WindowSizeMethod
            ? Task.FromException(new PlayerCommandException(-32000, "the window is full screen", data: null))
            : Task.CompletedTask;
        using var sut = this.CreateSync();

        sut.SendAll();
        await sut.Sent.ConfigureAwait(false);

        _ = this.refusals.Should().Equal("The player did not apply the window size: the window is full screen");
        _ = this.sent.Should().HaveCount(4);
    }

    [TestMethod]
    public async Task AClosedConnectionOrDisposalEndsTheSync()
    {
        this.reply = static (_, _) => Task.FromException(new PlayerCommandException(PlayerCommandException.ConnectionClosedCode, "closed", data: null));
        var sut = this.CreateSync();

        sut.SendAll();
        await sut.Sent.ConfigureAwait(false);
        sut.Dispose();
        this.settings.VSync = false;

        _ = this.sent.Should().ContainSingle("nothing is sent after the connection closed");
        _ = this.refusals.Should().BeEmpty();
    }

    private PlayerSettingsSync CreateSync()
        => new(
            this.settings,
            (method, parameters, _) =>
            {
                this.sent.Add($"{method} {parameters.ToJsonString()}");
                return this.reply(method, parameters);
            },
            this.refusals.Add,
            NullLogger.Instance);
}
