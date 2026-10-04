// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Windows.Foundation;
using Windows.UI.ViewManagement;

namespace DroidNet.Controls;

public partial class NumberBox
{
    private readonly UISettings layoutSettings = new();
    private FrameworkElement? valueGroup;
    private TextBlock? prefixTextBlock;
    private TextBlock? qualifierTextBlock;
    private (LabelPosition Position, double LabelWidth, double ValueWidth, bool Compact)? appliedLabelLayout;

    /// <inheritdoc />
    protected override Size MeasureOverride(Size availableSize)
    {
        this.UpdateLabelLayout(availableSize.Width);
        return base.MeasureOverride(availableSize);
    }

    private void UpdateLabelPosition()
    {
        this.appliedLabelLayout = null;
        this.UpdateLabelLayout(this.ActualWidth > 0 ? this.ActualWidth : double.PositiveInfinity);
        this.InvalidateMeasure();
    }

    private void UpdateLabelLayout(double availableWidth)
    {
        if (this.rootGrid is null || this.labelTextBlock is null || this.compactLabelTextBlock is null
            || this.backgroundBorder is null || this.valueGroup is null)
        {
            return;
        }

        var scale = Math.Max(this.layoutSettings.TextScaleFactor, this.FontSize / 14);
        var labelWidth = this.LabelWidth > 0 ? this.LabelWidth * scale : double.NaN;
        var proportional = this.LabelWidthRatio is > 0 and < 1;
        this.labelTextBlock.TextWrapping = proportional ? TextWrapping.NoWrap : TextWrapping.Wrap;
        this.labelTextBlock.TextTrimming = proportional ? TextTrimming.CharacterEllipsis : TextTrimming.None;
        this.UpdateValueAnnotations();
        this.backgroundBorder.Measure(new Size(double.PositiveInfinity, double.PositiveInfinity));
        var valueWidth = Math.Max(this.EditorMinimumWidth * scale, this.backgroundBorder.DesiredSize.Width);
        this.labelTextBlock.Measure(new Size(double.IsNaN(labelWidth) ? double.PositiveInfinity : labelWidth, double.PositiveInfinity));
        var labelPosition = this.SelectLabelPosition(availableWidth, labelWidth, valueWidth);
        var compact = this.IsCompact && labelPosition == LabelPosition.Left;
        this.ActualLabelPosition = labelPosition;
        this.rootGrid.ColumnSpacing = this.LabelSpacing;
        this.rootGrid.RowSpacing = this.LabelRowSpacing;
        var layout = (labelPosition, labelWidth, valueWidth, compact);
        if (this.appliedLabelLayout is { } applied && applied.Equals(layout))
        {
            return;
        }

        this.labelTextBlock.Visibility = !compact && labelPosition != LabelPosition.None ? Visibility.Visible : Visibility.Collapsed;
        this.compactLabelTextBlock.Visibility = compact ? Visibility.Visible : Visibility.Collapsed;
        this.rootGrid.RowDefinitions.Clear();
        this.rootGrid.ColumnDefinitions.Clear();
        Grid.SetRow(this.labelTextBlock, 0);
        Grid.SetColumn(this.labelTextBlock, 0);
        Grid.SetRow(this.valueGroup, 0);
        Grid.SetColumn(this.valueGroup, 0);
        if (compact || labelPosition == LabelPosition.None)
        {
            this.rootGrid.ColumnDefinitions.Add(ValueColumn(valueWidth));
        }
        else if (labelPosition is LabelPosition.Left or LabelPosition.Right)
        {
            this.LayoutLabelHorizontally(labelPosition, labelWidth, valueWidth);
        }
        else
        {
            this.LayoutLabelVertically(labelPosition, valueWidth);
        }

        this.appliedLabelLayout = layout;
    }

    private LabelPosition SelectLabelPosition(double availableWidth, double labelWidth, double valueWidth)
    {
        var position = string.IsNullOrWhiteSpace(this.Label) ? LabelPosition.None : this.LabelPosition;
        if (this.AutoStackLabel && !this.IsCompact && position is LabelPosition.Left or LabelPosition.Right)
        {
            var prefixWidth = this.MeasureAnnotation(this.prefixTextBlock);
            var qualifierWidth = this.MeasureAnnotation(this.qualifierTextBlock);
            var inlineMinimum = (double.IsNaN(labelWidth) ? this.labelTextBlock!.DesiredSize.Width : labelWidth)
                + this.LabelSpacing + valueWidth + prefixWidth + qualifierWidth;
            if (this.LabelWidthRatio is > 0 and < 1)
            {
                inlineMinimum = this.LabelSpacing + Math.Max(
                    (double.IsNaN(labelWidth) ? 0 : labelWidth) / this.LabelWidthRatio,
                    (valueWidth + prefixWidth + qualifierWidth) / (1 - this.LabelWidthRatio));
            }

            if (availableWidth < inlineMinimum)
            {
                return LabelPosition.Top;
            }
        }

        return position;
    }

    private void UpdateValueAnnotations()
    {
        if (this.prefixTextBlock is { } prefix)
        {
            prefix.Visibility = string.IsNullOrEmpty(this.Prefix) ? Visibility.Collapsed : Visibility.Visible;
        }

        if (this.qualifierTextBlock is { } qualifier)
        {
            qualifier.Visibility = string.IsNullOrEmpty(this.Qualifier) ? Visibility.Collapsed : Visibility.Visible;
            qualifier.MinWidth = string.IsNullOrEmpty(this.Qualifier) ? 0 : this.QualifierMinimumWidth;
        }
    }

    private double MeasureAnnotation(TextBlock? annotation)
    {
        if (annotation is null || annotation.Visibility == Visibility.Collapsed)
        {
            return 0;
        }

        annotation.Measure(new Size(double.PositiveInfinity, double.PositiveInfinity));
        return annotation.DesiredSize.Width;
    }

    private void LayoutLabelHorizontally(LabelPosition position, double labelWidth, double valueWidth)
    {
        var labelColumn = new ColumnDefinition
        {
            Width = double.IsNaN(labelWidth) ? new GridLength(1, GridUnitType.Star) : new GridLength(labelWidth),
        };
        var valueColumn = ValueColumn(valueWidth);
        if (this.LabelWidthRatio is > 0 and < 1)
        {
            labelColumn.Width = new GridLength(this.LabelWidthRatio, GridUnitType.Star);
            valueColumn.Width = new GridLength(1 - this.LabelWidthRatio, GridUnitType.Star);
            valueColumn.MinWidth = 0;
        }

        var labelOnLeft = position == LabelPosition.Left;
        this.rootGrid!.ColumnDefinitions.Add(labelOnLeft ? labelColumn : valueColumn);
        this.rootGrid.ColumnDefinitions.Add(labelOnLeft ? valueColumn : labelColumn);
        Grid.SetColumn(this.labelTextBlock!, labelOnLeft ? 0 : 1);
        Grid.SetColumn(this.valueGroup!, labelOnLeft ? 1 : 0);
    }

    private void LayoutLabelVertically(LabelPosition position, double valueWidth)
    {
        this.rootGrid!.ColumnDefinitions.Add(ValueColumn(valueWidth));
        this.rootGrid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        this.rootGrid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        var labelOnTop = position == LabelPosition.Top;
        Grid.SetRow(this.labelTextBlock!, labelOnTop ? 0 : 1);
        Grid.SetRow(this.valueGroup!, labelOnTop ? 1 : 0);
    }

    private static ColumnDefinition ValueColumn(double minimumWidth)
        => new() { Width = new GridLength(1, GridUnitType.Star), MinWidth = minimumWidth };
}
