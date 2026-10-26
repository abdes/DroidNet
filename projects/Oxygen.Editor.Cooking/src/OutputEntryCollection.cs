// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.ComponentModel;
using DroidNet.Controls.OutputConsole.Model;

namespace Oxygen.Editor.Cooking;

/// <summary>A transcript that reports a batch of new lines as one change, so the console applies it once.</summary>
public sealed class OutputEntryCollection : ObservableCollection<OutputLogEntry>
{
    /// <summary>Appends entries and raises a single collection change for all of them.</summary>
    /// <param name="entries">The new lines, in order.</param>
    public void AddRange(IReadOnlyList<OutputLogEntry> entries)
    {
        ArgumentNullException.ThrowIfNull(entries);
        if (entries.Count == 0)
        {
            return;
        }

        var start = this.Count;
        foreach (var entry in entries)
        {
            this.Items.Add(entry);
        }

        this.OnPropertyChanged(new PropertyChangedEventArgs(nameof(this.Count)));
        this.OnPropertyChanged(new PropertyChangedEventArgs("Item[]"));
        this.OnCollectionChanged(new NotifyCollectionChangedEventArgs(NotifyCollectionChangedAction.Add, entries.ToList(), start));
    }
}
