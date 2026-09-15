// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls.OutputConsole.Model;

namespace DroidNet.Controls.OutputConsole;

/// <summary>Exposes message selection for hosts that switch between retained transcripts.</summary>
public partial class OutputConsoleView
{
    /// <summary>Captures the selected messages without depending on realized containers.</summary>
    /// <returns>The currently selected entries in the transcript.</returns>
    public IReadOnlyList<OutputLogEntry> CaptureSelection()
        => this.List.SelectedItems.OfType<OutputLogEntry>().ToArray();

    /// <summary>Restores selection for entries still present in the displayed transcript.</summary>
    /// <param name="entries">Previously selected entries.</param>
    public void RestoreSelection(IEnumerable<OutputLogEntry> entries)
    {
        ArgumentNullException.ThrowIfNull(entries);
        this.List.SelectedItems.Clear();
        foreach (var entry in entries)
        {
            if (this.viewItems.Contains(entry))
            {
                this.List.SelectedItems.Add(entry);
            }
        }
    }
}
