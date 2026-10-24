// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections;
using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.Globalization;
using DroidNet.Controls.OutputConsole.Model;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;

namespace DroidNet.Controls.OutputConsole;

/// <summary>
///     The console's sources: pulling from an <see cref="OutputLogBuffer" />, following a collection changed on the
///     UI thread, rebuilding the rows and pausing.
/// </summary>
public sealed partial class OutputConsoleView
{
    // A larger burst is cheaper to show by replacing the rows than by adding them one by one.
    private const int RebuildThreshold = 1024;

    private readonly DispatcherQueueTimer pauseTimer;
    private OutputLogBuffer? buffer;
    private long lastSequence;
    private int clearGeneration;
    private int drainQueued;
    private long collectionOrdinal;

    private string SearchText => (this.TextFilter ?? string.Empty).Trim();

    private OutputLogFilter CurrentFilter() => new(
        this.LevelFilter,
        new HashSet<string>(this.HiddenSubsystems ?? [], StringComparer.Ordinal),
        this.SearchText,
        this.SearchMode,
        this.FoldDuplicates);

    private void AttachSource(IEnumerable? source)
    {
        switch (source)
        {
            case OutputLogBuffer logBuffer:
                this.buffer = logBuffer;
                logBuffer.EntriesAvailable += this.OnEntriesAvailable;
                break;
            case INotifyCollectionChanged changes:
                changes.CollectionChanged += this.OnSourceCollectionChanged;
                break;
            default:
                break;
        }

        // A new source shows as it is now; a paused view freezes it from there.
        this.lastSequence = this.IsPaused && this.buffer is { } shown ? shown.LastSequence : 0;
        this.collectionOrdinal = this.IsPaused && this.buffer is null && source is IEnumerable<OutputLogEntry> items ? items.Count() : 0;
        this.BufferContainer.Visibility = this.buffer is null ? Visibility.Collapsed : Visibility.Visible;
    }

    private void DetachSource(IEnumerable? source)
    {
        if (source is OutputLogBuffer logBuffer)
        {
            logBuffer.EntriesAvailable -= this.OnEntriesAvailable;
        }
        else if (source is INotifyCollectionChanged changes)
        {
            changes.CollectionChanged -= this.OnSourceCollectionChanged;
        }

        this.buffer = null;
    }

    // Runs on the producer's thread: only schedule the drain.
    private void OnEntriesAvailable(object? sender, EventArgs e)
    {
        if (Interlocked.Exchange(ref this.drainQueued, 1) == 0)
        {
            _ = this.DispatcherQueue.TryEnqueue(this.Drain);
        }
    }

    private void Drain()
    {
        Volatile.Write(ref this.drainQueued, 0);
        if (!this.isLoaded || this.buffer is not { } source || this.IsPaused)
        {
            return;
        }

        var read = source.ReadSince(this.lastSequence);
        if (read.ClearGeneration != this.clearGeneration || read.Entries.Count >= RebuildThreshold)
        {
            this.RebuildView();
            return;
        }

        if (read.Entries.Count == 0)
        {
            return;
        }

        var sourced = new SourcedEntry[read.Entries.Count];
        for (var i = 0; i < sourced.Length; i++)
        {
            sourced[i] = new SourcedEntry(read.Entries[i].Sequence, read.Entries[i]);
        }

        this.lastSequence = sourced[^1].Ordinal;
        this.ApplyDelta(this.projection.Append(sourced, read.FirstRetainedSequence));
    }

    private void OnSourceCollectionChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        if (!this.isLoaded || this.IsPaused)
        {
            return;
        }

        if (e.Action == NotifyCollectionChangedAction.Add && e.NewItems is { } added && e.NewStartingIndex == this.collectionOrdinal)
        {
            var sourced = new List<SourcedEntry>(added.Count);
            foreach (var entry in added.OfType<OutputLogEntry>())
            {
                sourced.Add(new SourcedEntry(++this.collectionOrdinal, entry));
            }

            this.ApplyDelta(this.projection.Append(sourced, firstRetainedOrdinal: 1));
            return;
        }

        this.RebuildView();
    }

    private void ApplyDelta(ProjectionDelta delta)
    {
        for (var i = 0; i < delta.RemovedFromHead; i++)
        {
            this.ForgetMatch(this.viewItems[0]);
            this.viewItems.RemoveAt(0);
        }

        if (delta.HeadChanged)
        {
            this.RebindAt(0);
        }

        if (delta.TailChanged)
        {
            this.RebindAt(this.viewItems.Count - 1);
        }

        foreach (var row in delta.Added)
        {
            this.viewItems.Add(row);
        }

        this.WidenSubsystemColumn(delta.Added);
        this.CountNewMatches(delta.Added);
        this.UpdateBufferStatus();
    }

    private void RebuildView()
    {
        if (!this.isLoaded)
        {
            return;
        }

        var selection = this.CaptureSelection();
        var offset = this.FollowTail ? null : this.scrollViewer?.VerticalOffset;

        this.projection.Filter = this.CurrentFilter();
        var rows = this.projection.Rebuild(this.ReadAll());
        this.viewItems = [with(rows)];
        this.awaitingScrollToEnd = this.FollowTail;
        this.List.ItemsSource = this.viewItems;
        this.RestoreSelection(selection);

        if (this.FollowTail)
        {
            this.ScrollToEnd();
        }
        else if (offset is { } restore)
        {
            _ = this.DispatcherQueue.TryEnqueue(
                DispatcherQueuePriority.Low,
                () => this.scrollViewer?.ChangeView(horizontalOffset: null, restore, zoomFactor: null, disableAnimation: true));
        }

        this.columns = this.columns with { SubsystemChars = 0 };
        this.WidenSubsystemColumn(rows);
        this.RecountMatches();
        this.UpdateBufferStatus();
        this.UpdateSubsystemsSummary();
    }

    private IEnumerable<SourcedEntry> ReadAll()
    {
        if (this.buffer is { } source)
        {
            var read = source.ReadSince(0);
            this.clearGeneration = read.ClearGeneration;
            if (this.IsPaused)
            {
                // A paused view keeps showing only what it showed when it paused.
                var shownUpTo = this.lastSequence;
                return read.Entries.Where(e => e.Sequence <= shownUpTo).Select(static e => new SourcedEntry(e.Sequence, e));
            }

            this.lastSequence = read.Entries.Count > 0 ? read.Entries[^1].Sequence : read.FirstRetainedSequence - 1;
            return read.Entries.Select(static e => new SourcedEntry(e.Sequence, e));
        }

        if (this.ItemsSource is IEnumerable<OutputLogEntry> items)
        {
            var entries = items.ToList();
            if (this.IsPaused)
            {
                // A paused view keeps showing only what it showed when it paused.
                entries = entries[..(int)Math.Min(this.collectionOrdinal, entries.Count)];
            }
            else
            {
                this.collectionOrdinal = entries.Count;
            }

            return entries.Select(static (entry, index) => new SourcedEntry(index + 1, entry));
        }

        return [];
    }

    private void OnIsPausedChanged(bool paused)
    {
        this.PauseChanged?.Invoke(this, new ToggleEventArgs(paused));
        if (paused)
        {
            // Loading starts the timer of a view paused before it loaded.
            if (this.isLoaded)
            {
                this.pauseTimer.Start();
            }

            this.UpdatePauseLabel();
            return;
        }

        this.pauseTimer.Stop();
        this.PauseToggle.Label = "Pause";
        if (this.buffer is not null)
        {
            this.Drain();
        }
        else
        {
            this.RebuildView();
        }
    }

    // The entries the buffer captured that the paused view does not show.
    private long PendingCount() => this.buffer is { } source ? source.LastSequence - this.lastSequence : 0;

    private void UpdatePauseLabel()
    {
        var pending = this.PendingCount();
        this.PauseToggle.Label = pending > 0
            ? string.Create(CultureInfo.CurrentCulture, $"Paused · {pending:N0} new")
            : "Paused";
        this.UpdateBufferStatus();
    }
}
