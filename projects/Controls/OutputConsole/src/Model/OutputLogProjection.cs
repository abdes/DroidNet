// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Serilog.Events;

namespace DroidNet.Controls.OutputConsole.Model;

/// <summary>
///     Turns the console's source entries into its rows: filters them, folds consecutive identical ones and
///     drops evicted ones. It holds no UI state and runs on the UI thread.
/// </summary>
/// <param name="filter">The initial filter.</param>
internal sealed class OutputLogProjection(OutputLogFilter filter)
{
    private List<OutputLogRow> rows = [];

    /// <summary>
    ///     Gets or sets the filter. Changing it requires a <see cref="Rebuild" />.
    /// </summary>
    public OutputLogFilter Filter { get; set; } = filter;

    /// <summary>
    ///     Gets the current rows, oldest first.
    /// </summary>
    public IReadOnlyList<OutputLogRow> Rows => this.rows;

    /// <summary>
    ///     Gets a value indicating whether an entry contains a search text, ignoring case, in its message,
    ///     subsystem, source, channel or exception text.
    /// </summary>
    /// <param name="entry">The entry.</param>
    /// <param name="text">The trimmed, non-empty search text.</param>
    /// <returns><see langword="true" /> when the entry matches.</returns>
    public static bool Matches(OutputLogEntry entry, string text)
        => entry.Message.Contains(text, StringComparison.OrdinalIgnoreCase)
            || entry.Subsystem?.Contains(text, StringComparison.OrdinalIgnoreCase) == true
            || entry.Source?.Contains(text, StringComparison.OrdinalIgnoreCase) == true
            || entry.Channel?.Contains(text, StringComparison.OrdinalIgnoreCase) == true
            || entry.ExceptionText?.Contains(text, StringComparison.OrdinalIgnoreCase) == true;

    /// <summary>
    ///     Replaces every row with the projection of the given entries.
    /// </summary>
    /// <param name="entries">The source entries, oldest first.</param>
    /// <returns>The new rows.</returns>
    public IReadOnlyList<OutputLogRow> Rebuild(IEnumerable<SourcedEntry> entries)
    {
        ArgumentNullException.ThrowIfNull(entries);
        this.rows = [];
        foreach (var entry in entries)
        {
            _ = this.Add(entry, existingTail: null);
        }

        return this.rows;
    }

    /// <summary>
    ///     Drops what the source evicted and projects new entries.
    /// </summary>
    /// <param name="entries">The new source entries, oldest first.</param>
    /// <param name="firstRetainedOrdinal">The ordinal of the oldest entry the source still holds.</param>
    /// <returns>The changes the view must apply to its row collection, in the order of the members.</returns>
    public ProjectionDelta Append(IReadOnlyList<SourcedEntry> entries, long firstRetainedOrdinal)
    {
        ArgumentNullException.ThrowIfNull(entries);

        var removed = 0;
        while (removed < this.rows.Count && this.rows[removed].LastOrdinal < firstRetainedOrdinal)
        {
            removed++;
        }

        this.rows.RemoveRange(0, removed);
        var headChanged = this.rows.Count > 0 && this.rows[0].TrimBefore(firstRetainedOrdinal);

        var existingTail = this.rows.Count > 0 ? this.rows[^1] : null;
        var firstAdded = this.rows.Count;
        var tailChanged = false;
        foreach (var entry in entries)
        {
            tailChanged |= this.Add(entry, existingTail);
        }

        return new ProjectionDelta(removed, headChanged, tailChanged, this.rows.GetRange(firstAdded, this.rows.Count - firstAdded));
    }

    /// <summary>
    ///     Gets a value indicating whether an entry passes the level, subsystem and, in filter mode, text filters.
    /// </summary>
    /// <param name="entry">The entry.</param>
    /// <returns><see langword="true" /> when the entry is shown.</returns>
    public bool Passes(OutputLogEntry entry)
    {
        if ((this.Filter.Levels & ToMask(entry.Level)) == LevelMask.None)
        {
            return false;
        }

        if (this.Filter.HiddenSubsystems.Contains(entry.Subsystem ?? OutputLogFilter.OtherSubsystem))
        {
            return false;
        }

        return this.Filter.Mode != OutputSearchMode.Filter
            || this.Filter.Text.Length == 0
            || Matches(entry, this.Filter.Text);
    }

    private static LevelMask ToMask(LogEventLevel level) => level switch
    {
        LogEventLevel.Verbose => LevelMask.Verbose,
        LogEventLevel.Debug => LevelMask.Debug,
        LogEventLevel.Information => LevelMask.Information,
        LogEventLevel.Warning => LevelMask.Warning,
        LogEventLevel.Error => LevelMask.Error,
        LogEventLevel.Fatal => LevelMask.Fatal,
        _ => LevelMask.All,
    };

    private static bool Folds(OutputLogEntry previous, OutputLogEntry next)
        => previous.Exception is null
            && next.Exception is null
            && previous.Level == next.Level
            && string.Equals(previous.Subsystem, next.Subsystem, StringComparison.Ordinal)
            && string.Equals(previous.Source, next.Source, StringComparison.Ordinal)
            && string.Equals(previous.Channel, next.Channel, StringComparison.Ordinal)
            && string.Equals(previous.Message, next.Message, StringComparison.Ordinal);

    /// <summary>
    ///     Adds an entry that passes the filter, folding it into the last row when it can.
    /// </summary>
    /// <param name="entry">The entry.</param>
    /// <param name="existingTail">The last row before the current append, if any.</param>
    /// <returns><see langword="true" /> when the entry was folded into <paramref name="existingTail" />.</returns>
    private bool Add(SourcedEntry entry, OutputLogRow? existingTail)
    {
        if (!this.Passes(entry.Entry))
        {
            return false;
        }

        if (this.Filter.Fold && this.rows.Count > 0 && Folds(this.rows[^1].Last, entry.Entry))
        {
            var tail = this.rows[^1];
            tail.Fold(entry);
            return ReferenceEquals(tail, existingTail);
        }

        this.rows.Add(new OutputLogRow(entry));
        return false;
    }
}
