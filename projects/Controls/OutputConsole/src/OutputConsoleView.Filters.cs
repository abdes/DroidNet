// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using DroidNet.Controls.OutputConsole.Model;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls.OutputConsole;

/// <summary>
///     The toolbar: its wiring and the Levels dropdown.
/// </summary>
public sealed partial class OutputConsoleView
{
    private static readonly (LevelMask Flag, string Name)[] Levels =
    [
        (LevelMask.Verbose, "Verbose"), (LevelMask.Debug, "Debug"), (LevelMask.Information, "Information"),
        (LevelMask.Warning, "Warning"), (LevelMask.Error, "Error"), (LevelMask.Fatal, "Fatal"),
    ];

    private void WireToolbar()
    {
        this.ClearButton.Click += (_, _) => this.ClearOutput();
        this.CopyButton.Click += async (_, _) => await this.CopySelectionAsync().ConfigureAwait(true);
        this.SaveButton.Click += async (_, _) => await this.SaveOutputAsync().ConfigureAwait(true);
        this.SearchModeToggle.Checked += (_, _) => this.OnSearchModeToggled(filter: true);
        this.SearchModeToggle.Unchecked += (_, _) => this.OnSearchModeToggled(filter: false);
        this.PreviousMatchButton.Click += (_, _) => this.MoveToMatch(forward: false);
        this.NextMatchButton.Click += (_, _) => this.MoveToMatch(forward: true);
        this.SearchBox.QuerySubmitted += (_, _) => this.MoveToMatch(forward: true);
        this.WireSubsystems();
        this.BufferFlyout.Opening += (_, _) => this.OnBufferFlyoutShown(shown: true);
        this.BufferFlyout.Closed += (_, _) => this.OnBufferFlyoutShown(shown: false);
    }

    private void ClearOutput()
    {
        this.buffer?.Clear();
        this.ClearRequested?.Invoke(this, EventArgs.Empty);
    }

    private void OnFiltersChanged()
    {
        if (this.levelsWired)
        {
            this.SyncLevelMenuChecks();
        }

        if (this.isLoaded)
        {
            this.RebuildView();
        }
    }

    private void InitializeLevelItems()
    {
        // Show Information and above unless the host chose the levels.
        if (this.ReadLocalValue(LevelFilterProperty) == DependencyProperty.UnsetValue &&
            this.LevelFilter == LevelMask.All)
        {
            this.LevelFilter = LevelMask.Information | LevelMask.Warning | LevelMask.Error | LevelMask.Fatal;
        }

        void Wire(ToggleMenuFlyoutItem item, LevelMask flag)
        {
            item.IsChecked = this.LevelFilter.HasFlag(flag);
            item.Click += (_, _) =>
            {
                this.LevelFilter = item.IsChecked ? this.LevelFilter | flag : this.LevelFilter & ~flag;
                this.SyncLevelMenuChecks();
            };
        }

        Wire(this.LevelVerboseItem, LevelMask.Verbose);
        Wire(this.LevelDebugItem, LevelMask.Debug);
        Wire(this.LevelInformationItem, LevelMask.Information);
        Wire(this.LevelWarningItem, LevelMask.Warning);
        Wire(this.LevelErrorItem, LevelMask.Error);
        Wire(this.LevelFatalItem, LevelMask.Fatal);
        this.UpdateLevelsSummary();
    }

    private void SyncLevelMenuChecks()
    {
        this.LevelVerboseItem.IsChecked = this.LevelFilter.HasFlag(LevelMask.Verbose);
        this.LevelDebugItem.IsChecked = this.LevelFilter.HasFlag(LevelMask.Debug);
        this.LevelInformationItem.IsChecked = this.LevelFilter.HasFlag(LevelMask.Information);
        this.LevelWarningItem.IsChecked = this.LevelFilter.HasFlag(LevelMask.Warning);
        this.LevelErrorItem.IsChecked = this.LevelFilter.HasFlag(LevelMask.Error);
        this.LevelFatalItem.IsChecked = this.LevelFilter.HasFlag(LevelMask.Fatal);
        this.UpdateLevelsSummary();
    }

    private void UpdateLevelsSummary()
    {
        var selectedCount = Levels.Count(level => this.LevelFilter.HasFlag(level.Flag));

        // Name the most verbose level shown: the levels above it are usually shown too.
        var summary = selectedCount == Levels.Length
            ? "All"
            : Levels.FirstOrDefault(level => this.LevelFilter.HasFlag(level.Flag)).Name ?? "None";
        this.LevelsDropDown.Content = string.Create(
            CultureInfo.InvariantCulture,
            $"Levels: {summary} ({selectedCount}/{Levels.Length})");
    }
}
