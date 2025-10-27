// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.ComponentModel;

namespace DroidNet.Controls.OutputConsole.Model;

/// <summary>
///     An observable, fixed-capacity ring buffer optimized for UI binding.
///     Inherits from <see cref="ObservableCollection{T}" /> to provide standard incremental notifications.
///     When the buffer is paused via <see cref="IsPaused" />, change notifications are suppressed
///     except for a <see cref="NotifyCollectionChangedAction.Reset" /> which is raised when unpausing
///     to allow UI listeners to resynchronize.
/// </summary>
public partial class OutputLogBuffer : ObservableCollection<OutputLogEntry>
{
    private const string IndexerPropertyName = "Item[]";
    private readonly Lock syncLock = new();
    private bool paused;

    /// <summary>
    ///     Initializes a new instance of the <see cref="OutputLogBuffer" /> class with the specified
    ///     capacity. The capacity must be a positive integer.
    /// </summary>
    /// <param name="capacity">
    ///     Maximum number of entries retained by the buffer. Older entries are discarded when the capacity
    ///     is exceeded.
    /// </param>
    /// <exception cref="ArgumentOutOfRangeException">Thrown when <paramref name="capacity" /> is less than or equal to zero.</exception>
    public OutputLogBuffer(int capacity = 10000)
    {
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(capacity);
        this.Capacity = capacity;
    }

    /// <summary>
    ///     Gets or sets a value indicating whether change notifications are paused.
    ///     When set to <see langword="true" />, property and collection change notifications
    ///     (other than <see cref="NotifyCollectionChangedAction.Reset" />) are suppressed.
    ///     When set back to <see langword="false" />, a Reset notification is raised so
    ///     listeners can resynchronize their views.
    /// </summary>
    public bool IsPaused
    {
        get => this.paused;
        set
        {
            if (this.paused == value)
            {
                return;
            }

            this.paused = value;
            if (!this.paused)
            {
                // After a pause, ensure listeners can resync
                this.OnCollectionChanged(new NotifyCollectionChangedEventArgs(NotifyCollectionChangedAction.Reset));
            }
        }
    }

    /// <summary>
    ///     Gets the maximum number of entries retained by the buffer.
    /// </summary>
    public int Capacity { get; }

    /// <summary>
    ///     Appends an <see cref="OutputLogEntry" /> to the end of the buffer.
    ///     If the buffer exceeds <see cref="Capacity" />, the oldest entries are removed.
    /// </summary>
    /// <param name="entry">The log entry to append.</param>
    public void Append(OutputLogEntry entry) => this.Add(entry);

    /// <summary>
    ///     Clears all entries from the buffer.
    /// </summary>
    public new void Clear()
    {
        lock (this.syncLock)
        {
            this.ClearItems();
        }
    }

    /// <summary>
    ///     Inserts an item into the buffer. The buffer always appends items to the end
    ///     regardless of the provided <paramref name="index" />, to provide ring semantics.
    ///     After insertion the buffer is trimmed to the configured <see cref="Capacity" />.
    /// </summary>
    /// <inheritdoc />
    protected override void InsertItem(int index, OutputLogEntry item)
    {
        lock (this.syncLock)
        {
            // Always append to the end; ignore index for ring semantics
            base.InsertItem(this.Count, item);
            this.TrimIfNeeded();
        }
    }

    /// <inheritdoc />
    protected override void RemoveItem(int index)
    {
        lock (this.syncLock)
        {
            base.RemoveItem(index);
        }
    }

    /// <summary>
    ///     Raises collection change notifications. When the buffer is paused notifications
    ///     are suppressed except for a Reset action which is always forwarded to allow
    ///     listeners to resynchronize.
    /// </summary>
    /// <inheritdoc />
    protected override void OnCollectionChanged(NotifyCollectionChangedEventArgs e)
    {
        // Always notify Reset (e.g., Clear) even when paused so UIs can resync immediately.
        if (!this.paused || e.Action == NotifyCollectionChangedAction.Reset)
        {
            // Block reentrancy to prevent nested collection changes during event handling
            using (this.BlockReentrancy())
            {
                base.OnCollectionChanged(e);
            }
        }
    }

    /// <summary>
    ///     Raises property change notifications. When paused, only notifications for <see cref="Collection{T}.Count" />
    ///     and the indexer ("Item[]") are forwarded.
    /// </summary>
    /// <inheritdoc />
    protected override void OnPropertyChanged(PropertyChangedEventArgs e)
    {
        if (!this.paused ||
            string.Equals(e.PropertyName, nameof(this.Count), StringComparison.Ordinal) ||
            string.Equals(e.PropertyName, IndexerPropertyName, StringComparison.Ordinal))
        {
            base.OnPropertyChanged(e);
        }
    }

    /// <summary>
    ///     Removes oldest entries if the current count exceeds the configured <see cref="Capacity" />.
    /// </summary>
    private void TrimIfNeeded()
    {
        while (this.Count > this.Capacity)
        {
            // Remove at head to maintain ring behavior
            this.RemoveItem(0);
        }
    }
}
