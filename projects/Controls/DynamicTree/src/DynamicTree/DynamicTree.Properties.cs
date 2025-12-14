// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls;

/// <summary>
///     Properties for the <see cref="DynamicTree" /> control.
/// </summary>
public partial class DynamicTree
{
    /// <summary>
    ///     The backing <see cref="DependencyProperty" /> for the <see cref="ThumbnailTemplateSelector" /> property.
    /// </summary>
    public static readonly DependencyProperty ThumbnailTemplateSelectorProperty = DependencyProperty.Register(
        nameof(ThumbnailTemplateSelector),
        typeof(DataTemplateSelector),
        typeof(DynamicTree),
        new PropertyMetadata(defaultValue: null));

    /// <summary>
    ///     The backing <see cref="DependencyProperty"/> for the <see cref="IsFilteringEnabled"/> property.
    /// </summary>
    public static readonly DependencyProperty IsFilteringEnabledProperty = DependencyProperty.Register(
        nameof(IsFilteringEnabled),
        typeof(bool),
        typeof(DynamicTree),
        new PropertyMetadata(defaultValue: false, (d, _) => ((DynamicTree)d).UpdateDisplayedItems()));

    /// <summary>
    ///     The backing <see cref="DependencyProperty"/> for the <see cref="DisplayedItems"/> property.
    /// </summary>
    public static readonly DependencyProperty DisplayedItemsProperty = DependencyProperty.Register(
        nameof(DisplayedItems),
        typeof(object),
        typeof(DynamicTree),
        new PropertyMetadata(defaultValue: null));

    /// <summary>
    /// Attached property storing the drop indicator position for an item container.
    /// </summary>
    public static readonly DependencyProperty DropIndicatorProperty = DependencyProperty.RegisterAttached(
        "DropIndicator",
        typeof(DropIndicatorPosition),
        typeof(DynamicTree),
        new PropertyMetadata(DropIndicatorPosition.None));

    /// <summary>
    /// Describes where a drop indicator should be shown for a tree item.
    /// </summary>
    public enum DropIndicatorPosition
    {
        /// <summary>
        /// No indicator is shown.
        /// </summary>
        None,

        /// <summary>
        /// Show an indicator before the item.
        /// </summary>
        Before,

        /// <summary>
        /// Show an indicator after the item.
        /// </summary>
        After,
    }

    /// <summary>
    ///     Gets or sets the data template selector for the thumbnails in the dynamic tree.
    /// </summary>
    public DataTemplateSelector ThumbnailTemplateSelector
    {
        get => (DataTemplateSelector)this.GetValue(ThumbnailTemplateSelectorProperty);
        set => this.SetValue(ThumbnailTemplateSelectorProperty, value);
    }

    /// <summary>
    ///     Gets or sets a value indicating whether the control renders <see cref="DynamicTreeViewModel.FilteredItems"/>
    ///     instead of <see cref="DynamicTreeViewModel.ShownItems"/>.
    /// </summary>
    public bool IsFilteringEnabled
    {
        get => (bool)this.GetValue(IsFilteringEnabledProperty);
        set => this.SetValue(IsFilteringEnabledProperty, value);
    }

    /// <summary>
    ///     Gets the items currently used as the ItemsRepeater source.
    /// </summary>
    public object? DisplayedItems
    {
        get => this.GetValue(DisplayedItemsProperty);
        private set => this.SetValue(DisplayedItemsProperty, value);
    }

    /// <summary>
    /// Gets the drop indicator position attached to the specified element.
    /// </summary>
    /// <param name="element">The element to query.</param>
    /// <returns>The current <see cref="DropIndicatorPosition"/>.</returns>
    public static DropIndicatorPosition GetDropIndicator(DependencyObject element)
        => (DropIndicatorPosition)element.GetValue(DropIndicatorProperty);

    /// <summary>
    /// Sets the drop indicator position on the specified element.
    /// </summary>
    /// <param name="element">The element to update.</param>
    /// <param name="value">The indicator position to set.</param>
    public static void SetDropIndicator(DependencyObject element, DropIndicatorPosition value)
        => element.SetValue(DropIndicatorProperty, value);
}
