// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using DroidNet.Controls.OutputConsole.Model;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Shapes;
using Serilog.Events;

namespace DroidNet.Controls.OutputConsole;

/// <summary>
///     The buffer ring on the toolbar and its flyout: how full the buffer is, what it retains and what this view
///     shows.
/// </summary>
public sealed partial class OutputConsoleView
{
    private bool bufferFlyoutShown;

    private static double Percent(OutputLogStatistics statistics) => statistics.Count * 100.0 / statistics.Capacity;

    private void UpdateBufferStatus()
    {
        if (this.buffer is not { } source || this.brushes is not { } palette)
        {
            return;
        }

        var statistics = source.GetStatistics();
        var evicting = statistics.EvictedCount > 0;
        this.BufferRing.Maximum = statistics.Capacity;
        this.BufferRing.Value = statistics.Count;
        this.BufferRing.Foreground = evicting ? palette.Caution : palette.Accent;
        ToolTipService.SetToolTip(
            this.BufferButton,
            string.Create(
                CultureInfo.CurrentCulture,
                $"Buffer: {Percent(statistics):0}% ({statistics.Count:N0} of {statistics.Capacity:N0})"));

        // Flyout.IsOpen is still false while the flyout is opening, so the view tracks it.
        if (this.bufferFlyoutShown)
        {
            this.FillBufferFlyout(statistics, palette);
        }
    }

    private void OnBufferFlyoutShown(bool shown)
    {
        this.bufferFlyoutShown = shown;
        if (shown)
        {
            this.UpdateBufferStatus();
        }
    }

    private void FillBufferFlyout(OutputLogStatistics statistics, ConsoleBrushes palette)
    {
        var culture = CultureInfo.CurrentCulture;
        this.BufferPercentText.Text = string.Create(culture, $"{Percent(statistics):0.0}%");
        this.BufferRetainedText.Text = string.Create(culture, $"{statistics.Count:N0} of {statistics.Capacity:N0} entries retained");
        this.BufferBar.Maximum = statistics.Capacity;
        this.BufferBar.Value = statistics.Count;
        if (statistics.EvictedCount > 0)
        {
            this.BufferHeadroomText.Text = string.Create(culture, $"{statistics.EvictedCount:N0} older entries overwritten since the last clear");
            this.BufferHeadroomText.Foreground = palette.Caution;
        }
        else
        {
            this.BufferHeadroomText.Text = string.Create(culture, $"{statistics.Capacity - statistics.Count:N0} entries until the oldest are overwritten");
            this.BufferHeadroomText.Foreground = palette.Tertiary;
        }

        this.BufferLevelCounts.Children.Clear();
        _ = this.AddLevelCount(statistics.CountOf(LogEventLevel.Error) + statistics.CountOf(LogEventLevel.Fatal), "error", "errors", palette.Critical);
        _ = this.AddLevelCount(statistics.CountOf(LogEventLevel.Warning), "warning", "warnings", palette.Caution);
        _ = this.AddLevelCount(statistics.CountOf(LogEventLevel.Information), "info", "info", palette.Secondary);
        var debug = this.AddLevelCount(statistics.CountOf(LogEventLevel.Debug) + statistics.CountOf(LogEventLevel.Verbose), "debug/verbose", "debug/verbose", palette.Tertiary);
        VariableSizedWrapGrid.SetColumnSpan(debug, 2);

        var entries = this.viewItems.Sum(static row => row.Count);
        var folded = entries - this.viewItems.Count;
        this.BufferViewText.Text = folded > 0
            ? string.Create(culture, $"{entries:N0} entries match the filters, shown as {this.viewItems.Count:N0} rows ({folded:N0} folded)")
            : string.Create(culture, $"{entries:N0} entries match the filters");

        var pending = this.PendingCount();
        this.BufferPausedText.Visibility = this.IsPaused ? Visibility.Visible : Visibility.Collapsed;
        this.BufferPausedText.Text = string.Create(culture, $"Paused: {pending:N0} new entries not shown");
    }

    private StackPanel AddLevelCount(int count, string singular, string plural, Brush dot)
    {
        var item = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 6, VerticalAlignment = VerticalAlignment.Center };
        item.Children.Add(new Ellipse { Width = 8, Height = 8, Fill = dot, VerticalAlignment = VerticalAlignment.Center });
        item.Children.Add(new TextBlock
        {
            Text = string.Create(CultureInfo.CurrentCulture, $"{count:N0} {(count == 1 ? singular : plural)}"),
        });
        this.BufferLevelCounts.Children.Add(item);
        return item;
    }
}
