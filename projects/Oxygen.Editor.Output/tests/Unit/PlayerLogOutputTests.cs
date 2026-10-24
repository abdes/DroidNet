// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Controls.OutputConsole.Model;
using Microsoft.Extensions.Logging;
using Moq;
using Oxygen.Editor.LivePreview.Play;
using Serilog.Events;

namespace Oxygen.Editor.Output.Unit.Tests;

/// <summary>The Output panel shows the player's output as the player's entries; a new session is marked in it.</summary>
[TestClass]
public sealed class PlayerLogOutputTests
{
    private static readonly DateTimeOffset Received = new(2026, 10, 21, 12, 0, 0, TimeSpan.Zero);

    private static readonly SubsystemMap TestSubsystems = new([new(OutputModule.PlayerSource, "Player")]);

    /// <summary>A line of the player's output is an entry of the player's source, with its channel and time.</summary>
    [TestMethod]
    public void LineIsAnEntryOfThePlayer()
    {
        var log = new Mock<IPlayerLog>();
        var buffer = new OutputLogBuffer(TestSubsystems);
        using var output = new PlayerLogOutput(log.Object, buffer);

        log.Raise(player => player.LogReceived += null, new PlayerLogEventArgs(Received, LogLevel.Warning, "Scripts", "slow frame"));

        var entry = buffer.Should().ContainSingle().Subject;
        _ = entry.Source.Should().Be(OutputModule.PlayerSource);
        _ = entry.Channel.Should().Be("Scripts");
        _ = entry.Message.Should().Be("slow frame");
        _ = entry.Timestamp.Should().Be(Received);
        _ = entry.Level.Should().Be(LogEventLevel.Warning);
    }

    /// <summary>Each severity of the player shows as the matching level of the panel.</summary>
    /// <param name="level">The player's severity.</param>
    /// <param name="expected">The level the panel shows.</param>
    [TestMethod]
    [DataRow(LogLevel.Critical, LogEventLevel.Fatal)]
    [DataRow(LogLevel.Error, LogEventLevel.Error)]
    [DataRow(LogLevel.Warning, LogEventLevel.Warning)]
    [DataRow(LogLevel.Information, LogEventLevel.Information)]
    [DataRow(LogLevel.Debug, LogEventLevel.Debug)]
    [DataRow(LogLevel.Trace, LogEventLevel.Verbose)]
    public void SeverityShowsAsTheMatchingLevel(LogLevel level, LogEventLevel expected)
    {
        var log = new Mock<IPlayerLog>();
        var buffer = new OutputLogBuffer(TestSubsystems);
        using var output = new PlayerLogOutput(log.Object, buffer);

        log.Raise(player => player.LogReceived += null, new PlayerLogEventArgs(Received, level, channel: null, "line"));

        _ = buffer.Should().ContainSingle().Which.Level.Should().Be(expected);
    }

    /// <summary>A line of the player's output belongs to the Player subsystem.</summary>
    [TestMethod]
    public void LineBelongsToThePlayerSubsystem()
    {
        var log = new Mock<IPlayerLog>();
        var buffer = new OutputLogBuffer(TestSubsystems);
        using var output = new PlayerLogOutput(log.Object, buffer);

        log.Raise(player => player.LogReceived += null, new PlayerLogEventArgs(Received, LogLevel.Information, channel: null, "line"));

        _ = buffer.Should().ContainSingle().Which.Subsystem.Should().Be("Player");
    }

    /// <summary>A new session's start is marked in the output, and what the panel showed stays.</summary>
    [TestMethod]
    public void SessionStartMarksTheOutput()
    {
        var log = new Mock<IPlayerLog>();
        var buffer = new OutputLogBuffer(TestSubsystems);
        buffer.Append(new OutputLogEntry { Message = "editor line" });
        using var output = new PlayerLogOutput(log.Object, buffer);

        log.Raise(player => player.LogReceived += null, PlayerLogEventArgs.SessionStarted());

        _ = buffer.Should().HaveCount(2);
        _ = buffer.First().Message.Should().Be("editor line");
        var marker = buffer.Last();
        _ = marker.Level.Should().Be(LogEventLevel.Information);
        _ = marker.Source.Should().Be(OutputModule.PlayerSource);
        _ = marker.Message.Should().Be(PlayerLogOutput.SessionStartedMessage);
    }

    /// <summary>Once disposed with its window, the output no longer follows the player.</summary>
    [TestMethod]
    public void DisposedOutputNoLongerFollowsThePlayer()
    {
        var log = new Mock<IPlayerLog>();
        var buffer = new OutputLogBuffer(TestSubsystems);
        var output = new PlayerLogOutput(log.Object, buffer);

        output.Dispose();
        log.Raise(player => player.LogReceived += null, new PlayerLogEventArgs(Received, LogLevel.Error, channel: null, "after"));

        _ = buffer.Should().BeEmpty();
    }
}
