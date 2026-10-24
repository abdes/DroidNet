// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using DroidNet.Controls.OutputConsole.Model;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Documents;
using Microsoft.UI.Xaml.Input;

namespace DroidNet.Controls.OutputConsole;

/// <summary>
///     The presentation of one <see cref="OutputLogRow" />: exception glyph, the prefix
///     <c>[18:13:56.503 INF (Router)]</c>, fold badge and message. The prefix sits in a fixed-width column shared by
///     every row, so messages line up. Built in code, with no bindings, and its prefix runs are reused, so realizing a
///     row while scrolling costs only a few property sets.
/// </summary>
internal sealed partial class OutputLogRowView : Grid
{
    private const double GutterWidth = 18;
    private const double BadgeGap = 8;

    private readonly FontIcon exceptionGlyph;
    private readonly TextBlock prefix;
    private readonly Run open = new() { Text = "[" };
    private readonly Run time = new();
    private readonly Run level = new();
    private readonly Run subsystem = new();
    private readonly Run close = new() { Text = "]" };
    private readonly Border foldBadge;
    private readonly TextBlock foldCount;
    private readonly TextBlock message;
    private ConsoleColumns? appliedColumns;
    private Action<OutputLogRow>? requestDetails;

    /// <summary>
    ///     Initializes a new instance of the <see cref="OutputLogRowView" /> class.
    /// </summary>
    public OutputLogRowView()
    {
        this.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(GutterWidth) });
        this.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        this.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
        this.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });

        this.exceptionGlyph = new FontIcon
        {
            Glyph = "",
            FontSize = 12,
            Visibility = Visibility.Collapsed,
            VerticalAlignment = VerticalAlignment.Center,
            HorizontalAlignment = HorizontalAlignment.Left,
        };
        ToolTipService.SetToolTip(this.exceptionGlyph, "Has an exception. Press Enter for details.");
        this.exceptionGlyph.Tapped += this.OnExceptionGlyphTapped;

        this.prefix = NewCell(1);
        this.prefix.Inlines.Add(this.open);
        this.prefix.Inlines.Add(this.time);
        this.prefix.Inlines.Add(this.level);
        this.prefix.Inlines.Add(this.subsystem);
        this.prefix.Inlines.Add(this.close);

        this.foldCount = new TextBlock { FontSize = 11 };
        this.foldBadge = new Border
        {
            CornerRadius = new CornerRadius(4),
            Padding = new Thickness(4, 0, 4, 0),
            Margin = new Thickness(0, 1, BadgeGap, 1),
            VerticalAlignment = VerticalAlignment.Center,
            Visibility = Visibility.Collapsed,
            Child = this.foldCount,
        };
        SetColumn(this.foldBadge, 2);

        this.message = NewCell(3);

        this.Children.Add(this.exceptionGlyph);
        this.Children.Add(this.prefix);
        this.Children.Add(this.foldBadge);
        this.Children.Add(this.message);
    }

    /// <summary>
    ///     Gets the row currently shown.
    /// </summary>
    public OutputLogRow? Row { get; private set; }

    /// <summary>
    ///     Shows a row.
    /// </summary>
    /// <param name="row">The row.</param>
    /// <param name="columns">The shared column layout.</param>
    /// <param name="brushes">The theme brushes.</param>
    /// <param name="requestDetails">Called when the user taps the exception glyph.</param>
    public void Bind(OutputLogRow row, ConsoleColumns columns, ConsoleBrushes brushes, Action<OutputLogRow> requestDetails)
    {
        ArgumentNullException.ThrowIfNull(row);
        ArgumentNullException.ThrowIfNull(columns);
        ArgumentNullException.ThrowIfNull(brushes);

        this.Row = row;
        this.requestDetails = requestDetails;
        this.ApplyColumns(columns);

        var first = row.First;
        this.exceptionGlyph.Visibility = first.Exception is null ? Visibility.Collapsed : Visibility.Visible;
        this.exceptionGlyph.Foreground = brushes.Critical;

        this.open.Foreground = brushes.Tertiary;
        this.close.Foreground = brushes.Tertiary;
        this.time.Foreground = brushes.Tertiary;
        this.time.Text = columns.ShowTimestamps ? ConsoleColumns.TimePart(row.Last.Timestamp) : string.Empty;
        this.level.Foreground = brushes.TagFor(first.Level);
        this.level.Text = OutputLogFormatter.LevelTag(first.Level);
        this.subsystem.Foreground = brushes.Secondary;
        this.subsystem.Text = columns.ShowSubsystem ? ConsoleColumns.SubsystemPart(first.Subsystem) : string.Empty;

        this.BindFold(row, brushes);

        this.message.Text = first.Message;
        this.message.Foreground = brushes.MessageFor(first.Level);
        this.message.TextHighlighters.Clear();
    }

    /// <summary>
    ///     Highlights every occurrence of the search text in the message.
    /// </summary>
    /// <param name="text">The trimmed search text; nothing is highlighted when empty.</param>
    /// <param name="isCurrent">Whether the row is the current find match.</param>
    /// <param name="brushes">The theme brushes.</param>
    public void Highlight(string text, bool isCurrent, ConsoleBrushes brushes)
    {
        ArgumentNullException.ThrowIfNull(brushes);
        this.message.TextHighlighters.Clear();
        if (TextHighlights.Create(this.message.Text, text, isCurrent, brushes) is { } highlighter)
        {
            this.message.TextHighlighters.Add(highlighter);
        }
    }

    private static TextBlock NewCell(int column)
    {
        var cell = new TextBlock
        {
            TextWrapping = TextWrapping.NoWrap,
            VerticalAlignment = VerticalAlignment.Top,
        };
        SetColumn(cell, column);
        return cell;
    }

    private void ApplyColumns(ConsoleColumns columns)
    {
        if (ReferenceEquals(columns, this.appliedColumns))
        {
            return;
        }

        this.appliedColumns = columns;
        this.ColumnDefinitions[1].Width = new GridLength(columns.PrefixWidth);
        this.message.TextWrapping = columns.WordWrap ? TextWrapping.Wrap : TextWrapping.NoWrap;
    }

    private void BindFold(OutputLogRow row, ConsoleBrushes brushes)
    {
        if (row.Count == 1)
        {
            this.foldBadge.Visibility = Visibility.Collapsed;
            ToolTipService.SetToolTip(this.foldBadge, value: null);
            return;
        }

        this.foldBadge.Visibility = Visibility.Visible;
        this.foldBadge.Background = brushes.BadgeBackground;
        this.foldCount.Foreground = brushes.Secondary;
        this.foldCount.Text = "×" + row.Count.ToString(CultureInfo.InvariantCulture);
        ToolTipService.SetToolTip(
            this.foldBadge,
            string.Create(
                CultureInfo.InvariantCulture,
                $"{row.Count} occurrences from {ConsoleColumns.Time(row.First.Timestamp)} to {ConsoleColumns.Time(row.Last.Timestamp)}"));
    }

    private void OnExceptionGlyphTapped(object sender, TappedRoutedEventArgs e)
    {
        if (this.Row is { } row && this.requestDetails is { } request)
        {
            e.Handled = true;
            request(row);
        }
    }
}
