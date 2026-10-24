// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using DroidNet.Controls.OutputConsole.Model;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls.OutputConsole;

/// <summary>
///     The Subsystems dropdown: a multi-select flyout with a three-state All check box, one check box per subsystem
///     with its line count, and changes that apply as the user checks.
/// </summary>
public sealed partial class OutputConsoleView
{
    private const string OtherSubsystemLabel = "Other";

    private readonly List<(string Key, CheckBox Box)> subsystemBoxes = [];
    private bool syncingSubsystemBoxes;

    private static string SubsystemLabel(string key) => key.Length == 0 ? OtherSubsystemLabel : key;

    private void WireSubsystems()
    {
        this.SubsystemsFlyout.Opening += (_, _) => this.BuildSubsystemsFlyout();
        this.AllSubsystemsCheckBox.Checked += (_, _) => this.OnAllSubsystemsToggled(shown: true);
        this.AllSubsystemsCheckBox.Unchecked += (_, _) => this.OnAllSubsystemsToggled(shown: false);
        this.AllSubsystemsCheckBox.Indeterminate += (_, _) => this.OnAllSubsystemsIndeterminate();
    }

    // The subsystems to offer, and how many lines each has in the source. For a buffer, every subsystem its map
    // defines, whether or not it has logged; the counts are the buffer's running per-subsystem counts, so no entry is
    // scanned. For a collection, the subsystems its entries have. "" stands for entries without one; any hidden key is
    // kept so it can be shown again.
    private (List<string> Known, Dictionary<string, int> Lines) SubsystemInventory()
    {
        var known = new SortedSet<string>(StringComparer.Ordinal);
        var lines = new Dictionary<string, int>(StringComparer.Ordinal);
        if (this.buffer is { } source)
        {
            known.UnionWith(source.Subsystems.Names);
            foreach (var (name, count) in source.GetStatistics().Subsystems)
            {
                lines[name ?? OutputLogFilter.OtherSubsystem] = count;
            }
        }
        else if (this.ItemsSource is IEnumerable<OutputLogEntry> items)
        {
            foreach (var entry in items)
            {
                var key = entry.Subsystem ?? OutputLogFilter.OtherSubsystem;
                lines[key] = lines.GetValueOrDefault(key) + 1;
            }
        }

        known.UnionWith(lines.Keys);
        known.UnionWith(this.HiddenSubsystems ?? []);

        // Other sorts last.
        var ordered = known.Where(static key => key.Length > 0).ToList();
        if (known.Contains(OutputLogFilter.OtherSubsystem))
        {
            ordered.Add(OutputLogFilter.OtherSubsystem);
        }

        return (ordered, lines);
    }

    private List<string> KnownSubsystems() => this.SubsystemInventory().Known;

    private void BuildSubsystemsFlyout()
    {
        var (known, lines) = this.SubsystemInventory();
        var hidden = this.HiddenSubsystems ?? [];
        this.syncingSubsystemBoxes = true;
        this.SubsystemsList.Children.Clear();
        this.subsystemBoxes.Clear();
        foreach (var key in known)
        {
            var count = lines.GetValueOrDefault(key);
            var box = this.NewSubsystemBox(key, count, shown: !hidden.Contains(key, StringComparer.Ordinal));
            var row = new Grid();
            row.Children.Add(box);
            if (count > 0)
            {
                row.Children.Add(this.NewLineCount(count));
            }

            this.SubsystemsList.Children.Add(row);
            this.subsystemBoxes.Add((key, box));
        }

        this.AllSubsystemsCount.Text = lines.Values.Sum().ToString("N0", CultureInfo.CurrentCulture);
        this.SyncAllSubsystemsCheckBox();
        this.syncingSubsystemBoxes = false;
    }

    private CheckBox NewSubsystemBox(string key, int count, bool shown)
    {
        var label = new TextBlock { Text = SubsystemLabel(key) };
        if (count == 0 && this.brushes is { } palette)
        {
            // Dimmed only, to draw the eye away: the check box works as any other.
            label.Foreground = palette.Tertiary;
        }

        var box = new CheckBox { Content = label, IsChecked = shown };
        box.Checked += (_, _) => this.OnSubsystemToggled(key, shown: true);
        box.Unchecked += (_, _) => this.OnSubsystemToggled(key, shown: false);
        return box;
    }

    private TextBlock NewLineCount(int count)
    {
        var text = new TextBlock
        {
            Text = count.ToString("N0", CultureInfo.CurrentCulture),
            HorizontalAlignment = HorizontalAlignment.Right,
            VerticalAlignment = VerticalAlignment.Center,
        };
        if (this.brushes is { } palette)
        {
            text.Foreground = palette.Secondary;
        }

        return text;
    }

    private void OnSubsystemToggled(string key, bool shown)
    {
        if (this.syncingSubsystemBoxes)
        {
            return;
        }

        this.SetSubsystemShown(key, shown);
        this.syncingSubsystemBoxes = true;
        this.SyncAllSubsystemsCheckBox();
        this.syncingSubsystemBoxes = false;
    }

    // Checked when every subsystem is shown, clear when none is, mixed otherwise.
    private void SyncAllSubsystemsCheckBox()
    {
        var shown = this.subsystemBoxes.Count(static entry => entry.Box.IsChecked == true);
        bool? all = shown == this.subsystemBoxes.Count ? true : shown == 0 ? false : null;
        this.AllSubsystemsCheckBox.IsChecked = all;
    }

    private void OnAllSubsystemsToggled(bool shown)
    {
        if (this.syncingSubsystemBoxes)
        {
            return;
        }

        this.syncingSubsystemBoxes = true;
        foreach (var (_, box) in this.subsystemBoxes)
        {
            box.IsChecked = shown;
        }

        this.syncingSubsystemBoxes = false;
        this.HiddenSubsystems = shown ? [] : [.. this.subsystemBoxes.Select(static entry => entry.Key)];
    }

    // WinUI's select-all pattern: a click on a checked three-state box moves it to the mixed state; the user meant to
    // clear it, so it is cleared, which hides every subsystem.
    private void OnAllSubsystemsIndeterminate()
    {
        if (!this.syncingSubsystemBoxes)
        {
            this.AllSubsystemsCheckBox.IsChecked = false;
        }
    }

    private void SetSubsystemShown(string key, bool shown)
    {
        var hidden = new HashSet<string>(this.HiddenSubsystems ?? [], StringComparer.Ordinal);
        _ = shown ? hidden.Remove(key) : hidden.Add(key);
        this.HiddenSubsystems = [.. hidden];
    }

    private void ShowOnlySubsystem(string key)
        => this.HiddenSubsystems = [.. this.KnownSubsystems().Where(known => !string.Equals(known, key, StringComparison.Ordinal))];

    private void UpdateSubsystemsSummary()
    {
        var known = this.KnownSubsystems();
        var hidden = this.HiddenSubsystems ?? [];
        var shown = known.Where(key => !hidden.Contains(key, StringComparer.Ordinal)).ToList();
        this.SubsystemsDropDown.Content = shown.Count switch
        {
            _ when shown.Count == known.Count => "Subsystems: All",
            0 => "Subsystems: None",
            1 => "Subsystems: " + SubsystemLabel(shown[0]),
            _ => string.Create(CultureInfo.InvariantCulture, $"Subsystems: {shown.Count} of {known.Count}"),
        };
    }
}
