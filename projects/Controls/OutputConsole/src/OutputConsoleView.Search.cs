// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using DroidNet.Controls.OutputConsole.Model;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Input;

namespace DroidNet.Controls.OutputConsole;

/// <summary>
///     The search box: Filter mode hides non-matching lines, Find mode highlights matches and moves between them.
/// </summary>
public sealed partial class OutputConsoleView
{
    private readonly DispatcherQueueTimer searchTimer;
    private OutputLogRow? currentMatch;
    private int matchCount;
    private bool syncingSearchMode;

    private bool IsFinding => this.SearchMode == OutputSearchMode.Find && this.SearchText.Length > 0;

    // Typing restarts the timer, so a search runs once the user pauses.
    private void ScheduleSearch()
    {
        if (!this.isLoaded)
        {
            return;
        }

        this.searchTimer.Stop();
        this.searchTimer.Start();
    }

    private void ApplySearch()
    {
        this.searchTimer.Stop();
        this.currentMatch = null;
        if (this.SearchMode == OutputSearchMode.Filter)
        {
            this.RebuildView();
            return;
        }

        this.RecountMatches();
        this.RefreshRealizedRows();
        this.RefreshDetails();
    }

    private void OnSearchModeChanged()
    {
        this.SyncSearchModeUi();
        if (this.isLoaded)
        {
            this.currentMatch = null;
            this.RebuildView();
        }
    }

    private void SyncSearchModeUi()
    {
        this.syncingSearchMode = true;
        this.SearchModeToggle.IsChecked = this.SearchMode == OutputSearchMode.Filter;
        this.syncingSearchMode = false;
        this.UpdateMatchText();
    }

    private void OnSearchModeToggled(bool filter)
    {
        if (!this.syncingSearchMode)
        {
            this.SearchMode = filter ? OutputSearchMode.Filter : OutputSearchMode.Find;
        }
    }

    private void RecountMatches()
    {
        this.matchCount = this.IsFinding ? this.CountMatches(this.viewItems) : 0;
        this.UpdateMatchText();
    }

    private void CountNewMatches(IReadOnlyList<OutputLogRow> added)
    {
        if (!this.IsFinding || added.Count == 0)
        {
            return;
        }

        this.matchCount += this.CountMatches(added);
        this.UpdateMatchText();
    }

    private int CountMatches(IEnumerable<OutputLogRow> rows)
    {
        var text = this.SearchText;
        return rows.Count(row => row.Matches(text));
    }

    // Called before a row leaves the view.
    private void ForgetMatch(OutputLogRow row)
    {
        if (!this.IsFinding || !row.Matches(this.SearchText))
        {
            return;
        }

        this.matchCount--;
        if (ReferenceEquals(row, this.currentMatch))
        {
            this.currentMatch = null;
        }

        this.UpdateMatchText();
    }

    private void UpdateMatchText()
    {
        if (!this.IsFinding)
        {
            this.FindNavigation.Visibility = Visibility.Collapsed;
            return;
        }

        this.FindNavigation.Visibility = Visibility.Visible;
        this.MatchCountText.Text = this.matchCount == 0
            ? "No results"
            : this.currentMatch is { } current
                ? string.Create(CultureInfo.CurrentCulture, $"{this.MatchOrdinal(current)} of {this.matchCount:N0}")
                : string.Create(CultureInfo.CurrentCulture, $"{this.matchCount:N0} results");
    }

    private int MatchOrdinal(OutputLogRow match)
    {
        var text = this.SearchText;
        var ordinal = 0;
        foreach (var row in this.viewItems)
        {
            if (row.Matches(text))
            {
                ordinal++;
            }

            if (ReferenceEquals(row, match))
            {
                return ordinal;
            }
        }

        return 0;
    }

    private void HighlightRow(OutputLogRowView view, OutputLogRow row)
    {
        if (this.brushes is not null)
        {
            view.Highlight(this.SearchText, ReferenceEquals(row, this.currentMatch), this.brushes);
        }
    }

    private void MoveToMatch(bool forward)
    {
        var text = this.SearchText;
        var count = this.viewItems.Count;
        if (text.Length == 0 || count == 0)
        {
            return;
        }

        var start = this.List.SelectedIndex >= 0 ? this.List.SelectedIndex : forward ? -1 : count;
        for (var step = 1; step <= count; step++)
        {
            var index = (((start + (forward ? step : -step)) % count) + count) % count;
            if (this.viewItems[index].Matches(text))
            {
                this.ShowMatch(this.viewItems[index]);
                return;
            }
        }
    }

    private void ShowMatch(OutputLogRow row)
    {
        this.currentMatch = row;
        this.List.SelectedItem = row;
        this.List.ScrollIntoView(row);
        this.RefreshRealizedRows();
        this.UpdateMatchText();
        this.RefreshDetails();
    }

    private void OnFindNextAccelerator(KeyboardAccelerator sender, KeyboardAcceleratorInvokedEventArgs e)
    {
        e.Handled = true;
        this.MoveToMatch(forward: true);
    }

    private void OnFindPrevAccelerator(KeyboardAccelerator sender, KeyboardAcceleratorInvokedEventArgs e)
    {
        e.Handled = true;
        this.MoveToMatch(forward: false);
    }
}
