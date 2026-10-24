// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls.OutputConsole.Model;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls.OutputConsole;

/// <summary>Exposes message selection for hosts that switch between retained transcripts.</summary>
public partial class OutputConsoleView
{
    private OutputLogRow? lastSelectedRow;
    private bool updatingSelectedItem;

    /// <summary>Captures the selected messages without depending on realized containers.</summary>
    /// <returns>The entries of the selected lines, folded ones included, in source order.</returns>
    public IReadOnlyList<OutputLogEntry> CaptureSelection()
        => [.. this.SelectedRowsInOrder().SelectMany(static row => row.Entries)];

    /// <summary>Restores selection for entries still present in the displayed transcript.</summary>
    /// <param name="entries">Previously selected entries.</param>
    public void RestoreSelection(IEnumerable<OutputLogEntry> entries)
    {
        ArgumentNullException.ThrowIfNull(entries);
        var wanted = new HashSet<OutputLogEntry>(entries);
        this.List.SelectedItems.Clear();
        if (wanted.Count == 0)
        {
            return;
        }

        foreach (var row in this.viewItems)
        {
            if (wanted.Contains(row.First) || wanted.Contains(row.Last) || (row.Count > 2 && row.Entries.Any(wanted.Contains)))
            {
                this.List.SelectedItems.Add(row);
            }
        }
    }

    private IEnumerable<OutputLogRow> SelectedRowsInOrder()
        => this.List.SelectedItems.OfType<OutputLogRow>().OrderBy(static row => row.FirstOrdinal);

    private void OnListSelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        var added = e.AddedItems.OfType<OutputLogRow>().LastOrDefault();
        if (added is not null)
        {
            this.lastSelectedRow = added;
        }
        else if (this.lastSelectedRow is { } previous && !this.List.SelectedItems.Contains(previous))
        {
            this.lastSelectedRow = this.List.SelectedItems.OfType<OutputLogRow>().LastOrDefault();
        }

        this.updatingSelectedItem = true;
        this.SelectedItem = this.lastSelectedRow?.Last;
        this.updatingSelectedItem = false;

        this.CopyButton.IsEnabled = this.List.SelectedItems.Count > 0;
        this.RefreshDetails();
    }

    private void OnSelectedItemChanged(OutputLogEntry? entry)
    {
        if (this.updatingSelectedItem || !this.isLoaded)
        {
            return;
        }

        if (entry is null)
        {
            this.List.SelectedItems.Clear();
            return;
        }

        var row = this.viewItems.FirstOrDefault(row => row.Entries.Contains(entry));
        if (row is not null)
        {
            this.List.SelectedItem = row;
            this.List.ScrollIntoView(row);
        }
    }
}
