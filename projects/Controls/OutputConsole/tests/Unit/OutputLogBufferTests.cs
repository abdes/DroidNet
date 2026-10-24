// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using AwesomeAssertions;
using DroidNet.Controls.OutputConsole.Model;
using Serilog.Events;

namespace Controls.OutputConsole.Tests;

[TestClass]
public class OutputLogBufferTests
{
    private static readonly SubsystemMap Subsystems = new(
    [
        new("Oxygen.Engine", "Engine"),
        new("Player", "Player"),
    ]);

    [TestMethod]
    public void Constructor_WithoutMap_Throws()
    {
        static void Act() => _ = new OutputLogBuffer(null!);

        _ = ((Action)Act).Should().Throw<ArgumentNullException>();
    }

    [TestMethod]
    public void Constructor_WithInvalidCapacity_Throws()
    {
        static void Act() => _ = new OutputLogBuffer(Subsystems, 0);

        _ = ((Action)Act).Should().Throw<ArgumentOutOfRangeException>();
    }

    [TestMethod]
    public void Append_AssignsIncreasingSequencesAndSubsystems()
    {
        var buffer = new OutputLogBuffer(Subsystems, 10);
        var engine = Entry("first", "Oxygen.Engine");
        var unknown = Entry("second", "ThirdParty.Lib");

        buffer.Append(engine);
        buffer.Append(unknown);

        _ = engine.Sequence.Should().Be(1);
        _ = unknown.Sequence.Should().Be(2);
        _ = engine.Subsystem.Should().Be("Engine");
        _ = unknown.Subsystem.Should().BeNull();
        _ = buffer.LastSequence.Should().Be(2);
    }

    [TestMethod]
    public void Append_SameEntryTwice_Throws()
    {
        var buffer = new OutputLogBuffer(Subsystems, 10);
        var entry = Entry("once");
        buffer.Append(entry);

        var act = () => buffer.Append(entry);

        _ = act.Should().Throw<ArgumentException>();
    }

    [TestMethod]
    public void Append_PastCapacity_OverwritesOldestAndCountsEvictions()
    {
        var buffer = new OutputLogBuffer(Subsystems, 3);

        for (var i = 1; i <= 5; i++)
        {
            buffer.Append(Entry(string.Create(CultureInfo.InvariantCulture, $"m{i}")));
        }

        _ = buffer.Snapshot().Select(e => e.Message).Should().Equal("m3", "m4", "m5");
        var read = buffer.ReadSince(0);
        _ = read.FirstRetainedSequence.Should().Be(3);
        _ = buffer.GetStatistics().EvictedCount.Should().Be(2);
    }

    [TestMethod]
    public void ReadSince_ReturnsOnlyNewerRetainedEntries()
    {
        var buffer = new OutputLogBuffer(Subsystems, 4);
        for (var i = 1; i <= 6; i++)
        {
            buffer.Append(Entry(string.Create(CultureInfo.InvariantCulture, $"m{i}")));
        }

        var fromEvicted = buffer.ReadSince(1);
        var fromRetained = buffer.ReadSince(4);
        var upToDate = buffer.ReadSince(6);

        _ = fromEvicted.Entries.Select(e => e.Sequence).Should().Equal(3, 4, 5, 6);
        _ = fromRetained.Entries.Select(e => e.Sequence).Should().Equal(5, 6);
        _ = upToDate.Entries.Should().BeEmpty();
    }

    [TestMethod]
    public void ReadSince_WhenEmpty_ReportsNextSequenceAsFirstRetained()
    {
        var buffer = new OutputLogBuffer(Subsystems, 4);
        buffer.Append(Entry("m1"));
        buffer.Clear();

        var read = buffer.ReadSince(0);

        _ = read.Entries.Should().BeEmpty();
        _ = read.FirstRetainedSequence.Should().Be(2);
    }

    [TestMethod]
    public void Clear_KeepsSequencesIncreasingAndAdvancesGeneration()
    {
        var buffer = new OutputLogBuffer(Subsystems, 4);
        buffer.Append(Entry("before"));
        var generation = buffer.ReadSince(0).ClearGeneration;

        buffer.Clear();
        var after = Entry("after");
        buffer.Append(after);

        _ = after.Sequence.Should().Be(2);
        _ = buffer.ReadSince(0).ClearGeneration.Should().Be(generation + 1);
        _ = buffer.GetStatistics().EvictedCount.Should().Be(0);
    }

    [TestMethod]
    public void EntriesAvailable_IsRaisedOnceUntilRead()
    {
        var buffer = new OutputLogBuffer(Subsystems, 10);
        var raised = 0;
        buffer.EntriesAvailable += (_, _) => raised++;

        buffer.Append(Entry("a"));
        buffer.Append(Entry("b"));
        _ = buffer.ReadSince(0);
        buffer.Append(Entry("c"));

        _ = raised.Should().Be(2);
    }

    [TestMethod]
    public void EntriesAvailable_IsRaisedOnClear()
    {
        var buffer = new OutputLogBuffer(Subsystems, 10);
        _ = buffer.ReadSince(0);
        var raised = 0;
        buffer.EntriesAvailable += (_, _) => raised++;

        buffer.Clear();

        _ = raised.Should().Be(1);
    }

    [TestMethod]
    public void GetStatistics_CountsRetainedEntriesByLevelAndSubsystemAfterWraparound()
    {
        var buffer = new OutputLogBuffer(Subsystems, 3);
        buffer.Append(Entry("evicted", "Oxygen.Engine", LogEventLevel.Error));
        buffer.Append(Entry("w", "Oxygen.Engine", LogEventLevel.Warning));
        buffer.Append(Entry("p", "Player"));
        buffer.Append(Entry("other", source: null));

        var statistics = buffer.GetStatistics();

        _ = statistics.Count.Should().Be(3);
        _ = statistics.CountOf(LogEventLevel.Error).Should().Be(0);
        _ = statistics.CountOf(LogEventLevel.Warning).Should().Be(1);
        _ = statistics.CountOf(LogEventLevel.Information).Should().Be(2);
        _ = statistics.Subsystems.Should().Equal(
            new KeyValuePair<string?, int>("Engine", 1),
            new KeyValuePair<string?, int>("Player", 1),
            new KeyValuePair<string?, int>(key: null, 1));
    }

    [TestMethod]
    public void Append_FromManyThreads_LosesNoSequence()
    {
        const int threads = 8;
        const int perThread = 2_000;
        var buffer = new OutputLogBuffer(Subsystems, threads * perThread);

        _ = Parallel.For(0, threads, _ =>
        {
            for (var i = 0; i < perThread; i++)
            {
                buffer.Append(Entry("concurrent"));
            }
        });

        var sequences = buffer.Snapshot().Select(e => e.Sequence).ToList();
        _ = sequences.Should().Equal(Enumerable.Range(1, threads * perThread).Select(i => (long)i));
    }

    private static OutputLogEntry Entry(string message, string? source = "Player", LogEventLevel level = LogEventLevel.Information)
        => new() { Message = message, Source = source, Level = level };
}
