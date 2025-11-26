// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections;
using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.ComponentModel;

namespace DroidNet.Controls.OutputConsole.Model;

/// <summary>
///     An observable, fixed-capacity ring buffer optimized for UI binding.
///     When the buffer is paused via <see cref="IsPaused" />, change notifications are suppressed
///     except for a <see cref="NotifyCollectionChangedAction.Reset" /> which is raised when unpausing
///     to allow UI listeners to resynchronize.
/// </summary>
public partial class OutputLogBuffer : INotifyCollectionChanged, INotifyPropertyChanged, IEnumerable<OutputLogEntry>
{
    private const string IndexerPropertyName = "Item[]";
    private readonly Lock syncLock = new();
    private readonly List<OutputLogEntry> items;
    private int updateDepth;
    private bool isDirty;
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
        this.items = new List<OutputLogEntry>(capacity);
    }

    /// <inheritdoc />
    public event NotifyCollectionChangedEventHandler? CollectionChanged;

    /// <inheritdoc />
    public event PropertyChangedEventHandler? PropertyChanged;

    /// <summary>
    ///     Gets or sets a value indicating whether change notifications are paused.
    ///     When set to <see langword="true" />, property and collection change notifications
    ///     (other than <see cref="NotifyCollectionChangedAction.Reset" />) are suppressed.
    ///     When set back to <see langword="false" />, a Reset notification is raised so
    ///     listeners can resynchronize their views.
    /// </summary>
    public bool IsPaused
    {
        get
        {
            lock (this.syncLock)
            {
                return this.paused;
            }
        }

        set
        {
            lock (this.syncLock)
            {
                if (this.paused == value)
                {
                    return;
                }

                this.paused = value;
            }

            if (!value)
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
    ///     Gets the current number of elements in the buffer.
    /// </summary>
    public int Count
    {
        get
        {
            lock (this.syncLock)
            {
                return this.items.Count;
            }
        }
    }

    /// <summary>
    ///     Gets the element at the specified index.
    /// </summary>
    /// <param name="index">The zero-based index of the element to get.</param>
    /// <returns>The element at the specified index.</returns>
    public OutputLogEntry this[int index]
    {
        get
        {
            lock (this.syncLock)
            {
                return this.items[index];
            }
        }
    }

    /// <summary>
    ///     Appends an <see cref="OutputLogEntry" /> to the end of the buffer.
    ///     If the buffer exceeds <see cref="Capacity" />, the oldest entries are removed.
    /// </summary>
    /// <param name="entry">The log entry to append.</param>
    public void Append(OutputLogEntry entry)
    {
        bool wasPaused;
        bool isInBatch;
        var wasRotation = false;
        int newIndex;

        lock (this.syncLock)
        {
            if (this.items.Count == this.Capacity)
            {
                this.items.RemoveAt(0);
                wasRotation = true;
            }

            this.items.Add(entry);

            // capture index while still holding the lock so callers reading by index
            // from a CollectionChanged handler get a stable, correct reference
            newIndex = this.items.Count - 1;
            wasPaused = this.paused;
            isInBatch = this.updateDepth > 0;

            if (isInBatch)
            {
                this.isDirty = true;
            }
        }

        if (wasPaused || isInBatch)
        {
            return;
        }

        // During rotation, use Reset to avoid per-item Remove events
        if (wasRotation)
        {
            this.OnCollectionChanged(new NotifyCollectionChangedEventArgs(NotifyCollectionChangedAction.Reset));
            this.OnPropertyChanged(nameof(this.Count));
            this.OnPropertyChanged(IndexerPropertyName);
        }
        else
        {
            // use the index captured under the lock to avoid races where other
            // threads mutated the list between the append and event creation
            this.OnCollectionChanged(new NotifyCollectionChangedEventArgs(NotifyCollectionChangedAction.Add, entry, newIndex));
            this.OnPropertyChanged(nameof(this.Count));
            this.OnPropertyChanged(IndexerPropertyName);
        }
    }

    /// <summary>
    ///     Clears all entries from the buffer.
    /// </summary>
    public void Clear()
    {
        lock (this.syncLock)
        {
            this.items.Clear();
        }

        this.OnCollectionChanged(new NotifyCollectionChangedEventArgs(NotifyCollectionChangedAction.Reset));
        this.OnPropertyChanged(nameof(this.Count));
        this.OnPropertyChanged(IndexerPropertyName);
    }

    /// <summary>
    ///     Begins a batch update operation. While in batch mode, collection change notifications
    ///     are suppressed. Call <see cref="EndUpdate" /> to complete the batch and raise a single
    ///     <see cref="NotifyCollectionChangedAction.Reset" /> notification.
    /// </summary>
    /// <remarks>
    ///     This method supports nested calls. Notifications are only raised when the outermost
    ///     batch is completed (when update depth returns to zero).
    /// </remarks>
    public void BeginUpdate()
    {
        lock (this.syncLock)
        {
            this.updateDepth++;
        }
    }

    /// <summary>
    ///     Ends a batch update operation. If this completes the outermost batch (update depth
    ///     reaches zero) and changes were made, a <see cref="NotifyCollectionChangedAction.Reset" />
    ///     notification is raised.
    /// </summary>
    public void EndUpdate()
    {
        var shouldNotify = false;

        lock (this.syncLock)
        {
            if (this.updateDepth > 0)
            {
                this.updateDepth--;
            }

            if (this.updateDepth == 0 && this.isDirty)
            {
                shouldNotify = true;
                this.isDirty = false;
            }
        }

        if (shouldNotify)
        {
            this.OnCollectionChanged(new NotifyCollectionChangedEventArgs(NotifyCollectionChangedAction.Reset));
            this.OnPropertyChanged(nameof(this.Count));
            this.OnPropertyChanged(IndexerPropertyName);
        }
    }

    /// <inheritdoc />
    public IEnumerator<OutputLogEntry> GetEnumerator()
    {
        List<OutputLogEntry> snapshot;
        lock (this.syncLock)
        {
            snapshot = [.. this.items];
        }

        return snapshot.GetEnumerator();
    }

    /// <inheritdoc />
    IEnumerator IEnumerable.GetEnumerator() => this.GetEnumerator();

    /// <summary>
    ///     Raises the <see cref="CollectionChanged" /> event.
    /// </summary>
    /// <param name="e">The event data.</param>
    protected virtual void OnCollectionChanged(NotifyCollectionChangedEventArgs e)
        => this.CollectionChanged?.Invoke(this, e);

    /// <summary>
    ///     Raises the <see cref="PropertyChanged" /> event.
    /// </summary>
    /// <param name="propertyName">The name of the property that changed.</param>
    protected virtual void OnPropertyChanged(string propertyName)
        => this.PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
}
