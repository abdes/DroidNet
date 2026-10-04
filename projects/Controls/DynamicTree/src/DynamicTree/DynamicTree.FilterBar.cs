// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;

namespace DroidNet.Controls;

/// <summary>
/// Partial implementation of the DynamicTree control that contains the filter-bar
/// related dependency properties and UI helpers.
/// </summary>
public partial class DynamicTree
{
    /// <summary>Identifies the opt-in filter bar capability.</summary>
    public static readonly DependencyProperty IsFilterBarEnabledProperty = DependencyProperty.Register(
        nameof(IsFilterBarEnabled),
        typeof(bool),
        typeof(DynamicTree),
        new PropertyMetadata(defaultValue: false, (d, _) => ((DynamicTree)d).UpdateFilterBar()));

    /// <summary>Identifies the requested filter bar visibility.</summary>
    public static readonly DependencyProperty IsFilterBarVisibleProperty = DependencyProperty.Register(
        nameof(IsFilterBarVisible),
        typeof(bool),
        typeof(DynamicTree),
        new PropertyMetadata(defaultValue: true, (d, _) => ((DynamicTree)d).UpdateFilterBar()));

    /// <summary>Identifies the text entered in the filter bar.</summary>
    public static readonly DependencyProperty FilterTextProperty = DependencyProperty.Register(
        nameof(FilterText),
        typeof(string),
        typeof(DynamicTree),
        new PropertyMetadata(string.Empty, (d, _) => ((DynamicTree)d).UpdateFilterBar()));

    /// <summary>Identifies the filter input placeholder.</summary>
    public static readonly DependencyProperty FilterBarPlaceholderTextProperty = DependencyProperty.Register(
        nameof(FilterBarPlaceholderText), typeof(string), typeof(DynamicTree), new PropertyMetadata("Filter"));

    /// <summary>Identifies optional application-specific filter bar content.</summary>
    public static readonly DependencyProperty FilterBarAccessoryContentProperty = DependencyProperty.Register(
        nameof(FilterBarAccessoryContent), typeof(object), typeof(DynamicTree), new PropertyMetadata(defaultValue: null));

    /// <summary>Identifies the consumer-selected native filter input style.</summary>
    public static readonly DependencyProperty FilterBarInputStyleProperty = DependencyProperty.Register(
        nameof(FilterBarInputStyle),
        typeof(Style),
        typeof(DynamicTree),
        new PropertyMetadata(defaultValue: null, (d, _) => ((DynamicTree)d).UpdateFilterBar()));

    private FrameworkElement? filterBar;
    private TextBox? filterTextBox;
    private FontIcon? filterSearchIcon;

    /// <summary>Gets or sets a value indicating whether gets or sets whether the built-in filter bar is available. Defaults to false.</summary>
    /// <remarks>This does not change IsFilteringEnabled or assign a filter predicate.</remarks>
    public bool IsFilterBarEnabled
    {
        get => (bool)this.GetValue(IsFilterBarEnabledProperty);
        set => this.SetValue(IsFilterBarEnabledProperty, value);
    }

    /// <summary>Gets or sets a value indicating whether gets or sets whether an enabled filter bar is visible. Defaults to true.</summary>
    /// <remarks>Hiding the bar consumes no layout space and preserves its text and the current filter.</remarks>
    public bool IsFilterBarVisible
    {
        get => (bool)this.GetValue(IsFilterBarVisibleProperty);
        set => this.SetValue(IsFilterBarVisibleProperty, value);
    }

    /// <summary>Gets or sets the filter input text.</summary>
    /// <remarks>Bind this property two-way to application-owned filter state; matching remains a view-model concern.</remarks>
    public string FilterText
    {
        get => (string)this.GetValue(FilterTextProperty);
        set => this.SetValue(FilterTextProperty, value);
    }

    /// <summary>Gets or sets the filter input placeholder and accessible name.</summary>
    public string FilterBarPlaceholderText
    {
        get => (string)this.GetValue(FilterBarPlaceholderTextProperty);
        set => this.SetValue(FilterBarPlaceholderTextProperty, value);
    }

    /// <summary>Gets or sets optional content displayed before the filter input, such as a filter menu.</summary>
    public object? FilterBarAccessoryContent
    {
        get => this.GetValue(FilterBarAccessoryContentProperty);
        set => this.SetValue(FilterBarAccessoryContentProperty, value);
    }

    /// <summary>Gets or sets the filter input style, such as a native compact text-control style.</summary>
    /// <remarks>Null uses the default WinUI text input style. Tree row density does not override input sizing.</remarks>
    public Style? FilterBarInputStyle
    {
        get => (Style?)this.GetValue(FilterBarInputStyleProperty);
        set => this.SetValue(FilterBarInputStyleProperty, value);
    }

    private void ApplyFilterBarTemplate()
    {
        this.filterBar = this.GetTemplateChild("PartFilterBar") as FrameworkElement;
        this.filterTextBox = this.GetTemplateChild("PartFilterTextBox") as TextBox;
        this.filterSearchIcon = this.GetTemplateChild("PartFilterSearchIcon") as FontIcon;
        this.UpdateFilterBar();
    }

    private void UpdateFilterBar()
    {
        this.filterBar?.Visibility = this.IsFilterBarEnabled && this.IsFilterBarVisible
                ? Visibility.Visible : Visibility.Collapsed;

        this.filterSearchIcon?.Visibility = string.IsNullOrEmpty(this.FilterText)
                ? Visibility.Visible : Visibility.Collapsed;

        if (this.filterTextBox is not null)
        {
            this.filterTextBox.Style = this.FilterBarInputStyle;
            this.filterTextBox.ClearValue(Control.PaddingProperty);
            var padding = this.filterTextBox.Padding;

            // Reserve placeholder space for the decorative icon; the native clear button reserves its own space.
            this.filterTextBox.Padding = string.IsNullOrEmpty(this.FilterText)
                ? new Thickness(padding.Left, padding.Top, padding.Right + (this.filterSearchIcon?.FontSize ?? 0) + 20, padding.Bottom)
                : padding;
        }
    }

    private bool IsFilterBarElement(DependencyObject? element)
    {
        for (var current = element; current is not null; current = VisualTreeHelper.GetParent(current))
        {
            if (ReferenceEquals(current, this.filterBar))
            {
                return true;
            }
        }

        return false;
    }
}
