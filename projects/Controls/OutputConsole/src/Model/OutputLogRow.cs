// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.OutputConsole.Model;

/// <summary>
///     One line of the console: a single entry, or consecutive identical entries folded together.
/// </summary>
/// <remarks>
///     Rows are created and changed only by <see cref="OutputLogProjection" />: an entry is folded into the last
///     row, or the oldest entries of the first row are evicted.
/// </remarks>
/// <param name="entry">The row's entry.</param>
internal sealed class OutputLogRow(SourcedEntry entry)
{
    private SourcedEntry first = entry;
    private List<SourcedEntry>? all;

    /// <summary>
    ///     Gets the oldest entry of the row.
    /// </summary>
    public OutputLogEntry First => this.first.Entry;

    /// <summary>
    ///     Gets the newest entry of the row; <see cref="First" /> when the row is not folded.
    /// </summary>
    public OutputLogEntry Last => this.all is null ? this.first.Entry : this.all[^1].Entry;

    /// <summary>
    ///     Gets the number of entries in the row.
    /// </summary>
    public int Count => this.all?.Count ?? 1;

    /// <summary>
    ///     Gets the ordinal of <see cref="First" />.
    /// </summary>
    public long FirstOrdinal => this.first.Ordinal;

    /// <summary>
    ///     Gets the ordinal of <see cref="Last" />.
    /// </summary>
    public long LastOrdinal => this.all is null ? this.first.Ordinal : this.all[^1].Ordinal;

    /// <summary>
    ///     Gets every entry of the row, oldest first.
    /// </summary>
    public IReadOnlyList<OutputLogEntry> Entries
        => this.all is null ? [this.first.Entry] : [.. this.all.Select(static sourced => sourced.Entry)];

    /// <summary>
    ///     Gets a value indicating whether any entry of the row contains the text, as the search matches it.
    /// </summary>
    /// <param name="text">The trimmed search text.</param>
    /// <returns><see langword="true" /> when the row's entry matches.</returns>
    /// <remarks>Folded entries share the matched fields except the exception, and folded entries have none.</remarks>
    public bool Matches(string text) => OutputLogProjection.Matches(this.First, text);

    /// <summary>
    ///     Folds a newer identical entry into the row.
    /// </summary>
    /// <param name="entry">The entry.</param>
    internal void Fold(SourcedEntry entry)
    {
        this.all ??= [this.first];
        this.all.Add(entry);
    }

    /// <summary>
    ///     Removes the entries older than an ordinal.
    /// </summary>
    /// <param name="firstRetainedOrdinal">The ordinal of the oldest entry to keep.</param>
    /// <returns><see langword="true" /> when the row lost entries.</returns>
    /// <remarks>The caller ensures <see cref="LastOrdinal" /> is at least <paramref name="firstRetainedOrdinal" />.</remarks>
    internal bool TrimBefore(long firstRetainedOrdinal)
    {
        if (this.all is null || this.first.Ordinal >= firstRetainedOrdinal)
        {
            return false;
        }

        var keepFrom = this.all.FindIndex(sourced => sourced.Ordinal >= firstRetainedOrdinal);
        this.all.RemoveRange(0, keepFrom);
        this.first = this.all[0];
        return true;
    }
}
