// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Data;
using Windows.Foundation;
using NumberBox = DroidNet.Controls.NumberBox;

namespace Oxygen.Editor.Controls;

public partial class PropertyCard
{
    private readonly List<EditorPropertySnapshot> editorPropertySnapshots = [];
    private NumberBox? scalarEditor;

    private bool MeasureScalarEditor(Size availableSize)
    {
        this.SynchronizeScalarEditor();
        if (this.scalarEditor is not { } number)
        {
            return false;
        }

        number.AutoStackLabel = this.IsHeaderVisible && this.Layout == PropertyLayout.Auto;
        number.LabelPosition = !this.IsHeaderVisible ? LabelPosition.None
            : this.Layout == PropertyLayout.Stacked ? LabelPosition.Top : LabelPosition.Left;
        this.header!.Visibility = Visibility.Collapsed;
        this.headerQualifier!.Visibility = Visibility.Collapsed;
        this.prefix!.Visibility = Visibility.Collapsed;
        this.suffix!.Visibility = Visibility.Collapsed;
        if (this.leading is not null)
        {
            this.leading.Visibility = Visibility.Collapsed;
        }

        if (this.appliedLayout is null)
        {
            this.layoutRoot!.ColumnDefinitions.Clear();
            this.layoutRoot.RowDefinitions.Clear();
            this.layoutRoot.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            this.layoutRoot.ColumnSpacing = 0;
            this.layoutRoot.RowSpacing = 0;
            var valueGroup = (FrameworkElement)this.editor!.Parent;
            Grid.SetColumn(valueGroup, 0);
            Grid.SetRow(valueGroup, 0);
        }

        var width = availableSize.Width - this.Padding.Left - this.Padding.Right;
        this.editor!.Measure(new Size(Math.Max(0, width), availableSize.Height));
        this.ActualLayout = number.ActualLabelPosition is LabelPosition.Top or LabelPosition.Bottom
            ? PropertyLayout.Stacked : PropertyLayout.Inline;
        this.appliedLayout = this.ActualLayout;
        return true;
    }

    private void SynchronizeScalarEditor()
    {
        var candidate = this.FindScalarEditor();
        if (!ReferenceEquals(candidate, this.scalarEditor))
        {
            this.RestoreScalarEditor();
            this.scalarEditor = candidate;
            this.appliedLayout = null;
            if (candidate is not null)
            {
                this.BindScalarMetadata(candidate);
            }
        }
    }

    private NumberBox? FindScalarEditor()
    {
        if (!this.UseEditorLabel || this.IsCompound || this.LeadingContent is not null || this.ContentTemplate is not null)
        {
            return null;
        }

        if (this.Content is NumberBox number)
        {
            return number;
        }

        // A diagnostics panel is scalar; never traverse into vectors or multi-input compositions.
        if (this.Content is Panel panel && panel.Children.All(child => child is NumberBox or TextBlock))
        {
            var numbers = panel.Children.OfType<NumberBox>().ToArray();
            return numbers.Length == 1 ? numbers[0] : null;
        }

        return null;
    }

    private void BindScalarMetadata(NumberBox number)
    {
        this.BindEditorProperty(number, NumberBox.LabelProperty, nameof(this.PropertyName));
        this.BindEditorProperty(number, NumberBox.LabelWidthProperty, nameof(this.LabelWidth));
        this.BindEditorProperty(number, NumberBox.EditorMinimumWidthProperty, nameof(this.EditorMinimumWidth));
        this.BindEditorProperty(number, NumberBox.PrefixProperty, nameof(this.Prefix));
        this.BindEditorProperty(number, NumberBox.QualifierProperty, nameof(this.Qualifier));
        this.BindEditorProperty(number, NumberBox.QualifierMinimumWidthProperty, nameof(this.QualifierMinimumWidth));
        this.BindEditorProperty(number, NumberBox.LabelForegroundProperty, nameof(this.Foreground));
        this.BindEditorProperty(number, FontSizeProperty, nameof(this.FontSize));
        this.SetEditorProperty(number, NumberBox.LabelSpacingProperty, 12d);
        this.SetEditorProperty(number, NumberBox.LabelRowSpacingProperty, 4d);
        this.SaveEditorProperty(number, NumberBox.AutoStackLabelProperty);
        this.SaveEditorProperty(number, NumberBox.LabelPositionProperty);
    }

    private void BindEditorProperty(NumberBox number, DependencyProperty property, string sourceProperty)
    {
        this.SaveEditorProperty(number, property);
        number.SetBinding(property, new Binding { Source = this, Path = new PropertyPath(sourceProperty), Mode = BindingMode.OneWay });
    }

    private void SetEditorProperty(NumberBox number, DependencyProperty property, object value)
    {
        this.SaveEditorProperty(number, property);
        number.SetValue(property, value);
    }

    private void SaveEditorProperty(NumberBox number, DependencyProperty property)
        => this.editorPropertySnapshots.Add(new EditorPropertySnapshot(property, number.ReadLocalValue(property), number.GetBindingExpression(property)?.ParentBinding));

    private void RestoreScalarEditor()
    {
        if (this.scalarEditor is not { } number)
        {
            return;
        }

        foreach (var snapshot in this.editorPropertySnapshots)
        {
            if (snapshot.Binding is { } binding)
            {
                number.SetBinding(snapshot.Property, binding);
            }
            else if (ReferenceEquals(snapshot.Value, DependencyProperty.UnsetValue))
            {
                number.ClearValue(snapshot.Property);
            }
            else
            {
                number.SetValue(snapshot.Property, snapshot.Value);
            }
        }

        this.editorPropertySnapshots.Clear();
    }

    private sealed record EditorPropertySnapshot(DependencyProperty Property, object Value, Binding? Binding);
}
