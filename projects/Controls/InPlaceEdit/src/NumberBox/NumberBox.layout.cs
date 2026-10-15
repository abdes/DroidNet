// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Windows.Foundation;

namespace DroidNet.Controls;

/// <summary>
/// Layout- and measurement-related implementation for the NumberBox control.
/// </summary>
/// <remarks>
///     The widths that decide the layout, of the label, the value field and its annotations, come from text metrics and
///     the template parts' own properties. No template part is measured outside the box's own layout pass, so a
///     decision never leaves a part with layout state from a width it does not get.
/// </remarks>
public partial class NumberBox : IPreferredWidthSource
{
    private FrameworkElement? valueGroup;
    private TextBlock? prefixTextBlock;
    private TextBlock? unitTextBlock;
    private ColumnDefinition? valueColumn;
    private (LabelPosition position, double fixedLabelWidth, bool compact)? appliedLabelLayout;

    /// <inheritdoc />
    /// <remarks>
    ///     The width holds the label when it shows beside the value, the value field with its compact label, and the
    ///     prefix and unit. An automatically stacked label never stacks at this width.
    /// </remarks>
    public double PreferredWidth
    {
        get
        {
            _ = this.ApplyTemplate();
            if (!this.HasLayoutParts)
            {
                return this.MinWidth;
            }

            var fixedLabelWidth = this.ScaledLabelWidth;
            var position = this.EffectiveLabelPosition;
            var compact = this.IsCompact && position is LabelPosition.Left or LabelPosition.Right;
            var valueGroupWidth = this.ValueGroupWidth(compact);
            var oneRowWidth = compact || position == LabelPosition.None
                ? valueGroupWidth
                : position is LabelPosition.Left or LabelPosition.Right
                    ? this.InlineWidth(fixedLabelWidth, valueGroupWidth)
                    : Math.Max(double.IsNaN(fixedLabelWidth) ? this.LabelTextWidth : Math.Min(this.LabelTextWidth, fixedLabelWidth), valueGroupWidth);
            return Math.Max(this.MinWidth, oneRowWidth);
        }
    }

    private bool HasLayoutParts => this.rootGrid is not null && this.labelTextBlock is not null && this.compactLabelTextBlock is not null
        && this.backgroundBorder is not null && this.valueGroup is not null;

    // A fixed label width grows with the text, like the label it holds.
    private double ScaledLabelWidth => this.LabelWidth > 0 ? this.LabelWidth * Math.Max(TextScale.Factor, this.FontSize / 14) : double.NaN;

    private LabelPosition EffectiveLabelPosition => string.IsNullOrWhiteSpace(this.Label) ? LabelPosition.None : this.LabelPosition;

    private double LabelTextWidth => this.labelTextBlock is null ? 0 : TextMeasurer.Width(this.Label, this.labelTextBlock);

    /// <inheritdoc />
    protected override Size MeasureOverride(Size availableSize)
    {
        this.UpdateLabelLayout(availableSize.Width);
        return base.MeasureOverride(availableSize);
    }

    private static ColumnDefinition ValueColumn(double minimumWidth)
        => new() { Width = new GridLength(1, GridUnitType.Star), MinWidth = minimumWidth };

    // An annotation's text and the gap its margin keeps; zero without text.
    private static double AnnotationWidth(TextBlock? annotation, string? text)
        => annotation is null || string.IsNullOrEmpty(text) ? 0 : TextMeasurer.Width(text, annotation) + annotation.Margin.Left + annotation.Margin.Right;

    // Drops the applied layout so the next measure lays the label out again.
    private void UpdateLabelPosition()
    {
        this.appliedLabelLayout = null;
        this.InvalidateMeasure();
    }

    private void UpdateLabelLayout(double availableWidth)
    {
        if (!this.HasLayoutParts)
        {
            return;
        }

        var fixedLabelWidth = this.ScaledLabelWidth;
        var proportional = this.LabelWidthRatio is > 0 and < 1;
        this.labelTextBlock!.TextWrapping = proportional ? TextWrapping.NoWrap : TextWrapping.Wrap;
        this.labelTextBlock.TextTrimming = proportional ? TextTrimming.CharacterEllipsis : TextTrimming.None;
        this.UpdateValueAnnotations();
        var placement = this.SelectLabelPosition(availableWidth, fixedLabelWidth);
        var compact = this.IsCompact && placement is LabelPosition.Left or LabelPosition.Right;
        this.ActualLabelPosition = placement;
        this.rootGrid!.ColumnSpacing = this.LabelSpacing;
        this.rootGrid.RowSpacing = this.LabelRowSpacing;
        var layout = (placement, fixedLabelWidth, compact);
        if (this.appliedLabelLayout is not { } applied || !applied.Equals(layout))
        {
            this.ApplyLabelLayout(placement, fixedLabelWidth, compact);
            this.appliedLabelLayout = layout;
        }

        // The value column keeps room for the whole value group, its prefix and unit included, unless proportional
        // columns share the row; the group's width follows the shown value, so it is refreshed on every measure.
        var horizontal = !compact && placement is LabelPosition.Left or LabelPosition.Right;
        this.valueColumn?.MinWidth = proportional && horizontal ? 0 : this.ValueGroupWidth(compact);
    }

    private void ApplyLabelLayout(LabelPosition placement, double fixedLabelWidth, bool compact)
    {
        this.labelTextBlock!.Visibility = !compact && placement != LabelPosition.None ? Visibility.Visible : Visibility.Collapsed;
        this.compactLabelTextBlock!.Visibility = compact ? Visibility.Visible : Visibility.Collapsed;
        if (compact)
        {
            this.PlaceCompactLabel(labelOnLeft: placement == LabelPosition.Left);
        }

        this.rootGrid!.RowDefinitions.Clear();
        this.rootGrid.ColumnDefinitions.Clear();
        Grid.SetRow(this.labelTextBlock, 0);
        Grid.SetColumn(this.labelTextBlock, 0);
        Grid.SetRow(this.valueGroup!, 0);
        Grid.SetColumn(this.valueGroup!, 0);
        this.valueColumn = ValueColumn(minimumWidth: 0);
        if (compact || placement == LabelPosition.None)
        {
            this.rootGrid.ColumnDefinitions.Add(this.valueColumn);
        }
        else if (placement is LabelPosition.Left or LabelPosition.Right)
        {
            this.LayoutLabelHorizontally(placement, fixedLabelWidth);
        }
        else
        {
            this.LayoutLabelVertically(placement);
        }
    }

    // A compact label sits inside the value field, before or after the value.
    private void PlaceCompactLabel(bool labelOnLeft)
    {
        Grid.SetColumn(this.compactLabelTextBlock!, labelOnLeft ? 0 : 2);
        this.compactLabelTextBlock!.Margin = labelOnLeft ? new Thickness(8, 0, 4, 0) : new Thickness(4, 0, 8, 0);
    }

    private LabelPosition SelectLabelPosition(double availableWidth, double fixedLabelWidth)
    {
        var position = this.EffectiveLabelPosition;
        return this.AutoStackLabel && !this.IsCompact && position is LabelPosition.Left or LabelPosition.Right
            && availableWidth < this.InlineWidth(fixedLabelWidth, this.ValueGroupWidth(compact: false))
            ? LabelPosition.Top
            : position;
    }

    // The width a label beside the value needs, with the value group, the label spacing and any proportional columns.
    private double InlineWidth(double fixedLabelWidth, double valueGroupWidth)
        => this.LabelWidthRatio is > 0 and < 1
            ? this.LabelSpacing + Math.Max((double.IsNaN(fixedLabelWidth) ? 0 : fixedLabelWidth) / this.LabelWidthRatio, valueGroupWidth / (1 - this.LabelWidthRatio))
            : (double.IsNaN(fixedLabelWidth) ? this.LabelTextWidth : fixedLabelWidth) + this.LabelSpacing + valueGroupWidth;

    // The prefix, the value field and the unit, on one row.
    private double ValueGroupWidth(bool compact)
        => AnnotationWidth(this.prefixTextBlock, this.Prefix) + this.FieldWidth(compact) + AnnotationWidth(this.unitTextBlock, this.Unit);

    // The bordered value field: the shown value, at least the width of the mask's widest value, and a compact label.
    private double FieldWidth(bool compact)
    {
        var text = this.valueTextBlock is { } value
            ? Math.Max(value.MinWidth, TextMeasurer.Width(this.DisplayText, value) + value.Padding.Left + value.Padding.Right)
            : 0;
        var compactLabelWidth = compact ? AnnotationWidth(this.compactLabelTextBlock, this.Label) : 0;
        var border = this.backgroundBorder!;
        return text + compactLabelWidth + border.BorderThickness.Left + border.BorderThickness.Right + border.Margin.Left + border.Margin.Right;
    }

    private void UpdateValueAnnotations()
    {
        if (this.prefixTextBlock is { } prefixPart)
        {
            prefixPart.Visibility = string.IsNullOrEmpty(this.Prefix) ? Visibility.Collapsed : Visibility.Visible;
        }

        if (this.unitTextBlock is { } unitPart)
        {
            unitPart.Visibility = string.IsNullOrEmpty(this.Unit) ? Visibility.Collapsed : Visibility.Visible;
        }
    }

    private void LayoutLabelHorizontally(LabelPosition position, double fixedLabelWidth)
    {
        var labelColumn = new ColumnDefinition
        {
            Width = double.IsNaN(fixedLabelWidth) ? new GridLength(1, GridUnitType.Star) : new GridLength(fixedLabelWidth),
        };
        var valueGroupColumn = this.valueColumn!;
        if (this.LabelWidthRatio is > 0 and < 1)
        {
            labelColumn.Width = new GridLength(this.LabelWidthRatio, GridUnitType.Star);
            valueGroupColumn.Width = new GridLength(1 - this.LabelWidthRatio, GridUnitType.Star);
        }

        var labelOnLeft = position == LabelPosition.Left;
        this.rootGrid!.ColumnDefinitions.Add(labelOnLeft ? labelColumn : valueGroupColumn);
        this.rootGrid.ColumnDefinitions.Add(labelOnLeft ? valueGroupColumn : labelColumn);
        Grid.SetColumn(this.labelTextBlock!, labelOnLeft ? 0 : 1);
        Grid.SetColumn(this.valueGroup!, labelOnLeft ? 1 : 0);
    }

    private void LayoutLabelVertically(LabelPosition position)
    {
        this.rootGrid!.ColumnDefinitions.Add(this.valueColumn!);
        this.rootGrid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        this.rootGrid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        var labelOnTop = position == LabelPosition.Top;
        Grid.SetRow(this.labelTextBlock!, labelOnTop ? 0 : 1);
        Grid.SetRow(this.valueGroup!, labelOnTop ? 1 : 0);
    }
}
