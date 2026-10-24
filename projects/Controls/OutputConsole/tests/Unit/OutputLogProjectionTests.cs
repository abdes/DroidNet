// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Controls.OutputConsole.Model;
using Serilog.Events;

namespace Controls.OutputConsole.Tests;

[TestClass]
public class OutputLogProjectionTests
{
    private static readonly HashSet<string> NoneHidden = [with(StringComparer.Ordinal)];

    [TestMethod]
    [DataRow(LogEventLevel.Debug, false)]
    [DataRow(LogEventLevel.Information, true)]
    [DataRow(LogEventLevel.Error, true)]
    public void Passes_FiltersByLevel(LogEventLevel level, bool expected)
    {
        var projection = new OutputLogProjection(Filter(levels: LevelMask.Information | LevelMask.Error));

        _ = projection.Passes(Entry("m", level: level)).Should().Be(expected);
    }

    [TestMethod]
    public void Passes_HidesHiddenSubsystemsAndOther()
    {
        var projection = new OutputLogProjection(Filter(hidden: new HashSet<string>(StringComparer.Ordinal) { "Engine", OutputLogFilter.OtherSubsystem }));

        _ = projection.Passes(Entry("m", subsystem: "Engine")).Should().BeFalse();
        _ = projection.Passes(Entry("m", subsystem: null)).Should().BeFalse();
        _ = projection.Passes(Entry("m", subsystem: "Cooker")).Should().BeTrue();
    }

    [TestMethod]
    [DataRow("corridor")]
    [DataRow("COOK")]
    [DataRow("Content.Import")]
    [DataRow("assets")]
    [DataRow("InvalidData")]
    public void Passes_InFilterMode_MatchesEachFieldIgnoringCase(string text)
    {
        var entry = new OutputLogEntry
        {
            Message = "Importing Corridor_A",
            Source = "Content.Import",
            Channel = "assets",
            Exception = new InvalidDataException("bad"),
            Subsystem = "Cooker",
        };
        var projection = new OutputLogProjection(Filter(text: text));

        _ = projection.Passes(entry).Should().BeTrue();
    }

    [TestMethod]
    public void Passes_InFindMode_IgnoresTheText()
    {
        var projection = new OutputLogProjection(Filter(text: "absent", mode: OutputSearchMode.Find));

        _ = projection.Passes(Entry("present")).Should().BeTrue();
    }

    [TestMethod]
    public void Rebuild_FoldsOnlyConsecutiveIdenticalEntriesWithoutExceptions()
    {
        var projection = new OutputLogProjection(Filter(fold: true));
        var withException = new OutputLogEntry { Message = "same", Exception = new InvalidOperationException() };

        var rows = projection.Rebuild(Sourced(
            Entry("same"),
            Entry("same"),
            Entry("same", subsystem: "Engine"),
            Entry("same", subsystem: "Engine"),
            withException,
            Entry("same")));

        _ = rows.Select(r => r.Count).Should().Equal(2, 2, 1, 1);
        _ = rows[0].FirstOrdinal.Should().Be(1);
        _ = rows[0].LastOrdinal.Should().Be(2);
    }

    [TestMethod]
    public void Rebuild_DoesNotFoldEntriesOfDifferentChannels()
    {
        var projection = new OutputLogProjection(Filter(fold: true));
        var assets = new OutputLogEntry { Message = "same", Channel = "assets" };
        var render = new OutputLogEntry { Message = "same", Channel = "render" };

        var rows = projection.Rebuild(Sourced(assets, render));

        _ = rows.Should().HaveCount(2);
    }

    [TestMethod]
    public void Rebuild_WithoutFolding_KeepsEveryEntryInItsOwnRow()
    {
        var projection = new OutputLogProjection(Filter(fold: false));

        var rows = projection.Rebuild(Sourced(Entry("same"), Entry("same")));

        _ = rows.Should().HaveCount(2);
    }

    [TestMethod]
    public void Rebuild_FoldsAcrossFilteredOutEntries()
    {
        var projection = new OutputLogProjection(Filter(levels: LevelMask.Warning, fold: true));

        var rows = projection.Rebuild(Sourced(
            Entry("w", level: LogEventLevel.Warning),
            Entry("hidden"),
            Entry("w", level: LogEventLevel.Warning)));

        _ = rows.Should().ContainSingle().Which.Count.Should().Be(2);
    }

    [TestMethod]
    public void Append_AddsNewRowsAndReportsThem()
    {
        var projection = new OutputLogProjection(Filter());
        _ = projection.Rebuild(Sourced(Entry("a")));

        var delta = projection.Append(Sourced(2, Entry("b"), Entry("c")), firstRetainedOrdinal: 1);

        _ = delta.RemovedFromHead.Should().Be(0);
        _ = delta.HeadChanged.Should().BeFalse();
        _ = delta.TailChanged.Should().BeFalse();
        _ = delta.Added.Select(r => r.First.Message).Should().Equal("b", "c");
        _ = projection.Rows.Should().HaveCount(3);
    }

    [TestMethod]
    public void Append_FoldingIntoTheExistingTail_ReportsTailChanged()
    {
        var projection = new OutputLogProjection(Filter(fold: true));
        _ = projection.Rebuild(Sourced(Entry("repeat")));

        var delta = projection.Append(Sourced(2, Entry("repeat"), Entry("repeat")), firstRetainedOrdinal: 1);

        _ = delta.TailChanged.Should().BeTrue();
        _ = delta.Added.Should().BeEmpty();
        _ = projection.Rows.Should().ContainSingle().Which.Count.Should().Be(3);
    }

    [TestMethod]
    public void Append_FoldingIntoARowAddedInTheSameAppend_IsNotATailChange()
    {
        var projection = new OutputLogProjection(Filter(fold: true));
        _ = projection.Rebuild(Sourced(Entry("old")));

        var delta = projection.Append(Sourced(2, Entry("new"), Entry("new")), firstRetainedOrdinal: 1);

        _ = delta.TailChanged.Should().BeFalse();
        _ = delta.Added.Should().ContainSingle().Which.Count.Should().Be(2);
    }

    [TestMethod]
    public void Append_RemovesEvictedRowsFromTheHead()
    {
        var projection = new OutputLogProjection(Filter());
        _ = projection.Rebuild(Sourced(Entry("1"), Entry("2"), Entry("3")));

        var delta = projection.Append(Sourced(4, Entry("4")), firstRetainedOrdinal: 3);

        _ = delta.RemovedFromHead.Should().Be(2);
        _ = delta.HeadChanged.Should().BeFalse();
        _ = projection.Rows.Select(r => r.First.Message).Should().Equal("3", "4");
    }

    [TestMethod]
    public void Append_EvictionInsideAFoldedHeadRow_TrimsItAndReportsHeadChanged()
    {
        var projection = new OutputLogProjection(Filter(fold: true));
        _ = projection.Rebuild(Sourced(Entry("x"), Entry("x"), Entry("x"), Entry("y")));

        var delta = projection.Append([], firstRetainedOrdinal: 3);

        _ = delta.RemovedFromHead.Should().Be(0);
        _ = delta.HeadChanged.Should().BeTrue();
        var head = projection.Rows[0];
        _ = head.Count.Should().Be(1);
        _ = head.FirstOrdinal.Should().Be(3);
    }

    [TestMethod]
    public void Append_EvictingEverything_EmptiesTheRows()
    {
        var projection = new OutputLogProjection(Filter());
        _ = projection.Rebuild(Sourced(Entry("1"), Entry("2")));

        var delta = projection.Append([], firstRetainedOrdinal: 10);

        _ = delta.RemovedFromHead.Should().Be(2);
        _ = projection.Rows.Should().BeEmpty();
    }

    [TestMethod]
    public void Entries_OfAFoldedRow_AreAllEntriesOldestFirst()
    {
        var projection = new OutputLogProjection(Filter(fold: true));
        var first = Entry("x");
        var second = Entry("x");

        var row = projection.Rebuild(Sourced(first, second)).Single();

        _ = row.Entries.Should().Equal(first, second);
        _ = row.First.Should().BeSameAs(first);
        _ = row.Last.Should().BeSameAs(second);
    }

    private static OutputLogFilter Filter(
        LevelMask levels = LevelMask.All,
        IReadOnlySet<string>? hidden = null,
        string text = "",
        OutputSearchMode mode = OutputSearchMode.Filter,
        bool fold = false)
        => new(levels, hidden ?? NoneHidden, text, mode, fold);

    private static OutputLogEntry Entry(string message, LogEventLevel level = LogEventLevel.Information, string? subsystem = "Editor")
    {
        var entry = new OutputLogEntry
        {
            Message = message,
            Level = level,
            Source = "Test",
            Subsystem = subsystem,
        };
        return entry;
    }

    private static List<SourcedEntry> Sourced(params OutputLogEntry[] entries) => Sourced(1, entries);

    private static List<SourcedEntry> Sourced(long firstOrdinal, params OutputLogEntry[] entries)
        => [.. entries.Select((entry, index) => new SourcedEntry(firstOrdinal + index, entry))];
}
