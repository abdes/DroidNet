// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Routing.Debugger.UI.Styles;

/// <summary>
/// A custom control to display an item property.
/// </summary>
public partial class ItemProperty : Grid
{
    /// <summary>
    /// Identifies the PropertyName dependency property.
    /// </summary>
    public static readonly DependencyProperty PropertyNameProperty = DependencyProperty.Register(
        nameof(PropertyName),
        typeof(string),
        typeof(ItemProperty),
        new PropertyMetadata(default(string), OnPropertyNameChanged));

    /// <summary>
    /// Identifies the PropertyValue dependency property.
    /// </summary>
    public static readonly DependencyProperty PropertyValueProperty = DependencyProperty.Register(
        nameof(PropertyValue),
        typeof(string),
        typeof(ItemProperty),
        new PropertyMetadata(default(string), OnPropertyValueChanged));

    private const string ItemPropertyValueStyleKey = "ItemPropertyValue";
    private const string ItemPropertyNameStyleKey = "ItemPropertyName";

    private readonly TextBlock nameTextBlock;
    private readonly TextBlock valueTextBlock;

    /// <summary>
    /// Initializes a new instance of the <see cref="ItemProperty"/> class.
    /// Sets the style and column definitions, and initializes the text blocks for property name and value.
    /// </summary>
    public ItemProperty()
    {
        this.Style = (Style)Application.Current.Resources[nameof(ItemProperty)];

        this.ColumnDefinitions.Add(
            new ColumnDefinition()
            {
                Width = new GridLength(2, GridUnitType.Star),
            });
        this.ColumnDefinitions.Add(
            new ColumnDefinition()
            {
                Width = new GridLength(3, GridUnitType.Star),
            });

        this.nameTextBlock = MakeCell(0);
        this.Children.Add(this.nameTextBlock);

        this.valueTextBlock = MakeCell(1);
        this.Children.Add(this.valueTextBlock);
    }

    /// <summary>
    /// Gets or sets the value of the property.
    /// </summary>
    public string PropertyValue
    {
        get => (string)this.GetValue(PropertyValueProperty);
        set => this.SetValue(PropertyValueProperty, value);
    }

    /// <summary>
    /// Gets or sets the name of the property.
    /// </summary>
    public string PropertyName
    {
        get => (string)this.GetValue(PropertyNameProperty);
        set => this.SetValue(PropertyNameProperty, value);
    }

    /// <summary>
    /// Creates a TextBlock for a specified column with appropriate style.
    /// </summary>
    /// <param name="column">The column index where the TextBlock will be placed.</param>
    /// <returns>A new instance of <see cref="TextBlock"/> with the specified style and column settings.</returns>
    private static TextBlock MakeCell(int column)
    {
        var content = new TextBlock()
        {
            Text = string.Empty,
            Style = (Style)Application.Current.Resources[column == 0
                ? ItemPropertyNameStyleKey
                : ItemPropertyValueStyleKey],
        };
        content.SetValue(RowProperty, 0);
        content.SetValue(ColumnProperty, column);

        return content;
    }

    private static void OnPropertyNameChanged(DependencyObject d, DependencyPropertyChangedEventArgs args)
    {
        var control = (ItemProperty)d;
        control.nameTextBlock.Text = args.NewValue?.ToString() ?? string.Empty;
    }

    private static void OnPropertyValueChanged(DependencyObject d, DependencyPropertyChangedEventArgs args)
    {
        var control = (ItemProperty)d;
        control.valueTextBlock.Text = args.NewValue?.ToString() ?? string.Empty;
    }
}
