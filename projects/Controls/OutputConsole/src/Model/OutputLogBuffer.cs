// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections;
using Serilog.Events;

namespace DroidNet.Controls.OutputConsole.Model;

/// <summary>
///     A thread-safe, fixed-capacity ring of log entries that producers append to from any thread and that
///     views read from.
/// </summary>
/// <remarks>
///     <para>
///         Appending is O(1): it takes a short lock, writes the entry into the ring, overwriting the oldest one
///         when the ring is full, and gives it the next <see cref="OutputLogEntry.Sequence" /> and its
///         <see cref="OutputLogEntry.Subsystem" />. Producers never run view code: the buffer raises
///         <see cref="EntriesAvailable" /> at most once until the next <see cref="ReadSince" />, and views pull the
///         new entries on their own thread.
///     </para>
///     <para>
///         Entries leave the buffer only when newer ones overwrite them, which <see cref="OutputLogStatistics.EvictedCount" />
///         counts, or when <see cref="Clear" /> is called.
///     </para>
/// </remarks>
public sealed partial class OutputLogBuffer : IEnumerable<OutputLogEntry>
{
    private const int LevelCount = (int)LogEventLevel.Fatal + 1;

    private readonly Lock syncLock = new();
    private readonly OutputLogEntry?[] items;
    private readonly int[] countsByLevel = new int[LevelCount];
    private readonly Dictionary<string, int> countsBySubsystem = [with(StringComparer.Ordinal)];
    private int head;
    private int count;
    private int withoutSubsystem;
    private long lastSequence;
    private long evictedCount;
    private int clearGeneration;
    private int signaled;

    /// <summary>
    ///     Initializes a new instance of the <see cref="OutputLogBuffer" /> class.
    /// </summary>
    /// <param name="subsystems">The map that resolves each appended entry's subsystem from its source.</param>
    /// <param name="capacity">
    ///     Maximum number of entries retained by the buffer. The oldest entries are overwritten when the capacity
    ///     is exceeded.
    /// </param>
    /// <exception cref="ArgumentOutOfRangeException">Thrown when <paramref name="capacity" /> is less than or equal to zero.</exception>
    public OutputLogBuffer(SubsystemMap subsystems, int capacity = 10000)
    {
        ArgumentNullException.ThrowIfNull(subsystems);
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(capacity);
        this.Subsystems = subsystems;
        this.Capacity = capacity;
        this.items = new OutputLogEntry?[capacity];
    }

    /// <summary>
    ///     Occurs on the producer's thread when entries become available to read: at most once until the next
    ///     <see cref="ReadSince" />, and after <see cref="Clear" />. Handlers must only schedule their read; they must
    ///     not read the buffer or touch UI.
    /// </summary>
    public event EventHandler? EntriesAvailable;

    /// <summary>
    ///     Gets the map that resolves appended entries' subsystems.
    /// </summary>
    public SubsystemMap Subsystems { get; }

    /// <summary>
    ///     Gets the maximum number of entries retained by the buffer.
    /// </summary>
    public int Capacity { get; }

    /// <summary>
    ///     Gets the current number of retained entries.
    /// </summary>
    public int Count
    {
        get
        {
            lock (this.syncLock)
            {
                return this.count;
            }
        }
    }

    /// <summary>
    ///     Gets the sequence of the newest entry ever appended, or <c>0</c> before the first append.
    /// </summary>
    public long LastSequence => Volatile.Read(ref this.lastSequence);

    /// <summary>
    ///     Appends an entry, resolving its subsystem and giving it the next sequence. When the buffer is full,
    ///     the oldest entry is overwritten.
    /// </summary>
    /// <param name="entry">The log entry to append.</param>
    /// <exception cref="ArgumentException">The entry was already appended to a buffer.</exception>
    public void Append(OutputLogEntry entry)
    {
        ArgumentNullException.ThrowIfNull(entry);
        if (entry.Sequence != 0)
        {
            throw new ArgumentException("The entry was already appended to a buffer.", nameof(entry));
        }

        // The map is immutable, so resolution needs no lock.
        entry.Subsystem = this.Subsystems.Resolve(entry.Source);

        lock (this.syncLock)
        {
            if (this.count == this.Capacity)
            {
                this.Uncount(this.items[this.head]!);
                this.items[this.head] = entry;
                this.head = (this.head + 1) % this.Capacity;
                this.evictedCount++;
            }
            else
            {
                this.items[(this.head + this.count) % this.Capacity] = entry;
                this.count++;
            }

            this.CountIn(entry);
            entry.Sequence = this.lastSequence + 1;
            Volatile.Write(ref this.lastSequence, entry.Sequence);
        }

        this.Signal();
    }

    /// <summary>
    ///     Reads the entries appended after a sequence, and re-arms <see cref="EntriesAvailable" />.
    /// </summary>
    /// <param name="afterSequence">The sequence of the last entry the reader has seen; <c>0</c> to read everything retained.</param>
    /// <returns>The new entries, oldest first, with the buffer's retention state.</returns>
    public OutputLogRead ReadSince(long afterSequence)
    {
        lock (this.syncLock)
        {
            Volatile.Write(ref this.signaled, 0);
            return new OutputLogRead(this.CopySince(afterSequence), this.FirstRetainedSequence(), this.clearGeneration);
        }
    }

    /// <summary>
    ///     Copies every retained entry, oldest first.
    /// </summary>
    /// <returns>The retained entries.</returns>
    public IReadOnlyList<OutputLogEntry> Snapshot()
    {
        lock (this.syncLock)
        {
            return this.CopySince(0);
        }
    }

    /// <summary>
    ///     Removes every entry. Sequences keep increasing; readers see a new
    ///     <see cref="OutputLogRead.ClearGeneration" />.
    /// </summary>
    public void Clear()
    {
        lock (this.syncLock)
        {
            Array.Clear(this.items);
            Array.Clear(this.countsByLevel);
            this.countsBySubsystem.Clear();
            this.withoutSubsystem = 0;
            this.head = 0;
            this.count = 0;
            this.evictedCount = 0;
            this.clearGeneration++;
        }

        this.Signal();
    }

    /// <summary>
    ///     Gets a snapshot of what the buffer retains.
    /// </summary>
    /// <returns>The statistics.</returns>
    public OutputLogStatistics GetStatistics()
    {
        lock (this.syncLock)
        {
            var subsystems = this.countsBySubsystem
                .OrderBy(static pair => pair.Key, StringComparer.Ordinal)
                .Select(static pair => new KeyValuePair<string?, int>(pair.Key, pair.Value))
                .ToList();
            if (this.withoutSubsystem > 0)
            {
                subsystems.Add(new KeyValuePair<string?, int>(key: null, this.withoutSubsystem));
            }

            return new OutputLogStatistics(
                this.Capacity,
                this.count,
                this.evictedCount,
                [.. this.countsByLevel],
                subsystems);
        }
    }

    /// <inheritdoc />
    public IEnumerator<OutputLogEntry> GetEnumerator() => this.Snapshot().GetEnumerator();

    /// <inheritdoc />
    IEnumerator IEnumerable.GetEnumerator() => this.GetEnumerator();

    private long FirstRetainedSequence() => this.lastSequence - this.count + 1;

    private OutputLogEntry[] CopySince(long afterSequence)
    {
        var first = this.FirstRetainedSequence();
        var start = Math.Max(afterSequence + 1, first);
        var length = this.lastSequence - start + 1;
        if (length <= 0)
        {
            return [];
        }

        var copy = new OutputLogEntry[length];
        var offset = this.head + (int)(start - first);
        for (var i = 0; i < copy.Length; i++)
        {
            copy[i] = this.items[(offset + i) % this.Capacity]!;
        }

        return copy;
    }

    private void CountIn(OutputLogEntry entry)
    {
        this.countsByLevel[(int)entry.Level]++;
        if (entry.Subsystem is null)
        {
            this.withoutSubsystem++;
        }
        else
        {
            this.countsBySubsystem[entry.Subsystem] = this.countsBySubsystem.GetValueOrDefault(entry.Subsystem) + 1;
        }
    }

    private void Uncount(OutputLogEntry entry)
    {
        this.countsByLevel[(int)entry.Level]--;
        if (entry.Subsystem is null)
        {
            this.withoutSubsystem--;
        }
        else if (--this.countsBySubsystem[entry.Subsystem] == 0)
        {
            _ = this.countsBySubsystem.Remove(entry.Subsystem);
        }
    }

    private void Signal()
    {
        if (Interlocked.Exchange(ref this.signaled, 1) == 0)
        {
            this.EntriesAvailable?.Invoke(this, EventArgs.Empty);
        }
    }
}
