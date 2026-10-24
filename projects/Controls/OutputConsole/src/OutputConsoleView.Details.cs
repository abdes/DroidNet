// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Text;
using DroidNet.Controls.OutputConsole.Model;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Documents;
using Microsoft.UI.Xaml.Input;

namespace DroidNet.Controls.OutputConsole;

/// <summary>
///     The details pane: the full message, exception and properties of the selected line.
/// </summary>
public sealed partial class OutputConsoleView
{
    /// <summary>The default <see cref="DetailsSplitRatio" />: the message takes 60% of the details pane, the properties 40%.</summary>
    public const double DefaultDetailsSplitRatio = 0.6;

    /// <summary>The smallest <see cref="DetailsSplitRatio" />.</summary>
    public const double MinDetailsSplitRatio = 0.2;

    /// <summary>The largest <see cref="DetailsSplitRatio" />.</summary>
    public const double MaxDetailsSplitRatio = 0.8;

    // A split that moved less than this is the layout rounding, not the user dragging the splitter.
    private const double SplitRatioTolerance = 0.005;

    private const double DefaultDetailsHeight = 200;
    private const double MinDetailsHeight = 96;

    // Shown elsewhere in the pane, so not listed again with the other properties.
    private static readonly HashSet<string> ShownProperties =
    [
        with(StringComparer.Ordinal),
        OutputLogEntry.SourcePropertyName,
        OutputLogEntry.ChannelPropertyName,
        "Thread",
        "File",
        "Line",
    ];

    private double detailsHeight = DefaultDetailsHeight;
    private OutputLogRow? detailsRow;
    private bool applyingDetailsSplit;

    private void WireDetailsPane()
    {
        this.CopyDetailsButton.Click += (_, _) => this.CopyDetails();
        this.CloseDetailsButton.Click += (_, _) => this.IsDetailsPaneOpen = false;
        this.DetailsBodyScroller.SizeChanged += (_, _) => this.RecordDetailsSplit();
        this.ApplyDetailsSplit();
    }

    // Star widths keep the split as the pane resizes; the splitter's drag lands in RecordDetailsSplit.
    private void ApplyDetailsSplit()
    {
        var ratio = double.IsFinite(this.DetailsSplitRatio)
            ? Math.Clamp(this.DetailsSplitRatio, MinDetailsSplitRatio, MaxDetailsSplitRatio)
            : DefaultDetailsSplitRatio;
        this.applyingDetailsSplit = true;
        this.DetailsBodyColumn.Width = new GridLength(ratio, GridUnitType.Star);
        this.DetailsPropertiesColumn.Width = new GridLength(1 - ratio, GridUnitType.Star);
        this.applyingDetailsSplit = false;
    }

    // Reads the split the user dragged to, so the host can keep it.
    private void RecordDetailsSplit()
    {
        var total = this.DetailsBodyColumn.ActualWidth + this.DetailsPropertiesColumn.ActualWidth;
        if (this.applyingDetailsSplit || !this.IsDetailsPaneOpen || total <= 0)
        {
            return;
        }

        var ratio = Math.Clamp(this.DetailsBodyColumn.ActualWidth / total, MinDetailsSplitRatio, MaxDetailsSplitRatio);
        if (Math.Abs(ratio - this.DetailsSplitRatio) > SplitRatioTolerance)
        {
            this.DetailsSplitRatio = Math.Round(ratio, 3, MidpointRounding.ToEven);
        }
    }

    private void ShowDetails()
    {
        if (this.lastSelectedRow is not null)
        {
            this.IsDetailsPaneOpen = true;
        }
    }

    private void ShowDetails(OutputLogRow row)
    {
        this.List.SelectedItem = row;
        this.IsDetailsPaneOpen = true;
    }

    private void OnShowDetailsAccelerator(KeyboardAccelerator sender, KeyboardAcceleratorInvokedEventArgs e)
    {
        e.Handled = true;
        this.ShowDetails();
    }

    private void OnCloseDetailsAccelerator(KeyboardAccelerator sender, KeyboardAcceleratorInvokedEventArgs e)
    {
        e.Handled = this.IsDetailsPaneOpen;
        this.IsDetailsPaneOpen = false;
    }

    private void OnIsDetailsPaneOpenChanged(bool open)
    {
        if (!open && this.DetailsRow.ActualHeight >= MinDetailsHeight)
        {
            this.detailsHeight = this.DetailsRow.ActualHeight;
        }

        this.DetailsRow.MinHeight = open ? MinDetailsHeight : 0;
        this.DetailsRow.Height = new GridLength(open ? this.detailsHeight : 0);
        this.DetailsPane.Visibility = open ? Visibility.Visible : Visibility.Collapsed;
        this.DetailsSplitter.Visibility = this.DetailsPane.Visibility;
        this.RefreshDetails();
    }

    private void RefreshDetails()
    {
        if (!this.IsDetailsPaneOpen || this.brushes is null)
        {
            return;
        }

        this.detailsRow = this.lastSelectedRow;
        this.CopyDetailsButton.IsEnabled = this.detailsRow is not null;
        this.DetailsHeader.Inlines.Clear();
        this.DetailsBody.Blocks.Clear();
        this.DetailsBody.TextHighlighters.Clear();
        this.DetailsProperties.Children.Clear();
        this.DetailsProperties.RowDefinitions.Clear();

        if (this.detailsRow is not { } row)
        {
            this.DetailsHeader.Inlines.Add(new Run { Text = "Select a line to see its details.", Foreground = this.brushes.Tertiary });
            return;
        }

        this.FillDetailsHeader(row.Last);
        this.FillDetailsBody(row.Last);
        this.FillDetailsProperties(row);
    }

    private void FillDetailsHeader(OutputLogEntry entry)
    {
        var palette = this.brushes!;

        // The row's prefix, always with the time: [18:13:56.503 INF (Router)].
        var header = this.DetailsHeader.Inlines;
        header.Add(new Run { Text = "[" + ConsoleColumns.TimePart(entry.Timestamp), Foreground = palette.Tertiary });
        header.Add(new Run { Text = OutputLogFormatter.LevelTag(entry.Level), Foreground = palette.TagFor(entry.Level) });
        header.Add(new Run { Text = ConsoleColumns.SubsystemPart(entry.Subsystem), Foreground = palette.Secondary });
        header.Add(new Run { Text = "]", Foreground = palette.Tertiary });
    }

    private void FillDetailsBody(OutputLogEntry entry)
    {
        var palette = this.brushes!;
        var paragraph = new Paragraph();
        var content = new StringBuilder(entry.Message).Append('\n');
        paragraph.Inlines.Add(new Run { Text = content.ToString(), Foreground = palette.MessageFor(entry.Level) });

        if (entry.ExceptionText is { } exception)
        {
            var lines = exception.ReplaceLineEndings("\n").Split('\n');
            for (var i = 0; i < lines.Length; i++)
            {
                var line = lines[i] + "\n";
                var brush = i == 0
                    ? palette.Critical
                    : lines[i].TrimStart().StartsWith("---", StringComparison.Ordinal) ? palette.Tertiary : palette.Secondary;
                paragraph.Inlines.Add(new Run { Text = line, Foreground = brush });
                _ = content.Append(line);
            }
        }

        this.DetailsBody.Blocks.Add(paragraph);
        if (TextHighlights.Create(content.ToString(), this.SearchText, ReferenceEquals(this.detailsRow, this.currentMatch), palette) is { } highlighter)
        {
            this.DetailsBody.TextHighlighters.Add(highlighter);
        }
    }

    private void FillDetailsProperties(OutputLogRow row)
    {
        var entry = row.Last;
        this.AddDetailsProperty("Time", entry.Timestamp.ToString("yyyy-MM-dd HH:mm:ss.fff zzz", CultureInfo.InvariantCulture));
        this.AddDetailsProperty("Source", entry.Source);
        this.AddDetailsProperty("Channel", entry.Channel);
        this.AddDetailsProperty("Thread", PropertyText(entry, "Thread"));
        var file = PropertyText(entry, "File");
        var line = PropertyText(entry, "Line");
        this.AddDetailsProperty("File", file is null ? null : line is null ? file : file + ":" + line);

        foreach (var (key, value) in entry.Properties.OrderBy(static pair => pair.Key, StringComparer.Ordinal))
        {
            if (!ShownProperties.Contains(key))
            {
                this.AddDetailsProperty(key, OutputLogFormatter.FormatValue(value));
            }
        }

        if (row.Count > 1)
        {
            this.AddDetailsProperty("Occurrences", row.Count.ToString(CultureInfo.CurrentCulture));
            this.AddDetailsProperty("First", ConsoleColumns.Time(row.First.Timestamp));
            this.AddDetailsProperty("Last", ConsoleColumns.Time(row.Last.Timestamp));
        }

        static string? PropertyText(OutputLogEntry entry, string key)
            => entry.Properties.TryGetValue(key, out var value) && value is not null
                ? Convert.ToString(value, CultureInfo.InvariantCulture)
                : null;
    }

    private void AddDetailsProperty(string key, string? value)
    {
        if (value is null)
        {
            return;
        }

        var row = this.DetailsProperties.RowDefinitions.Count;
        this.DetailsProperties.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });

        var keyText = new TextBlock { Text = key, Foreground = this.brushes!.Tertiary };
        Grid.SetRow(keyText, row);
        var valueText = new TextBlock { Text = value, TextWrapping = TextWrapping.Wrap, IsTextSelectionEnabled = true };
        Grid.SetRow(valueText, row);
        Grid.SetColumn(valueText, 1);

        this.DetailsProperties.Children.Add(keyText);
        this.DetailsProperties.Children.Add(valueText);
    }
}
