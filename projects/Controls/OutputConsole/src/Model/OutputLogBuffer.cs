// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.ComponentModel;

namespace DroidNet.Controls.OutputConsole.Model;

/// <summary>
/// An observable, fixed-capacity ring buffer optimized for UI binding.
/// Inherits from ObservableCollection to provide standard incremental notifications.
/// </summary>
public class OutputLogBuffer : ObservableCollection<OutputLogEntry>
{
    private readonly int _capacity;
    private bool _paused;

    public OutputLogBuffer(int capacity = 10000)
    {
        if (capacity <= 0) throw new ArgumentOutOfRangeException(nameof(capacity));
        _capacity = capacity;
    }

    public bool IsPaused
    {
        get => _paused;
        set
        {
            if (_paused == value) return;
            _paused = value;
            if (!_paused)
            {
                // After a pause, ensure listeners can resync
                OnCollectionChanged(new NotifyCollectionChangedEventArgs(NotifyCollectionChangedAction.Reset));
            }
        }
    }

    public int Capacity => _capacity;

    public void Append(OutputLogEntry entry)
    {
        Add(entry);
    }

    public new void Clear() => base.ClearItems();

    private void TrimIfNeeded()
    {
        while (Count > _capacity)
        {
            // Remove at head to maintain ring behavior
            base.RemoveItem(0);
        }
    }

    protected override void InsertItem(int index, OutputLogEntry item)
    {
        // Always append to the end; ignore index for ring semantics
        base.InsertItem(Count, item);
        TrimIfNeeded();
    }

    protected override void OnCollectionChanged(NotifyCollectionChangedEventArgs e)
    {
        if (!_paused)
        {
            base.OnCollectionChanged(e);
        }
    }

    protected override void OnPropertyChanged(PropertyChangedEventArgs e)
    {
        if (!_paused || e.PropertyName == nameof(Count) || e.PropertyName == "Item[]")
        {
            base.OnPropertyChanged(e);
        }
    }
}
